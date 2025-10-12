#ifndef PREPROCESS_H
#define PREPROCESS_H

#include "hash_t.h"

extern generic_hash* global_stopwords;

char ***stem_articles(char ***article_vecs, long int count);

void load_stopwords(const char* filename);
void free_stopwords(void);

char ***stem_articles(char ***article_vecs, long int count);

char ***tokenize_articles(char **article_texts, long int count);
char ***remove_stopwords(char ***article_vecs, long int count);
void free_article_vecs(char ***article_vecs, long int count);

#endif
