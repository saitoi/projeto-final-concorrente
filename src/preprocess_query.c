#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include <libstemmer.h>
#include "../include/hash_t.h"
#include "../include/file_io.h"

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

void *preprocess_query(void *arg) {
  (void)arg; // Unused parameter
  fprintf(stdout, "WIP..\n");
  return NULL;
}

int preprocess_query_single(const char *query_user, const hash_t *global_idf,
                            hash_t **query_tf_out, double *query_norm_out) {
  if (!query_user || !global_idf || !query_tf_out || !query_norm_out) {
    return -1;
  }

  // 1. Tokenizar
  long int token_count = 0;
  char **tokens = tokenize_query(query_user, &token_count);
  if (!tokens) {
    fprintf(stderr, "Erro ao tokenizar query\n");
    return -1;
  }

  // 2. Converter para minúsculas
  for (long int i = 0; i < token_count; i++) {
    for (char *p = tokens[i]; *p; p++) {
      *p = tolower(*p);
    }
  }

  // 3. Remover stopwords e palavras com 1 letra
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
  token_count = write_idx;

  // 4. Stemming
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

  // 5. Calcular TF (frequência de cada palavra)
  hash_t *query_tf = hash_new();
  for (long int i = 0; i < token_count; i++) {
    const char *word = tokens[i];
    double current_freq = hash_find(query_tf, word);
    hash_add(query_tf, word, current_freq + 1.0);
  }

  // 6. Calcular TF-IDF usando IDF global
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

  // 7. Calcular norma do vetor TF-IDF
  double norm = 0.0;
  for (size_t i = 0; i < query_tf->cap; i++) {
    for (HashEntry *e = query_tf->buckets[i]; e; e = e->next) {
      norm += e->value * e->value;
    }
  }
  norm = sqrt(norm);

  // Liberar tokens
  for (long int i = 0; i < token_count; i++) {
    free(tokens[i]);
  }
  free(tokens);

  *query_tf_out = query_tf;
  *query_norm_out = norm;
  return 0;
}

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

    // Similaridade cosseno = (query · doc) / (||query|| * ||doc||)
    double doc_norm = global_doc_norms[doc_id];
    if (query_norm > 0.0 && doc_norm > 0.0) {
      similarities[doc_id] = dot_product / (query_norm * doc_norm);
    } else {
      similarities[doc_id] = 0.0;
    }
  }

  return similarities;
}
