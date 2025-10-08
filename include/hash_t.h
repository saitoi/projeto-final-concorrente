#ifndef HASH_T_H
#define HASH_T_H

typedef struct hash_t hash_t;

// Create e destroy
hash_t *hash_create(void);
void hash_free(hash_t *d);

// Set e get
void hash_set(hash_t *d, const char *key, double value);
int hash_get(hash_t *d, const char *key, double *out);

#endif