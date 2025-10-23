/**
 * @file hash_t.c
 * @brief Implementação de tabela hash genérica (string -> double)
 *
 * Este arquivo implementa uma tabela hash de encadeamento separado otimizada
 * para armazenar mapeamentos de strings para valores double. É utilizada para:
 * - TF (Term Frequency): frequência de termos por documento
 * - IDF (Inverse Document Frequency): valores IDF do vocabulário
 * - Vetores TF-IDF: representação vetorial de documentos
 *
 * Características:
 * - Capacidade inicial configurável via macros
 * - Rehashing automático quando fator de carga > 0.75
 * - Função hash: djb2
 * - Resolução de colisões: encadeamento separado
 */

#include "../include/hash_t.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef TF_HASH_INIT_CAP
#define TF_HASH_INIT_CAP 1024    /**< Capacidade inicial para TF hash */
#endif
#ifndef IMAP_INIT_CAP
#define IMAP_INIT_CAP 16          /**< Capacidade inicial para mapa de índices */
#endif
#ifndef GENERIC_HASH_INIT_CAP
#define GENERIC_HASH_INIT_CAP 256 /**< Capacidade inicial padrão */
#endif
#define MAX_LOAD 0.75             /**< Fator de carga máximo antes de rehash */

/* ------------- Funções Auxiliares ------------- */

/**
 * @brief Duplica uma string de forma segura
 *
 * @param s String a ser duplicada
 * @return Ponteiro para nova string alocada
 * @note Termina o programa em caso de falha de alocação
 */
static char *safe_strdup(const char *s) {
  size_t n = strlen(s) + 1;
  char *p = malloc(n);
  if (!p) {
    perror("malloc");
    exit(1);
  }
  memcpy(p, s, n);
  return p;
}

/**
 * @brief Calcula hash de string usando algoritmo djb2
 *
 * Algoritmo djb2 de Dan Bernstein - rápido e com boa distribuição.
 *
 * @param str String a ser hasheada
 * @param len Comprimento da string
 * @return Valor hash de 64 bits
 */
uint64_t hash_str(const char *str, size_t len) {
  uint64_t hash = 5381;
  for (size_t i = 0; i < len; i++) {
    hash = ((hash << 5) + hash) + (unsigned char)str[i];
  }
  return hash;
}

/* ------------- Hash Genérica (str -> double) ------------- */

/**
 * @brief Cria nova tabela hash
 *
 * Aloca e inicializa uma nova tabela hash com capacidade padrão.
 *
 * @return Ponteiro para nova hash_t
 * @note Termina o programa em caso de falha de alocação
 */
hash_t *hash_new(void) {
  hash_t *set = malloc(sizeof(*set));
  if (!set) {
    perror("malloc");
    exit(1);
  }

  set->cap = GENERIC_HASH_INIT_CAP;
  set->size = 0;
  set->buckets = calloc(set->cap, sizeof(HashEntry *));
  if (!set->buckets) {
    perror("calloc");
    exit(1);
  }

  return set;
}

/**
 * @brief Libera memória de uma tabela hash
 *
 * Libera todas as entradas, buckets e a estrutura da hash.
 *
 * @param set Tabela hash a ser liberada
 */
void hash_free(hash_t *set) {
  if (!set)
    return;

  for (size_t i = 0; i < set->cap; i++) {
    HashEntry *e = set->buckets[i];
    while (e) {
      HashEntry *n = e->next;
      free(e->word);
      free(e);
      e = n;
    }
  }

  free(set->buckets);
  free(set);
}

/**
 * @brief Realiza rehashing da tabela para nova capacidade
 *
 * Reinsere todas as entradas em uma nova tabela com capacidade maior.
 * Chamada automaticamente quando fator de carga excede MAX_LOAD.
 *
 * @param set Tabela hash a ser redimensionada
 * @param ncap Nova capacidade (deve ser potência de 2)
 * @note Função estática, uso interno apenas
 */
static void hash_rehash(hash_t *set, size_t ncap) {
  HashEntry **nb = calloc(ncap, sizeof(HashEntry *));
  if (!nb) {
    perror("calloc");
    exit(1);
  }

  for (size_t i = 0; i < set->cap; i++) {
    HashEntry *e = set->buckets[i];
    while (e) {
      HashEntry *nx = e->next;
      size_t idx = hash_str(e->word, e->wlen) & (ncap - 1);
      e->next = nb[idx];
      nb[idx] = e;
      e = nx;
    }
  }

  free(set->buckets);
  set->buckets = nb;
  set->cap = ncap;
}

/**
 * @brief Adiciona ou atualiza entrada na tabela hash
 *
 * Se a palavra já existir, não faz nada (mantém valor original).
 * Se não existir, cria nova entrada com o valor fornecido.
 * Realiza rehashing automático se necessário.
 *
 * @param set Tabela hash
 * @param word Palavra (chave)
 * @param value Valor a ser armazenado
 */
void hash_add(hash_t *set, const char *word, double value) {
  size_t wlen = strlen(word);

  if ((set->size + 1) > (size_t)(set->cap * MAX_LOAD)) {
    size_t ncap = set->cap << 1;
    hash_rehash(set, ncap);
  }

  size_t idx = hash_str(word, wlen) & (set->cap - 1);

  for (HashEntry *e = set->buckets[idx]; e; e = e->next) {
    if (e->wlen == wlen && memcmp(e->word, word, wlen) == 0) {
      return;
    }
  }

  HashEntry *e = malloc(sizeof(*e));
  if (!e) {
    perror("malloc");
    exit(1);
  }

  e->word = safe_strdup(word);
  e->wlen = wlen;
  e->value = value;
  e->next = set->buckets[idx];
  set->buckets[idx] = e;
  set->size++;
}

/**
 * @brief Verifica se palavra existe na tabela hash
 *
 * @param set Tabela hash
 * @param word Palavra a buscar
 * @return 1 se encontrada, 0 caso contrário
 */
int hash_contains(const hash_t *set, const char *word) {
  if (!set || !set->cap)
    return 0;

  size_t wlen = strlen(word);
  size_t idx = hash_str(word, wlen) & (set->cap - 1);

  for (HashEntry *e = set->buckets[idx]; e; e = e->next) {
    if (e->wlen == wlen && memcmp(e->word, word, wlen) == 0) {
      return 1;
    }
  }

  return 0;
}

/**
 * @brief Merge array de hashes locais para array global
 *
 * Copia ponteiros de hashes locais (src) para posições específicas
 * do array global (dst). Usado para consolidar TF de threads.
 *
 * @param dst Array de hashes destino (global)
 * @param src Array de hashes origem (local da thread)
 * @param count Número de hashes a copiar
 * @param start Índice inicial no array destino
 */
void hashes_merge(hash_t **dst, hash_t **src, long int count, long int start) {
  if (!dst || !src)
    return;

  for (long int i = 0; i < count; i++)
    dst[start + i] = src[i];
}

/**
 * @brief Merge duas tabelas hash (copia entradas de src para dst)
 *
 * Adiciona todas as entradas da hash origem para a hash destino.
 * Usado para consolidar vocabulário IDF de threads locais.
 *
 * @param dst Tabela hash destino
 * @param src Tabela hash origem
 */
void hash_merge(hash_t *dst, const hash_t *src) {
  if (!dst || !src || !src->cap)
    return;

  for (size_t i = 0; i < src->cap; i++) {
    HashEntry *e = src->buckets[i];
    while (e) {
      hash_add(dst, e->word, e->value);
      e = e->next;
    }
  }
}

/**
 * @brief Retorna número de entradas na tabela hash
 *
 * Conta todas as entradas percorrendo os buckets.
 *
 * @param set Tabela hash
 * @return Número de entradas
 */
size_t hash_size(const hash_t *set) {
  if (!set || !set->cap)
    return 0;

  long int size = 0;
  for (size_t i = 0; i < set->cap; i++) {
    HashEntry *e = set->buckets[i];
    while (e) {
      size++;
      e = e->next;
    }
  }

  return size;
}

/**
 * @brief Busca valor associado a uma palavra
 *
 * @param set Tabela hash
 * @param word Palavra a buscar
 * @return Valor associado, ou 0.0 se não encontrado
 */
double hash_find(const hash_t *set, const char *word) {
  if (!set || !word)
    return 0.0;

  size_t wlen = strlen(word);
  size_t idx = hash_str(word, wlen) % set->cap;
  HashEntry *e = set->buckets[idx];
  while (e) {
    if (!strcmp(e->word, word))
      return e->value;
    e = e->next;
  }

  return 0.0;
}

/**
 * @brief Converte tabela hash para array de strings
 *
 * Cria array com todas as palavras (chaves) da hash.
 * Útil para iterar sobre vocabulário.
 *
 * @param set Tabela hash
 * @return Array de ponteiros para strings, ou NULL se vazio
 * @note Caller deve liberar array (mas não as strings)
 */
const char **hash_to_vec(const hash_t *set) {
  if (!set || !set->cap)
    return NULL;

  size_t count = hash_size(set);
  const char **vec = (const char **)malloc(count * sizeof(const char *));
  if (!vec) {
    perror("malloc");
    exit(1);
  }

  size_t idx = 0;
  for (size_t i = 0; i < set->cap; i++) {
    HashEntry *e = set->buckets[i];
    while (e) {
      vec[idx++] = e->word;
      e = e->next;
    }
  }

  return vec;
}
