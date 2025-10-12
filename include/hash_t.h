#ifndef HASH_T_H
#define HASH_T_H

#include <stdint.h>
#include <stddef.h>

/* ---------- Generic Hash Set (string → exists) ---------- */
typedef struct generic_hash generic_hash;

generic_hash* generic_hash_new(void);
void generic_hash_free(generic_hash* set);
void generic_hash_add(generic_hash* set, const char* word);
int generic_hash_contains(const generic_hash* set, const char* word);

// IMap (int -> double)
typedef struct IEntry {
    int key;
    double val;
    struct IEntry* next;
} IEntry;

// IMap (int -> double)
typedef struct {
    IEntry** b;
    size_t cap, size;
} IMap;

IMap imap_new(size_t cap);
void imap_free(IMap* m);
int imap_get(const IMap* m, int k, double* out);
void imap_set(IMap* m, int k, double v);
void imap_add(IMap* m, int k, double dv);

/* ---------- TF Hash (string → IMap) for TF-IDF ---------- */
typedef struct OEntry {
    char* key;
    size_t klen;
    IMap map;
    struct OEntry* next;
} OEntry;

typedef struct {
    OEntry** b;
    size_t cap, size;
} tf_hash;

tf_hash* tf_hash_new(void);
void tf_hash_free(tf_hash* h);
void tf_hash_set(tf_hash* h, const char* word, int k, double v);
void tf_hash_add(tf_hash* h, const char* word, int k, double dv);
int tf_hash_get(const tf_hash* h, const char* word, int k, double* out);
void tf_hash_merge(tf_hash* dst, const tf_hash* src);

#endif
