#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <libstemmer.h>

#include "../include/preprocess.h"
#include "../include/sqlite_helper.h"

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

sem_t mutex;

void *preprocess(void *arg) {
  thread_args *t = (thread_args *)arg;
  long int count = t->end - t->start;
  char **article_texts;
  char ***article_vecs;

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
  article_vecs = remove_stopwords(article_vecs, count);

  for (long int i = 0; i < count; ++i) {
    if (!article_vecs[i])
      continue;
    for (long int j = 0; article_vecs[i][j] != NULL; ++j) {
      LOG(stdout, "Thread %ld, Article %ld, Token %ld: %s", t->id, i, j,
          article_vecs[i][j]);
    }
  }

  // Liberar memória
  for (long int i = 0; i < count; ++i) {
    if (article_texts[i])
      free(article_texts[i]);
  }
  free(article_texts);

  free_article_vecs(article_vecs, count);

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
    fprintf(stderr, "Número de entradas excede o limite máximo de documentos (%d)\n", MAX_DOCS);
    return 1;
  }
  
  // Caso o arquivo não exista: Pré-processamento
  if (access(filename_tfidf, F_OK) == -1) {
    pthread_t *tids;
    const char *query_count = "select count(*) from \"%w\";";

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

    long int base = entries / nthreads;
    long int rem = entries % nthreads;
    for (long int i = 0; i < nthreads; ++i) {
      thread_args *arg = (thread_args *)malloc(sizeof(thread_args));
      if (!arg) {
        fprintf(stderr, "Erro ao alocar memória para argumentos da thread\n");
        free(tids);
        return 1;
      }
      arg->id = i;
      arg->filename_db = filename_db;
      arg->tablename = tablename;
      arg->start = i * base + (i < rem ? i : rem);
      arg->end = arg->start + base + (i < rem);
      if (pthread_create(&tids[i], NULL, preprocess, (void *)arg)) {
        fprintf(stderr, "Erro ao criar thread %ld\n", i);
        free(tids);
        return 1;
      }
    }

    for (long int i = 0; i < nthreads; ++i) {
      if (pthread_join(tids[i], NULL)) {
        fprintf(stderr, "Erro ao esperar thread %ld\n", i);
        free(tids);
        return 1;
      }
    }

    free(tids);

    // Liberar stopwords globais
    free_stopwords();

  } else {
    // Carregar estrutura hash TF-IDF do arquivo
    printf("Arquivo binário encontrado: %s\n", filename_tfidf);
  }

  sem_init(&mutex, 0, 1);

  sem_destroy(&mutex);

  return 0;
}
