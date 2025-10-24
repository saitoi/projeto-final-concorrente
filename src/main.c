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
#include <unistd.h>

#include "../include/file_io.h"
#include "../include/hash_t.h"
#include "../include/log.h"
#include "../include/preprocess.h"
#include "../include/preprocess_query.h"
#include "../include/sqlite_helper.h"

/* --------------- Variáveis globais --------------- */

/** @defgroup sync Variáveis de Sincronização
 *  Estruturas para sincronização entre threads
 *  @{
 */
pthread_mutex_t mutex;           /**< Mutex para proteção de seções críticas */
pthread_barrier_t barrier;       /**< Barreira para sincronização de threads */
/** @} */

/** @defgroup global_data Estruturas Globais de Dados
 *  Hashes e vetores compartilhados entre threads
 *  @{
 */
hash_t **global_tf;              /**< Array de hashes TF (Term Frequency) por documento */
hash_t *global_idf;              /**< Hash IDF (Inverse Document Frequency) global */
double *global_doc_norms;        /**< Array com normas dos vetores de documentos */
size_t global_vocab_size;        /**< Tamanho do vocabulário (palavras únicas) */
long int global_entries = 0;     /**< Número total de documentos processados */
/** @} */

/** @defgroup config Variáveis de Configuração
 *  @{
 */
int VERBOSE = 0;                 /**< Flag de verbosidade (0=desabilitado, 1=habilitado) */
/** @} */

/* --------------- Macros --------------- */

#define MAX_THREADS 16           /**< Número máximo de threads suportadas */

/**
 * @struct thread_args
 * @brief Argumentos passados para cada thread de pré-processamento
 */
typedef struct {
  long int start;                /**< Índice inicial do intervalo de documentos */
  long int end;                  /**< Índice final do intervalo de documentos */
  long int nthreads;             /**< Número total de threads */
  long int id;                   /**< ID da thread (0 a nthreads-1) */
  const char *filename_db;       /**< Caminho para o arquivo SQLite */
  const char *tablename;         /**< Nome da tabela no banco de dados */
} thread_args;

/**
 * @struct Config
 * @brief Estrutura de configuração do programa
 *
 * Agrupa todos os parâmetros de linha de comando em uma única estrutura.
 */
typedef struct {
  long int entries;              /**< Quantidade de entradas a processar (0=todas) */
  const char *filename_db;       /**< Arquivo SQLite com os documentos */
  const char *query_user;        /**< Query do usuário (string direta) */
  const char *query_filename;    /**< Arquivo contendo a query do usuário */
  const char *tablename;         /**< Nome da tabela no banco de dados */
  int nthreads;                  /**< Número de threads para pré-processamento */
  int k;                         /**< Número de documentos top-k a retornar */
  int test;                      /**< Modo de teste (0=desabilitado) */
  int verbose;                   /**< Verbosidade (0=desabilitado, 1=habilitado) */
} Config;

typedef struct {
  long int doc_id;
  double similarity;
} DocSim;

int parse_cli(int argc, char **argv, Config *cfg);
int compare_sim(const void *a, const void *b);
void *preprocess_1(void *args);
void *preprocess_2(void *args);
void format_filenames(char *filename_tf, char *filename_idf,
                      char *filename_doc_norms, const char *tablename,
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
  // Inicializar configuração com valores padrão
  Config cfg = {
    .nthreads = 4,
    .entries = 0,
    .filename_db = "./data/wiki-small.db",
    .query_user = "shakespeare english literature",
    .query_filename = NULL,
    .tablename = "sample_articles",
    .k = 10,
    .test = 0,
    .verbose = 0
  };

  // [1]
  if (parse_cli(argc, argv, &cfg) != 0) {
    return 1;
  }

  // Ler query de arquivo se fornecido
  if (cfg.query_filename && strlen(cfg.query_filename) > 3)
    cfg.query_user = get_filecontent(cfg.query_filename);

  // Atualiza variável global de verbosidade
  VERBOSE = cfg.verbose;

  LOG(stdout,
      "Parâmetros nomeados:\n"
      "\targc: %d\n"
      "\tnthreads: %d\n"
      "\tentries: %ld\n"
      "\tfilename_db: %s\n"
      "\tquery_user: %s\n"
      "\ttablename: %s\n"
      "\ttest: %d"
      "\tk: %d",
      argc, cfg.nthreads, cfg.entries, cfg.filename_db, cfg.query_user, cfg.tablename, cfg.test, cfg.k);

  if (cfg.nthreads <= 0 || cfg.nthreads > MAX_THREADS) {
    fprintf(stderr,
            "Número de threads inválido (%d). Deve estar entre 1 e %d\n",
            cfg.nthreads, MAX_THREADS);
    return 1;
  }

  pthread_mutex_init(&mutex, NULL);

  // Determinar número de entradas primeiro (para criar nomes de arquivo)
  const char *query_count = "select count(*) from \"%w\";";
  long int total = get_single_int(cfg.filename_db, query_count, cfg.tablename);
  if (!cfg.entries || cfg.entries > total) {
    LOG(stdout, "Número de entradas %ld excedeu a quantidade total de documentos: %ld", cfg.entries, total);
    cfg.entries = total;
  }

  // Criar nomes de arquivo com tablename e número de entradas
  char filename_tf[256];
  char filename_idf[256];
  char filename_doc_norms[256];
  format_filenames(filename_tf, filename_idf, filename_doc_norms,
                   cfg.tablename, cfg.entries);

  // Caso o arquivo não exista: Pré-processamento
  if (access(filename_tf, F_OK) == -1 ||
      access(filename_idf, F_OK) == -1 ||
      access(filename_doc_norms, F_OK) == -1) {

    pthread_t *tids = (pthread_t*) malloc(sizeof(pthread_t) * cfg.nthreads);
    if (!tids) {
      fprintf(stderr, "Falha ao alocar memória para tids\n");
      return 1;
    }

    thread_args args[MAX_THREADS];

    // Inicializar estruturas globais
    global_idf = hash_new();
    global_tf = (hash_t **)calloc(cfg.entries, sizeof(hash_t *));
    if (!global_tf) {
      fprintf(stderr, "Falha ao alocar memória para global_tf\n");
      return 1;
    }

    // Inicializar cada hash TF individual
    for (long int i = 0; i < cfg.entries; i++) {
      global_tf[i] = hash_new();
      if (!global_tf[i]) {
        fprintf(stderr, "Falha ao alocar hash TF para documento %ld\n", i);
        return 1;
      }
    }

    global_entries = cfg.entries;

    // Carregar stopwords (compartilhado por todas threads)
    load_stopwords("assets/stopwords.txt");
    if (!global_stopwords) {
      fprintf(stderr, "Falha ao carregar stopwords\n");
      return 1;
    }

    printf("Qtd. artigos: %ld\n", cfg.entries);

    // Calcular divisão de trabalho
    long int base = cfg.entries / cfg.nthreads;
    long int rem = cfg.entries % cfg.nthreads;

    /* ---------- FASE 1: Construir Vocabulário ---------- */

    printf("\n[FASE 1] Construindo vocabulário...\n");

    for (long int i = 0; i < cfg.nthreads; ++i) {
      args[i].id = i;
      args[i].nthreads = cfg.nthreads;
      args[i].filename_db = cfg.filename_db;
      args[i].tablename = cfg.tablename;
      args[i].start = i * base + (i < rem ? i : rem);
      args[i].end = args[i].start + base + (i < rem);

      if (pthread_create(&tids[i], NULL, preprocess_1, (void *)&args[i])) {
        fprintf(stderr, "Erro ao criar thread %ld\n", i);
        return 1;
      }
    }

    // Aguardar conclusão da Fase 1
    for (long int i = 0; i < cfg.nthreads; ++i) {
      if (pthread_join(tids[i], NULL)) {
        fprintf(stderr, "Erro ao esperar thread %ld\n", i);
        return 1;
      }
    }

    printf("[FASE 1] Vocabulário construído: %zu palavras\n", hash_size(global_idf));

    // Calcular IDF global (single-threaded, entre as fases)
    printf("[FASE 1] Calculando IDF global...\n");
    set_idf_value(global_idf, global_tf, (double)global_entries, global_entries);
    global_vocab_size = hash_size(global_idf);

    // Alocar normas
    global_doc_norms = (double *)calloc(global_entries, sizeof(double));
    if (!global_doc_norms) {
      fprintf(stderr, "Erro ao alocar memória para global_doc_norms\n");
      return 1;
    }

    printf("IDF computado. Tamanho do vocabulário: %zu palavras\n", global_vocab_size);

    /* ========== FASE 2: Calcular TF-IDF ========== */
    printf("\n[FASE 2] Calculando TF-IDF e normas...\n");

    for (long int i = 0; i < cfg.nthreads; ++i) {
      if (pthread_create(&tids[i], NULL, preprocess_2, (void *)&args[i])) {
        fprintf(stderr, "Erro ao criar thread %ld\n", i);
        return 1;
      }
    }

    // Aguardar conclusão da Fase 2
    for (long int i = 0; i < cfg.nthreads; ++i) {
      if (pthread_join(tids[i], NULL)) {
        fprintf(stderr, "Erro ao esperar thread %ld\n", i);
        return 1;
      }
    }

    printf("[FASE 2] TF-IDF e normas calculados!\n");

    // Imprimir TF hash global final
    LOG(stdout, "=== TF Hash Global Final ===");
    // TODO: Implementar print para hash_t**
    // print_tf_hash(global_tf, -1, VERBOSE);

    // [15]
    // Salvar estruturas globais em arquivos binários
    printf("\nSalvando estruturas em disco\n");

    save_hash_array(global_tf, global_entries, filename_tf);
    save_hash(global_idf, filename_idf);
    save_doc_norms(global_doc_norms, global_entries, filename_doc_norms);

    // Liberar stopwords (usado apenas no pré-processamento)
    free_stopwords();

    // Liberar array de thread IDs
    free(tids);

  } else {

    /* --------------- Carregamento dos Binários --------------- */

    printf("Arquivos binários encontrados, carregando estruturas...\n");

    global_tf = load_hash_array(filename_tf, &global_entries);
    if (!global_tf) {
      fprintf(stderr, "Erro ao carregar global_tf de %s\n", filename_tf);
      pthread_mutex_destroy(&mutex);
      return 1;
    }

    global_idf = load_hash(filename_idf);
    if (!global_idf) {
      fprintf(stderr, "Erro ao carregar global_idf de %s\n", filename_idf);
      // Liberar global_tf
      for (long int i = 0; i < global_entries; i++) {
        if (global_tf[i])
          hash_free(global_tf[i]);
      }
      free(global_tf);
      pthread_mutex_destroy(&mutex);
      return 1;
    }

    global_doc_norms = load_doc_norms(filename_doc_norms, &global_entries);
    if (!global_doc_norms) {
      fprintf(stderr, "Erro ao carregar global_doc_norms\n");
      hash_free(global_idf);
      for (long int i = 0; i < global_entries; i++) {
        if (global_tf[i])
          hash_free(global_tf[i]);
      }
      free(global_tf);
      pthread_mutex_destroy(&mutex);
      return 1;
    }

    global_vocab_size = hash_size(global_idf);

    printf("Todas as estruturas foram carregadas com sucesso!\n");

    // Carregar stopwords para processar queries
    load_stopwords("assets/stopwords.txt");
  }

  /* --------------- Consulta do Usuário --------------- */

  if (cfg.query_user) {
    // printf("Consulta do usuário: %s\n", cfg.query_user);

    // Carregar stopwords se não estiverem carregadas
    if (!global_stopwords) {
      load_stopwords("assets/stopwords.txt");
      if (!global_stopwords) {
        fprintf(stderr, "Falha ao carregar stopwords para processar query\n");
        return 1;
      }
    }

    // Processar query (sem threads - é um vetor pequeno)
    hash_t *query_tf;
    double query_norm;

    int result = preprocess_query(cfg.query_user, global_idf, &query_tf, &query_norm);
    if (result != 0) {
      fprintf(stderr, "Erro ao processar consulta do usuário\n");
    } else {
      printf("Consulta processada com sucesso!\n");
      printf("Norma da query: %.6f\n", query_norm);
      printf("Tamanho do vetor TF-IDF da query: %zu palavras\n", hash_size(query_tf));

      // DEBUG: Exibir palavras da query
      printf("Palavras na query (após processamento):\n");
      for (size_t i = 0; i < query_tf->cap; i++) {
        for (HashEntry *e = query_tf->buckets[i]; e; e = e->next) {
          printf("  '%s': TF-IDF=%.6f\n", e->word, e->value);
        }
      }

      // Calcular similaridade com todos os documentos
      double *similarities = compute_similarities(query_tf, query_norm, global_tf,
                                                   global_doc_norms, global_entries, cfg.nthreads);
      if (!similarities) {
        fprintf(stderr, "Erro ao calcular similaridades\n");
      } else {
        printf("\n=== Similaridades (Top 10) ===\n");

        // Encontrar os top-k documentos mais similares
        // Criar array de índices

        DocSim *scores = (DocSim *)malloc(global_entries * sizeof(DocSim));
        if (scores) {
          for (long int i = 0; i < global_entries; i++) {
            scores[i].doc_id = i;
            scores[i].similarity = similarities[i];
          }

          qsort(scores, global_entries, sizeof(DocSim), compare_sim);

          // Exibir top-k
          long int top_k = global_entries < cfg.k ? global_entries : cfg.k;
          printf("Exibindo top %ld documentos:\n", top_k);
          for (long int i = 0; i < top_k; i++) {
            printf("Doc %ld: %.6f\n", scores[i].doc_id, scores[i].similarity);
          }

          // Buscar o corpus dos top-k documentos
          printf("\n=== Corpus dos Top-%ld Documentos ===\n", top_k);
          long int *top_ids = (long int *)malloc(top_k * sizeof(long int));
          if (top_ids) {
            for (long int i = 0; i < top_k; i++) {
              top_ids[i] = scores[i].doc_id;
            }

            char **documents = get_documents_by_ids(cfg.filename_db, cfg.tablename, top_ids, top_k);
            if (documents) {
              for (long int i = 0; i < top_k; i++) {
                if (documents[i]) {
                  printf("\n--- Documento %ld (similaridade: %.6f) ---\n",
                         top_ids[i], scores[i].similarity);
                  // Limitar a exibição a 200 caracteres
                  if (strlen(documents[i]) > 200) {
                    printf("%.200s...\n", documents[i]);
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

      // Liberar hash da query
      hash_free(query_tf);
    }
  } else {
    printf("Nenhuma consulta fornecida\n");
  }

  // Impressão das palavras com IDF (primeiras 30 entradas)
  if (VERBOSE) {
    printf("\n=== Primeiras 30 palavras com IDF ===\n");
    size_t count = 0;
    size_t limit = 30;

    for (size_t i = 0; i < global_idf->cap && count < limit; i++) {
      for (HashEntry *e = global_idf->buckets[i]; e && count < limit;
           e = e->next) {
        printf("Palavra: %-25s IDF: %.6f\n", e->word, e->value);
        count++;
      }
    }
  }

  // Liberar todas as estruturas globais
  LOG(stderr, "DEBUG: Liberando global_tf (%ld documentos)", global_entries);
  if (global_tf) {
    for (long int i = 0; i < global_entries; i++) {
      if (global_tf[i]) {
        hash_free(global_tf[i]);
      }
    }
    free(global_tf);
  }
  LOG(stderr, "DEBUG: global_tf liberado");
  if (global_idf)
    hash_free(global_idf);

  // Liberar normas
  if (global_doc_norms)
    free(global_doc_norms);

  pthread_mutex_destroy(&mutex);

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
 * - --filename_db: Arquivo SQLite
 * - --query_user: Query direta do usuário
 * - --query_filename: Arquivo contendo query
 * - --tablename: Nome da tabela no banco
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
  LOG(stderr, "Processando %d argumentos", argc);
  fflush(stderr);

  for (int i = 1; i < argc; ++i) {
    LOG(stderr, "argv[%d]: %s", i, argv[i]);
    fflush(stderr);

    if (strcmp(argv[i], "--nthreads") == 0 && i + 1 < argc) 
      cfg->nthreads = atoi(argv[++i]);
    else if (strcmp(argv[i], "--entries") == 0 && i + 1 < argc) 
      cfg->entries = atol(argv[++i]);
    else if (strcmp(argv[i], "--filename_db") == 0 && i + 1 < argc) 
      cfg->filename_db = argv[++i];
    else if (strcmp(argv[i], "--query_user") == 0 && i + 1 < argc)
      cfg->query_user = argv[++i];
    else if (strcmp(argv[i], "--query_filename") == 0 && i + 1 < argc)
      cfg->query_filename = argv[++i];
    else if (strcmp(argv[i], "--tablename") == 0 && i + 1 < argc)
      cfg->tablename = argv[++i];
    else if (strcmp(argv[i], "--k") == 0 && i + 1 < argc)
      cfg->k = atoi(argv[++i]);
    else if (strcmp(argv[i], "--test") == 0 && i + 1 < argc)
      cfg->test = atoi(argv[++i]);
    else if (strcmp(argv[i], "--verbose") == 0)
      cfg->verbose = 1;
    else {
      fprintf(stderr,
        "Uso: %s <parametros nomeados>\n"
        "--verbose: Verbosidade (default: 0)\n"
        "--nthreads: Número de threads (default: 4)\n"
        "--entries: Quantidade de entradas para pré-processamento (default: "
        "Toda tabela 'sample_articles')\n"
        "--filename_db: Nome do arquivo Sqlite (default: './data/wiki-small.db')\n"
        "--query_user: Consulta do usuário (default: 'shakespeare english literature')\n"
        "--query_filename: Arquivo com a consulta do usuário\n"
        "--tablename: Nome da tabela consultada (default: "
        "'sample_articles')\n"
        "--k: Top-k documentos mais similares (default: 10)\n"
        "--test: Modo de teste (default: 0)\n",
        argv[0]);
      return 1;
    }
  }
  return 0;
}

/**
 * @brief FASE 1: Construir vocabulário e TF local
 *
 * Pipeline:
 * 1. Extrair textos do SQLite
 * 2. Tokenizar
 * 3. Remover stopwords
 * 4. Stemming
 * 5. Popular TF local
 * 6. Popular vocabulário local
 * 7. Merge nas estruturas globais (com mutex)
 *
 * @param arg Ponteiro para thread_args
 * @return NULL
 */
void *preprocess_1(void *arg) {
  thread_args *t = (thread_args *)arg;
  long int count = t->end - t->start;

  LOG(stdout, "[FASE 1] T%ld : Processando %ld documentos [%ld, %ld]",
      t->id, count, t->start, t->end - 1);

  if (count <= 0) {
    pthread_exit(NULL);
  }

  hash_t *idf = hash_new();

  // [1] Recuperar textos
  char **article_texts = get_str_arr(t->filename_db,
                                      "select article_text from \"%w\" "
                                      "where article_id between ? and ? "
                                      "order by article_id asc",
                                      t->start, t->end - 1, t->tablename);
  if (!article_texts) {
    fprintf(stderr, "Thread %ld: Erro ao obter dados do banco\n", t->id);
    pthread_exit(NULL);
  }

  // [2] Tokenizar
  LOG(stdout, "[FASE 1] T%ld: Tokenizando textos..", t->id);
  char ***article_vecs = tokenize(article_texts, count);
  if (!article_vecs) {
    fprintf(stderr, "Thread %ld: Erro ao tokenizar\n", t->id);
    pthread_exit(NULL);
  }

  // [3] Remover stopwords
  LOG(stdout, "[FASE 1] T%ld: Removendo stopwords..", t->id);
  remove_stopwords(article_vecs, count);

  // [4] Stemming
  LOG(stdout, "[FASE 1] T%ld: Stemming..", t->id);
  stem(article_vecs, count);

  // [5] Popular TF local
  LOG(stdout, "[FASE 1] T%ld: Populando hash TF..", t->id);
  populate_tf_hash(global_tf, article_vecs, count, t->start);

  // [6] Popular vocabulário local
  LOG(stdout, "[FASE 1] T%ld: Populando vocabulário..", t->id);
  set_idf_words(idf, article_vecs, count);

  // [7] Merge nas estruturas globais (seção crítica)
  LOG(stdout, "[FASE 1] T%ld: Populando vocabulário e frequências globais..", t->id);
  pthread_mutex_lock(&mutex);
  hash_merge(global_idf, idf);
  pthread_mutex_unlock(&mutex);

  LOG(stdout, "[FASE 1] T%ld: Concluída", t->id);

  // Limpar memória local
  for (long int i = 0; i < count; i++) {
    if (article_texts[i]) free(article_texts[i]);
  }

  free(article_texts);
  free_article_vecs(article_vecs, count);
  // free(tf);  // Ponteiros movidos para global_tf
  hash_free(idf);
  pthread_exit(NULL);
}

/**
 * @brief FASE 2: Calcular TF-IDF e normas
 *
 * Usa IDF global já calculado para:
 * 1. Transformar TF em TF-IDF
 * 2. Calcular normas dos vetores
 *
 * @param arg Ponteiro para thread_args
 * @return NULL
 */
void *preprocess_2(void *arg) {
  thread_args *t = (thread_args *)arg;
  long int count = t->end - t->start;

  LOG(stdout, "Thread %ld [FASE 2]: Calculando TF-IDF e normas", t->id);

  if (count <= 0) {
    pthread_exit(NULL);
  }

  // [1] Converter TF para TF-IDF usando IDF global
  compute_tf_idf(global_tf, global_idf, count, t->start);

  // [2] Calcular normas dos documentos
  compute_doc_norms(global_doc_norms, global_tf, count, global_vocab_size, t->start);

  LOG(stdout, "Thread %ld [FASE 2]: Concluída", t->id);
  pthread_exit(NULL);
}

/**
 * @brief Formata nomes de arquivos de modelo com tablename e entries
 *
 * Cria nomes no formato: models/<tipo>_<tablename>_<entries>.bin
 * Exemplo: models/tf_sample_articles_1000.bin
 *
 * @param filename_tf Buffer para nome do arquivo TF (mín. 256 bytes)
 * @param filename_idf Buffer para nome do arquivo IDF (mín. 256 bytes)
 * @param filename_doc_norms Buffer para nome do arquivo de normas (mín. 256 bytes)
 * @param tablename Nome da tabela
 * @param entries Número de entradas
 */
void format_filenames(char *filename_tf, char *filename_idf,
                      char *filename_doc_norms, const char *tablename,
                      long int entries) {
  snprintf(filename_tf, 256, "models/tf_%s_%ld.bin", tablename, entries);
  snprintf(filename_idf, 256, "models/idf_%s_%ld.bin", tablename, entries);
  snprintf(filename_doc_norms, 256, "models/doc_norms_%s_%ld.bin", tablename, entries);
}

int compare_sim(const void *a, const void *b) {
    const DocSim *doc1 = (const DocSim *)a;
    const DocSim *doc2 = (const DocSim *)b;
    return doc1->similarity > doc2->similarity ? -1 : doc1->similarity < doc2->similarity;
}