#ifndef PREPROCESS_QUERY_H
#define PREPROCESS_QUERY_H

#include "hash_t.h"

char **tokenize_query(const char *query_user, long int *token_count);
int preprocess_query_single(const char *query_user, const hash_t *global_idf,
                            hash_t **query_tf_out, double *query_norm_out);
double *compute_similarities(const hash_t *query_tf, double query_norm,
                             hash_t **global_tf, const double *global_doc_norms,
                             long int num_docs);

#endif
