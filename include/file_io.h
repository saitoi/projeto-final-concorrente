#ifndef FILE_IO_H
#define FILE_IO_H

#include "hash_t.h"
#include <stddef.h>

/* ==================== Stopwords ==================== */

extern hash_t *global_stopwords;

void load_stopwords(const char *filename);
void free_stopwords(void);

/* ==================== Funções de Serialização ==================== */

int save_hash(const hash_t *gh, const char *filename);
int save_hash_array(hash_t **hashes, long int num_hashes, const char *filename);
int save_doc_vecs(double **doc_vecs, long int num_docs, size_t vocab_size,
                  const char *filename);
int save_doc_norms(const double *norms, long int num_docs,
                   const char *filename);
int save_vocab(const char **vocab, size_t vocab_size, const char *filename);

/* ==================== Funções de Carregamento ==================== */

hash_t *load_hash(const char *filename);
hash_t **load_hash_array(const char *filename, long int *num_hashes_out);
double **load_doc_vecs(const char *filename, long int *num_docs_out,
                       size_t *vocab_size_out);
double *load_doc_norms(const char *filename, long int *num_docs_out);
const char **load_vocab(const char *filename, size_t *vocab_size_out);

#endif
