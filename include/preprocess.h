#ifndef PREPROCESS_H
#define PREPROCESS_H

#include "hash_t.h"

// Conjunto global de stopwords (compartilhados entre as threads)
extern generic_hash* global_stopwords;

void load_stopwords(const char* filename);
void free_stopwords(void);

char ***tokenize_articles(char **article_texts, long int count);
char ***remove_stopwords(char ***article_vecs, long int count);
void free_article_vecs(char ***article_vecs, long int count);

#endif
