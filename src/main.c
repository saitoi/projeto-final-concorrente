#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../include/preprocess.h"
#include "../include/sqlite_helper.h"
#include "../include/hash_t.h"

pthread_mutex_t mutex;

static pthread_once_t once = PTHREAD_ONCE_INIT;

tf_hash *global_tf;
generic_hash *global_idf;
const char **global_vocab;
int VERBOSE = 0;

#define MAX_DOCS 97549

#define LOG(output, fmt, ...)                                                  \
  do {                                                                         \
    if (VERBOSE)                                                               \
      fprintf(output, "[VERBOSE] " fmt "\n", ##__VA_ARGS__);                   \
  } while (0)

// Ordem decrescente de tam
typedef struct {
  long int start;
  long int end;
  long int id;
  const char *filename_db;
  const char *tablename;
} thread_args;

void print_tf_hash(tf_hash *tf, long int thread_id) {
  if (!tf)
    return;

  LOG(stdout, "Thread %ld - TF Hash Contents:", thread_id);
  LOG(stdout, "  Hash capacity: %zu, size: %zu", tf->cap, tf->size);

  size_t word_count = 0;
  for (size_t i = 0; i < tf->cap; i++) {
    for (OEntry *e = tf->b[i]; e; e = e->next) {
      word_count++;
      LOG(stdout, "  Word '%s' (len=%zu):", e->key, e->klen);

      // Imprimir documentos e frequências
      size_t doc_count = 0;
      for (size_t j = 0; j < e->map.cap; j++) {
        for (IEntry *ie = e->map.b[j]; ie; ie = ie->next) {
          LOG(stdout, "    Doc %d: %.0f", ie->key, ie->val);
          doc_count++;
        }
      }
      LOG(stdout, "    Total docs for '%s': %zu", e->key, doc_count);

      // Limitar a saída para não ficar muito grande
      if (word_count >= 20) {
        LOG(stdout, "  ... (mostrando apenas as primeiras 20 palavras)");
        return;
      }
    }
  }
  LOG(stdout, "Thread %ld - Total de palavras únicas: %zu", thread_id,
      word_count);
}

void *preprocess(void *arg) {
  // Thread parameters
  thread_args *t = (thread_args *)arg;
  long int count = t->end - t->start;

  // Hashes locais
  tf_hash *tf = tf_hash_new();
  generic_hash* idf = generic_hash_new();

  // String arrays
  char **article_texts;
  char ***article_vecs;

  // Se a thread não tem artigos para processar, retorna
  if (count <= 0) {
    LOG(stdout, "Thread %ld sem artigos para processar", t->id);
    pthread_exit(NULL);
  }

  LOG(stdout,
      "Thread %ld iniciou:\n"
      "\tstart: %ld\n"
      "\tchunk: %ld\n"
      "\tfilename_db: %s\n",
      t->id, t->start, t->end, t->filename_db);

  // Passo-a-passo
  // 0. Pre-processamento no DuckDB (Converter para ASCII + remover caracteres
  // especiais)
  // 1. Inicializar conexão com o banco
  // 2. Executar consulta para obter textos dos intervalos desejados

  article_texts = get_str_arr(t->filename_db,
                              "select article_text from \"%w\" "
                              "where article_id between ? and ?",
                              t->start, t->end - 1, t->tablename);
  if (!article_texts) {
    fprintf(stderr, "Erro ao obter dados do banco\n");
    pthread_exit(NULL);
  }

  LOG(stdout, "Primeiro artigo da thread %ld: %s\n", t->id, article_texts[0]);

  // 3. Tokenizar os textos recuperados
  article_vecs = tokenize_articles(article_texts, count);
  if (!article_vecs) {
    fprintf(stderr, "Erro ao tokenizar artigos\n");
    pthread_exit(NULL);
  }

  // 4. Remoção de Stopwords
  remove_stopwords(article_vecs, count);

  // 5. Stemming
  stem_articles(article_vecs, count);

  // 6. Popula o hash com os termos e suas frequências
  populate_tf_hash(tf, article_vecs, count, t->start);

  for (long int i = 0; i < count && i < 20; ++i) {
    if (!article_vecs[i])
      continue;
    for (long int j = 0; article_vecs[i][j] != NULL; ++j) {
      LOG(stdout, "Thread %ld, Article %ld, Token %ld: %s", t->id, i, j,
          article_vecs[i][j]);
    }
  }

  // 7. Computar vocabulário local
  generate_vocab(idf, article_vecs, count);

  // 8. Mergir hashes locais numa global
  pthread_mutex_lock(&mutex);
  tf_hash_merge(global_tf, tf);
  generic_hash_merge(global_idf, idf);
  pthread_mutex_unlock(&mutex);

  // 9. IDF
  // pthread_once(&once, );

  
  // Liberar memória
  for (long int i = 0; i < count; ++i) {
    if (article_texts[i])
      free(article_texts[i]);
  }

  fprintf(stdout, "Tamanho da Hash local da thread %zu: %ld\n", t->id, generic_hash_size(idf));

  free(article_texts);
  free_article_vecs(article_vecs, count);
  tf_hash_free(tf);
  generic_hash_free(idf);

  pthread_exit(NULL);
}

int main(int argc, char *argv[]) {
  const char *filename_tfidf = "tfidf.bin", *filename_db = "wiki-small.db",
             *tablename = "sample_articles";
  const char *query_user = "exemplo";
  int nthreads = 4;
  long int entries = 0;

  // CLI com parâmetros nomeados
  for (int i = 1; i < argc; ++i) {
    if (strcmp(argv[i], "--nthreads") == 0 && i + 1 < argc)
      nthreads = atoi(argv[++i]);
    else if (strcmp(argv[i], "--entries") == 0 && i + 1 < argc)
      entries = atol(argv[++i]);
    else if (strcmp(argv[i], "--filename_db") == 0 && i + 1 < argc)
      filename_db = argv[++i];
    else if (strcmp(argv[i], "--filename_tfidf") == 0 && i + 1 < argc)
      filename_tfidf = argv[++i];
    else if (strcmp(argv[i], "--query_user") == 0 && i + 1 < argc)
      query_user = argv[++i];
    else if (strcmp(argv[i], "--tablename") == 0 && i + 1 < argc)
      tablename = argv[++i];
    else if (strcmp(argv[i], "--verbose") == 0)
      VERBOSE = 1;
    else {
      fprintf(
          stderr,
          "Uso: %s <parametros nomeados>\n"
          "--verbose: Verbosidade (default: 0)\n"
          "--nthreads: Número de threads (default: 4)\n"
          "--entries: Quantidade de entradas para pré-processamento (default: "
          "Toda tabela 'sample_articles')\n"
          "--filename_db: Nome do arquivo Sqlite (default: 'wiki-small.db')\n"
          "--filename_tfidf: Nome do arquivo com estrutura do TF-IDF (default: "
          "'tfidf.bin')\n"
          "--query_user: Consulta do usuário (default: 'exemplo')\n"
          "--tablename: Nome da tabela consultada (default: "
          "'sample_articles')\n",
          argv[0]);
      return 1;
    }
  }

  LOG(stdout,
      "Parâmetros nomeados:\n"
      "\targc: %d\n"
      "\tnthreads: %d\n"
      "\tentries: %ld\n"
      "\tfilename_tfidf: %s\n"
      "\tfilename_db: %s\n"
      "\tquery_user: %s\n"
      "\ttablename: %s",
      argc, nthreads, entries, filename_tfidf, filename_db, query_user,
      tablename);

  if (nthreads <= 0) {
    fprintf(stderr, "Número de threads inválido (%d)\n", nthreads);
    return 1;
  }

  if (entries > MAX_DOCS) {
    fprintf(stderr,
            "Número de entradas excede o limite máximo de documentos (%d)\n",
            MAX_DOCS);
    return 1;
  }

  pthread_mutex_init(&mutex, NULL);
  
  // Caso o arquivo não exista: Pré-processamento
  if (access(filename_tfidf, F_OK) == -1) {
    pthread_t *tids;
    const char *query_count = "select count(*) from \"%w\";";
    global_tf = tf_hash_new();
    global_idf = generic_hash_new();

    // Carregar stopwords uma vez (compartilhado por todas threads)
    load_stopwords("assets/stopwords.txt");
    if (!global_stopwords) {
      fprintf(stderr, "Falha ao carregar stopwords\n");
      return 1;
    }

    // Sobreescreve count=0
    // Quantos registros na tabela 'sample_articles'
    if (!entries)
      entries = get_single_int(filename_db, query_count, tablename);

    printf("Qtd. artigos: %ld\n", entries);

    tids = (pthread_t *)malloc(nthreads * sizeof(pthread_t));
    if (!tids) {
      fprintf(stderr,
              "Erro ao alocar memória para identificador das threads\n");
      return 1;
    }

    thread_args **args = (thread_args **)malloc(nthreads * sizeof(thread_args *));
    if (!args) {
      fprintf(stderr, "Erro ao alocar memória para argumentos das threads\n");
      free(tids);
      return 1;
    }

    long int base = entries / nthreads;
    long int rem = entries % nthreads;
    for (long int i = 0; i < nthreads; ++i) {
      args[i] = (thread_args *)malloc(sizeof(thread_args));
      if (!args[i]) {
        fprintf(stderr, "Erro ao alocar memória para argumentos da thread\n");
        for (long int j = 0; j < i; ++j) free(args[j]);
        free(args);
        free(tids);
        return 1;
      }
      args[i]->id = i;
      args[i]->filename_db = filename_db;
      args[i]->tablename = tablename;
      args[i]->start = i * base + (i < rem ? i : rem);
      args[i]->end = args[i]->start + base + (i < rem);
      if (pthread_create(&tids[i], NULL, preprocess, (void *)args[i])) {
        fprintf(stderr, "Erro ao criar thread %ld\n", i);
        for (long int j = 0; j <= i; ++j) free(args[j]);
        free(args);
        free(tids);
        return 1;
      }
    }

    for (long int i = 0; i < nthreads; ++i) {
      if (pthread_join(tids[i], NULL)) {
        fprintf(stderr, "Erro ao esperar thread %ld\n", i);
        for (long int j = 0; j < nthreads; ++j) free(args[j]);
        free(args);
        free(tids);
        return 1;
      }
    }

    for (long int i = 0; i < nthreads; ++i) free(args[i]);
    free(args);
    free(tids);

    // Imprimir TF hash global final
    LOG(stdout, "=== TF Hash Global Final ===");
    print_tf_hash(global_tf, -1);

    // Computar IDF para todas as palavras do vocabulário
    if (global_idf) {
      set_idf(global_idf, global_tf, (double) entries);
      fprintf(stdout, "Tamanho da Hash global: %zu\n", generic_hash_size(global_idf));
    }

    // Liberar stopwords e tf hash globais
    free_stopwords();
    tf_hash_free(global_tf);

  } else {
    // Carregar estrutura hash TF-IDF do arquivo
    printf("Arquivo binário encontrado: %s\n", filename_tfidf);
  }

  // Impressão das palavras com IDF (primeiras 30 entradas)
  if (global_idf) {
    printf("\n=== Primeiras 30 palavras com IDF ===\n");
    size_t count = 0;
    size_t limit = 30;

    for (size_t i = 0; i < global_idf->cap && count < limit; i++) {
      for (GEntry *e = global_idf->buckets[i]; e && count < limit; e = e->next) {
        printf("Palavra: %-25s IDF: %.6f\n", e->word, e->value);
        count++;
      }
    }

    generic_hash_free(global_idf);
  }

  pthread_mutex_destroy(&mutex);

  return 0;
}
