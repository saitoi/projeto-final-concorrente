/**
 * @file preprocess_query.c
 * @brief Processamento de queries reutilizando funções de documentos
 */

#include "../include/preprocess_query.h"
#include "../include/file_io.h"
#include "../include/hash_t.h"
#include "../include/log.h"
#include "../include/preprocess.h"
#include "../include/sqlite_helper.h"
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// External global variables from main.c
extern hash_t **global_tf;
extern hash_t *global_idf;
extern double *global_doc_norms;
extern long int global_entries;
extern int VERBOSE;

/**
 * @brief Argumentos para threads de cálculo de similaridade
 */
typedef struct {
  long int start;                 // Documento inicial
  long int end;                   // Documento final (exclusivo)
  const hash_t *query_tf;         // Hash TF-IDF da query
  double query_norm;              // Norma da query
  hash_t **global_tf;             // Array de hashes TF-IDF dos documentos
  const double *global_doc_norms; // Array de normas dos documentos
  double *similarities;           // Array de similaridades (compartilhado)
} similarity_args;

/**
 * @brief Função executada por cada thread para calcular similaridades
 */
void *compute_similarities_thread(void *arg) {
  similarity_args *args = (similarity_args *)arg;

  for (long int doc_id = args->start; doc_id < args->end; doc_id++) {
    hash_t *doc_tf = args->global_tf[doc_id];
    if (!doc_tf) {
      args->similarities[doc_id] = 0.0;
      continue;
    }

    double dot_product = 0.0;
    for (size_t i = 0; i < args->query_tf->cap; i++) {
      for (HashEntry *query_entry = args->query_tf->buckets[i]; query_entry;
           query_entry = query_entry->next) {
        double doc_tfidf = hash_find(doc_tf, query_entry->word);
        if (doc_tfidf > 0.0) {
          dot_product += query_entry->value * doc_tfidf;
        }
      }
    }

    double doc_norm = args->global_doc_norms[doc_id];
    if (args->query_norm > 0.0 && doc_norm > 0.0) {
      args->similarities[doc_id] = dot_product / (args->query_norm * doc_norm);
    } else {
      args->similarities[doc_id] = 0.0;
    }
  }

  pthread_exit(NULL);
}

/**
 * @brief Processa query reutilizando pipeline de documentos
 */
int preprocess_query(const char *query_user, const hash_t *global_idf,
                     hash_t **query_tf_out, double *query_norm_out) {
  if (!query_user || !global_idf) {
    return -1;
  }

  // Converter query para formato char** (array de 1 string)
  char **query_array = malloc(2 * sizeof(char *));
  if (!query_array)
    return -1;
  query_array[0] = strdup(query_user);
  query_array[1] = NULL;

  // Pipeline usando funções de documentos
  char ***tokens = tokenize(query_array, 1);
  free(query_array[0]);
  free(query_array);

  if (!tokens || !tokens[0]) {
    if (tokens)
      free(tokens);
    return -1;
  }

  remove_stopwords(tokens, 1);
  stem(tokens, 1);

  // Calcular TF
  hash_t *query_tf = hash_new();
  for (long int i = 0; tokens[0][i] != NULL; i++) {
    hash_add(query_tf, tokens[0][i], 1.0);
  }

  // Calcular TF-IDF
  // Não posso usar populate_tf_hash pois ele recebe um vetor de hashes
  for (size_t i = 0; i < query_tf->cap; i++) {
    for (HashEntry *e = query_tf->buckets[i]; e; e = e->next) {
      if (e->value > 0) {
        double idf = hash_find(global_idf, e->word);
        e->value = (idf == 0.0) ? 0.0 : (1.0 + log2(e->value)) * idf;
      }
    }
  }

  // Calcular norma
  double norm = 0.0;
  for (size_t i = 0; i < query_tf->cap; i++) {
    for (HashEntry *e = query_tf->buckets[i]; e; e = e->next) {
      norm += e->value * e->value;
    }
  }
  norm = sqrt(norm);

  free_article_vecs(tokens, 1);

  *query_tf_out = query_tf;
  *query_norm_out = norm;
  return 0;
}

/**
 * @brief Calcula similaridade cosseno usando threads paralelas
 */
double *compute_similarities(const hash_t *query_tf, double query_norm,
                             hash_t **global_tf, const double *global_doc_norms,
                             long int num_docs, int nthreads) {
  if (!query_tf || !global_tf || !global_doc_norms || num_docs <= 0) {
    return NULL;
  }

  if (nthreads <= 0)
    nthreads = 1;
  if (nthreads > 16)
    nthreads = 16;

  double *similarities = (double *)calloc(num_docs, sizeof(double));
  if (!similarities)
    return NULL;

  pthread_t *threads = malloc(nthreads * sizeof(pthread_t));
  similarity_args *args = malloc(nthreads * sizeof(similarity_args));

  if (!threads || !args) {
    free(similarities);
    if (threads)
      free(threads);
    if (args)
      free(args);
    return NULL;
  }

  // Dividir trabalho entre threads
  long int docs_per_thread = num_docs / nthreads;
  long int remainder = num_docs % nthreads;

  for (int i = 0; i < nthreads; i++) {
    args[i].start = i * docs_per_thread + (i < remainder ? i : remainder);
    args[i].end = args[i].start + docs_per_thread + (i < remainder ? 1 : 0);
    args[i].query_tf = query_tf;
    args[i].query_norm = query_norm;
    args[i].global_tf = global_tf;
    args[i].global_doc_norms = global_doc_norms;
    args[i].similarities = similarities;

    if (pthread_create(&threads[i], NULL, compute_similarities_thread,
                       &args[i])) {
      fprintf(stderr, "Erro ao criar thread %d para similaridade\n", i);
      // Cleanup
      for (int j = 0; j < i; j++) {
        pthread_join(threads[j], NULL);
      }
      free(threads);
      free(args);
      free(similarities);
      return NULL;
    }
  }

  // Aguardar conclusão
  for (int i = 0; i < nthreads; i++) {
    pthread_join(threads[i], NULL);
  }

  free(threads);
  free(args);

  return similarities;
}

/**
 * @brief Função auxiliar para calcular tempo decorrido
 */
static inline double get_elapsed_time(struct timespec *start,
                                      struct timespec *end) {
  return (end->tv_sec - start->tv_sec) + (end->tv_nsec - start->tv_nsec) / 1e9;
}

/**
 * @brief Processa query do usuário, calcula similaridades e exibe resultados
 *
 * @param query_user String da query do usuário
 * @param nthreads Número de threads para cálculo de similaridades
 * @param k Número de documentos top-k a retornar
 * @param db Caminho para o banco de dados SQLite
 * @param table Nome da tabela no banco de dados
 * @return 0 em sucesso, 1 em erro
 */
int process_and_display_query(const char *query_user, int nthreads, int k,
                              const char *db, const char *table) {
  if (!query_user) {
    printf("Nenhuma consulta fornecida\n");
    return 0;
  }

  // Carregar stopwords se não estiverem carregadas
  if (!global_stopwords) {
    load_stopwords("assets/stopwords.txt");
    if (!global_stopwords) {
      fprintf(stderr, "Falha ao carregar stopwords para processar query\n");
      return 1;
    }
  }

  // Processar query (single thread)
  hash_t *query_tf;
  double query_norm;

  int result = preprocess_query(query_user, global_idf, &query_tf, &query_norm);
  if (result != 0) {
    fprintf(stderr, "Erro ao processar consulta do usuário\n");
    return 1;
  }

  log_info("Consulta processada com sucesso!");
  log_info("Norma da query: %.6f", query_norm);
  log_info("Tamanho do vetor TF-IDF da query: %zu palavras",
           hash_size(query_tf));

  // DEBUG: Exibir palavras da query
  if (VERBOSE) {
    printf("Palavras na query (após processamento):\n");
    for (size_t i = 0; i < query_tf->cap; i++) {
      for (HashEntry *e = query_tf->buckets[i]; e; e = e->next) {
        double idf = hash_find(global_idf, e->word);
        printf("  '%s': IDF=%.6f, TF-IDF=%.6f\n", e->word, idf, e->value);
      }
    }
  }

  // Calcular similaridade com todos os documentos
  struct timespec t_start_sim, t_end_sim;
  clock_gettime(CLOCK_MONOTONIC, &t_start_sim);

  double *similarities =
      compute_similarities(query_tf, query_norm, global_tf, global_doc_norms,
                           global_entries, nthreads);

  clock_gettime(CLOCK_MONOTONIC, &t_end_sim);
  double elapsed_sim = get_elapsed_time(&t_start_sim, &t_end_sim);

  if (!similarities) {
    fprintf(stderr, "Erro ao calcular similaridades\n");
    hash_free(query_tf);
    return 1;
  }
  log_info("[SIMILARIDADE] Tempo: %.3f segundos", elapsed_sim);

  // Encontrar os top-k documentos mais similares
  DocSim *scores = (DocSim *)malloc(global_entries * sizeof(DocSim));
  if (scores) {
    for (long int i = 0; i < global_entries; i++) {
      scores[i].doc_id = i;
      scores[i].similarity = similarities[i];
    }

    qsort(scores, global_entries, sizeof(DocSim), compare_sim);

    // Exibir top-k
    long int top_k = global_entries < k ? global_entries : k;
    printf("\nTop %ld documentos mais similares:\n", top_k);
    printf("---------------------------------\n");

    long int *top_ids = (long int *)malloc(top_k * sizeof(long int));
    if (top_ids) {
      for (long int i = 0; i < top_k; i++) {
        top_ids[i] = scores[i].doc_id;
      }

      char **documents = get_documents_by_ids(db, table, top_ids, top_k);
      if (documents) {
        for (long int i = 0; i < top_k; i++) {
          if (documents[i]) {
            // Similarity score in green, content in white
            printf("[%ld] \x1b[36m%.6f\x1b[90m  ", top_ids[i], scores[i].similarity);
            // printf("[%ld] %.6f  ", top_ids[i], scores[i].similarity);
            // Limitar a exibição a 100 caracteres
            if (strlen(documents[i]) > 100) {
              const char *p = documents[i];
              int j = 0;
              for (; *p && j < 100; ++j, p++)
                putchar(*p);
              if (*p)
                printf("...\x1b[0m");
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
  hash_free(query_tf);

  return 0;
}
