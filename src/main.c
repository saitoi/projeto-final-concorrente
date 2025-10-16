#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../include/preprocess.h"
#include "../include/sqlite_helper.h"
#include "../include/hash_t.h"

pthread_mutex_t mutex;
pthread_barrier_t barrier;
pthread_once_t once = PTHREAD_ONCE_INIT;

// Variáveis globais
tf_hash *global_tf;
generic_hash *global_idf;

double **global_doc_vec;
double *global_doc_norms;
const char **global_vocab;
size_t global_vocab_size;
long int global_entries = 0;
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
  long int nthreads;
  long int id;
  const char *filename_db;
  const char *tablename;
} thread_args;

// Função chamada uma única vez por pthread_once para computar IDF
void compute_idf_once(void) {
  LOG(stdout, "=== Computando IDF (executado uma única vez) ===");

  // Converter hash para vetor (vocabulário ordenado)
  global_vocab = generic_hash_to_vec(global_idf);
  if (global_vocab) {
    LOG(stdout, "Vocabulário convertido para vetor");
  }

  // Computar IDF para todas as palavras
  set_idf(global_idf, global_tf, (double)global_entries);

  // Obter tamanho do vocabulário
  global_vocab_size = generic_hash_size(global_idf);

  // Alocar memória para o vetor de vetores de documentos
  // Dimensão: [número_de_documentos][tamanho_vocabulário]
  global_doc_vec = (double**) malloc(global_entries * sizeof(double*));
  if (!global_doc_vec) {
    LOG(stderr, "Erro ao alocar memória para global_doc_vecs");
    exit(EXIT_FAILURE);
  }

  // Alocar cada vetor de documento
  for (long int i = 0; i < global_entries; ++i) {
    global_doc_vec[i] = (double*) calloc(global_vocab_size, sizeof(double));
    if (!global_doc_vec[i]) {
      LOG(stderr, "Erro ao alocar memória para vetor do documento %ld", i);
      exit(EXIT_FAILURE);
    }
  }

  global_doc_norms = (double*) malloc(global_entries * sizeof(double));
  if (!global_doc_norms) {
    LOG(stderr, "Erro ao alocar memória para global_doc_norms");
    exit(EXIT_FAILURE);
  }
  
  fprintf(stdout, "IDF computado. Tamanho da Hash global: %zu\n", global_vocab_size);
  fprintf(stdout, "Vetores de documentos alocados: %ld documentos x %zu palavras\n",
          global_entries, global_vocab_size);
  fprintf(stdout, "Total de documentos para cálculo IDF: %ld\n", global_entries);
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

  // 9. Sincronizar todas as threads (esperar que todas terminem de mergir)
  LOG(stdout, "Thread %ld esperando na barreira", t->id);
  pthread_barrier_wait(&barrier);

  // 10. Computar IDF apenas uma vez (primeira thread que passar)
  pthread_once(&once, compute_idf_once);

  // 11. Computar vetores de documentos usando TF-IDF
  compute_doc_vecs(global_doc_vec, global_tf, global_idf, count, t->start);

  // 12. Computar normas dos documentos
  compute_doc_norms(global_doc_norms, global_doc_vec, count, global_vocab_size, t->start);

  // Imprimir alguns vetores para verificação
  LOG(stdout, "=== Vetores de documentos da Thread %ld ===", t->id);
  for (long int i = 0; i < count && i < 3; ++i) {
    long int doc_id = t->start + i;
    LOG(stdout, "Documento %ld:", doc_id);

    // Mostrar valores do vetor (primeiros 30 valores não-zero)
    size_t valores_mostrados = 0;
    for (size_t j = 0; j < global_vocab_size && valores_mostrados < 30; ++j) {
      if (global_doc_vec[doc_id][j] != 0.0) {
        LOG(stdout, "  [%zu] = %.6f", j, global_doc_vec[doc_id][j]);
        valores_mostrados++;
      }
    }

    // Mostrar a norma após o vetor
    LOG(stdout, "  Norma do documento %ld: %.6f", doc_id, global_doc_norms[doc_id]);
    LOG(stdout, "");  // Linha em branco para separar documentos
  }

  // 12. Computar normas dos documentos
  // compute_doc_norms(global_doc_vec, count);


  LOG(stdout, "Thread %ld passou da barreira e IDF já foi computado", t->id);

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

    // Armazenar em variável global para uso em compute_idf_once
    global_entries = entries;

    printf("Qtd. artigos: %ld\n", entries);

    // Inicializar barreira para sincronizar threads
    if (pthread_barrier_init(&barrier, NULL, nthreads)) {
      fprintf(stderr, "Erro ao inicializar barreira\n");
      return 1;
    }

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
      args[i]->nthreads = nthreads;
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

    // Destruir barreira após threads terminarem
    pthread_barrier_destroy(&barrier);

    // Imprimir TF hash global final
    LOG(stdout, "=== TF Hash Global Final ===");
    print_tf_hash(global_tf, -1, VERBOSE);

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
        printf("Palavra: %-25s IDF: %.6f\n", e->word, e->idf);
        count++;
      }
    }

    generic_hash_free(global_idf);
  }

  pthread_mutex_destroy(&mutex);

  return 0;
}
