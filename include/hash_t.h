#ifndef HASH_T_H
#define HASH_T_H

#include <stddef.h>
#include <stdint.h>

/* ---------- Generic Hash Set (string → exists) ---------- */

typedef struct GEntry {
  char *word;
  size_t wlen;
  double idf;
  struct GEntry *next;
} GEntry;

typedef struct generic_hash {
  GEntry **buckets;
  size_t cap;
  size_t size;
} generic_hash;

generic_hash *generic_hash_new(void);
void generic_hash_free(generic_hash *set);
void generic_hash_add(generic_hash *set, const char *word);
int generic_hash_contains(const generic_hash *set, const char *word);
void generic_hash_merge(generic_hash *dst, const generic_hash *src);
size_t generic_hash_size(const generic_hash *set);
const char **generic_hash_to_vec(const generic_hash *set);

/* ---------- IMap Hash (int -> double) ---------- */

typedef struct IEntry {
  int key;
  double val;
  struct IEntry *next;
} IEntry;

typedef struct {
  IEntry **b;
  size_t cap, size;
} IMap;

IMap imap_new(size_t cap);
void imap_free(IMap *m);
int imap_get(const IMap *m, int k, double *out);
void imap_set(IMap *m, int k, double v);
void imap_add(IMap *m, int k, double dv);

/* ---------- TF Hash (string → IMap) for TF-IDF ---------- */

typedef struct OEntry {
  char *key;
  size_t klen;
  IMap map;
  struct OEntry *next;
} OEntry;

typedef struct {
  OEntry **b;
  size_t cap, size;
} tf_hash;

tf_hash *tf_hash_new(void);
void tf_hash_free(tf_hash *h);
void tf_hash_set(tf_hash *h, const char *word, int k, double v);
void tf_hash_add(tf_hash *h, const char *word, int k, double dv);
int tf_hash_get_freq(const tf_hash *h, const char *word, int k, double *out);
int tf_hash_get_ni(const tf_hash *h, const char *word);
void tf_hash_merge(tf_hash *dst, const tf_hash *src);
void print_tf_hash(const tf_hash *tf, long int thread_id, int verbose);

#endif
