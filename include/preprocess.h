#ifndef PREPROCESS_H
#define PREPROCESS_H

#include "hash_t.h"

void set_idf_words(hash_t *vocab, hash_t **tf, long int start_doc, long int count);
void populate_tf_hash(hash_t **tf, char ***article_vecs, long int count, long int offset);
void set_idf_value(hash_t *set, hash_t **tf, double doc_count, long int num_docs);

void compute_tf_idf(hash_t **global_tf, hash_t *global_idf, long int count, long int offset);
void compute_doc_norms(double *global_doc_norms, hash_t **global_tf,
                       long int doc_count, long int vocab_size, long int offset);

char ***tokenize(char **article_texts, long int count);
void remove_stopwords(char ***article_vecs, long int count);
void stem(char ***article_vecs, long int count);
void free_article_vecs(char ***article_vecs, long int count);

#endif
