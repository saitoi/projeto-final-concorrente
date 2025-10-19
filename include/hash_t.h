#ifndef HASH_T_H
#define HASH_T_H

#include <stddef.h>
#include <stdint.h>

/* ---------- Hash Table (string → double) ---------- */

typedef struct HashEntry {
  char *word;
  size_t wlen;
  double value;
  struct HashEntry *next;
} HashEntry;

typedef struct hash_t {
  HashEntry **buckets;
  size_t cap;
  size_t size;
} hash_t;

hash_t *hash_new(void);
void hash_free(hash_t *set);
double hash_find(const hash_t *set, const char *word);
void hash_add(hash_t *set, const char *word, double value);
int hash_contains(const hash_t *set, const char *word);
void hash_merge(hash_t *dst, const hash_t *src);
void hashes_merge(hash_t **dst, hash_t **src, long int count);
size_t hash_size(const hash_t *set);
const char **hash_to_vec(const hash_t *set);
uint64_t hash_str(const char *str, size_t len);

#endif
