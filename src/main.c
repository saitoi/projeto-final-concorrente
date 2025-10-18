/*
 * Nomes:
 * Pedro Henrique Honorio Saito*
 * Milton Salgado Leandro
 * Marcos Henrique Junqueira Muniz Barbi Silva
 */

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../include/preprocess_query.h"
#include "../include/file_io.h"
#include "../include/hash_t.h"
#include "../include/preprocess.h"
#include "../include/sqlite_helper.h"

/* --------------- Variáveis globais --------------- */

// Variáveis de sincronização
pthread_mutex_t mutex;
pthread_barrier_t barrier;
pthread_once_t once = PTHREAD_ONCE_INIT;

// Hashes globais
tf_hash *global_tf;
generic_hash *global_idf;

// Vetores globais
double **global_doc_vec;
double *global_doc_norms;
const char **global_vocab;
size_t global_vocab_size;

// Variáveis globais de controle
long int global_entries = 0;
int VERBOSE = 0;

/* --------------- Macros --------------- */

#define MAX_DOCS 97549
#define LOG(output, fmt, ...)                                                  \
  do {                                                                         \
    if (VERBOSE)                                                               \
      fprintf(output, "[VERBOSE] " fmt "\n", ##__VA_ARGS__);                   \
  } while (0)

typedef struct {
  long int start;
  long int end;
  long int nthreads;
  long int id;
  const char *filename_db;
  const char *tablename;
} thread_args;

void compute_once(void);
void *preprocess(void *args);

/* --------------- Fluxo Principal --------------- */
//

int main(int argc, char *argv[]) {
  const char *filename_db = "wiki-small.db", *filename_tf = "models/tf.bin",
             *filename_idf = "models/idf.bin",
             *filename_doc_vec = "models/doc_vec.bin",
             *filename_doc_norms = "models/doc_norms.bin",
             *filename_vocab = "models/vocab.txt",
             *tablename = "sample_articles";
  const char *query_user = "shakespeare english literature";
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
      "\tfilename_db: %s\n"
      "\tquery_user: %s\n"
      "\ttablename: %s",
      argc, nthreads, entries, filename_db, query_user, tablename);

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
  if (access(filename_tf, F_OK) == -1 || access(filename_idf, F_OK) == -1 ||
      access(filename_doc_vec, F_OK) == -1 ||
      access(filename_doc_norms, F_OK) == -1 ||
      access(filename_vocab, F_OK) == -1) {
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

    thread_args *args = (thread_args *)malloc(nthreads * sizeof(thread_args));
    if (!args) {
      fprintf(stderr, "Erro ao alocar memória para argumentos das threads\n");
      free(tids);
      return 1;
    }

    long int base = entries / nthreads;
    long int rem = entries % nthreads;
    for (long int i = 0; i < nthreads; ++i) {
      args[i].id = i;
      args[i].nthreads = nthreads;
      args[i].filename_db = filename_db;
      args[i].tablename = tablename;
      args[i].start = i * base + (i < rem ? i : rem);
      args[i].end = args[i].start + base + (i < rem);
      if (pthread_create(&tids[i], NULL, preprocess, (void *)&args[i])) {
        fprintf(stderr, "Erro ao criar thread %ld\n", i);
        free(args);
        free(tids);
        return 1;
      }
    }

    for (long int i = 0; i < nthreads; ++i) {
      if (pthread_join(tids[i], NULL)) {
        fprintf(stderr, "Erro ao esperar thread %ld\n", i);
        free(args);
        free(tids);
        return 1;
      }
    }

    free(args);
    free(tids);

    // Destruir barreira após threads terminarem
    pthread_barrier_destroy(&barrier);

    // Imprimir TF hash global final
    LOG(stdout, "=== TF Hash Global Final ===");
    print_tf_hash(global_tf, -1, VERBOSE);

    // [15]
    // Salvar estruturas globais em arquivos binários
    printf("\nSalvando estruturas em disco\n");
    save_tf_hash(global_tf, filename_tf);
    save_generic_hash(global_idf, filename_idf);
    save_doc_vecs(global_doc_vec, global_entries, global_vocab_size,
                filename_doc_vec);
    save_doc_norms(global_doc_norms, global_entries, filename_doc_norms);
    save_vocab(global_vocab, global_vocab_size, filename_vocab);

    // Liberar stopwords e tf hash globais
    free_stopwords();
    tf_hash_free(global_tf);

  } else {

    /* --------------- Carregamento dos Binários --------------- */

    printf("Arquivos binários encontrados, carregando estruturas...\n");

    global_tf = load_tf_hash(filename_tf);
    if (!global_tf) {
      fprintf(stderr, "Erro ao carregar global_tf\n");
      pthread_mutex_destroy(&mutex);
      return 1;
    }

    global_idf = load_generic_hash(filename_idf);
    if (!global_idf) {
      fprintf(stderr, "Erro ao carregar global_idf\n");
      tf_hash_free(global_tf);
      pthread_mutex_destroy(&mutex);
      return 1;
    }

    global_doc_vec =
        load_doc_vecs(filename_doc_vec, &global_entries, &global_vocab_size);
    if (!global_doc_vec) {
      fprintf(stderr, "Erro ao carregar global_doc_vec\n");
      tf_hash_free(global_tf);
      generic_hash_free(global_idf);
      pthread_mutex_destroy(&mutex);
      return 1;
    }

    global_doc_norms = load_doc_norms(filename_doc_norms, NULL);
    if (!global_doc_norms) {
      fprintf(stderr, "Erro ao carregar global_doc_norms\n");
      tf_hash_free(global_tf);
      generic_hash_free(global_idf);
      for (long int i = 0; i < global_entries; i++)
        free(global_doc_vec[i]);
      free(global_doc_vec);
      pthread_mutex_destroy(&mutex);
      return 1;
    }

    global_vocab = load_vocab(filename_vocab, NULL);
    if (!global_vocab) {
      fprintf(stderr, "Erro ao carregar global_vocab\n");
      tf_hash_free(global_tf);
      generic_hash_free(global_idf);
      for (long int i = 0; i < global_entries; i++)
        free(global_doc_vec[i]);
      free(global_doc_vec);
      free(global_doc_norms);
      pthread_mutex_destroy(&mutex);
      return 1;
    }

    printf("Todas as estruturas foram carregadas com sucesso!\n");
  }

  /* --------------- Consulta do Usuário --------------- */
  
  if (query_user) {
    printf("Consulta do usuário: %s\n", query_user);
    pthread_t *tids_query;
    
    long int token_count = 0;
    char **query_tokens;

    query_tokens = tokenize_query(query_user, &token_count);

    LOG(stdout, "Tokenização da consulta do usuário concluída.");
    LOG(stdout, "Quantidade de tokens: %ld", token_count);

    tids_query = (pthread_t *)malloc(nthreads * sizeof(pthread_t));
    if (!tids_query) {
      fprintf(stderr,
              "Erro ao alocar memória para identificador das threads\n");
      return 1;
    }

    thread_args *args = (thread_args *)malloc(nthreads * sizeof(thread_args));
    if (!args) {
      fprintf(stderr, "Erro ao alocar memória para argumentos das threads\n");
      free(tids_query);
      return 1;
    }

    long int base = token_count / nthreads;
    long int rem = token_count % nthreads;
    for (long int i = 0; i < nthreads; ++i) {
      args[i].id = i;
      args[i].nthreads = nthreads;
      args[i].filename_db = filename_db;
      args[i].tablename = tablename;
      args[i].start = i * base + (i < rem ? i : rem);
      args[i].end = args[i].start + base + (i < rem);
    if (pthread_create(&tids_query[i], NULL, preprocess_query, (void *)&args[i])) {
        fprintf(stderr, "Erro ao criar thread %ld\n", i);
        free(args);
        free(tids_query);
        return 1;
      }
    }

    for (long int i = 0; i < nthreads; ++i) {
      if (pthread_join(tids_query[i], NULL)) {
        fprintf(stderr, "Erro ao esperar thread %ld\n", i);
        free(args);
        free(tids_query);
        return 1;
      }
    }

    free(args);
    free(tids_query);
  } else {
      printf("Nenhuma consulta fornecida\n");
  } 
  
  // Impressão das palavras com IDF (primeiras 30 entradas)
  if (global_idf && VERBOSE) {
    printf("\n=== Primeiras 30 palavras com IDF ===\n");
    size_t count = 0;
    size_t limit = 30;

    for (size_t i = 0; i < global_idf->cap && count < limit; i++) {
      for (GEntry *e = global_idf->buckets[i]; e && count < limit;
           e = e->next) {
        printf("Palavra: %-25s IDF: %.6f\n", e->word, e->idf);
        count++;
      }
    }

    generic_hash_free(global_idf);
  }

  pthread_mutex_destroy(&mutex);

  return 0;
}

/* --------------- PTHREAD_ONCE_INIT --------------- */
// Tarefas computadas uma única vez dentre todas as threads:
// 1. Extrair vocabulário completo do IDF (chaves da generic_hash *global_idf)
// 2. Computar o IDF de cada palavra no vocabulário
// 3. Obter tamanho do vocabulário e alocar vetor de documentos
// 4. Alocar vetor da norma de cada documento

void compute_once(void) {
  LOG(stdout, "Funções computadas uma única vez dentre todas as threads");

  // [1] Coleta as chaves da hash global_idf
  global_vocab = generic_hash_to_vec(global_idf);
  if (global_vocab) {
    LOG(stdout, "Vocabulário convertido para vetor");
  }

  // [2] Computa o IDF
  set_idf_value(global_idf, global_tf, (double)global_entries);

  // [3]
  global_vocab_size = generic_hash_size(global_idf);
  global_doc_vec = (double **)malloc(global_entries * sizeof(double *));
  if (!global_doc_vec) {
    LOG(stderr, "Erro ao alocar memória para global_doc_vecs");
    exit(EXIT_FAILURE);
  }

  for (long int i = 0; i < global_entries; ++i) {
    global_doc_vec[i] = (double *)calloc(global_vocab_size, sizeof(double));
    if (!global_doc_vec[i]) {
      LOG(stderr, "Erro ao alocar memória para vetor do documento %ld", i);
      exit(EXIT_FAILURE);
    }
  }

  // [4]
  global_doc_norms = (double *)malloc(global_entries * sizeof(double));
  if (!global_doc_norms) {
    LOG(stderr, "Erro ao alocar memória para global_doc_norms");
    exit(EXIT_FAILURE);
  }

  fprintf(stdout, "IDF computado. Tamanho da Hash global: %zu\n",
          global_vocab_size);
  fprintf(stdout,
          "Vetores de documentos alocados: %ld documentos x %zu palavras\n",
          global_entries, global_vocab_size);
  fprintf(stdout, "Total de documentos para cálculo IDF: %ld\n",
          global_entries);
}

/* --------------- Pré-processamento --------------- */
// Pré-processamento computado uma única vez sobre todos os documentos:
// 0.  Passo preliminar: Pré-processamento/tratamento do texto do wiki-small.db
// no DuckDB.
// 1.  Captar os parâmetros das threads: struct thread_args e computar o
// intervalo de documentos (long int count).
// 2.  Inicializar as hashes locais.
// 3.  Validações do intervalo de documentos e debug.
// 4.  Extrair a coluna de texto (`article_text`) do SQLite.
// 5.  Tokenizar o texto recuperado usando separador de whitespace (texto já
// havia sido pré-processado).
// 6.  Filtrar stopwords e redimensionar o vetor.
// 7.  Stemming: Processo de remoção de afixos (prefixos e sufixos).
// 8.  Populando a hash local de frequência dos termos (`tf_hash *global_tf`).
// 9.  Popular vocabulário local (chaves do `generic_hash *global_idf`).
// 10. Mergir TF e IDF hashes nas globais
// 11. Uso do padrão barrreira (`pthread_barrier_t barrier`) para sincronizar as threads
// 12. Executar `compute_once` descrito na seção anterior: PTHREAD_ONCE_INIT.
// 13. Transformar os documentos em vetores usando esquema de ponderação TF-IDF.
// 14. Computar as normas dos vetores gerados na etapa anterior.
// 15. Salvar todas as estruturas em arquivos binários (./models/*.bin).

void *preprocess(void *arg) {
  // [1] Parâmetros das threads
  thread_args *t = (thread_args *)arg;
  long int count = t->end - t->start;

  char **article_texts; // Textos dos artigos
  char ***article_vecs; // Vetores de tokens dos artigos

  // [2] Hashes locais: TF e IDF
  tf_hash *tf = tf_hash_new();
  generic_hash *idf = generic_hash_new();

  // [3] Validações
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

  // [4] Recuperação dos textos dos artigos
  article_texts = get_str_arr(t->filename_db,
                              "select article_text from \"%w\" "
                              "where article_id between ? and ?",
                              t->start, t->end - 1, t->tablename);
  if (!article_texts) {
    fprintf(stderr, "Erro ao obter dados do banco\n");
    pthread_exit(NULL);
  }

  LOG(stdout, "Primeiro artigo da thread %ld: %s\n", t->id, article_texts[0]);

  // [5] Tokenização
  article_vecs = tokenize_articles(article_texts, count);
  if (!article_vecs) {
    fprintf(stderr, "Erro ao tokenizar artigos\n");
    pthread_exit(NULL);
  }

  // [6] Remoção de Stopwords
  remove_stopwords(article_vecs, count);

  // [7] Stemming
  stem_articles(article_vecs, count);

  // [8] Populando hash com os termos e suas frequências
  populate_tf_hash(tf, article_vecs, count, t->start);

  for (long int i = 0; i < count && i < 20; ++i) {
    if (!article_vecs[i])
      continue;
    for (long int j = 0; article_vecs[i][j] != NULL; ++j) {
      LOG(stdout, "Thread %ld, Article %ld, Token %ld: %s", t->id, i, j,
          article_vecs[i][j]);
    }
  }

  // [9] Computar vocabulário local
  set_idf_words(idf, article_vecs, count);

  // [10] Mergir TF e IDF hashes
  pthread_mutex_lock(&mutex);
  tf_hash_merge(global_tf, tf);
  generic_hash_merge(global_idf, idf);
  pthread_mutex_unlock(&mutex);

  // [11] Sincronizar as threads antes de computar variáveis globais únicas
  LOG(stdout, "Thread %ld esperando na barreira", t->id);
  pthread_barrier_wait(&barrier);

  // [12] Variáveis globais computadas uma vez entre todas as threads
  pthread_once(&once, compute_once);

  // [13] Computar vetores de documentos usando TF-IDF
  compute_doc_vecs(global_doc_vec, global_tf, global_idf, count, t->start);

  // [14] Computar normas dos documentos
  compute_doc_norms(global_doc_norms, global_doc_vec, count, global_vocab_size,
                    t->start);

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
    LOG(stdout, "  Norma do documento %ld: %.6f", doc_id,
        global_doc_norms[doc_id]);
    LOG(stdout, ""); // Linha em branco para separar documentos
  }

  LOG(stdout, "Thread %ld passou da barreira e IDF já foi computado", t->id);

  // Liberar memória
  for (long int i = 0; i < count; ++i) {
    if (article_texts[i])
      free(article_texts[i]);
  }

  fprintf(stdout, "Tamanho da Hash local da thread %zu: %ld\n", t->id,
          generic_hash_size(idf));

  // Liberar memória de estruturas locais
  free(article_texts);
  free_article_vecs(article_vecs, count);
  tf_hash_free(tf);
  generic_hash_free(idf);

  pthread_exit(NULL);
}