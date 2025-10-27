/**
 * @file preprocess_query.c
 * @brief Processamento de queries reutilizando funções de documentos
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include <pthread.h>
#include "../include/hash_t.h"
#include "../include/preprocess.h"

/**
 * @brief Argumentos para threads de cálculo de similaridade
 */
typedef struct {
  long int start;                  // Documento inicial
  long int end;                    // Documento final (exclusivo)
  const hash_t *query_tf;          // Hash TF-IDF da query
  double query_norm;               // Norma da query
  hash_t **global_tf;              // Array de hashes TF-IDF dos documentos
  const double *global_doc_norms;  // Array de normas dos documentos
  double *similarities;            // Array de similaridades (compartilhado)
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
  if (!query_array) return -1;
  query_array[0] = strdup(query_user);
  query_array[1] = NULL;

  // Pipeline usando funções de documentos
  char ***tokens = tokenize(query_array, 1);
  free(query_array[0]);
  free(query_array);

  if (!tokens || !tokens[0]) {
    if (tokens) free(tokens);
    return -1;
  }

  // Lowercase manual
  for (long int i = 0; tokens[0][i] != NULL; i++) {
    for (char *p = tokens[0][i]; *p; p++) *p = tolower(*p);
  }

  remove_stopwords(tokens, 1);
  stem(tokens, 1);

  // Calcular TF
  hash_t *query_tf = hash_new();
  for (long int i = 0; tokens[0][i] != NULL; i++) {
    hash_add(query_tf, tokens[0][i], 1.0);
  }

  // Calcular TF-IDF
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

  if (nthreads <= 0) nthreads = 1;
  if (nthreads > 16) nthreads = 16;

  double *similarities = (double *)calloc(num_docs, sizeof(double));
  if (!similarities) return NULL;

  pthread_t *threads = malloc(nthreads * sizeof(pthread_t));
  similarity_args *args = malloc(nthreads * sizeof(similarity_args));

  if (!threads || !args) {
    free(similarities);
    if (threads) free(threads);
    if (args) free(args);
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

    if (pthread_create(&threads[i], NULL, compute_similarities_thread, &args[i])) {
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
