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
generic_hash **global_doc_hash;
double *global_doc_norms;

// Vetores globais
size_t global_vocab_size;

// Variáveis globais de controle
long int global_entries = 0;
int VERBOSE = 0;

/* --------------- Macros --------------- */

#define MAX_DOCS 97549
#define MAX_THREADS 16
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
  fprintf(stderr, "DEBUG: main() iniciando\n");
  fflush(stderr);
  const char *filename_db = "wiki-small.db", *filename_tf = "models/tf.bin",
             *filename_idf = "models/idf.bin",
             *filename_doc_norms = "models/doc_norms.bin",
             *tablename = "sample_articles";
  const char *query_user = "shakespeare english literature";
  int nthreads = 4;
  long int entries = 0;

  fprintf(stderr, "DEBUG: Variáveis locais inicializadas\n");
  fflush(stderr);

  // CLI com parâmetros nomeados
  fprintf(stderr, "DEBUG: Processando %d argumentos\n", argc);
  fflush(stderr);
  for (int i = 1; i < argc; ++i) {
    fprintf(stderr, "DEBUG: argv[%d] = %s\n", i, argv[i]);
    fflush(stderr);
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

  if (nthreads <= 0 || nthreads > MAX_THREADS) {
    fprintf(stderr, "Número de threads inválido (%d). Deve estar entre 1 e %d\n", nthreads, MAX_THREADS);
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
      access(filename_doc_norms, F_OK) == -1) {
    pthread_t tids[MAX_THREADS];
    thread_args args[MAX_THREADS];
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
        return 1;
      }
    }

    for (long int i = 0; i < nthreads; ++i) {
      if (pthread_join(tids[i], NULL)) {
        fprintf(stderr, "Erro ao esperar thread %ld\n", i);
        return 1;
      }
    }

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
    // TODO: Implementar salvação de global_doc_hash quando necessário
    save_doc_norms(global_doc_norms, global_entries, filename_doc_norms);

    // Liberar stopwords (usado apenas no pré-processamento)
    free_stopwords();

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

    // TODO: Implementar carregamento de global_doc_hash quando necessário
    // Por enquanto, vamos pular o carregamento dos vetores de documentos

    global_doc_norms = load_doc_norms(filename_doc_norms, &global_entries);
    if (!global_doc_norms) {
      fprintf(stderr, "Erro ao carregar global_doc_norms\n");
      tf_hash_free(global_tf);
      generic_hash_free(global_idf);
      pthread_mutex_destroy(&mutex);
      return 1;
    }

    global_vocab_size = generic_hash_size(global_idf);


    printf("Todas as estruturas foram carregadas com sucesso!\n");
  }

  /* --------------- Consulta do Usuário --------------- */
  
  if (query_user) {
    printf("Consulta do usuário: %s\n", query_user);
    pthread_t tids_query[MAX_THREADS];
    thread_args args[MAX_THREADS];

    long int token_count = 0;
    char **query_tokens;

    query_tokens = tokenize_query(query_user, &token_count);
    if (!query_tokens) {
      fprintf(stderr, "Erro ao tokenizar consulta do usuário\n");
      return 1;
    }

    LOG(stdout, "Tokenização da consulta do usuário concluída.");
    LOG(stdout, "Quantidade de tokens: %ld", token_count);

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
        return 1;
      }
    }

    for (long int i = 0; i < nthreads; ++i) {
      if (pthread_join(tids_query[i], NULL)) {
        fprintf(stderr, "Erro ao esperar thread %ld\n", i);
        return 1;
      }
    }

    // Liberar memória dos tokens da query
    for (long int i = 0; i < token_count; ++i) {
      if (query_tokens[i])
        free(query_tokens[i]);
    }
    free(query_tokens);
  } else {
      printf("Nenhuma consulta fornecida\n");
  } 
  
  // Impressão das palavras com IDF (primeiras 30 entradas)
  if (VERBOSE) {
    printf("\n=== Primeiras 30 palavras com IDF ===\n");
    size_t count = 0;
    size_t limit = 30;

    for (size_t i = 0; i < global_idf->cap && count < limit; i++) {
      for (GEntry *e = global_idf->buckets[i]; e && count < limit;
           e = e->next) {
        printf("Palavra: %-25s IDF: %.6f\n", e->word, e->value);
        count++;
      }
    }
  }

  // Liberar todas as estruturas globais
  if (global_tf)
    tf_hash_free(global_tf);
  if (global_idf)
    generic_hash_free(global_idf);

  // Liberar hashes de documentos
  if (global_doc_hash) {
    for (long int i = 0; i < global_entries; i++) {
      if (global_doc_hash[i])
        generic_hash_free(global_doc_hash[i]);
    }
    free(global_doc_hash);
  }

  // Liberar normas
  if (global_doc_norms)
    free(global_doc_norms);


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
  fprintf(stderr, "DEBUG: compute_once() iniciando\n");
  fflush(stderr);
  LOG(stdout, "Funções computadas uma única vez dentre todas as threads");

  // [1] Vocabulário: usar as chaves da hash global_idf diretamente quando necessário

  // [2] Computa o IDF
  fprintf(stderr, "DEBUG: Antes set_idf_value\n");
  fflush(stderr);
  set_idf_value(global_idf, global_tf, (double)global_entries);
  fprintf(stderr, "DEBUG: Depois set_idf_value\n");
  fflush(stderr);

  // [3] Alocar global_doc_hash (hash table para cada documento)
  global_vocab_size = generic_hash_size(global_idf);
  fprintf(stderr, "DEBUG: Alocando global_doc_hash: %ld docs\n", global_entries);
  fflush(stderr);

  global_doc_hash = (generic_hash **)malloc(global_entries * sizeof(generic_hash *));
  if (!global_doc_hash) {
    LOG(stderr, "Erro ao alocar memória para global_doc_hash");
    exit(EXIT_FAILURE);
  }

  for (long int i = 0; i < global_entries; ++i) {
      global_doc_hash[i] = generic_hash_new();
      if (!global_doc_hash[i]) {
        LOG(stderr, "Erro ao alocar memória para global_doc_hash[%ld]", i);
        exit(EXIT_FAILURE);
      }
  }

  // [4]
  global_doc_norms = (double *)calloc(global_entries, sizeof(double));
  if (!global_doc_norms) {
    LOG(stderr, "Erro ao alocar memória para global_doc_norms");
    exit(EXIT_FAILURE);
  }

  fprintf(stdout, "IDF computado. Tamanho do vocabulário: %zu palavras\n",
          global_vocab_size);
  fprintf(stdout,
          "Hashes de documentos alocados: %ld documentos\n",
          global_entries);
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
  fprintf(stderr, "DEBUG: preprocess() iniciando\n");
  fflush(stderr);

  // [1] Parâmetros das threads
  thread_args *t = (thread_args *)arg;
  long int count = t->end - t->start;

  fprintf(stderr, "DEBUG: Thread %ld - range: %ld to %ld (count=%ld)\n", t->id, t->start, t->end, count);
  fflush(stderr);

  char **article_texts; // Textos dos artigos
  char ***article_vecs; // Vetores de tokens dos artigos

  // [2] Hashes locais: TF e IDF
  fprintf(stderr, "DEBUG: Thread %ld - Criando hashes locais\n", t->id);
  fflush(stderr);
  tf_hash *tf = tf_hash_new();
  fprintf(stderr, "DEBUG: Thread %ld - tf_hash criado\n", t->id);
  fflush(stderr);
  generic_hash *idf = generic_hash_new();
  fprintf(stderr, "DEBUG: Thread %ld - generic_hash criado\n", t->id);
  fflush(stderr);

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
  fprintf(stderr, "DEBUG: Thread %ld - Antes get_str_arr\n", t->id);
  fflush(stderr);
  article_texts = get_str_arr(t->filename_db,
                              "select article_text from \"%w\" "
                              "where article_id between ? and ?"
                              "order by article_id asc",
                              t->start, t->end - 1, t->tablename);
  fprintf(stderr, "DEBUG: Thread %ld - Dvecepois get_str_arr\n", t->id);
  fflush(stderr);
  if (!article_texts) {
    fprintf(stderr, "Erro ao obter dados do banco\n");
    pthread_exit(NULL);
  }

  LOG(stdout, "Primeiro artigo da thread %ld: %s\n", t->id, article_texts[0]);

  // [5] Tokenização
  fprintf(stderr, "DEBUG: Thread %ld - Antes tokenize_articles\n", t->id);
  fflush(stderr);
  article_vecs = tokenize_articles(article_texts, count);
  fprintf(stderr, "DEBUG: Thread %ld - Depois tokenize_articles\n", t->id);
  fflush(stderr);
  if (!article_vecs) {
    fprintf(stderr, "Erro ao tokenizar artigos\n");
    pthread_exit(NULL);
  }

  // [6] Remoção de Stopwords
  fprintf(stderr, "DEBUG: Thread %ld - Antes remove_stopwords\n", t->id);
  fflush(stderr);
  remove_stopwords(article_vecs, count);
  fprintf(stderr, "DEBUG: Thread %ld - Depois remove_stopwords\n", t->id);
  fflush(stderr);

  // [7] Stemming
  fprintf(stderr, "DEBUG: Thread %ld - Antes stem_articles\n", t->id);
  fflush(stderr);
  stem_articles(article_vecs, count);
  fprintf(stderr, "DEBUG: Thread %ld - Depois stem_articles\n", t->id);
  fflush(stderr);

  // [8] Populando hash com os termos e suas frequências
  fprintf(stderr, "DEBUG: Thread %ld - Antes populate_tf_hash\n", t->id);
  fflush(stderr);
  populate_tf_hash(tf, article_vecs, count, t->start);
  fprintf(stderr, "DEBUG: Thread %ld - Depois populate_tf_hash\n", t->id);
  fflush(stderr);

  for (long int i = 0; i < count && i < 20; ++i) {
    if (!article_vecs[i])
      continue;
    for (long int j = 0; article_vecs[i][j] != NULL; ++j) {
      LOG(stdout, "Thread %ld, Article %ld, Token %ld: %s", t->id, i, j,
          article_vecs[i][j]);
    }
  }

  // [9] Computar vocabulário local
  fprintf(stderr, "DEBUG: Thread %ld - Antes set_idf_words\n", t->id);
  fflush(stderr);
  set_idf_words(idf, article_vecs, count);
  fprintf(stderr, "DEBUG: Thread %ld - Depois set_idf_words\n", t->id);
  fflush(stderr);

  // [10] Mergir TF e IDF hashes
  fprintf(stderr, "DEBUG: Thread %ld - Antes merge (lock)\n", t->id);
  fflush(stderr);
  pthread_mutex_lock(&mutex);
  fprintf(stderr, "DEBUG: Thread %ld - Lock adquirido, merging\n", t->id);
  fflush(stderr);
  tf_hash_merge(global_tf, tf);
  fprintf(stderr, "DEBUG: Thread %ld - tf_hash_merge OK\n", t->id);
  fflush(stderr);
  generic_hash_merge(global_idf, idf);
  fprintf(stderr, "DEBUG: Thread %ld - generic_hash_merge OK\n", t->id);
  fflush(stderr);
  pthread_mutex_unlock(&mutex);
  fprintf(stderr, "DEBUG: Thread %ld - Lock liberado\n", t->id);
  fflush(stderr);

  // [11] Sincronizar as threads antes de computar variáveis globais únicas
  LOG(stdout, "Thread %ld esperando na barreira", t->id);
  fprintf(stderr, "DEBUG: Thread %ld - Antes barrier_wait\n", t->id);
  fflush(stderr);
  pthread_barrier_wait(&barrier);
  fprintf(stderr, "DEBUG: Thread %ld - Depois barrier_wait\n", t->id);
  fflush(stderr);

  // [12] Variáveis globais computadas uma vez entre todas as threads
  fprintf(stderr, "DEBUG: Thread %ld - Antes pthread_once\n", t->id);
  fflush(stderr);
  pthread_once(&once, compute_once);
  fprintf(stderr, "DEBUG: Thread %ld - Depois pthread_once\n", t->id);
  fflush(stderr);

  // [13] Computar vetores de documentos usando TF-IDF
  fprintf(stderr, "DEBUG: Thread %ld - Antes compute_doc_vecs\n", t->id);
  fflush(stderr);
  compute_doc_hash(global_doc_hash, global_tf, global_idf, count, t->start);
  fprintf(stderr, "DEBUG: Thread %ld - Depois compute_doc_vecs\n", t->id);
  fflush(stderr);

  // [14] Computar normas dos documentos
  fprintf(stderr, "DEBUG: Thread %ld - Antes compute_doc_norms\n", t->id);
  fflush(stderr);
  compute_doc_norms(global_doc_norms, global_doc_hash, count, global_vocab_size,
                    t->start);
  fprintf(stderr, "DEBUG: Thread %ld - Depois compute_doc_norms\n", t->id);
  fflush(stderr);

  // Imprimir alguns vetores para verificação
  LOG(stdout, "=== Vetores de documentos da Thread %ld ===", t->id);
  for (long int i = 0; i < count && i < 3; ++i) {
    long int doc_id = t->start + i;
    LOG(stdout, "Documento %ld:", doc_id);

    // Mostrar valores do hash (primeiros 30 valores não-zero)
    size_t valores_mostrados = 0;
    for (size_t j = 0; j < global_doc_hash[doc_id]->cap && valores_mostrados < 30; ++j) {
      for (GEntry *e = global_doc_hash[doc_id]->buckets[j]; e && valores_mostrados < 30; e = e->next) {
        if (e->value != 0.0) {
          LOG(stdout, "  %s = %.6f", e->word, e->value);
          valores_mostrados++;
        }
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