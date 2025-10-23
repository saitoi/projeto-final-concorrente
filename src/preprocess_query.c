/**
 * @file preprocess_query.c
 * @brief Processamento de queries de usuário e cálculo de similaridade
 *
 * Este arquivo implementa o processamento de consultas do usuário para
 * o sistema de recuperação de informações:
 * - Tokenização de queries
 * - Pré-processamento (lowercase, stopwords, stemming)
 * - Cálculo de TF-IDF da query
 * - Computação de similaridade cosseno entre query e documentos
 *
 * Pipeline similar ao de documentos, mas otimizado para queries únicas.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include <time.h>
#include <libstemmer.h>
#include "../include/hash_t.h"
#include "../include/file_io.h"

/**
 * @brief Tokeniza string de query em array de palavras
 *
 * Divide query usando whitespace como delimitador e retorna array de tokens.
 *
 * @param query_user String com a query do usuário
 * @param token_count Ponteiro para armazenar número de tokens encontrados
 * @return Array de strings (tokens), ou NULL em erro
 * @note Caller deve liberar o array e as strings individualmente
 */
char **tokenize_query(const char *query_user, long int *token_count) {
  if (!query_user) {
    return NULL;
  }

  char *text_copy = strdup(query_user);
  if (!text_copy) {
    return NULL;
  }

  char *token = strtok(text_copy, " \t\n\r");
  while (token != NULL) {
    (*token_count)++;
    token = strtok(NULL, " \t\n\r");
  }
  free(text_copy);

  if (*token_count == 0) {
    return NULL;
  }

  char **query_tokens = malloc((*token_count + 1) * sizeof(char *));
  if (!query_tokens) {
    fprintf(stderr, "Erro ao alocar query_tokens\n");
    return NULL;
  }

  text_copy = strdup(query_user);
  if (!text_copy) {
    free(query_tokens);
    return NULL;
  }

  long int i = 0;
  token = strtok(text_copy, " \t\n\r");
  while (token != NULL) {
    query_tokens[i] = strdup(token);
    if (!query_tokens[i]) {
      // Cleanup on allocation failure
      for (long int j = 0; j < i; j++) {
        free(query_tokens[j]);
      }
      free(query_tokens);
      free(text_copy);
      return NULL;
    }
    i++;
    token = strtok(NULL, " \t\n\r");
  }
  query_tokens[i] = NULL;

  free(text_copy);
  return query_tokens;
}

/**
 * @brief Processa query completa e calcula seu vetor TF-IDF
 *
 * Pipeline completo de pré-processamento da query:
 * 1. Tokenização
 * 2. Conversão para lowercase
 * 3. Remoção de stopwords
 * 4. Stemming
 * 5. Cálculo de TF
 * 6. Cálculo de TF-IDF (usando IDF global dos documentos)
 * 7. Cálculo da norma do vetor
 *
 * Inclui medição de tempo para cada etapa.
 *
 * @param query_user String com a query do usuário
 * @param global_idf Hash IDF global dos documentos
 * @param query_tf_out Ponteiro para armazenar hash TF-IDF da query
 * @param query_norm_out Ponteiro para armazenar norma do vetor
 * @return 0 em sucesso, -1 em erro
 */
int preprocess_query_single(const char *query_user, const hash_t *global_idf,
                            hash_t **query_tf_out, double *query_norm_out) {
  if (!query_user || !global_idf || !query_tf_out || !query_norm_out) {
    return -1;
  }

  clock_t start, end;
  double time_taken;

  // 1. Tokenizar
  start = clock();
  long int token_count = 0;
  char **tokens = tokenize_query(query_user, &token_count);
  if (!tokens) {
    fprintf(stderr, "Erro ao tokenizar query\n");
    return -1;
  }
  end = clock();
  time_taken = ((double)(end - start)) / CLOCKS_PER_SEC;
  printf("[TIMING] Tokenização: %.6f segundos (%ld tokens)\n", time_taken, token_count);

  // 2. Converter para minúsculas
  start = clock();
  for (long int i = 0; i < token_count; i++) {
    for (char *p = tokens[i]; *p; p++) {
      *p = tolower(*p);
    }
  }
  end = clock();
  time_taken = ((double)(end - start)) / CLOCKS_PER_SEC;
  printf("[TIMING] Lowercase: %.6f segundos\n", time_taken);

  // 3. Remover stopwords e palavras com 1 letra
  start = clock();
  long int write_idx = 0;
  for (long int read_idx = 0; read_idx < token_count; read_idx++) {
    const char *word = tokens[read_idx];
    // Manter palavra se NÃO for stopword E tiver mais de 1 letra
    if (!hash_contains(global_stopwords, word) && strlen(word) > 1) {
      tokens[write_idx++] = tokens[read_idx];
    } else {
      free(tokens[read_idx]);
    }
  }
  long int old_token_count = token_count;
  token_count = write_idx;
  end = clock();
  time_taken = ((double)(end - start)) / CLOCKS_PER_SEC;
  printf("[TIMING] Remover stopwords: %.6f segundos (%ld -> %ld tokens)\n", time_taken, old_token_count, token_count);

  // 4. Stemming
  start = clock();
  struct sb_stemmer *stemmer = sb_stemmer_new("english", NULL);
  if (!stemmer) {
    // Liberar tokens
    for (long int i = 0; i < token_count; i++) {
      free(tokens[i]);
    }
    free(tokens);
    return -1;
  }

  for (long int i = 0; i < token_count; i++) {
    const char *stemmed = (const char *)sb_stemmer_stem(
        stemmer, (const sb_symbol *)tokens[i], strlen(tokens[i]));
    if (stemmed) {
      free(tokens[i]);
      tokens[i] = strdup(stemmed);
    }
  }

  sb_stemmer_delete(stemmer);
  end = clock();
  time_taken = ((double)(end - start)) / CLOCKS_PER_SEC;
  printf("[TIMING] Stemming: %.6f segundos\n", time_taken);

  // 5. Calcular TF (frequência de cada palavra)
  start = clock();
  hash_t *query_tf = hash_new();
  for (long int i = 0; i < token_count; i++) {
    const char *word = tokens[i];
    hash_add(query_tf, word, 1.0);
  }
  end = clock();
  time_taken = ((double)(end - start)) / CLOCKS_PER_SEC;
  printf("[TIMING] Calcular TF: %.6f segundos (%zu palavras únicas)\n", time_taken, query_tf->size);

  // 6. Calcular TF-IDF usando IDF global
  start = clock();
  for (size_t i = 0; i < query_tf->cap; i++) {
    for (HashEntry *e = query_tf->buckets[i]; e; e = e->next) {
      if (e->value > 0) {
        double idf = hash_find(global_idf, e->word);
        if (idf == 0.0) {
          // Palavra não existe no vocabulário - IDF = 0
          e->value = 0.0;
        } else {
          // TF-IDF = (1 + log(TF)) * IDF
          double tfidf_value = (1.0 + log2(e->value)) * idf;
          e->value = tfidf_value;
        }
      }
    }
  }
  end = clock();
  time_taken = ((double)(end - start)) / CLOCKS_PER_SEC;
  printf("[TIMING] Calcular TF-IDF: %.6f segundos\n", time_taken);

  // 7. Calcular norma do vetor TF-IDF
  start = clock();
  double norm = 0.0;
  for (size_t i = 0; i < query_tf->cap; i++) {
    for (HashEntry *e = query_tf->buckets[i]; e; e = e->next) {
      norm += e->value * e->value;
    }
  }
  norm = sqrt(norm);
  end = clock();
  time_taken = ((double)(end - start)) / CLOCKS_PER_SEC;
  printf("[TIMING] Calcular norma: %.6f segundos\n", time_taken);

  // Liberar tokens
  for (long int i = 0; i < token_count; i++) {
    free(tokens[i]);
  }
  free(tokens);

  *query_tf_out = query_tf;
  *query_norm_out = norm;
  return 0;
}

/**
 * @brief Calcula similaridade cosseno entre query e todos os documentos
 *
 * Computa similaridade usando:
 * similarity(Q, D) = (Q · D) / (||Q|| * ||D||)
 *
 * onde · é produto interno e || || é norma euclidiana.
 *
 * @param query_tf Hash TF-IDF da query
 * @param query_norm Norma do vetor da query
 * @param global_tf Array de hashes TF-IDF dos documentos
 * @param global_doc_norms Array de normas dos documentos
 * @param num_docs Número total de documentos
 * @return Array de similaridades (uma por documento), ou NULL em erro
 * @note Caller deve liberar o array retornado
 */
double *compute_similarities(const hash_t *query_tf, double query_norm,
                             hash_t **global_tf, const double *global_doc_norms,
                             long int num_docs) {
  if (!query_tf || !global_tf || !global_doc_norms || num_docs <= 0) {
    return NULL;
  }

  // Alocar vetor de similaridades
  double *similarities = (double *)calloc(num_docs, sizeof(double));
  if (!similarities) {
    fprintf(stderr, "Erro ao alocar vetor de similaridades\n");
    return NULL;
  }

  // Para cada documento
  for (long int doc_id = 0; doc_id < num_docs; doc_id++) {
    hash_t *doc_tf = global_tf[doc_id];
    if (!doc_tf) {
      similarities[doc_id] = 0.0;
      continue;
    }

    double dot_product = 0.0;

    // Para cada palavra na query, buscar no documento e calcular produto interno
    for (size_t i = 0; i < query_tf->cap; i++) {
      for (HashEntry *query_entry = query_tf->buckets[i]; query_entry;
           query_entry = query_entry->next) {

        // Buscar a mesma palavra no documento
        double doc_tfidf = hash_find(doc_tf, query_entry->word);

        // Se a palavra existe no documento, contribui para o produto interno
        if (doc_tfidf > 0.0) {
          double query_tfidf = query_entry->value;
          dot_product += query_tfidf * doc_tfidf;
        }
      }
    }

    // Similaridade cosseno
    double doc_norm = global_doc_norms[doc_id];
    if (query_norm > 0.0 && doc_norm > 0.0) {
      similarities[doc_id] = dot_product / (query_norm * doc_norm);
    } else {
      similarities[doc_id] = 0.0;
    }
  }

  return similarities;
}
