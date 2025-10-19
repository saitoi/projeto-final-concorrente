#include "../include/hash_t.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef TF_HASH_INIT_CAP
#define TF_HASH_INIT_CAP 1024
#endif
#ifndef IMAP_INIT_CAP
#define IMAP_INIT_CAP 16
#endif
#ifndef GENERIC_HASH_INIT_CAP
#define GENERIC_HASH_INIT_CAP 256
#endif
#define MAX_LOAD 0.75

/* ------------- Funções Auxiliares ------------- */

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

// djb2 string hash
uint64_t hash_str(const char *str, size_t len) {
  uint64_t hash = 5381;
  for (size_t i = 0; i < len; i++) {
    hash = ((hash << 5) + hash) + (unsigned char)str[i];
  }
  return hash;
}

// integer hash
static uint64_t hash_int(int k) {
  uint64_t x = (uint64_t)k;
  x = ((x >> 16) ^ x) * 0x45d9f3b;
  x = ((x >> 16) ^ x) * 0x45d9f3b;
  x = (x >> 16) ^ x;
  return x;
}

/* ------------- Hash Genérica (str -> double) ------------- */

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

void hashes_merge(hash_t **dst, hash_t **src, long int count) {
    static long int offset = 0;
    if (!dst || !src) return;

    for (long int i = 0; i < count; i++)
        dst[offset + i] = src[i];

    offset += count;
}

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

