#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libstemmer.h>
#include "../include/hash_t.h"

generic_hash* global_stopwords = NULL;

void load_stopwords(const char* filename) {
  FILE* f = fopen(filename, "r");
  if (!f) {
    fprintf(stderr, "Erro ao abrir arquivo de stopwords: %s\n", filename);
    return;
  }

  global_stopwords = generic_hash_new();

  char line[256];
  while (fgets(line, sizeof(line), f)) {
    line[strcspn(line, "\n")] = '\0';

    if (strlen(line) == 0) continue;

    generic_hash_add(global_stopwords, line);
  }

  fclose(f);
}

void free_stopwords(void) {
  if (global_stopwords) {
    generic_hash_free(global_stopwords);
    global_stopwords = NULL;
  }
}

char ***stem_articles(char ***article_vecs, long int count) {
  struct sb_stemmer *stemmer = sb_stemmer_new("english", NULL);
  if (!stemmer) {
      fprintf(stderr, "Erro ao criar o Stemmer.\n");
      return NULL;
  }
  
  for (long int i = 0; i < count; ++i) {
      if (!article_vecs[i]) continue;
      
      for (long int j = 0; article_vecs[i][j] != NULL; ++j) {
        const char* stemmed = (const char*) sb_stemmer_stem(
              stemmer,
              (const sb_symbol*)article_vecs[i][j],
              strlen(article_vecs[i][j])
          );
        free(article_vecs[i][j]);
        article_vecs[i][j] = strdup(stemmed); // Não é seguro
      }
      
  }
  sb_stemmer_delete(stemmer);
  return article_vecs;
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

    long int estimated_tokens = strlen(article_texts[i]) / 5 + 1;
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

char ***remove_stopwords(char ***article_vecs, long int count) {
  if (!global_stopwords) {
    fprintf(stderr, "Stopwords não carregadas. Chame load_stopwords() primeiro.\n");
    return article_vecs;
  }

  for (long int i = 0; i < count; ++i) {
    if (!article_vecs[i]) continue;

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

  return article_vecs;
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
