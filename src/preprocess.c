#include "../include/file_io.h"
#include "../include/hash_t.h"
#include "../include/log.h"
#include <libstemmer.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void set_idf_words(hash_t *vocab, char ***article_vecs, long int count) {
  if (!article_vecs) {
    fprintf(stderr, "Erro: article_vecs é nulo.\n");
    pthread_exit(NULL);
  }

  for (long int i = 0; i < count; ++i) {
    if (!article_vecs[i])
      continue;

    for (long int j = 0; article_vecs[i][j] != NULL; ++j) {
      hash_add(vocab, article_vecs[i][j], 0.0);
    }
  }
}

void set_idf_value(hash_t *set, hash_t **tf, double doc_count,
                   long int num_docs) {
  if (!set || !tf) {
    fprintf(stderr, "Erro: set ou tf é nulo.\n");
    pthread_exit(NULL);
  }

  // Primeiro, zerar todos os valores (vamos usar como contador)
  for (size_t i = 0; i < set->cap; i++) {
    HashEntry *e = set->buckets[i];
    while (e) {
      e->value = 0.0;
      e = e->next;
    }
  }

  // Percorrer cada documento uma única vez e incrementar contador para cada palavra
  for (long int doc_id = 0; doc_id < num_docs; doc_id++) {
    if (!tf[doc_id])
      continue;

    // Para cada palavra neste documento
    for (size_t i = 0; i < tf[doc_id]->cap; i++) {
      HashEntry *doc_entry = tf[doc_id]->buckets[i];
      while (doc_entry) {
        // Buscar essa palavra no vocabulário global e incrementar seu contador
        size_t wlen = doc_entry->wlen;
        size_t idx = hash_str(doc_entry->word, wlen) & (set->cap - 1);

        for (HashEntry *vocab_entry = set->buckets[idx]; vocab_entry; vocab_entry = vocab_entry->next) {
          if (vocab_entry->wlen == wlen && memcmp(vocab_entry->word, doc_entry->word, wlen) == 0) {
            vocab_entry->value += 1.0;
            break;
          }
        }

        doc_entry = doc_entry->next;
      }
    }
  }

  // Agora calcular o IDF com base nos contadores
  for (size_t i = 0; i < set->cap; i++) {
    HashEntry *e = set->buckets[i];
    while (e) {
      if (e->value > 0)
        e->value = log2(doc_count / e->value);
      else
        e->value = 0.0;
      e = e->next;
    }
  }
}

void compute_tf_idf(hash_t **global_tf, hash_t *global_idf, long int count,
                    long int offset) {
  if (!global_tf || !global_idf || count <= 0) {
    fprintf(stderr, "Erro: global_tf, global_idf, ou count inválido.\n");
    pthread_exit(NULL);
  }

  // Para cada documento
  for (long int i = offset; i < offset + count; ++i) {
    hash_t *doc_tf = global_tf[i];
    if (!doc_tf)
      continue;

    // Para cada palavra no documento (iterar sobre buckets da hash)
    for (size_t j = 0; j < doc_tf->cap; ++j) {
      HashEntry *e = doc_tf->buckets[j];
      while (e) {
        // e->value contém a frequência (TF) da palavra no documento
        if (e->value > 0) {
          // Buscar o IDF da palavra
          double idf = hash_find(global_idf, e->word);

          // Calcular TF-IDF: (1 + log2(freq)) * IDF
          double tfidf_value = (1.0 + log2(e->value)) * idf;

          // Atualizar o valor para TF-IDF
          e->value = tfidf_value;
        }
        e = e->next;
      }
    }
  }
}

void compute_doc_norms(double *global_doc_norms, hash_t **global_tf,
                       long int doc_count, long int vocab_size,
                       long int offset) {
  if (!global_doc_norms || doc_count <= 0 || vocab_size <= 0 || offset < 0) {
    fprintf(stderr, "Erro nos argumentos de entrada.\n");
    return;
  }

  for (long int i = 0; i < doc_count; i++) {
    long int doc_id = offset + i;
    hash_t *doc_vec = global_tf[doc_id];

    if (!doc_vec) {
      global_doc_norms[doc_id] = 0.0;
      continue;
    }

    double norm = 0.0;

    // Calcular a soma dos quadrados de todos os valores TF-IDF do documento
    for (size_t j = 0; j < doc_vec->cap; j++) {
      HashEntry *e = doc_vec->buckets[j];
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

void populate_tf_hash(hash_t **tf, char ***article_vecs, long int count) {
  for (long int i = 0; i < count; ++i) {
    if (!tf[i] || !article_vecs[i])
      continue;

    for (long int j = 0; article_vecs[i][j] != NULL; ++j) {
      if (!article_vecs[i][j])
        continue;

      // Buscar se a palavra já existe
      const char *word = article_vecs[i][j];
      size_t wlen = strlen(word);
      size_t idx = hash_str(word, wlen) & (tf[i]->cap - 1);

      int found = 0;
      for (HashEntry *e = tf[i]->buckets[idx]; e; e = e->next) {
        if (e->wlen == wlen && memcmp(e->word, word, wlen) == 0) {
          // Palavra já existe, incrementar frequência
          e->value += 1.0;
          found = 1;
          break;
        }
      }

      if (!found) {
        // Palavra não existe, adicionar com frequência 1.0
        hash_add(tf[i], word, 1.0);
      }
    }
  }
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

    // Estimativa mais conservadora: assume palavras curtas (média 3 chars +
    // espaço) Adiciona margem de segurança de 100 tokens
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
    char *saveptr; // (thread-safe)
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

    // Filtra stopwords e palavras com apenas uma letra
    long int write_idx = 0;
    for (long int read_idx = 0; article_vecs[i][read_idx] != NULL; ++read_idx) {
      const char *word = article_vecs[i][read_idx];
      // Mantém a palavra se NÃO for stopword E tiver mais de 1 letra
      if (!hash_contains(global_stopwords, word) && strlen(word) > 1) {
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
