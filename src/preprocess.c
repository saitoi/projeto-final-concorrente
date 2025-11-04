/**
 * @file preprocess.c
 * @brief Pipeline de pré-processamento de documentos para TF-IDF
 *
 * Este arquivo implementa o pipeline completo de pré-processamento paralelo
 * de documentos usado no sistema de recuperação de informações:
 * - Tokenização de textos
 * - Remoção de stopwords
 * - Aplicação de stemming (normalização morfológica)
 * - Construção de vocabulário (IDF)
 * - Cálculo de Term Frequency (TF)
 * - Cálculo de TF-IDF
 * - Computação de normas vetoriais
 *
 * Projetado para execução paralela com múltiplas threads.
 */

#include "../include/preprocess.h"
#include "../include/file_io.h"
#include "../include/hash_t.h"
#include "../include/log.h"
#include "../include/sqlite_helper.h"
#include <libstemmer.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// External global variables from main.c
extern hash_t **global_tf;
extern hash_t *global_idf;
extern double *global_doc_norms;
extern size_t global_vocab_size;
extern long int global_entries;

/**
 * @brief Popula vocabulário com palavras únicas dos documentos
 *
 * Extrai todas as palavras únicas de um conjunto de documentos tokenizados
 * e adiciona ao hash de vocabulário. Usado para construir o vocabulário
 * global antes de calcular IDF.
 *
 * @param vocab Hash de vocabulário (IDF) a ser populado
 * @param article_vecs Array de vetores de tokens (documentos tokenizados)
 * @param count Número de documentos no array
 */
void set_idf_words(hash_t *vocab, hash_t **tf, long int start_doc,
                   long int count) {
  if (!vocab || !tf) {
    fprintf(stderr, "Erro: vocab ou tf é nulo.\n");
    pthread_exit(NULL);
  }

  // Para cada documento local, contar n_i (documentos que contêm cada palavra)
  for (long int doc_idx = 0; doc_idx < count; doc_idx++) {
    long int doc_id = start_doc + doc_idx;
    if (!tf[doc_id])
      continue;

    // Para cada palavra neste documento
    for (size_t i = 0; i < tf[doc_id]->cap; i++) {
      HashEntry *doc_entry = tf[doc_id]->buckets[i];
      while (doc_entry) {
        // Buscar entrada no vocabulário
        HashEntry *vocab_entry = hash_get(vocab, doc_entry->word);

        if (!vocab_entry) {
          // Palavra não existe: adicionar com n_i = 1 e marcar epoch
          hash_add(vocab, doc_entry->word, 1.0);
          // Buscar novamente após add (pode ter ocorrido rehash)
          vocab_entry = hash_get(vocab, doc_entry->word);
          if (vocab_entry)
            vocab_entry->epoch = doc_id;
        } else {
          // Palavra existe: incrementar n_i apenas se não foi vista neste
          // documento
          if (vocab_entry->epoch != (unsigned long)doc_id) {
            vocab_entry->value += 1.0;
            vocab_entry->epoch = doc_id;
          }
        }

        doc_entry = doc_entry->next;
      }
    }
  }
}

// Isso daqui é extremamente idiota
// Isso daqui já é feito em set_idf_words
void set_idf_freq(hash_t *idf_local, hash_t **tf, long int start_doc,
                  long int num_docs) {
  if (!idf_local || !tf) {
    fprintf(stderr, "Erro: idf_local ou tf é nulo.\n");
    pthread_exit(NULL);
  }

  // Zerar todos os valores antes de contar
  for (size_t i = 0; i < idf_local->cap; i++) {
    HashEntry *e = idf_local->buckets[i];
    while (e) {
      e->value = 0.0;
      e = e->next;
    }
  }

  // Para cada documento local, contar quantos documentos contêm cada palavra
  for (long int doc_idx = 0; doc_idx < num_docs; doc_idx++) {
    long int doc_id = start_doc + doc_idx;
    if (!tf[doc_id])
      continue;

    // Para cada palavra neste documento
    for (size_t i = 0; i < tf[doc_id]->cap; i++) {
      HashEntry *doc_entry = tf[doc_id]->buckets[i];
      while (doc_entry) {
        size_t wlen = doc_entry->wlen;
        size_t idx = hash_str(doc_entry->word, wlen) & (idf_local->cap - 1);

        // Buscar palavra no vocabulário local e incrementar contador (n_i
        // local)
        for (HashEntry *vocab_entry = idf_local->buckets[idx]; vocab_entry;
             vocab_entry = vocab_entry->next) {
          if (vocab_entry->wlen == wlen &&
              memcmp(vocab_entry->word, doc_entry->word, wlen) == 0) {
            vocab_entry->value += 1.0;
            break;
          }
        }
        doc_entry = doc_entry->next;
      }
    }
  }
}

/**
 * @brief Calcula valores IDF para todas as palavras do vocabulário
 *
 * Computa o IDF (Inverse Document Frequency) usando a fórmula:
 * IDF(palavra) = log2(total_documentos / documentos_contendo_palavra)
 *
 * @param set Hash de vocabulário (IDF) a ser calculado
 * @param tf Array de hashes TF de todos os documentos
 * @param doc_count Número total de documentos (double para cálculo)
 * @param num_docs Número de documentos no array tf
 */
void set_idf_value(hash_t *set, hash_t **tf, double doc_count,
                   long int num_docs) {
  if (!set || !tf) {
    fprintf(stderr, "Erro: set ou tf é nulo.\n");
    pthread_exit(NULL);
  }

  // Primeiro, zerar todos os valores (vamos usar como contador)
  for (size_t i = 0; i < set->cap; i++) {
    HashEntry *e = set->buckets[i];
    while (e) {
      e->value = 0.0;
      e = e->next;
    }
  }

  // Percorrer cada documento uma única vez e incrementar contador para cada
  // palavra
  for (long int doc_id = 0; doc_id < num_docs; doc_id++) {
    if (!tf[doc_id])
      continue;

    // Para cada palavra neste documento
    for (size_t i = 0; i < tf[doc_id]->cap; i++) {
      HashEntry *doc_entry = tf[doc_id]->buckets[i];
      while (doc_entry) {
        // Buscar essa palavra no vocabulário global e incrementar seu contador
        size_t wlen = doc_entry->wlen;
        size_t idx = hash_str(doc_entry->word, wlen) & (set->cap - 1);

        for (HashEntry *vocab_entry = set->buckets[idx]; vocab_entry;
             vocab_entry = vocab_entry->next) {
          if (vocab_entry->wlen == wlen &&
              memcmp(vocab_entry->word, doc_entry->word, wlen) == 0) {
            vocab_entry->value += 1.0;
            break;
          }
        }

        doc_entry = doc_entry->next;
      }
    }
  }

  // Agora calcular o IDF com base nos contadores
  for (size_t i = 0; i < set->cap; i++) {
    HashEntry *e = set->buckets[i];
    while (e) {
      if (e->value > 0)
        e->value = log2(doc_count / e->value);
      else
        e->value = 0.0;
      e = e->next;
    }
  }
}

/**
 * @brief Converte valores TF para TF-IDF nos vetores de documentos
 *
 * Transforma frequências (TF) em valores TF-IDF usando a fórmula:
 * TF-IDF = (1 + log2(TF)) * IDF
 *
 * @param global_tf Array global de hashes TF
 * @param global_idf Hash global de IDF
 * @param count Número de documentos a processar
 * @param offset Índice inicial no array global_tf
 */
void compute_tf_idf(hash_t **global_tf, hash_t *global_idf, long int count,
                    long int offset) {
  if (!global_tf || !global_idf || count <= 0) {
    fprintf(stderr, "Erro: global_tf, global_idf, ou count inválido.\n");
    pthread_exit(NULL);
  }

  // Para cada documento
  for (long int i = offset; i < offset + count; ++i) {
    hash_t *doc_tf = global_tf[i];
    if (!doc_tf)
      continue;

    // Para cada palavra no documento (iterar sobre buckets da hash)
    for (size_t j = 0; j < doc_tf->cap; ++j) {
      HashEntry *e = doc_tf->buckets[j];
      while (e) {
        // e->value contém a frequência (TF) da palavra no documento
        if (e->value > 0) {
          // Buscar o IDF da palavra
          double idf = hash_find(global_idf, e->word);

          // Calcular TF-IDF: (1 + log2(freq)) * IDF
          double tfidf_value = (1.0 + log2(e->value)) * idf;

          // Atualizar o valor para TF-IDF
          e->value = tfidf_value;
        }
        e = e->next;
      }
    }
  }
}

/**
 * @brief Calcula norma euclidiana dos vetores TF-IDF de documentos
 *
 * Computa ||doc|| = sqrt(sum(tfidf^2)) para normalização da similaridade.
 *
 * @param global_doc_norms Array de normas a ser preenchido
 * @param global_tf Array de hashes TF-IDF dos documentos
 * @param doc_count Número de documentos a processar
 * @param vocab_size Tamanho do vocabulário (não usado atualmente)
 * @param offset Índice inicial no array global
 */
void compute_doc_norms(double *global_doc_norms, hash_t **global_tf,
                       long int doc_count, long int vocab_size,
                       long int offset) {
  if (!global_doc_norms || doc_count <= 0 || vocab_size <= 0 || offset < 0) {
    fprintf(stderr, "Erro nos argumentos de entrada.\n");
    return;
  }

  for (long int i = 0; i < doc_count; i++) {
    long int doc_id = offset + i;
    hash_t *doc_vec = global_tf[doc_id];

    if (!doc_vec) {
      global_doc_norms[doc_id] = 0.0;
      continue;
    }

    double norm = 0.0;

    // Calcular a soma dos quadrados de todos os valores TF-IDF do documento
    for (size_t j = 0; j < doc_vec->cap; j++) {
      HashEntry *e = doc_vec->buckets[j];
      while (e) {
        norm += e->value * e->value;
        e = e->next;
      }
    }

    // Tomar a raiz quadrada para obter a norma Euclidiana
    global_doc_norms[doc_id] = sqrt(norm);
  }
}

/**
 * @brief Aplica stemming (normalização morfológica) aos tokens
 *
 * Reduz palavras às suas raízes usando algoritmo Porter Stemmer.
 * Exemplo: "running" -> "run", "computers" -> "comput"
 *
 * @param article_vecs Array de vetores de tokens a serem normalizados
 * @param count Número de documentos no array
 */
void stem(char ***article_vecs, long int count) {
  struct sb_stemmer *stemmer = sb_stemmer_new("english", NULL);
  if (!stemmer) {
    fprintf(stderr, "Erro ao criar o Stemmer.\n");
    pthread_exit(NULL);
  }

  for (long int i = 0; i < count; ++i) {
    if (!article_vecs[i])
      continue;

    for (long int j = 0; article_vecs[i][j] != NULL; ++j) {
      const char *stemmed = (const char *)sb_stemmer_stem(
          stemmer, (const sb_symbol *)article_vecs[i][j],
          strlen(article_vecs[i][j]));
      free(article_vecs[i][j]);
      article_vecs[i][j] = strdup(stemmed);
    }
  }
  sb_stemmer_delete(stemmer);
}

/**
 * @brief Popula hashes TF com frequências de termos dos documentos
 *
 * Conta ocorrências de cada palavra em cada documento e armazena
 * em estruturas hash individuais.
 *
 * @param tf Array de hashes TF (um por documento)
 * @param article_vecs Array de vetores de tokens (documentos tokenizados)
 * @param count Número de documentos
 */
void populate_tf_hash(hash_t **tf, char ***article_vecs, long int count,
                      long int offset) {
  struct sb_stemmer *stemmer = sb_stemmer_new("english", NULL);
  if (!stemmer) {
    fprintf(stderr, "Erro ao criar o Stemmer.\n");
    pthread_exit(NULL);
  }

  for (long int i = 0; i < count; ++i) {
    long int global_idx = offset + i;
    if (!tf[global_idx] || !article_vecs[i])
      continue;

    for (long int j = 0; article_vecs[i][j] != NULL; ++j) {
      if (!article_vecs[i][j])
        continue;

      const char *word = article_vecs[i][j];
      // Remoção de stopwords diretamente no populate
      if (!hash_contains(global_stopwords, word) && strlen(word) > 1) {
        const char *stemmed = (const char *)sb_stemmer_stem(
            stemmer, (const sb_symbol *)article_vecs[i][j],
            strlen(article_vecs[i][j]));
        hash_add(tf[global_idx], stemmed, 1.0);
        free(article_vecs[i][j]);
        article_vecs[i][j] = NULL; // Evitar double free
      } else {
        free(article_vecs[i][j]);
        article_vecs[i][j] = NULL; // Liberar stopwords também
      }
    }
  }
  sb_stemmer_delete(stemmer);
}

/**
 * @brief Tokeniza textos de documentos em arrays de palavras
 *
 * Divide textos em tokens usando whitespace como delimitador.
 * Thread-safe (usa strtok_r).
 *
 * @param article_texts Array de strings com textos dos documentos
 * @param count Número de documentos
 * @return Array de vetores de tokens, ou NULL em erro
 * @note Caller deve liberar usando free_article_vecs()
 */
char ***tokenize(char **article_texts, long int count) {
  char ***article_vecs = malloc(count * sizeof(char **));
  if (!article_vecs) {
    fprintf(stderr, "Erro ao alocar article_vecs\n");
    return NULL;
  }

  const char *delimiters = " \t\n\r,.:;!?/";

  for (long int i = 0; i < count; ++i) {
    if (!article_texts[i]) {
      article_vecs[i] = NULL;
      continue;
    }

    // Contar tokens sem modificar a string original
    long int token_count = 0;
    const char *ptr = article_texts[i];
    int in_token = 0;

    while (*ptr) {
      if (strchr(delimiters, *ptr)) {
        in_token = 0;
      } else if (!in_token) {
        token_count++;
        in_token = 1;
      }
      ptr++;
    }

    // Alocar array de ponteiros
    article_vecs[i] = malloc((token_count + 1) * sizeof(char *));
    if (!article_vecs[i]) {
      continue;
    }

    // Uma única cópia para tokenização
    char *text_copy = strdup(article_texts[i]);
    if (!text_copy) {
      free(article_vecs[i]);
      article_vecs[i] = NULL;
      continue;
    }

    // Tokenizar e copiar
    long int j = 0;
    char *saveptr;
    char *token = strtok_r(text_copy, delimiters, &saveptr);
    while (token != NULL) {
      article_vecs[i][j] = strdup(token);
      j++;
      token = strtok_r(NULL, delimiters, &saveptr);
    }
    article_vecs[i][j] = NULL;

    free(text_copy);
  }

  return article_vecs;
}

/**
 * @brief Remove stopwords e palavras de uma letra dos tokens
 *
 * Filtra palavras comuns sem valor semântico (a, the, is, etc.)
 * e palavras muito curtas. Modifica os arrays in-place.
 *
 * @param article_vecs Array de vetores de tokens a serem filtrados
 * @param count Número de documentos
 */
void remove_stopwords(char ***article_vecs, long int count) {
  if (!global_stopwords) {
    fprintf(stderr,
            "Stopwords não carregadas. Chame load_stopwords() primeiro.\n");
    pthread_exit(NULL);
  }

  for (long int i = 0; i < count; ++i) {
    if (!article_vecs[i])
      continue;

    // Filtra stopwords e palavras com apenas uma letra
    long int write_idx = 0;
    for (long int read_idx = 0; article_vecs[i][read_idx] != NULL; ++read_idx) {
      const char *word = article_vecs[i][read_idx];
      // Mantém a palavra se NÃO for stopword E tiver mais de 1 letra
      if (!hash_contains(global_stopwords, word) && strlen(word) > 1) {
        article_vecs[i][write_idx++] = article_vecs[i][read_idx];
      } else {
        free(article_vecs[i][read_idx]);
      }
    }
    article_vecs[i][write_idx] = NULL;
  }
}

/**
 * @brief Libera memória alocada para vetores de tokens
 *
 * @param article_vecs Array de vetores de tokens a serem liberados
 * @param count Número de documentos
 */
void free_article_vecs(char ***article_vecs, long int count) {
  if (!article_vecs)
    return;

  for (long int i = 0; i < count; ++i) {
    if (article_vecs[i]) {
      for (long int j = 0; article_vecs[i][j] != NULL; ++j) {
        free(article_vecs[i][j]);
      }
      free(article_vecs[i]);
    }
  }
  free(article_vecs);
}

/**
 * @brief FASE 1: Construir vocabulário e TF local
 *
 * Pipeline:
 * 1. Extrair textos do SQLite
 * 2. Tokenizar
 * 3. Remover stopwords
 * 4. Stemming
 * 5. Popular TF local (global_tf)
 * 6. Popular vocabulário local (IDF)
 *
 * @param arg Ponteiro para thread_args
 * @return IDF local (hash_t*) para merge posterior no thread principal
 */
void *preprocess_1(void *arg) {
  thread_args *t = (thread_args *)arg;
  long int count = t->end - t->start;

  log_info("[FASE 1] T%02ld: Processando %ld documentos [%ld, %ld]", t->id,
           count, t->start, t->end - 1);

  if (count <= 0) {
    pthread_exit(NULL);
  }

  hash_t *idf = hash_new();

  // [1] Recuperar textos
  char **article_texts = get_str_arr(t->db,
                                     "select article_text from \"%w\" "
                                     "where article_id between ? and ? "
                                     "order by article_id asc",
                                     t->start, t->end - 1, t->table);
  if (!article_texts) {
    fprintf(stderr, "Thread %02ld: Erro ao obter dados do banco\n", t->id);
    pthread_exit(NULL);
  }

  // [2] Tokenizar
  log_info("[FASE 1] T%02ld: Tokenizando textos..", t->id);
  char ***article_vecs = tokenize(article_texts, count);
  if (!article_vecs) {
    fprintf(stderr, "Thread %02ld: Erro ao tokenizar\n", t->id);
    pthread_exit(NULL);
  }

  // [3] Popular TF local (já faz stopwords+stemming internamente)
  log_info("[FASE 1] T%02ld: Removendo stopwords e Stemmizando..", t->id);
  log_info("[FASE 1] T%02ld: Populando hash TF..", t->id);
  populate_tf_hash(global_tf, article_vecs, count, t->start);

  // [4] Popular vocabulário local com n_i (usa os hashes TF já processados)
  log_info("[FASE 1] T%02ld: Populando vocabulário..", t->id);
  set_idf_words(idf, global_tf, t->start, count);

  log_info("[FASE 1] T%02ld: Concluída", t->id);

  // Limpar memória local
  for (long int i = 0; i < count; i++) {
    if (article_texts[i])
      free(article_texts[i]);
  }

  free(article_texts);
  free_article_vecs(article_vecs, count);

  // Retornar IDF local para merge no thread principal
  pthread_exit((void *)idf);
}

/**
 * @brief FASE 2: Calcular TF-IDF e normas
 *
 * Usa IDF global já calculado para:
 * 1. Transformar TF em TF-IDF
 * 2. Calcular normas dos vetores
 *
 * @param arg Ponteiro para thread_args
 * @return NULL
 */
void *preprocess_2(void *arg) {
  thread_args *t = (thread_args *)arg;
  long int count = t->end - t->start;

  log_info("[FASE 2] T%02ld: Calculando TF-IDF e normas", t->id);

  if (count <= 0) {
    pthread_exit(NULL);
  }

  // [1] Converter TF para TF-IDF usando IDF global
  compute_tf_idf(global_tf, global_idf, count, t->start);

  // [2] Calcular normas dos documentos
  compute_doc_norms(global_doc_norms, global_tf, count, global_vocab_size,
                    t->start);

  log_info("[FASE 2] T%02ld: Concluída", t->id);
  pthread_exit(NULL);
}

/**
 * @brief Função de comparação para ordenar documentos por similaridade
 *
 * @param a Ponteiro para primeiro DocSim
 * @param b Ponteiro para segundo DocSim
 * @return -1 se a > b, 1 se a < b, 0 se iguais (ordem decrescente)
 */
int compare_sim(const void *a, const void *b) {
  const DocSim *doc1 = (const DocSim *)a;
  const DocSim *doc2 = (const DocSim *)b;
  return doc1->similarity > doc2->similarity
             ? -1
             : doc1->similarity < doc2->similarity;
}

/**
 * @brief Função auxiliar para calcular tempo decorrido
 */
static inline double get_elapsed_time(struct timespec *start,
                                      struct timespec *end) {
  return (end->tv_sec - start->tv_sec) + (end->tv_nsec - start->tv_nsec) / 1e9;
}

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
int run_preprocessing(int nthreads, long int entries, const char *db,
                      const char *table, const char *filename_tf,
                      const char *filename_idf,
                      const char *filename_doc_norms) {
  const int MAX_THREADS = 16;

  pthread_t *tids = (pthread_t *)malloc(sizeof(pthread_t) * nthreads);
  if (!tids) {
    fprintf(stderr, "Falha ao alocar memória para tids\n");
    return 1;
  }

  thread_args args[MAX_THREADS];

  // Inicializar estruturas globais
  global_idf = hash_new();
  global_tf = (hash_t **)calloc(entries, sizeof(hash_t *));
  if (!global_tf) {
    fprintf(stderr, "Falha ao alocar memória para global_tf\n");
    free(tids);
    return 1;
  }

  // Inicializar cada hash TF individual
  for (long int i = 0; i < entries; i++) {
    global_tf[i] = hash_new();
    if (!global_tf[i]) {
      fprintf(stderr, "Falha ao alocar hash TF para documento %ld\n", i);
      free(tids);
      return 1;
    }
  }

  global_entries = entries;

  // Carregar stopwords (compartilhado por todas threads)
  load_stopwords("assets/stopwords.txt");
  if (!global_stopwords) {
    fprintf(stderr, "Falha ao carregar stopwords\n");
    free(tids);
    return 1;
  }

  printf("Qtd. artigos: %ld\n", entries);

  // Calcular divisão de trabalho
  long int base = entries / nthreads;
  long int rem = entries % nthreads;

  /* ---------- FASE 1: Construir Vocabulário ---------- */

  struct timespec t_start_fase1, t_end_fase1;
  clock_gettime(CLOCK_MONOTONIC, &t_start_fase1);

  printf("\n[FASE 1] Construindo vocabulário...\n");

  for (long int i = 0; i < nthreads; ++i) {
    args[i].id = i;
    args[i].nthreads = nthreads;
    args[i].db = db;
    args[i].table = table;
    args[i].start = i * base + (i < rem ? i : rem);
    args[i].end = args[i].start + base + (i < rem);

    if (pthread_create(&tids[i], NULL, preprocess_1, (void *)&args[i])) {
      fprintf(stderr, "Erro ao criar thread %ld\n", i);
      free(tids);
      return 1;
    }
  }

  // Aguardar conclusão da Fase 1 e coletar IDFs locais
  hash_t *local_idfs[MAX_THREADS];
  for (long int i = 0; i < nthreads; ++i) {
    void *ret_val;
    if (pthread_join(tids[i], &ret_val)) {
      fprintf(stderr, "Erro ao esperar thread %ld\n", i);
      free(tids);
      return 1;
    }
    local_idfs[i] = (hash_t *)ret_val;
  }

  // Merge dos IDFs locais no global (fora da seção crítica)
  printf("[FASE 1] Fazendo merge dos vocabulários locais...\n");
  for (long int i = 0; i < nthreads; ++i) {
    if (local_idfs[i]) {
      hash_merge(global_idf, local_idfs[i]);
      hash_free(local_idfs[i]);
    }
  }

  printf("[FASE 1] Vocabulário construído: %zu palavras\n",
         hash_size(global_idf));

  // Aplicar log2(N / n_i) em todos os valores
  printf("[FASE 1] Calculando IDF global...\n");
  for (size_t i = 0; i < global_idf->cap; i++) {
    HashEntry *e = global_idf->buckets[i];
    while (e) {
      if (e->value > 0)
        e->value = log2((double)global_entries / e->value);
      else
        e->value = 0.0;
      e = e->next;
    }
  }
  global_vocab_size = hash_size(global_idf);

  // Alocar normas
  global_doc_norms = (double *)calloc(global_entries, sizeof(double));
  if (!global_doc_norms) {
    fprintf(stderr, "Erro ao alocar memória para global_doc_norms\n");
    free(tids);
    return 1;
  }

  clock_gettime(CLOCK_MONOTONIC, &t_end_fase1);
  double elapsed_fase1 = get_elapsed_time(&t_start_fase1, &t_end_fase1);
  printf("[FASE 1] Concluída.. IDF computado e vocabulário com %zu palavras\n",
         global_vocab_size);
  printf("[FASE 1] Tempo: %.3f segundos\n", elapsed_fase1);

  /* ========== FASE 2: Calcular TF-IDF ========== */
  struct timespec t_start_fase2, t_end_fase2;
  clock_gettime(CLOCK_MONOTONIC, &t_start_fase2);

  printf("\n[FASE 2] Calculando TF-IDF e normas...\n");

  for (long int i = 0; i < nthreads; ++i) {
    if (pthread_create(&tids[i], NULL, preprocess_2, (void *)&args[i])) {
      fprintf(stderr, "Erro ao criar thread %ld\n", i);
      free(tids);
      return 1;
    }
  }

  // Aguardar conclusão da Fase 2
  for (long int i = 0; i < nthreads; ++i) {
    if (pthread_join(tids[i], NULL)) {
      fprintf(stderr, "Erro ao esperar thread %ld\n", i);
      free(tids);
      return 1;
    }
  }

  clock_gettime(CLOCK_MONOTONIC, &t_end_fase2);
  double elapsed_fase2 = get_elapsed_time(&t_start_fase2, &t_end_fase2);
  printf("[FASE 2] TF-IDF e normas calculados!\n");
  printf("[FASE 2] Tempo: %.3f segundos\n", elapsed_fase2);

  // Salvar estruturas globais em arquivos binários
  printf("\nSalvando estruturas em disco\n");

  save_hash_array(global_tf, global_entries, filename_tf);
  save_hash(global_idf, filename_idf);
  save_doc_norms(global_doc_norms, global_entries, filename_doc_norms);

  // Liberar stopwords
  free_stopwords();

  // Liberar array de thread IDs
  free(tids);

  return 0;
}
