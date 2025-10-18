#include "../include/file_io.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ==================== Funções de Serialização ==================== */

int save_tf_hash(const tf_hash *tf, const char *filename) {
  if (!tf || !filename) {
    fprintf(stderr, "Erro: tf_hash ou filename é nulo\n");
    return -1;
  }

  FILE *fp = fopen(filename, "wb");
  if (!fp) {
    fprintf(stderr, "Erro ao abrir arquivo %s para escrita\n", filename);
    return -1;
  }

  // Salvar capacidade e tamanho
  fwrite(&tf->cap, sizeof(size_t), 1, fp);
  fwrite(&tf->size, sizeof(size_t), 1, fp);

  // Contar total de entradas para escrever
  size_t total_entries = 0;
  for (size_t i = 0; i < tf->cap; i++) {
    for (OEntry *e = tf->b[i]; e; e = e->next) {
      total_entries++;
    }
  }
  fwrite(&total_entries, sizeof(size_t), 1, fp);

  // Salvar cada entrada
  for (size_t i = 0; i < tf->cap; i++) {
    for (OEntry *e = tf->b[i]; e; e = e->next) {
      // Salvar palavra
      fwrite(&e->klen, sizeof(size_t), 1, fp);
      fwrite(e->key, sizeof(char), e->klen, fp);

      // Salvar IMap
      fwrite(&e->map.cap, sizeof(size_t), 1, fp);
      fwrite(&e->map.size, sizeof(size_t), 1, fp);

      // Contar entradas do IMap
      size_t imap_entries = 0;
      for (size_t j = 0; j < e->map.cap; j++) {
        for (IEntry *ie = e->map.b[j]; ie; ie = ie->next) {
          imap_entries++;
        }
      }
      fwrite(&imap_entries, sizeof(size_t), 1, fp);

      // Salvar pares chave-valor do IMap
      for (size_t j = 0; j < e->map.cap; j++) {
        for (IEntry *ie = e->map.b[j]; ie; ie = ie->next) {
          fwrite(&ie->key, sizeof(int), 1, fp);
          fwrite(&ie->val, sizeof(double), 1, fp);
        }
      }
    }
  }

  fclose(fp);
  printf("global_tf salvo em %s (%zu entradas)\n", filename, total_entries);
  return 0;
}

int save_generic_hash(const generic_hash *gh, const char *filename) {
  if (!gh || !filename) {
    fprintf(stderr, "Erro: generic_hash ou filename é nulo\n");
    return -1;
  }

  FILE *fp = fopen(filename, "wb");
  if (!fp) {
    fprintf(stderr, "Erro ao abrir arquivo %s para escrita\n", filename);
    return -1;
  }

  // Salvar capacidade e tamanho
  fwrite(&gh->cap, sizeof(size_t), 1, fp);
  fwrite(&gh->size, sizeof(size_t), 1, fp);

  // Salvar cada entrada
  size_t entries_written = 0;
  for (size_t i = 0; i < gh->cap; i++) {
    for (GEntry *e = gh->buckets[i]; e; e = e->next) {
      fwrite(&e->wlen, sizeof(size_t), 1, fp);
      fwrite(e->word, sizeof(char), e->wlen, fp);
      fwrite(&e->idf, sizeof(double), 1, fp);
      entries_written++;
    }
  }

  fclose(fp);
  printf("global_idf salvo em %s (%zu entradas)\n", filename, entries_written);
  return 0;
}

int save_doc_vecs(double **doc_vecs, long int num_docs, size_t vocab_size,
                  const char *filename) {
  if (!doc_vecs || !filename) {
    fprintf(stderr, "Erro: doc_vecs ou filename é nulo\n");
    return -1;
  }

  FILE *fp = fopen(filename, "wb");
  if (!fp) {
    fprintf(stderr, "Erro ao abrir arquivo %s para escrita\n", filename);
    return -1;
  }

  // Salvar dimensões
  fwrite(&num_docs, sizeof(long int), 1, fp);
  fwrite(&vocab_size, sizeof(size_t), 1, fp);

  // Salvar cada vetor de documento
  for (long int i = 0; i < num_docs; i++) {
    fwrite(doc_vecs[i], sizeof(double), vocab_size, fp);
  }

  fclose(fp);
  printf("global_doc_vec salvo em %s (%ld docs x %zu palavras)\n",
         filename, num_docs, vocab_size);
  return 0;
}

int save_doc_norms(const double *norms, long int num_docs, const char *filename) {
  if (!norms || !filename) {
    fprintf(stderr, "Erro: norms ou filename é nulo\n");
    return -1;
  }

  FILE *fp = fopen(filename, "wb");
  if (!fp) {
    fprintf(stderr, "Erro ao abrir arquivo %s para escrita\n", filename);
    return -1;
  }

  // Salvar número de documentos
  fwrite(&num_docs, sizeof(long int), 1, fp);

  // Salvar normas
  fwrite(norms, sizeof(double), num_docs, fp);

  fclose(fp);
  printf("global_doc_norms salvo em %s (%ld normas)\n", filename, num_docs);
  return 0;
}

int save_vocab(const char **vocab, size_t vocab_size, const char *filename) {
  if (!vocab || !filename) {
    fprintf(stderr, "Erro: vocab ou filename é nulo\n");
    return -1;
  }

  FILE *fp = fopen(filename, "w");
  if (!fp) {
    fprintf(stderr, "Erro ao abrir arquivo %s para escrita\n", filename);
    return -1;
  }

  // Salvar cada palavra do vocabulário
  for (size_t i = 0; i < vocab_size; i++) {
    fprintf(fp, "%s\n", vocab[i]);
  }

  fclose(fp);
  printf("global_vocab salvo em %s (%zu palavras)\n", filename, vocab_size);
  return 0;
}

/* ==================== Funções de Carregamento ==================== */

tf_hash *load_tf_hash(const char *filename) {
  if (!filename) {
    fprintf(stderr, "Erro: filename é nulo\n");
    return NULL;
  }

  FILE *fp = fopen(filename, "rb");
  if (!fp) {
    fprintf(stderr, "Erro ao abrir arquivo %s para leitura\n", filename);
    return NULL;
  }

  tf_hash *tf = tf_hash_new();
  if (!tf) {
    fclose(fp);
    return NULL;
  }

  // Ler capacidade, tamanho e total de entradas (não usaremos cap/size diretamente)
  size_t cap, size, total_entries;
  fread(&cap, sizeof(size_t), 1, fp);
  fread(&size, sizeof(size_t), 1, fp);
  fread(&total_entries, sizeof(size_t), 1, fp);

  // Ler cada entrada
  for (size_t i = 0; i < total_entries; i++) {
    // Ler palavra
    size_t klen;
    fread(&klen, sizeof(size_t), 1, fp);

    char *word = (char *)malloc(klen + 1);
    if (!word) {
      tf_hash_free(tf);
      fclose(fp);
      return NULL;
    }
    fread(word, sizeof(char), klen, fp);
    word[klen] = '\0';

    // Ler IMap (cap, size, entries)
    size_t map_cap, map_size, imap_entries;
    fread(&map_cap, sizeof(size_t), 1, fp);
    fread(&map_size, sizeof(size_t), 1, fp);
    fread(&imap_entries, sizeof(size_t), 1, fp);

    // Ler pares chave-valor do IMap
    for (size_t j = 0; j < imap_entries; j++) {
      int doc_id;
      double freq;
      fread(&doc_id, sizeof(int), 1, fp);
      fread(&freq, sizeof(double), 1, fp);

      tf_hash_set(tf, word, doc_id, freq);
    }

    free(word);
  }

  fclose(fp);
  printf("global_tf carregado de %s (%zu entradas)\n", filename, total_entries);
  return tf;
}

generic_hash *load_generic_hash(const char *filename) {
  if (!filename) {
    fprintf(stderr, "Erro: filename é nulo\n");
    return NULL;
  }

  FILE *fp = fopen(filename, "rb");
  if (!fp) {
    fprintf(stderr, "Erro ao abrir arquivo %s para leitura\n", filename);
    return NULL;
  }

  generic_hash *gh = generic_hash_new();
  if (!gh) {
    fclose(fp);
    return NULL;
  }

  // Ler capacidade e tamanho (para info)
  size_t cap, size;
  fread(&cap, sizeof(size_t), 1, fp);
  fread(&size, sizeof(size_t), 1, fp);

  // Ler cada entrada
  size_t entries_read = 0;
  while (!feof(fp)) {
    size_t wlen;
    if (fread(&wlen, sizeof(size_t), 1, fp) != 1) break;

    char *word = (char *)malloc(wlen + 1);
    if (!word) {
      generic_hash_free(gh);
      fclose(fp);
      return NULL;
    }

    if (fread(word, sizeof(char), wlen, fp) != wlen) {
      free(word);
      break;
    }
    word[wlen] = '\0';

    double idf;
    if (fread(&idf, sizeof(double), 1, fp) != 1) {
      free(word);
      break;
    }

    generic_hash_add(gh, word);

    // Encontrar a entrada recém-adicionada e setar o IDF
    // Precisamos fazer hash da palavra para encontrá-la
    size_t hash = 5381;
    for (size_t i = 0; i < wlen; i++) {
      hash = ((hash << 5) + hash) + (unsigned char)word[i];
    }
    size_t bucket = hash % gh->cap;

    for (GEntry *e = gh->buckets[bucket]; e; e = e->next) {
      if (e->wlen == wlen && memcmp(e->word, word, wlen) == 0) {
        e->idf = idf;
        break;
      }
    }

    free(word);
    entries_read++;
  }

  fclose(fp);
  printf("global_idf carregado de %s (%zu entradas)\n", filename, entries_read);
  return gh;
}

double **load_doc_vecs(const char *filename, long int *num_docs_out, size_t *vocab_size_out) {
  if (!filename) {
    fprintf(stderr, "Erro: filename é nulo\n");
    return NULL;
  }

  FILE *fp = fopen(filename, "rb");
  if (!fp) {
    fprintf(stderr, "Erro ao abrir arquivo %s para leitura\n", filename);
    return NULL;
  }

  // Ler dimensões
  long int num_docs;
  size_t vocab_size;
  fread(&num_docs, sizeof(long int), 1, fp);
  fread(&vocab_size, sizeof(size_t), 1, fp);

  // Alocar matriz
  double **doc_vecs = (double **)malloc(num_docs * sizeof(double *));
  if (!doc_vecs) {
    fclose(fp);
    return NULL;
  }

  // Ler cada vetor de documento
  for (long int i = 0; i < num_docs; i++) {
    doc_vecs[i] = (double *)malloc(vocab_size * sizeof(double));
    if (!doc_vecs[i]) {
      for (long int j = 0; j < i; j++) free(doc_vecs[j]);
      free(doc_vecs);
      fclose(fp);
      return NULL;
    }
    fread(doc_vecs[i], sizeof(double), vocab_size, fp);
  }

  fclose(fp);
  printf("global_doc_vec carregado de %s (%ld docs x %zu palavras)\n",
         filename, num_docs, vocab_size);

  if (num_docs_out) *num_docs_out = num_docs;
  if (vocab_size_out) *vocab_size_out = vocab_size;

  return doc_vecs;
}

double *load_doc_norms(const char *filename, long int *num_docs_out) {
  if (!filename) {
    fprintf(stderr, "Erro: filename é nulo\n");
    return NULL;
  }

  FILE *fp = fopen(filename, "rb");
  if (!fp) {
    fprintf(stderr, "Erro ao abrir arquivo %s para leitura\n", filename);
    return NULL;
  }

  // Ler número de documentos
  long int num_docs;
  fread(&num_docs, sizeof(long int), 1, fp);

  // Alocar e ler normas
  double *norms = (double *)malloc(num_docs * sizeof(double));
  if (!norms) {
    fclose(fp);
    return NULL;
  }

  fread(norms, sizeof(double), num_docs, fp);

  fclose(fp);
  printf("global_doc_norms carregado de %s (%ld normas)\n", filename, num_docs);

  if (num_docs_out) *num_docs_out = num_docs;

  return norms;
}

const char **load_vocab(const char *filename, size_t *vocab_size_out) {
  if (!filename) {
    fprintf(stderr, "Erro: filename é nulo\n");
    return NULL;
  }

  FILE *fp = fopen(filename, "r");
  if (!fp) {
    fprintf(stderr, "Erro ao abrir arquivo %s para leitura\n", filename);
    return NULL;
  }

  // Primeiro, contar quantas linhas temos
  size_t count = 0;
  char line[1024];
  while (fgets(line, sizeof(line), fp)) {
    count++;
  }
  rewind(fp);

  // Alocar array de ponteiros
  const char **vocab = (const char **)malloc(count * sizeof(char *));
  if (!vocab) {
    fclose(fp);
    return NULL;
  }

  // Ler cada palavra
  size_t i = 0;
  while (fgets(line, sizeof(line), fp) && i < count) {
    line[strcspn(line, "\n")] = '\0';
    vocab[i] = strdup(line);
    if (!vocab[i]) {
      for (size_t j = 0; j < i; j++) free((void *)vocab[j]);
      free(vocab);
      fclose(fp);
      return NULL;
    }
    i++;
  }

  fclose(fp);
  printf("global_vocab carregado de %s (%zu palavras)\n", filename, count);

  if (vocab_size_out) *vocab_size_out = count;

  return vocab;
}
