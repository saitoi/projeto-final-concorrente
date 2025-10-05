#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

void free_article_vecs(char ***article_vecs, long int count) {
  if (!article_vecs) return;

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
