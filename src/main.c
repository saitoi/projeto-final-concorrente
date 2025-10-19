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

#include "../include/file_io.h"
#include "../include/hash_t.h"
#include "../include/log.h"
#include "../include/preprocess.h"
#include "../include/preprocess_query.h"
#include "../include/sqlite_helper.h"

/* --------------- Variáveis globais --------------- */

// Variáveis de sincronização
pthread_mutex_t mutex;
pthread_barrier_t barrier;
pthread_once_t once = PTHREAD_ONCE_INIT;

// Hashes globais
hash_t **global_tf;
hash_t *global_idf;
double *global_doc_norms;

// Vetores globais
size_t global_vocab_size;

// Variáveis globais de controle
long int global_entries = 0;
int VERBOSE = 0;

/* --------------- Macros --------------- */

#define MAX_DOCS 97549
#define MAX_THREADS 16

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
  LOG(stderr, "DEBUG: main() iniciando");
  fflush(stderr);
  const char *filename_db = "wiki-small.db", *tablename = "sample_articles";
  const char *query_user = "shakespeare english literature";
  int nthreads = 4;
  long int entries = 0;
  int k = 10; // Top-k documentos mais similares

  LOG(stderr, "DEBUG: Variáveis locais inicializadas");
  fflush(stderr);

  // CLI com parâmetros nomeados
  LOG(stderr, "DEBUG: Processando %d argumentos", argc);
  fflush(stderr);
  for (int i = 1; i < argc; ++i) {
    LOG(stderr, "DEBUG: argv[%d] = %s", i, argv[i]);
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
    else if (strcmp(argv[i], "--k") == 0 && i + 1 < argc)
      k = atoi(argv[++i]);
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
          "'sample_articles')\n"
          "--k: Top-k documentos mais similares (default: 10)\n",
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
    fprintf(stderr,
            "Número de threads inválido (%d). Deve estar entre 1 e %d\n",
            nthreads, MAX_THREADS);
    return 1;
  }

  if (entries > MAX_DOCS) {
    fprintf(stderr,
            "Número de entradas excede o limite máximo de documentos (%d)\n",
            MAX_DOCS);
    return 1;
  }

  pthread_mutex_init(&mutex, NULL);

  // Determinar número de entradas primeiro (para criar nomes de arquivo)
  const char *query_count = "select count(*) from \"%w\";";
  long int total = get_single_int(filename_db, query_count, tablename);
  if (!entries || entries > total)
    entries = total;

  // Criar nomes de arquivo com sufixo do número de entradas
  char filename_tf_with_entries[256];
  char filename_idf_with_entries[256];
  char filename_doc_norms_with_entries[256];
  snprintf(filename_tf_with_entries, sizeof(filename_tf_with_entries),
           "models/tf_%ld.bin", entries);
  snprintf(filename_idf_with_entries, sizeof(filename_idf_with_entries),
           "models/idf_%ld.bin", entries);
  snprintf(filename_doc_norms_with_entries, sizeof(filename_doc_norms_with_entries),
           "models/doc_norms_%ld.bin", entries);

  // Caso o arquivo não exista: Pré-processamento
  if (access(filename_tf_with_entries, F_OK) == -1 ||
      access(filename_idf_with_entries, F_OK) == -1 ||
      access(filename_doc_norms_with_entries, F_OK) == -1) {
    pthread_t tids[MAX_THREADS];
    thread_args args[MAX_THREADS];
    global_idf = hash_new();
    global_tf = (hash_t **)calloc(entries, sizeof(hash_t *));
    if (!global_tf) {
      fprintf(stderr, "Falha ao alocar memória para global_tf\n");
      return 1;
    }

    // Carregar stopwords uma vez (compartilhado por todas threads)
    load_stopwords("assets/stopwords.txt");
    if (!global_stopwords) {
      fprintf(stderr, "Falha ao carregar stopwords\n");
      return 1;
    }

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
    // TODO: Implementar print para hash_t**
    // print_tf_hash(global_tf, -1, VERBOSE);

    // [15]
    // Salvar estruturas globais em arquivos binários
    printf("\nSalvando estruturas em disco\n");

    save_hash_array(global_tf, global_entries, filename_tf_with_entries);
    save_hash(global_idf, filename_idf_with_entries);
    save_doc_norms(global_doc_norms, global_entries, filename_doc_norms_with_entries);

    // Liberar stopwords (usado apenas no pré-processamento)
    free_stopwords();

  } else {

    /* --------------- Carregamento dos Binários --------------- */

    printf("Arquivos binários encontrados, carregando estruturas...\n");

    global_tf = load_hash_array(filename_tf_with_entries, &global_entries);
    if (!global_tf) {
      fprintf(stderr, "Erro ao carregar global_tf de %s\n", filename_tf_with_entries);
      pthread_mutex_destroy(&mutex);
      return 1;
    }

    global_idf = load_hash(filename_idf_with_entries);
    if (!global_idf) {
      fprintf(stderr, "Erro ao carregar global_idf de %s\n", filename_idf_with_entries);
      // Liberar global_tf
      for (long int i = 0; i < global_entries; i++) {
        if (global_tf[i])
          hash_free(global_tf[i]);
      }
      free(global_tf);
      pthread_mutex_destroy(&mutex);
      return 1;
    }

    global_doc_norms = load_doc_norms(filename_doc_norms_with_entries, &global_entries);
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
  }

  /* --------------- Consulta do Usuário --------------- */

  if (query_user) {
    printf("Consulta do usuário: %s\n", query_user);

    // Processar query (sem threads - é um vetor pequeno)
    hash_t *query_tf;
    double query_norm;

    int result = preprocess_query_single(query_user, global_idf, &query_tf, &query_norm);
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
                                                   global_doc_norms, global_entries);
      if (!similarities) {
        fprintf(stderr, "Erro ao calcular similaridades\n");
      } else {
        printf("\n=== Similaridades (Top 10) ===\n");

        // Encontrar os top-k documentos mais similares
        // Criar array de índices
        typedef struct {
          long int doc_id;
          double similarity;
        } DocScore;

        DocScore *scores = (DocScore *)malloc(global_entries * sizeof(DocScore));
        if (scores) {
          for (long int i = 0; i < global_entries; i++) {
            scores[i].doc_id = i;
            scores[i].similarity = similarities[i];
          }

          // Ordenar por similaridade (bubble sort simples - OK para poucos docs)
          for (long int i = 0; i < global_entries - 1; i++) {
            for (long int j = 0; j < global_entries - i - 1; j++) {
              if (scores[j].similarity < scores[j + 1].similarity) {
                DocScore temp = scores[j];
                scores[j] = scores[j + 1];
                scores[j + 1] = temp;
              }
            }
          }

          // Exibir top-k
          long int top_k = global_entries < k ? global_entries : k;
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

            char **documents = get_documents_by_ids(filename_db, tablename, top_ids, top_k);
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

/* --------------- PTHREAD_ONCE_INIT --------------- */
// Tarefas computadas uma única vez dentre todas as threads:
// 1. Extrair vocabulário completo do IDF (chaves da hash_t *global_idf)
// 2. Computar o IDF de cada palavra no vocabulário
// 3. Obter tamanho do vocabulário e alocar vetor de documentos
// 4. Alocar vetor da norma de cada documento

void compute_once(void) {
  LOG(stderr, "DEBUG: compute_once() iniciando");
  fflush(stderr);
  LOG(stdout, "Funções computadas uma única vez dentre todas as threads");

  // [1] Vocabulário: usar as chaves da hash global_idf diretamente quando
  // necessário

  // [2] Computa o IDF
  LOG(stderr, "DEBUG: Antes set_idf_value");
  fflush(stderr);
  set_idf_value(global_idf, global_tf, (double)global_entries, global_entries);
  LOG(stderr, "DEBUG: Depois set_idf_value");
  fflush(stderr);

  // [3] Vocabulário já está em global_idf
  global_vocab_size = hash_size(global_idf);
  LOG(stderr, "DEBUG: Tamanho do vocabulário: %zu palavras",
          global_vocab_size);
  fflush(stderr);

  // [4]
  global_doc_norms = (double *)calloc(global_entries, sizeof(double));
  if (!global_doc_norms) {
    LOG(stderr, "Erro ao alocar memória para global_doc_norms");
    exit(EXIT_FAILURE);
  }

  fprintf(stdout, "IDF computado. Tamanho do vocabulário: %zu palavras\n",
          global_vocab_size);
  fprintf(stdout, "Hashes de documentos alocados: %ld documentos\n",
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
// 9.  Popular vocabulário local (chaves do `hash_t *global_idf`).
// 10. Mergir TF e IDF hashes nas globais
// 11. Uso do padrão barrreira (`pthread_barrier_t barrier`) para sincronizar as
// threads
// 12. Executar `compute_once` descrito na seção anterior: PTHREAD_ONCE_INIT.
// 13. Transformar os documentos em vetores usando esquema de ponderação TF-IDF.
// 14. Computar as normas dos vetores gerados na etapa anterior.
// 15. Salvar todas as estruturas em arquivos binários (./models/*.bin).

void *preprocess(void *arg) {
  LOG(stderr, "DEBUG: preprocess() iniciando");
  fflush(stderr);

  // [1] Parâmetros das threads
  thread_args *t = (thread_args *)arg;
  long int count = t->end - t->start;

  LOG(stderr, "DEBUG: Thread %ld - range: %ld to %ld (count=%ld)", t->id,
          t->start, t->end, count);
  fflush(stderr);

  char **article_texts; // Textos dos artigos
  char ***article_vecs; // Vetores de tokens dos artigos

  // [2] Hashes locais: TF e IDF
  LOG(stderr, "DEBUG: Thread %ld - Criando hashes locais", t->id);
  fflush(stderr);
  hash_t **tf = (hash_t **)calloc(count, sizeof(hash_t *));
  if (!tf) {
    fprintf(stderr, "Falha ao alocar memória para tf local\n");
    pthread_exit(NULL);
  }

  // Inicializar cada hash de documento local
  for (long int i = 0; i < count; i++) {
    tf[i] = hash_new();
    if (!tf[i]) {
      fprintf(stderr, "Falha ao alocar memória para tf[%ld] na thread %ld\n", i,
              t->id);
      pthread_exit(NULL);
    }
  }

  LOG(stderr, "DEBUG: Thread %ld - tf_hash criado", t->id);
  fflush(stderr);
  hash_t *idf = hash_new();
  LOG(stderr, "DEBUG: Thread %ld - hash_t criado", t->id);
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
  LOG(stderr, "DEBUG: Thread %ld - Antes get_str_arr", t->id);
  fflush(stderr);
  article_texts = get_str_arr(t->filename_db,
                              "select article_text from \"%w\" "
                              "where article_id between ? and ?"
                              "order by article_id asc",
                              t->start, t->end - 1, t->tablename);
  LOG(stderr, "DEBUG: Thread %ld - Dvecepois get_str_arr", t->id);
  fflush(stderr);
  if (!article_texts) {
    fprintf(stderr, "Erro ao obter dados do banco\n");
    pthread_exit(NULL);
  }

  LOG(stdout, "Primeiro artigo da thread %ld: %s\n", t->id, article_texts[0]);

  // [5] Tokenização
  LOG(stderr, "DEBUG: Thread %ld - Antes tokenize_articles", t->id);
  fflush(stderr);
  article_vecs = tokenize_articles(article_texts, count);
  LOG(stderr, "DEBUG: Thread %ld - Depois tokenize_articles", t->id);
  fflush(stderr);
  if (!article_vecs) {
    fprintf(stderr, "Erro ao tokenizar artigos\n");
    pthread_exit(NULL);
  }

  // [6] Remoção de Stopwords
  LOG(stderr, "DEBUG: Thread %ld - Antes remove_stopwords", t->id);
  fflush(stderr);
  remove_stopwords(article_vecs, count);
  LOG(stderr, "DEBUG: Thread %ld - Depois remove_stopwords", t->id);
  fflush(stderr);

  // [7] Stemming
  LOG(stderr, "DEBUG: Thread %ld - Antes stem_articles", t->id);
  fflush(stderr);
  stem_articles(article_vecs, count);
  LOG(stderr, "DEBUG: Thread %ld - Depois stem_articles", t->id);
  fflush(stderr);

  // [8] Populando hash com os termos e suas frequências
  LOG(stderr, "DEBUG: Thread %ld - Antes populate_tf_hash", t->id);
  fflush(stderr);
  populate_tf_hash(tf, article_vecs, count);
  LOG(stderr, "DEBUG: Thread %ld - Depois populate_tf_hash", t->id);
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
  LOG(stderr, "DEBUG: Thread %ld - Antes set_idf_words", t->id);
  fflush(stderr);
  set_idf_words(idf, article_vecs, count);
  LOG(stderr, "DEBUG: Thread %ld - Depois set_idf_words", t->id);
  fflush(stderr);

  // [10] Mergir TF e IDF hashes
  LOG(stderr, "DEBUG: Thread %ld - Antes merge (lock)", t->id);
  fflush(stderr);
  pthread_mutex_lock(&mutex);
  LOG(stderr, "DEBUG: Thread %ld - Lock adquirido, merging", t->id);
  fflush(stderr);
  hashes_merge(global_tf, tf, count);
  LOG(stderr, "DEBUG: Thread %ld - tf_hash_merge OK", t->id);
  fflush(stderr);
  hash_merge(global_idf, idf);
  LOG(stderr, "DEBUG: Thread %ld - hash_merge OK", t->id);
  fflush(stderr);
  pthread_mutex_unlock(&mutex);
  LOG(stderr, "DEBUG: Thread %ld - Lock liberado", t->id);
  fflush(stderr);

  // [11] Sincronizar as threads antes de computar variáveis globais únicas
  LOG(stdout, "Thread %ld esperando na barreira", t->id);
  LOG(stderr, "DEBUG: Thread %ld - Antes barrier_wait", t->id);
  fflush(stderr);
  pthread_barrier_wait(&barrier);
  LOG(stderr, "DEBUG: Thread %ld - Depois barrier_wait", t->id);
  fflush(stderr);

  // [12] Variáveis globais computadas uma vez entre todas as threads
  LOG(stderr, "DEBUG: Thread %ld - Antes pthread_once", t->id);
  fflush(stderr);
  pthread_once(&once, compute_once);
  LOG(stderr, "DEBUG: Thread %ld - Depois pthread_once", t->id);
  fflush(stderr);

  // [13] Computar vetores de documentos usando TF-IDF
  LOG(stderr, "DEBUG: Thread %ld - Antes compute_doc_vecs", t->id);
  fflush(stderr);
  compute_tf_idf(global_tf, global_idf, count, t->start);
  LOG(stderr, "DEBUG: Thread %ld - Depois compute_doc_vecs", t->id);
  fflush(stderr);

  // [14] Computar normas dos documentos
  LOG(stderr, "DEBUG: Thread %ld - Antes compute_doc_norms", t->id);
  fflush(stderr);
  compute_doc_norms(global_doc_norms, global_tf, count, global_vocab_size,
                    t->start);
  LOG(stderr, "DEBUG: Thread %ld - Depois compute_doc_norms", t->id);
  fflush(stderr);

  // Imprimir alguns vetores para verificação
  LOG(stdout, "=== Vetores de documentos da Thread %ld ===", t->id);
  for (long int i = 0; i < count && i < 3; ++i) {
    long int doc_id = t->start + i;
    LOG(stdout, "Documento %ld:", doc_id);

    // Mostrar valores do hash (primeiros 30 valores não-zero)
    // trocar isso daqui
    size_t valores_mostrados = 0;
    for (size_t j = 0; j < global_tf[doc_id]->cap && valores_mostrados < 30;
         ++j) {
      for (HashEntry *e = global_tf[doc_id]->buckets[j];
           e && valores_mostrados < 30; e = e->next) {
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
          hash_size(idf));

  // Liberar memória de estruturas locais
  free(article_texts);
  free_article_vecs(article_vecs, count);
  // NOTA: Não liberamos tf[i] porque hashes_merge transferiu a propriedade
  // dos ponteiros para global_tf. Apenas liberamos o array.
  free(tf);
  hash_free(idf);

  pthread_exit(NULL);
}