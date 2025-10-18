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
      generic_hash_add(vocab, article_vecs[i][j]);
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
        e->idf = log2(doc_count / freq);
      else
        e->idf = 0.0;
      e = e->next;
    }
  }
}

void compute_doc_vecs(double **global_doc_vec, const tf_hash *global_tf,
                      generic_hash *global_idf, long int count,
                      long int offset) {
  if (!global_doc_vec || !global_tf || !global_idf || count <= 0) {
    fprintf(stderr, "Erro: global_doc_vec, global_vocab, global_tf, count ou "
                    "offset é inválido.\n");
    pthread_exit(NULL);
  }

  // Iterando sobre os documentos
  for (long int i = offset; i < offset + count; ++i) {
    // Índice da palavra no vetor de documentos
    size_t word_idx = 0;

    // Iterando sobre os buckets do vocabulário (global_idf)
    for (size_t j = 0; j < global_idf->cap; j++) {
      GEntry *e = global_idf->buckets[j];

      // Iterando sobre todas as palavras no bucket
      while (e) {
        double freq = 0.0;
        tf_hash_get_freq(global_tf, e->word, i, &freq);

        double tfidf_value = (freq == 0) ? 0.0 : (1.0 + log2(freq)) * e->idf;
        global_doc_vec[i][word_idx] = tfidf_value;

        e = e->next;
        word_idx++;
      }
    }
  }
}

void compute_doc_norms(double *global_doc_norms, double **global_doc_vecs,
                       long int doc_count, long int vocab_size,
                       long int offset) {
  for (long int i = offset; i < offset + doc_count; ++i) {
    double norm = 0.0;
    for (long int j = 0; j < vocab_size; ++j) {
      norm += global_doc_vecs[i][j] * global_doc_vecs[i][j];
    }
    norm = sqrt(norm);
    global_doc_norms[i] = norm;
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

tf_hash *populate_tf_hash(tf_hash *tf, char ***article_vecs, long int count,
                          long int offset) {
  for (long int i = 0; i < count; ++i) {
    if (!article_vecs[i])
      continue;

    for (long int j = 0; article_vecs[i][j] != NULL; ++j) {
      // Passa 1.0 para contar cada ocorrência da palavra no documento
      tf_hash_set(tf, (const char *)article_vecs[i][j], i + offset, 1.0);
    }
  }

  return tf;
}

char ***tokenize_articles(char **article_texts, long int count) {
  char ***article_vecs;

  article_vecs = malloc(count * sizeof(char **));
  if (!article_vecs) {
    fprintf(stderr, "Erro ao alocar article_vecs\n");
    return NULL;
  }

  for (long int i = 0; i < count; ++i) {
    if (!article_texts[i]) {
      article_vecs[i] = NULL;
      continue;
    }

    long int estimated_tokens = strlen(article_texts[i]) / 2 + 10;
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
    char *token = strtok(text_copy, " \t\n\r");
    while (token != NULL && j < estimated_tokens - 1) {
      article_vecs[i][j] = strdup(token);
      j++;
      token = strtok(NULL, " \t\n\r");
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