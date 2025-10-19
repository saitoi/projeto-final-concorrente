#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
  fprintf(stdout, "Working on it..");
  return NULL;
}
