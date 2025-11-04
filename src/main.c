/**
 * @file main.c
 * @brief Sistema de busca de documentos usando TF-IDF e similaridade de cosseno
 *
 * Este programa implementa um sistema de recuperação de informações que:
 * - Pré-processa documentos de um banco SQLite
 * - Calcula vetores TF-IDF para cada documento
 * - Processa queries de usuários
 * - Retorna os k documentos mais similares usando similaridade de cosseno
 *
 * @authors
 * - Pedro Henrique Honorio Saito
 * - Milton Salgado Leandro
 * - Marcos Henrique Junqueira Muniz Barbi Silva
 */

#include <pthread.h>
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

static inline double get_elapsed_time(struct timespec *start,
                                      struct timespec *end) {
  return (end->tv_sec - start->tv_sec) + (end->tv_nsec - start->tv_nsec) / 1e9;
}

/* --------------- Variáveis globais --------------- */

/** @defgroup global_data Estruturas Globais de Dados
 *  Hashes e vetores compartilhados entre threads
 *  @{
 */
hash_t **global_tf; /**< Array de hashes TF (Term Frequency) por documento */
hash_t *global_idf; /**< Hash IDF (Inverse Document Frequency) global */
double *global_doc_norms;    /**< Array com normas dos vetores de documentos */
size_t global_vocab_size;    /**< Tamanho do vocabulário (palavras únicas) */
long int global_entries = 0; /**< Número total de documentos processados */
/** @} */

/** @defgroup config Variáveis de Configuração
 *  @{
 */
int VERBOSE = 0; /**< Flag de verbosidade (0=desabilitado, 1=habilitado) */
/** @} */

/* --------------- Macros --------------- */

#define MAX_THREADS 16 /**< Número máximo de threads suportadas */
#define PRINT_IDF_WORDS 20

/**
 * @struct Config
 * @brief Estrutura de configuração do programa
 *
 * Agrupa todos os parâmetros de linha de comando em uma única estrutura.
 */
typedef struct {
  long int entries;       /**< Quantidade de entradas a processar (0=todas) */
  const char *db;         /**< Arquivo SQLite com os documentos */
  const char *query_user; /**< Query do usuário (string direta) */
  const char *query_filename; /**< Arquivo contendo a query do usuário */
  const char *table;          /**< Nome da tabela no banco de dados */
  int nthreads;               /**< Número de threads para pré-processamento */
  int k;                      /**< Número de documentos top-k a retornar */
  int test;                   /**< Modo de teste (0=desabilitado) */
  int verbose;                /**< Verbosidade (0=desabilitado, 1=habilitado) */
} Config;

int parse_cli(int argc, char **argv, Config *cfg);
void format_filenames(char *filename_tf, char *filename_idf,
                      char *filename_doc_norms, const char *table,
                      long int entries);

/* --------------- Fluxo Principal --------------- */

/**
 * @brief Função principal do programa
 *
 * Fluxo de execução:
 * 1. Parseia argumentos de linha de comando
 * 2. Verifica existência de modelos pré-processados em models/
 * 3. Se não existirem: Executa pré-processamento paralelo com threads
 * 4. Se existirem: Carrega estruturas dos arquivos binários
 * 5. Processa query do usuário e calcula similaridades
 * 6. Retorna top-k documentos mais similares
 *
 * @param argc Número de argumentos
 * @param argv Array de argumentos
 * @return 0 em sucesso, 1 em erro
 */
int main(int argc, char *argv[]) {
  struct timespec t_start_total, t_end_total;
  clock_gettime(CLOCK_MONOTONIC, &t_start_total);

  // Configurar nível de log padrão (não verbose)
  log_set_level(LOG_WARN);

  // Inicializar configuração com valores padrão
  Config cfg = {.nthreads = 4,
                .entries = 0,
                .db = "./data/wiki-small.db",
                .query_user = "shakespeare english literature",
                .query_filename = NULL,
                .table = "sample_articles",
                .k = 10,
                .test = 0,
                .verbose = 0};

  // [1]
  if (parse_cli(argc, argv, &cfg) != 0) {
    return 1;
  }

  // Ler query de arquivo se fornecido
  if (cfg.query_filename && strlen(cfg.query_filename) > 3)
    cfg.query_user = get_filecontent(cfg.query_filename);

  // Atualiza variável global de verbosidade e reconfigura log level
  VERBOSE = cfg.verbose;
  if (VERBOSE) {
    log_set_level(LOG_INFO); // Mostra INFO, WARN, ERROR, FATAL
  }

  log_info("Parâmetros nomeados:\n"
           "\targc: %d\n"
           "\tnthreads: %d\n"
           "\tentries: %ld\n"
           "\tdb: %s\n"
           "\tquery_user: %s\n"
           "\ttable: %s\n"
           "\ttest: %d\n"
           "\tk: %d",
           argc, cfg.nthreads, cfg.entries, cfg.db, cfg.query_user, cfg.table,
           cfg.test, cfg.k);

  if (cfg.nthreads <= 0 || cfg.nthreads > MAX_THREADS) {
    fprintf(stderr,
            "Número de threads inválido (%d). Deve estar entre 1 e %d\n",
            cfg.nthreads, MAX_THREADS);
    return 1;
  }

  // Determinar número de entradas primeiro (para criar nomes de arquivo)
  const char *query_count = "select count(*) from \"%w\";";
  long int total = get_single_int(cfg.db, query_count, cfg.table);
  if (!cfg.entries || cfg.entries > total) {
    log_info(
        "Número de entradas %ld excedeu a quantidade total de documentos: %ld",
        cfg.entries, total);
    cfg.entries = total;
  }

  // Criar nomes de arquivo com table e número de entradas
  char filename_tf[256];
  char filename_idf[256];
  char filename_doc_norms[256];
  format_filenames(filename_tf, filename_idf, filename_doc_norms, cfg.table,
                   cfg.entries);

  // Pré-processar ou carregar modelos existentes
  int load_result;
  if (access(filename_tf, F_OK) == -1 || access(filename_idf, F_OK) == -1 ||
      access(filename_doc_norms, F_OK) == -1) {
    load_result =
        run_preprocessing(cfg.nthreads, cfg.entries, cfg.db, cfg.table,
                          filename_tf, filename_idf, filename_doc_norms);
  } else {
    load_result = load_models(filename_tf, filename_idf, filename_doc_norms);
  }

  if (load_result != 0) {
    return 1;
  }

  /* --------------- Consulta do Usuário --------------- */

  if (process_and_display_query(cfg.query_user, cfg.nthreads, cfg.k, cfg.db,
                                cfg.table) != 0) {
    return 1;
  }

  // Impressão das palavras com IDF (primeiras 5 entradas)
  if (VERBOSE) {
    printf("\nTop %d palavras (IDF):\n", PRINT_IDF_WORDS);
    printf("---------------------\n");
    for (size_t i = 0, c = 0; i < global_idf->cap && c < PRINT_IDF_WORDS; i++)
      for (HashEntry *e = global_idf->buckets[i]; e && c < PRINT_IDF_WORDS;
           e = e->next, c++)
        printf("%-15s %.2f\n", e->word, e->value);
  }

  // Liberar todas as estruturas globais
  log_debug("DEBUG: Liberando global_tf (%ld documentos)", global_entries);
  if (global_tf) {
    for (long int i = 0; i < global_entries; i++) {
      if (global_tf[i]) {
        hash_free(global_tf[i]);
      }
    }
    free(global_tf);
  }
  log_debug("DEBUG: global_tf liberado");
  if (global_idf)
    hash_free(global_idf);

  // Liberar normas
  if (global_doc_norms)
    free(global_doc_norms);

  // Liberar stopwords
  free_stopwords();

  clock_gettime(CLOCK_MONOTONIC, &t_end_total);
  double elapsed_total = get_elapsed_time(&t_start_total, &t_end_total);
  printf("\n[TEMPO TOTAL] %.3f segundos\n", elapsed_total);

  return 0;
}

/**
 * @brief Processa argumentos de linha de comando
 *
 * Parseia argumentos nomeados no formato --parametro valor e atualiza
 * a estrutura Config fornecida.
 *
 * Parâmetros suportados:
 * - --nthreads: Número de threads
 * - --entries: Quantidade de documentos a processar
 * - --db: Arquivo SQLite
 * - --query_user: Query direta do usuário
 * - --query_filename: Arquivo contendo query
 * - --table: Nome da tabela no banco
 * - --k: Top-k documentos a retornar
 * - --test: Modo de teste
 * - --verbose: Ativa modo verboso
 *
 * @param argc Número de argumentos
 * @param argv Array de argumentos
 * @param cfg Ponteiro para estrutura Config a ser preenchida
 * @return 0 em sucesso, 1 em erro (argumento inválido)
 */
int parse_cli(int argc, char **argv, Config *cfg) {
  log_debug("Processando %d argumentos", argc);
  fflush(stderr);

  for (int i = 1; i < argc; ++i) {
    log_debug("argv[%d]: %s", i, argv[i]);
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
      fprintf(
          stderr,
          "Uso: %s <parametros nomeados>\n"
          "  --verbose: Verbosidade (default: 0)\n"
          "  --nthreads: Número de threads (default: 4)\n"
          "  --entries: Quantidade de entradas para pré-processamento "
          "(default: "
          "Toda tabela 'sample_articles')\n"
          "  --db: Nome do arquivo Sqlite (default: './data/wiki-small.db')\n"
          "  --query_user: Consulta do usuário (default: 'shakespeare english "
          "literature')\n"
          "  --query_filename: Arquivo com a consulta do usuário\n"
          "  --table: Nome da tabela consultada (default: "
          "'sample_articles')\n"
          "  --k: Top-k documentos mais similares (default: 10)\n"
          "  --test: Modo de teste (default: 0)\n",
          argv[0]);
      return 1;
    }
  }
  return 0;
}

/**
 * @brief Formata nomes de arquivos de modelo com table e entries
 *
 * Cria nomes no formato: models/<tipo>_<table>_<entries>.bin
 * Exemplo: models/tf_sample_articles_1000.bin
 *
 * @param filename_tf Buffer para nome do arquivo TF (mín. 256 bytes)
 * @param filename_idf Buffer para nome do arquivo IDF (mín. 256 bytes)
 * @param filename_doc_norms Buffer para nome do arquivo de normas (mín. 256
 * bytes)
 * @param table Nome da tabela
 * @param entries Número de entradas
 */
void format_filenames(char *filename_tf, char *filename_idf,
                      char *filename_doc_norms, const char *table,
                      long int entries) {
  snprintf(filename_tf, 256, "models/tf_%s_%ld.bin", table, entries);
  snprintf(filename_idf, 256, "models/idf_%s_%ld.bin", table, entries);
  snprintf(filename_doc_norms, 256, "models/doc_norms_%s_%ld.bin", table,
           entries);
}
