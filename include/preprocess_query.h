#ifndef PREPROCESS_QUERY_H
#define PREPROCESS_QUERY_H

char **tokenize_query(const char *query_user, long int *token_count);
void *preprocess_query(void *arg);

#endif
