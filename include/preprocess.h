#ifndef PREPROCESS_H
#define PREPROCESS_H

char ***tokenize_articles(char **article_texts, long int count);
void free_article_vecs(char ***article_vecs, long int count);

#endif
