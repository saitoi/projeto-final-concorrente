#ifndef PREPROCESS_H
#define PREPROCESS_H

#include "hash_t.h"

extern generic_hash *global_stopwords;

generic_hash *generate_vocab(generic_hash *vocab, char ***article_vecs, long int count);

tf_hash *populate_tf_hash(tf_hash *tf, char ***article_vecs, long int count,
                          long int offset);

void set_idf(generic_hash *set, const tf_hash *tf, double doc_count);

char ***stem_articles(char ***article_vecs, long int count);
void compute_doc_vecs(double **global_doc_vec, const tf_hash *global_tf, generic_hash *global_idf, long int count, long int offset);
void compute_doc_norms(double *global_doc_norms, double **global_doc_vecs, long int doc_count, long int vocab_size, long int offset);

void load_stopwords(const char *filename);
void free_stopwords(void);

char ***stem_articles(char ***article_vecs, long int count);

char ***tokenize_articles(char **article_texts, long int count);
char ***remove_stopwords(char ***article_vecs, long int count);
void free_article_vecs(char ***article_vecs, long int count);

#endif
