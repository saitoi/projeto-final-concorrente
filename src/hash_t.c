#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  long int idx;
  double freq;
} hash_elem;

typedef struct {
  hash_elem *elem;
  char *word;
  int in_use;
} hash_entry;

// Struct do TF-IDF = Tabela Hash
typedef struct {
  hash_entry *entries;
} hash_t;

// Bernstein hash function
static unsigned long hash(const char *str) {
  unsigned long h = 5381;
  int c;
  while ((c = *str++))
    h = ((h << 5) + h) + c;
  return h;
}

// Verificação da alocação feita externamente
hash_t *hash_create(long int tbl_size) {
  hash_t *d = (hash_t *)malloc(sizeof(hash_t));
  d->entries = calloc(tbl_size, sizeof(hash_entry));
  return d;
}

// void hash_set(hash_t *d, long int tbl_size, const char *word, double value) {
//     unsigned long idx = hash(key) % TABLE_SIZE;
//     while (d->entries[idx].in_use) {
//         if (strcmp(d->entries[idx].key, key) == 0) {
//             d->entries[idx].value = value;
//             return;
//         }
//         idx = (idx + 1) % TABLE_SIZE;
//     }
//     d->entries[idx].key = strdup(key);
//     d->entries[idx].value = value;
//     d->entries[idx].in_use = 1;
// }