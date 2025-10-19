#include "../include/hash_t.h"
#include "../include/file_io.h"
#include <libstemmer.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void set_idf_words(generic_hash *vocab, char ***article_vecs, long int count) {
  if (!article_vecs) {
    fprintf(stderr, "Erro: article_vecs é nulo.\n");
    pthread_exit(NULL);
  }

  for (long int i = 0; i < count; ++i) {
    if (!article_vecs[i])
      continue;

    for (long int j = 0; article_vecs[i][j] != NULL; ++j) {
      generic_hash_add(vocab, article_vecs[i][j], 0.0);
    }
  }
}

void set_idf_value(generic_hash *set, const tf_hash *tf, double doc_count) {
  if (!set || !tf) {
    fprintf(stderr, "Erro: set ou tf é nulo.\n");
    pthread_exit(NULL);
  }

  for (size_t i = 0; i < set->cap; i++) {
    GEntry *e = set->buckets[i];
    while (e) {
      int freq = tf_hash_get_ni(tf, e->word);
      if (freq > 0)
        e->value = log2(doc_count / freq);
      else
        e->value = 0.0;
      e = e->next;
    }
  }
}

void compute_tf_idf(generic_hash **global_tf, generic_hash *global_idf, long int count,
                      long int offset) {
  if (!global_tf || !global_idf || count <= 0) {
    fprintf(stderr, "Erro: global_doc_vec, global_vocab, global_tf, count ou "
                    "offset é inválido.\n");
    pthread_exit(NULL);
  }

  for (long int i = offset; i < offset + count; ++i) {
      size_t word_idx = 0;
      generic_hash *doc_tf = global_tf[i];
      for (long int j = 0; j < doc_tf->cap; ++j) {
          GEntry *e = doc_tf->buckets[j];
          while (e) {
              double freq = 0.0;
              
              // tf_hash_get_freq(global_tf, e->word, i, &freq);

              // Apenas adicionar palavras que aparecem no documento (freq > 0)
              if (e->value && e->value > 0) {
                  double idf = generic_hash_find(global_idf, e->word);
                  double tfidf_value = (1.0 + log2(e->value)) * e->value * idf;
                  doc_tf->value = (double*) malloc(sizeof(double));
                  if (!(doc_tf->value)) {
                      fprintf(stderr, "Erro: falha ao alocar memória para tf-idf.\n");
                      pthread_exit(NULL);
                  }
                  *(double*)doc_tf->value = tfidf_value;
              }

              e = e->next;
              word_idx++;
          }
      }
  }

  //   // Índice da palavra no vetor de documentos
  //   size_t word_idx = 0;

  //   // Iterando sobre os buckets do vocabulário (global_idf)
  //   for (size_t j = 0; j < global_idf->cap; j++) {
  //     GEntry *e = global_idf->buckets[j];

  //     // Iterando sobre todas as palavras no bucket
  //     while (e) {
  //       double freq = 0.0;
  //       tf_hash_get_freq(global_tf, e->word, i, &freq);

  //       // Apenas adicionar palavras que aparecem no documento (freq > 0)
  //       if (freq > 0) {
  //         double tfidf_value = (1.0 + log2(freq)) * e->value;
  //         generic_hash_add(global_doc_vec[i], e->word, tfidf_value);
  //       }

  //       e = e->next;
  //       word_idx++;
  //     }
  //   }
  // }
}

void compute_doc_norms(double *global_doc_norms, generic_hash **global_tf, long int doc_count, long int vocab_size,
                       long int offset) {
  if (!global_doc_norms || doc_count <= 0 || vocab_size <= 0 || offset < 0) {
    fprintf(stderr, "Erro nos argumentos de entrada.\n");
    return;
  }

  for (long int i = 0; i < doc_count; i++) {
    long int doc_id = offset + i;
    generic_hash *doc_vec = global_tf[doc_id];
    double norm = 0.0;

    // Calcular a soma dos quadrados de todos os valores TF-IDF do documento
    for (size_t j = 0; j < doc_vec->cap; j++) {
      GEntry *e = doc_vec->buckets[j];
      while (e) {
        norm += e->value * e->value;
        e = e->next;
      }
    }

    // Tomar a raiz quadrada para obter a norma Euclidiana
    global_doc_norms[doc_id] = sqrt(norm);
  }
}

void stem_articles(char ***article_vecs, long int count) {
  struct sb_stemmer *stemmer = sb_stemmer_new("english", NULL);
  if (!stemmer) {
    fprintf(stderr, "Erro ao criar o Stemmer.\n");
    pthread_exit(NULL);
  }

  for (long int i = 0; i < count; ++i) {
    if (!article_vecs[i])
      continue;

    for (long int j = 0; article_vecs[i][j] != NULL; ++j) {
      const char *stemmed = (const char *)sb_stemmer_stem(
          stemmer, (const sb_symbol *)article_vecs[i][j],
          strlen(article_vecs[i][j]));
      free(article_vecs[i][j]);
      article_vecs[i][j] = strdup(stemmed); // Não é seguro
    }
  }
  sb_stemmer_delete(stemmer);
}

void populate_tf_hash(generic_hash **tf, char ***article_vecs, long int count,
                          long int offset) {
  for (long int i = 0; i < count; ++i) {
    if (!tf[i])
      continue;

    for (long int j = 0; article_vecs[i][j] != NULL; ++j) {
      // Passa 1.0 para contar cada ocorrência da palavra no documento
      // tf_hash_set(tf[i], (const char *)article_vecs[i][j], i + offset, 1.0);
      if (article_vecs[i][j])
        generic_hash_add(tf[i], article_vecs[i][j], 1.0);
    }
  }
}

char ***tokenize_articles(char **article_texts, long int count) {
  char ***article_vecs;

  fprintf(stderr, "DEBUG: tokenize_articles - count=%ld\n", count);
  fflush(stderr);

  article_vecs = malloc(count * sizeof(char **));
  if (!article_vecs) {
    fprintf(stderr, "Erro ao alocar article_vecs\n");
    return NULL;
  }

  fprintf(stderr, "DEBUG: tokenize_articles - article_vecs alocado\n");
  fflush(stderr);

  for (long int i = 0; i < count; ++i) {
    if (i % 1000 == 0) {
      fprintf(stderr, "DEBUG: tokenize_articles - processando artigo %ld/%ld\n", i, count);
      fflush(stderr);
    }
    if (!article_texts[i]) {
      article_vecs[i] = NULL;
      continue;
    }

    // Estimativa mais conservadora: assume palavras curtas (média 3 chars + espaço)
    // Adiciona margem de segurança de 100 tokens
    long int estimated_tokens = strlen(article_texts[i]) / 3 + 100;
    article_vecs[i] = malloc(estimated_tokens * sizeof(char *));
    if (!article_vecs[i]) {
      article_vecs[i] = NULL;
      continue;
    }

    char *text_copy = strdup(article_texts[i]);
    if (!text_copy) {
      free(article_vecs[i]);
      article_vecs[i] = NULL;
      continue;
    }

    long int j = 0;
    char *saveptr;  // Para strtok_r (thread-safe)
    char *token = strtok_r(text_copy, " \t\n\r", &saveptr);
    while (token != NULL) {
      if (j >= estimated_tokens - 1) {
        // Buffer cheio, aumentar tamanho
        long int new_size = estimated_tokens * 2;
        char **new_vec = realloc(article_vecs[i], new_size * sizeof(char *));
        if (!new_vec) {
          // Falha no realloc, truncar tokens restantes
          break;
        }
        article_vecs[i] = new_vec;
        estimated_tokens = new_size;
      }
      article_vecs[i][j] = strdup(token);
      j++;
      token = strtok_r(NULL, " \t\n\r", &saveptr);
    }
    article_vecs[i][j] = NULL;

    free(text_copy);
  }

  return article_vecs;
}

void remove_stopwords(char ***article_vecs, long int count) {
  if (!global_stopwords) {
    fprintf(stderr,
            "Stopwords não carregadas. Chame load_stopwords() primeiro.\n");
    pthread_exit(NULL);
  }

  for (long int i = 0; i < count; ++i) {
    if (!article_vecs[i])
      continue;

    // Filtra stopwords
    long int write_idx = 0;
    for (long int read_idx = 0; article_vecs[i][read_idx] != NULL; ++read_idx) {
      if (!generic_hash_contains(global_stopwords, article_vecs[i][read_idx])) {
        article_vecs[i][write_idx++] = article_vecs[i][read_idx];
      } else {
        free(article_vecs[i][read_idx]);
      }
    }
    article_vecs[i][write_idx] = NULL;
  }
}

void free_article_vecs(char ***article_vecs, long int count) {
  if (!article_vecs)
    return;

  for (long int i = 0; i < count; ++i) {
    if (article_vecs[i]) {
      for (long int j = 0; article_vecs[i][j] != NULL; ++j) {
        free(article_vecs[i][j]);
      }
      free(article_vecs[i]);
    }
  }
  free(article_vecs);
}