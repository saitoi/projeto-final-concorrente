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
static uint64_t hash_str(const char *str, size_t len) {
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

generic_hash *generic_hash_new(void) {
  generic_hash *set = malloc(sizeof(*set));
  if (!set) {
    perror("malloc");
    exit(1);
  }

  set->cap = GENERIC_HASH_INIT_CAP;
  set->size = 0;
  set->buckets = calloc(set->cap, sizeof(GEntry *));
  if (!set->buckets) {
    perror("calloc");
    exit(1);
  }

  return set;
}

void generic_hash_free(generic_hash *set) {
  if (!set)
    return;

  for (size_t i = 0; i < set->cap; i++) {
    GEntry *e = set->buckets[i];
    while (e) {
      GEntry *n = e->next;
      free(e->word);
      free(e);
      e = n;
    }
  }

  free(set->buckets);
  free(set);
}

static void generic_hash_rehash(generic_hash *set, size_t ncap) {
  GEntry **nb = calloc(ncap, sizeof(GEntry *));
  if (!nb) {
    perror("calloc");
    exit(1);
  }

  for (size_t i = 0; i < set->cap; i++) {
    GEntry *e = set->buckets[i];
    while (e) {
      GEntry *nx = e->next;
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

void generic_hash_add(generic_hash *set, const char *word, double value) {
  size_t wlen = strlen(word);

  if ((set->size + 1) > (size_t)(set->cap * MAX_LOAD)) {
    size_t ncap = set->cap << 1;
    generic_hash_rehash(set, ncap);
  }

  size_t idx = hash_str(word, wlen) & (set->cap - 1);

  for (GEntry *e = set->buckets[idx]; e; e = e->next) {
    if (e->wlen == wlen && memcmp(e->word, word, wlen) == 0) {
      return;
    }
  }

  GEntry *e = malloc(sizeof(*e));
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

int generic_hash_contains(const generic_hash *set, const char *word) {
  if (!set || !set->cap)
    return 0;

  size_t wlen = strlen(word);
  size_t idx = hash_str(word, wlen) & (set->cap - 1);

  for (GEntry *e = set->buckets[idx]; e; e = e->next) {
    if (e->wlen == wlen && memcmp(e->word, word, wlen) == 0) {
      return 1;
    }
  }

  return 0;
}

void generic_hash_merge(generic_hash *dst, const generic_hash *src) {
  if (!dst || !src || !src->cap)
    return;

  for (size_t i = 0; i < src->cap; i++) {
    GEntry *e = src->buckets[i];
    while (e) {
      generic_hash_add(dst, e->word, e->value);
      e = e->next;
    }
  }
}

size_t generic_hash_size(const generic_hash *set) {
  if (!set || !set->cap)
    return 0;

  long int size = 0;
  for (size_t i = 0; i < set->cap; i++) {
    GEntry *e = set->buckets[i];
    while (e) {
      size++;
      e = e->next;
    }
  }

  return size;
}

const char **generic_hash_to_vec(const generic_hash *set) {
  if (!set || !set->cap)
    return NULL;

  size_t count = generic_hash_size(set);
  const char **vec = (const char **)malloc(count * sizeof(const char *));
  if (!vec) {
    perror("malloc");
    exit(1);
  }

  size_t idx = 0;
  for (size_t i = 0; i < set->cap; i++) {
    GEntry *e = set->buckets[i];
    while (e) {
      vec[idx++] = e->word;
      e = e->next;
    }
  }

  return vec;
}

/* ---------- IMap (int → double) ---------- */

IMap imap_new(size_t cap) {
  IMap m;
  m.cap = cap ? cap : IMAP_INIT_CAP;
  m.size = 0;
  m.b = calloc(m.cap, sizeof(IEntry *));
  if (!m.b) {
    perror("calloc");
    exit(1);
  }
  return m;
}

void imap_free(IMap *m) {
  for (size_t i = 0; i < m->cap; i++) {
    IEntry *e = m->b[i];
    while (e) {
      IEntry *n = e->next;
      free(e);
      e = n;
    }
  }
  free(m->b);
  m->b = NULL;
  m->cap = m->size = 0;
}

static void imap_rehash(IMap *m, size_t ncap) {
  IEntry **nb = calloc(ncap, sizeof(IEntry *));
  if (!nb) {
    perror("calloc");
    exit(1);
  }

  for (size_t i = 0; i < m->cap; i++) {
    IEntry *e = m->b[i];
    while (e) {
      IEntry *nx = e->next;
      size_t idx = hash_int(e->key) & (ncap - 1);
      e->next = nb[idx];
      nb[idx] = e;
      e = nx;
    }
  }

  free(m->b);
  m->b = nb;
  m->cap = ncap;
}

int imap_get(const IMap *m, int k, double *out) {
  if (!m->cap)
    return 0;

  size_t idx = hash_int(k) & (m->cap - 1);

  for (IEntry *e = m->b[idx]; e; e = e->next) {
    if (e->key == k) {
      if (out)
        *out = e->val;
      return 1;
    }
  }

  return 0;
}

void imap_set(IMap *m, int k, double v) {
  if ((m->size + 1) > (size_t)(m->cap * MAX_LOAD)) {
    size_t ncap = m->cap ? m->cap << 1 : IMAP_INIT_CAP;
    if ((ncap & (ncap - 1)) != 0) {
      size_t p = 1;
      while (p < ncap)
        p <<= 1;
      ncap = p;
    }
    imap_rehash(m, ncap);
  }

  size_t idx = hash_int(k) & (m->cap - 1);

  for (IEntry *e = m->b[idx]; e; e = e->next) {
    if (e->key == k) {
      e->val += v;
      return;
    }
  }

  IEntry *e = malloc(sizeof(*e));
  if (!e) {
    perror("malloc");
    exit(1);
  }
  e->key = k;
  e->val = v;
  e->next = m->b[idx];
  m->b[idx] = e;
  m->size++;
}

// Não acho que vai precisar
void imap_add(IMap *m, int k, double dv) {
  double cur;
  if (imap_get(m, k, &cur)) {
    imap_set(m, k, cur + dv);
  } else {
    imap_set(m, k, dv);
  }
}

/* ---------- TF Hash (string → IMap) ---------- */

tf_hash *tf_hash_new(void) {
  tf_hash *h = malloc(sizeof(*h));
  if (!h) {
    perror("malloc");
    exit(1);
  }

  size_t cap = TF_HASH_INIT_CAP;
  if ((cap & (cap - 1)) != 0) {
    size_t p = 1;
    while (p < cap)
      p <<= 1;
    cap = p;
  }

  h->cap = cap;
  h->size = 0;
  h->b = calloc(h->cap, sizeof(OEntry *));
  if (!h->b) {
    perror("calloc");
    exit(1);
  }

  return h;
}

void tf_hash_free(tf_hash *h) {
  if (!h)
    return;

  for (size_t i = 0; i < h->cap; i++) {
    OEntry *e = h->b[i];
    while (e) {
      OEntry *n = e->next;
      imap_free(&e->map);
      free(e->key);
      free(e);
      e = n;
    }
  }

  free(h->b);
  free(h);
}

static void tf_hash_rehash(tf_hash *h, size_t ncap) {
  OEntry **nb = calloc(ncap, sizeof(OEntry *));
  if (!nb) {
    perror("calloc");
    exit(1);
  }

  for (size_t i = 0; i < h->cap; i++) {
    OEntry *e = h->b[i];
    while (e) {
      OEntry *nx = e->next;
      size_t idx = hash_str(e->key, e->klen) & (ncap - 1);
      e->next = nb[idx];
      nb[idx] = e;
      e = nx;
    }
  }

  free(h->b);
  h->b = nb;
  h->cap = ncap;
}

static OEntry *tf_hash_find(tf_hash *h, const char *word, int create) {
  size_t klen = strlen(word);

  if ((h->size + 1) > (size_t)(h->cap * MAX_LOAD)) {
    size_t ncap = h->cap << 1;
    tf_hash_rehash(h, ncap);
  }

  size_t idx = hash_str(word, klen) & (h->cap - 1);

  for (OEntry *e = h->b[idx]; e; e = e->next) {
    if (e->klen == klen && memcmp(e->key, word, klen) == 0) {
      return e;
    }
  }

  if (!create)
    return NULL;

  OEntry *e = malloc(sizeof(*e));
  if (!e) {
    perror("malloc");
    exit(1);
  }

  e->key = safe_strdup(word);
  e->klen = klen;
  e->map = imap_new(IMAP_INIT_CAP);
  e->next = h->b[idx];
  h->b[idx] = e;
  h->size++;

  return e;
}

void tf_hash_set(tf_hash *h, const char *word, int k, double v) {
  OEntry *e = tf_hash_find(h, word, 1);
  imap_set(&e->map, k, v);
}

// Não precisa eu acho
void tf_hash_add(tf_hash *h, const char *word, int k, double dv) {
  OEntry *e = tf_hash_find(h, word, 1);
  imap_add(&e->map, k, dv);
}

int tf_hash_get_freq(const tf_hash *h, const char *word, int k, double *out) {
  size_t klen = strlen(word);
  size_t idx = hash_str(word, klen) & (h->cap - 1);

  for (OEntry *e = h->b[idx]; e; e = e->next) {
    if (e->klen == klen && memcmp(e->key, word, klen) == 0) {
      return imap_get(&e->map, k, out);
    }
  }

  return 0;
}

int tf_hash_get_ni(const tf_hash *h, const char *word) {
  size_t klen = strlen(word);
  size_t idx = hash_str(word, klen) & (h->cap - 1);

  for (OEntry *e = h->b[idx]; e; e = e->next) {
    if (e->klen == klen && memcmp(e->key, word, klen) == 0) {
      return e->map.size;
    }
  }

  return 0;
}

void tf_hash_merge(tf_hash *dst, const tf_hash *src) {
  for (size_t i = 0; i < src->cap; i++) {
    for (OEntry *se = src->b[i]; se; se = se->next) {
      OEntry *de = tf_hash_find(dst, se->key, 1);

      for (size_t j = 0; j < se->map.cap; j++) {
        for (IEntry *ie = se->map.b[j]; ie; ie = ie->next) {
          double cur;
          if (imap_get(&de->map, ie->key, &cur)) {
            imap_set(&de->map, ie->key, cur + ie->val);
          } else {
            imap_set(&de->map, ie->key, ie->val);
          }
        }
      }
    }
  }
}

void print_tf_hash(const tf_hash *tf, long int thread_id, int verbose) {
  if (!tf)
    return;

  if (!verbose)
    return;

  fprintf(stdout, "[VERBOSE] Thread %ld - TF Hash Contents:\n", thread_id);
  fprintf(stdout, "[VERBOSE]   Hash capacity: %zu, size: %zu\n", tf->cap,
          tf->size);

  size_t word_count = 0;
  for (size_t i = 0; i < tf->cap; i++) {
    for (OEntry *e = tf->b[i]; e; e = e->next) {
      word_count++;
      fprintf(stdout, "[VERBOSE]   Word '%s' (len=%zu):\n", e->key, e->klen);

      // Imprimir documentos e frequências
      size_t doc_count = 0;
      for (size_t j = 0; j < e->map.cap; j++) {
        for (IEntry *ie = e->map.b[j]; ie; ie = ie->next) {
          fprintf(stdout, "[VERBOSE]     Doc %d: %.0f\n", ie->key, ie->val);
          doc_count++;
        }
      }
      fprintf(stdout, "[VERBOSE]     Total docs for '%s': %zu\n", e->key,
              doc_count);

      // Limitar a saída para não ficar muito grande
      if (word_count >= 20) {
        fprintf(
            stdout,
            "[VERBOSE]   ... (mostrando apenas as primeiras 20 palavras)\n");
        return;
      }
    }
  }
  fprintf(stdout, "[VERBOSE] Thread %ld - Total de palavras únicas: %zu\n",
          thread_id, word_count);
}
