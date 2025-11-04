#ifndef PREPROCESS_QUERY_H
#define PREPROCESS_QUERY_H

#include "hash_t.h"

int preprocess_query(const char *query_user, const hash_t *global_idf,
                     hash_t **query_tf_out, double *query_norm_out);
double *compute_similarities(const hash_t *query_tf, double query_norm,
                             hash_t **global_tf, const double *global_doc_norms,
                             long int num_docs, int nthreads);

/**
 * @brief Processa query do usuário, calcula similaridades e exibe resultados
 *
 * @param query_user String da query do usuário
 * @param nthreads Número de threads para cálculo de similaridades
 * @param k Número de documentos top-k a retornar
 * @param db Caminho para o banco de dados SQLite
 * @param table Nome da tabela no banco de dados
 * @return 0 em sucesso, 1 em erro
 */
int process_and_display_query(const char *query_user, int nthreads, int k,
                              const char *db, const char *table);

#endif
