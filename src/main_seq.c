#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "../include/file_io.h"
#include "../include/hash_t.h"
#include "../include/log.h"
#include "../include/preprocess.h"
#include "../include/preprocess_query.h"
#include "../include/sqlite_helper.h"
#include "../include/time_utils.h"

/* --------------- Variáveis globais --------------- */

#define PRINT_IDF_WORDS 20

hash_t **global_tf = NULL;
hash_t *global_idf = NULL;
double *global_doc_norms = NULL;
long int global_entries = 0;

int VERBOSE = 0;

typedef struct {
  long int entries;
  const char *db;
  const char *query_user;
  const char *query_filename;
  const char *table;
  int nthreads;
  int k;
  int test;
  int verbose;
} Config;

static int parse_cli(int argc, char **argv, Config *cfg);
static void format_filenames(char *filename_tf, char *filename_idf,
                             char *filename_doc_norms, const char *table,
                             long int entries);
static hash_t *preprocess_phase1(const Config *cfg, long int start,
                                 long int end);
static int preprocess_documents_sequential(const Config *cfg,
                                           const char *filename_tf,
                                           const char *filename_idf,
                                           const char *filename_doc_norms);
static double *compute_similarities_seq(const hash_t *query_tf,
                                        double query_norm, hash_t **global_tf,
                                        const double *global_doc_norms,
                                        long int num_docs);
static void free_global_structures(void);

int main(int argc, char *argv[]) {
  struct timespec t_start_total, t_end_total;
  clock_gettime(CLOCK_MONOTONIC, &t_start_total);

  Config cfg = {
      .nthreads = 1,
      .entries = 0,
      .db = "./data/wiki-small.db",
      .query_user = "shakespeare english literature",
      .query_filename = NULL,
      .table = "sample_articles",
      .k = 10,
      .test = 0,
      .verbose = 0};

  if (parse_cli(argc, argv, &cfg) != 0) {
    return 1;
  }

  cfg.nthreads = 1;  // Versão sequencial sempre usa uma única thread

  if (cfg.query_filename && strlen(cfg.query_filename) > 3)
    cfg.query_user = get_filecontent(cfg.query_filename);

  VERBOSE = cfg.verbose;

  // Configurar nível de log baseado em VERBOSE
  if (VERBOSE) {
    log_set_level(LOG_INFO);
  } else {
    log_set_level(LOG_WARN);
  }

  log_info(
      "Parâmetros nomeados:\n"
      "\targc: %d\n"
      "\tnthreads (forçado): %d\n"
      "\tentries: %ld\n"
      "\tdb: %s\n"
      "\tquery_user: %s\n"
      "\ttable: %s\n"
      "\ttest: %d\n"
      "\tk: %d",
      argc, cfg.nthreads, cfg.entries, cfg.db, cfg.query_user, cfg.table,
      cfg.test, cfg.k);

  const char *query_count = "select count(*) from \"%w\";";
  long int total = get_single_int(cfg.db, query_count, cfg.table);
  if (!cfg.entries || cfg.entries > total) {
    log_info(
        "Número de entradas %ld excedeu a quantidade total de documentos: %ld",
        cfg.entries, total);
    cfg.entries = total;
  }

  if (cfg.entries <= 0) {
    fprintf(stderr, "Nenhum documento disponível para processamento.\n");
    return 1;
  }

  char filename_tf[256];
  char filename_idf[256];
  char filename_doc_norms[256];
  format_filenames(filename_tf, filename_idf, filename_doc_norms, cfg.table,
                   cfg.entries);

  if (access(filename_tf, F_OK) == -1 || access(filename_idf, F_OK) == -1 ||
      access(filename_doc_norms, F_OK) == -1) {
    printf("Modelos não encontrados. Executando pré-processamento sequencial.\n");
    if (preprocess_documents_sequential(&cfg, filename_tf, filename_idf,
                                        filename_doc_norms) != 0) {
      free_global_structures();
      return 1;
    }
  } else {
    printf("Arquivos binários encontrados.. ");

    global_tf = load_hash_array(filename_tf, &global_entries);
    if (!global_tf) {
      fprintf(stderr, "Erro ao carregar global_tf de %s\n", filename_tf);
      free_global_structures();
      return 1;
    }

    global_idf = load_hash(filename_idf);
    if (!global_idf) {
      fprintf(stderr, "Erro ao carregar global_idf de %s\n", filename_idf);
      free_global_structures();
      return 1;
    }

    global_doc_norms = load_doc_norms(filename_doc_norms, &global_entries);
    if (!global_doc_norms) {
      fprintf(stderr, "Erro ao carregar global_doc_norms\n");
      free_global_structures();
      return 1;
    }

    printf("Estruturas carregadas com sucesso.\n");

    load_stopwords("assets/stopwords.txt");
  }

  if (cfg.query_user) {
    if (!global_stopwords) {
      load_stopwords("assets/stopwords.txt");
      if (!global_stopwords) {
        fprintf(stderr, "Falha ao carregar stopwords para processar query\n");
        free_global_structures();
        return 1;
      }
    }

    hash_t *query_tf;
    double query_norm;

    int result =
        preprocess_query(cfg.query_user, global_idf, &query_tf, &query_norm);
    if (result != 0) {
      fprintf(stderr, "Erro ao processar consulta do usuário\n");
    } else {
      log_info( "Consulta processada com sucesso!\n");
      log_info( "Norma da query: %.6f\n", query_norm);
      log_info( "Tamanho do vetor TF-IDF da query: %zu palavras\n", query_tf->size);
      if (VERBOSE) {
        printf("Palavras na query (após processamento):\n");
        for (size_t i = 0; i < query_tf->cap; i++) {
          for (HashEntry *e = query_tf->buckets[i]; e; e = e->next) {
            double idf = hash_find(global_idf, e->word);
            printf("  '%s': IDF=%.6f, TF-IDF=%.6f\n", e->word, idf, e->value);
          }
        }
      }

      struct timespec t_start_sim, t_end_sim;
      clock_gettime(CLOCK_MONOTONIC, &t_start_sim);

      double *similarities = compute_similarities_seq(
          query_tf, query_norm, global_tf, global_doc_norms, global_entries);

      clock_gettime(CLOCK_MONOTONIC, &t_end_sim);
      double elapsed_sim = get_elapsed_time(&t_start_sim, &t_end_sim);

      if (!similarities) {
        fprintf(stderr, "Erro ao calcular similaridades\n");
      } else {
        log_info( "\n[SIMILARIDADE] Tempo: %.3f segundos\n", elapsed_sim);

        DocSim *scores = (DocSim *)malloc(global_entries * sizeof(DocSim));
        if (scores) {
          for (long int i = 0; i < global_entries; i++) {
            scores[i].doc_id = i;
            scores[i].similarity = similarities[i];
          }

          qsort(scores, global_entries, sizeof(DocSim), compare_sim);

          long int top_k =
              global_entries < cfg.k ? global_entries : cfg.k;
          printf("\nTop %ld documentos mais similares:\n", top_k);
          printf("---------------------------------\n");
          long int *top_ids = (long int *)malloc(top_k * sizeof(long int));
          if (top_ids) {
            for (long int i = 0; i < top_k; i++) {
              top_ids[i] = scores[i].doc_id;
            }

            char **documents =
                get_documents_by_ids(cfg.db, cfg.table, top_ids, top_k);
            if (documents) {
              for (long int i = 0; i < top_k; i++) {
                if (documents[i]) {
                  printf("[%ld] %.6f  ", top_ids[i], scores[i].similarity);
                  if (strlen(documents[i]) > 100) {
                    const char *p = documents[i];
                    int j = 0;
                    for (; *p && j < 100; ++j, p++)
                      putchar(*p);
                    if (*p)
                      printf("...");
                    printf("\n");
                  } else {
                    printf("%s\n", documents[i]);
                  }
                  free(documents[i]);
                }
              }
              free(documents);
            }
            free(top_ids);
          }
          free(scores);
        }
        free(similarities);
      }

      hash_free(query_tf);
    }
  } else {
    printf("Nenhuma consulta fornecida\n");
  }

  if (VERBOSE) {
    printf("\nTop %d palavras (IDF):\n", PRINT_IDF_WORDS);
    printf("---------------------\n");
    for (size_t i = 0, c = 0; i < global_idf->cap && c < PRINT_IDF_WORDS; i++)
      for (HashEntry *e = global_idf->buckets[i]; e && c < PRINT_IDF_WORDS;
           e = e->next, c++)
        printf("%-15s %.2f\n", e->word, e->value);
  }

  free_global_structures();

  clock_gettime(CLOCK_MONOTONIC, &t_end_total);
  double elapsed_total = get_elapsed_time(&t_start_total, &t_end_total);
  printf("\n[TEMPO TOTAL] %.3f segundos\n", elapsed_total);

  return 0;
}

static int preprocess_documents_sequential(const Config *cfg,
                                           const char *filename_tf,
                                           const char *filename_idf,
                                           const char *filename_doc_norms) {
  global_entries = cfg->entries;
  global_idf = hash_new();

  global_tf = (hash_t **)calloc(global_entries, sizeof(hash_t *));
  if (!global_tf) {
    fprintf(stderr, "Falha ao alocar memória para global_tf\n");
    return -1;
  }

  for (long int i = 0; i < global_entries; i++) {
    global_tf[i] = hash_new();
    if (!global_tf[i]) {
      fprintf(stderr, "Falha ao alocar hash TF para documento %ld\n", i);
      return -1;
    }
  }

  load_stopwords("assets/stopwords.txt");
  if (!global_stopwords) {
    fprintf(stderr, "Falha ao carregar stopwords\n");
    return -1;
  }

  printf("Qtd. artigos: %ld\n", global_entries);

  struct timespec t_start_fase1, t_end_fase1;
  clock_gettime(CLOCK_MONOTONIC, &t_start_fase1);

  printf("\n[FASE 1] Construindo vocabulário (sequencial)...\n");

  hash_t *local_idf = preprocess_phase1(cfg, 0, global_entries);
  if (!local_idf) {
    fprintf(stderr, "Erro na FASE 1 do pré-processamento sequencial\n");
    return -1;
  }

  hash_merge(global_idf, local_idf);
  hash_free(local_idf);

  printf("[FASE 1] Vocabulário construído: %zu palavras\n", global_idf->size);
  printf("[FASE 1] Calculando IDF global...\n");
  for (size_t i = 0; i < global_idf->cap; i++) {
    HashEntry *e = global_idf->buckets[i];
    while (e) {
      if (e->value > 0)
        e->value = log2((double)global_entries / e->value);
      else
        e->value = 0.0;
      e = e->next;
    }
  }
  global_doc_norms = (double *)calloc(global_entries, sizeof(double));
  if (!global_doc_norms) {
    fprintf(stderr, "Erro ao alocar memória para global_doc_norms\n");
    return -1;
  }

  clock_gettime(CLOCK_MONOTONIC, &t_end_fase1);
  double elapsed_fase1 = get_elapsed_time(&t_start_fase1, &t_end_fase1);
  printf("[FASE 1] Concluída.. IDF computado e vocabulário com %zu palavras\n", global_idf->size);
  printf("[FASE 1] Tempo: %.3f segundos\n", elapsed_fase1);

  struct timespec t_start_fase2, t_end_fase2;
  clock_gettime(CLOCK_MONOTONIC, &t_start_fase2);

  printf("\n[FASE 2] Calculando TF-IDF e normas (sequencial)...\n");
  compute_tf_idf(global_tf, global_idf, global_entries, 0);
  compute_doc_norms(global_doc_norms, global_tf, global_entries, 0);

  clock_gettime(CLOCK_MONOTONIC, &t_end_fase2);
  double elapsed_fase2 = get_elapsed_time(&t_start_fase2, &t_end_fase2);
  printf("[FASE 2] TF-IDF e normas calculados!\n");
  printf("[FASE 2] Tempo: %.3f segundos\n", elapsed_fase2);

  printf("\nSalvando estruturas em disco\n");
  save_hash_array(global_tf, global_entries, filename_tf);
  save_hash(global_idf, filename_idf);
  save_doc_norms(global_doc_norms, global_entries, filename_doc_norms);

  free_stopwords();

  return 0;
}

static hash_t *preprocess_phase1(const Config *cfg, long int start,
                                 long int end) {
  long int count = end - start;
  if (count <= 0) {
    return NULL;
  }

  hash_t *idf = hash_new();
  if (!idf)
    return NULL;

  char **article_texts =
      get_str_arr(cfg->db,
                  "select article_text from \"%w\" "
                  "where article_id between ? and ? "
                  "order by article_id asc",
                  start, end - 1, cfg->table);

  if (!article_texts) {
    hash_free(idf);
    return NULL;
  }

  log_info( "[FASE 1][SEQ]: Tokenizando textos..");
  char ***article_vecs = tokenize(article_texts, count);
  if (!article_vecs) {
    for (long int i = 0; i < count; i++) {
      free(article_texts[i]);
    }
    free(article_texts);
    hash_free(idf);
    return NULL;
  }

  log_info( "[FASE 1][SEQ]: Removendo stopwords e Stemmizando..");
  log_info( "[FASE 1][SEQ]: Populando hash TF..");
  populate_tf_hash(global_tf, article_vecs, count, start);
  log_info( "[FASE 1][SEQ]: Populando vocabulário..");
  set_idf_words(idf, global_tf, start, count);

  for (long int i = 0; i < count; i++) {
    free(article_texts[i]);
  }
  free(article_texts);
  free_article_vecs(article_vecs, count);

  return idf;
}

static double *compute_similarities_seq(const hash_t *query_tf,
                                        double query_norm, hash_t **global_tf,
                                        const double *global_doc_norms,
                                        long int num_docs) {
  if (!query_tf || !global_tf || !global_doc_norms || num_docs <= 0) {
    return NULL;
  }

  double *similarities = (double *)calloc(num_docs, sizeof(double));
  if (!similarities) {
    return NULL;
  }

  for (long int doc_id = 0; doc_id < num_docs; doc_id++) {
    hash_t *doc_tf = global_tf[doc_id];
    if (!doc_tf) {
      similarities[doc_id] = 0.0;
      continue;
    }

    double dot_product = 0.0;
    for (size_t i = 0; i < query_tf->cap; i++) {
      for (HashEntry *query_entry = query_tf->buckets[i]; query_entry;
           query_entry = query_entry->next) {
        double doc_tfidf = hash_find(doc_tf, query_entry->word);
        if (doc_tfidf > 0.0) {
          dot_product += query_entry->value * doc_tfidf;
        }
      }
    }

    double doc_norm = global_doc_norms[doc_id];
    if (query_norm > 0.0 && doc_norm > 0.0) {
      similarities[doc_id] =
          dot_product / (query_norm * doc_norm);
    } else {
      similarities[doc_id] = 0.0;
    }
  }

  return similarities;
}

static void free_global_structures(void) {
  if (global_tf) {
    for (long int i = 0; i < global_entries; i++) {
      if (global_tf[i]) {
        hash_free(global_tf[i]);
      }
    }
    free(global_tf);
    global_tf = NULL;
  }

  if (global_idf) {
    hash_free(global_idf);
    global_idf = NULL;
  }

  if (global_doc_norms) {
    free(global_doc_norms);
    global_doc_norms = NULL;
  }

  free_stopwords();

  global_entries = 0;
}

static int parse_cli(int argc, char **argv, Config *cfg) {
  log_debug( "Processando %d argumentos", argc);
  fflush(stderr);

  for (int i = 1; i < argc; ++i) {
    log_debug( "argv[%d]: %s", i, argv[i]);
    fflush(stderr);

    if (strcmp(argv[i], "--nthreads") == 0 && i + 1 < argc)
      cfg->nthreads = atoi(argv[++i]);
    else if (strcmp(argv[i], "--entries") == 0 && i + 1 < argc)
      cfg->entries = atol(argv[++i]);
    else if (strcmp(argv[i], "--db") == 0 && i + 1 < argc)
      cfg->db = argv[++i];
    else if (strcmp(argv[i], "--query_user") == 0 && i + 1 < argc)
      cfg->query_user = argv[++i];
    else if (strcmp(argv[i], "--query_filename") == 0 && i + 1 < argc)
      cfg->query_filename = argv[++i];
    else if (strcmp(argv[i], "--table") == 0 && i + 1 < argc)
      cfg->table = argv[++i];
    else if (strcmp(argv[i], "--k") == 0 && i + 1 < argc)
      cfg->k = atoi(argv[++i]);
    else if (strcmp(argv[i], "--test") == 0 && i + 1 < argc)
      cfg->test = atoi(argv[++i]);
    else if (strcmp(argv[i], "--verbose") == 0)
      cfg->verbose = 1;
    else {
      fprintf(stderr,
              "Uso: %s <parametros nomeados>\n"
              "  --verbose: Verbosidade (default: 0)\n"
              "  --nthreads: Número de threads (default: 4)\n"
              "  --entries: Quantidade de entradas para pré-processamento "
              "(default: Toda tabela 'sample_articles')\n"
              "  --db: Nome do arquivo Sqlite (default: './data/wiki-small.db')\n"
              "  --query_user: Consulta do usuário (default: 'shakespeare english "
              "literature')\n"
              "  --query_filename: Arquivo com a consulta do usuário\n"
              "  --table: Nome da tabela consultada (default: 'sample_articles')\n"
              "  --k: Top-k documentos mais similares (default: 10)\n"
              "  --test: Modo de teste (default: 0)\n",
              argv[0]);
      return 1;
    }
  }
  return 0;
}

static void format_filenames(char *filename_tf, char *filename_idf,
                             char *filename_doc_norms, const char *table,
                             long int entries) {
  snprintf(filename_tf, 256, "models/tf_%s_%ld.bin", table, entries);
  snprintf(filename_idf, 256, "models/idf_%s_%ld.bin", table, entries);
  snprintf(filename_doc_norms, 256, "models/doc_norms_%s_%ld.bin", table,
           entries);
}
