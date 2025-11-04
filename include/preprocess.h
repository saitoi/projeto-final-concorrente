#ifndef PREPROCESS_H
#define PREPROCESS_H

#include "hash_t.h"
#include <pthread.h>

/**
 * @struct DocSim
 * @brief Estrutura para armazenar ID de documento e sua similaridade
 */
typedef struct {
  long int doc_id;   /**< ID do documento */
  double similarity; /**< Similaridade com a query */
} DocSim;

/**
 * @struct thread_args
 * @brief Argumentos passados para cada thread de pré-processamento
 */
typedef struct {
  long int start;    /**< Índice inicial do intervalo de documentos */
  long int end;      /**< Índice final do intervalo de documentos */
  long int nthreads; /**< Número total de threads */
  long int id;       /**< ID da thread (0 a nthreads-1) */
  const char *db;    /**< Caminho para o arquivo SQLite */
  const char *table; /**< Nome da tabela no banco de dados */
} thread_args;

void set_idf_words(hash_t *vocab, hash_t **tf, long int start_doc,
                   long int count);
void populate_tf_hash(hash_t **tf, char ***article_vecs, long int count,
                      long int offset);
void set_idf_value(hash_t *set, hash_t **tf, double doc_count,
                   long int num_docs);

void compute_tf_idf(hash_t **global_tf, hash_t *global_idf, long int count,
                    long int offset);
void compute_doc_norms(double *global_doc_norms, hash_t **global_tf,
                       long int doc_count, long int vocab_size,
                       long int offset);

char ***tokenize(char **article_texts, long int count);
void remove_stopwords(char ***article_vecs, long int count);
void stem(char ***article_vecs, long int count);
void free_article_vecs(char ***article_vecs, long int count);

// Thread functions for two-phase preprocessing
void *preprocess_1(void *arg);
void *preprocess_2(void *arg);

// Comparison function for qsort
int compare_sim(const void *a, const void *b);

/**
 * @brief Executa pré-processamento completo (FASE 1 e FASE 2)
 *
 * @param nthreads Número de threads para processamento paralelo
 * @param entries Número de documentos a processar
 * @param db Caminho para o banco de dados SQLite
 * @param table Nome da tabela no banco de dados
 * @param filename_tf Nome do arquivo TF para salvar
 * @param filename_idf Nome do arquivo IDF para salvar
 * @param filename_doc_norms Nome do arquivo de normas para salvar
 * @return 0 em sucesso, 1 em erro
 */
pthread_t *run_preprocessing(int nthreads, long int entries, const char *db,
                      const char *table, const char *filename_tf,
                      const char *filename_idf, const char *filename_doc_norms);

#endif
