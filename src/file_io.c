#include "../include/file_io.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* -------------------- Stopwords -------------------- */

hash_t *global_stopwords = NULL;

void load_stopwords(const char *filename) {
  FILE *f = fopen(filename, "r");
  if (!f) {
    fprintf(stderr, "Erro ao abrir arquivo de stopwords: %s\n", filename);
    return;
  }

  global_stopwords = hash_new();

  char line[256];
  while (fgets(line, sizeof(line), f)) {
    line[strcspn(line, "\n")] = '\0';

    if (strlen(line) == 0)
      continue;

    hash_add(global_stopwords, line, 0.0);
  }

  fclose(f);
}

void free_stopwords(void) {
  if (global_stopwords) {
    hash_free(global_stopwords);
    global_stopwords = NULL;
  }
}

/* -------------------- Funções de Serialização -------------------- */

int save_hash(const hash_t *gh, const char *filename) {
  if (!gh || !filename) {
    fprintf(stderr, "Erro: hash_t ou filename é nulo\n");
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
    for (HashEntry *e = gh->buckets[i]; e; e = e->next) {
      fwrite(&e->wlen, sizeof(size_t), 1, fp);
      fwrite(e->word, sizeof(char), e->wlen, fp);
      fwrite(&e->value, sizeof(double), 1, fp);
      entries_written++;
    }
  }

  fclose(fp);
  printf("global_idf salvo em %s (%zu entradas)\n", filename, entries_written);
  return 0;
}

int save_hash_array(hash_t **hashes, long int num_hashes, const char *filename) {
  if (!hashes || !filename) {
    fprintf(stderr, "Erro: hashes ou filename é nulo\n");
    return -1;
  }

  FILE *fp = fopen(filename, "wb");
  if (!fp) {
    fprintf(stderr, "Erro ao abrir arquivo %s para escrita\n", filename);
    return -1;
  }

  // Salvar número de hashes
  fwrite(&num_hashes, sizeof(long int), 1, fp);

  // Salvar cada hash
  for (long int i = 0; i < num_hashes; i++) {
    hash_t *h = hashes[i];
    if (!h) {
      // Hash nulo - salvar apenas capacidade 0
      size_t zero = 0;
      fwrite(&zero, sizeof(size_t), 1, fp);
      fwrite(&zero, sizeof(size_t), 1, fp);
      continue;
    }

    // Salvar capacidade e tamanho
    fwrite(&h->cap, sizeof(size_t), 1, fp);
    fwrite(&h->size, sizeof(size_t), 1, fp);

    // Salvar cada entrada
    for (size_t j = 0; j < h->cap; j++) {
      for (HashEntry *e = h->buckets[j]; e; e = e->next) {
        fwrite(&e->wlen, sizeof(size_t), 1, fp);
        fwrite(e->word, sizeof(char), e->wlen, fp);
        fwrite(&e->value, sizeof(double), 1, fp);
      }
    }
  }

  fclose(fp);
  printf("global_tf salvo em %s (%ld hashes)\n", filename, num_hashes);
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
  printf("global_doc_vec salvo em %s (%ld docs x %zu palavras)\n", filename,
         num_docs, vocab_size);
  return 0;
}

int save_doc_norms(const double *norms, long int num_docs,
                   const char *filename) {
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

/* -------------------- Funções de Carregamento -------------------- */

char *get_filecontent(const char *filename_txt) {
  if (!filename_txt) {
    fprintf(stderr, "Erro: filename é nulo\n");
    return NULL;
  }

  // Capturando ponteiro para o arquivo
  FILE *fp = fopen(filename_txt, "r");
  if (!fp) {
    fprintf(stderr, "Erro ao abrir arquivo %s para leitura\n", filename_txt);
    return NULL;
  }

  // Tamanho do arquivo
  fseek(fp, 0, SEEK_END);
  long int size = ftell(fp);
  rewind(fp);

  if (size <= 0) {
      fprintf(stderr, "Erro: arquivo vazio ou inválido\n");
      fclose(fp);
      return NULL;
  } 

  // Ler conteúdo do arquivo
  char *content = (char *)malloc(size + 1);
  if (!content) {
    fprintf(stderr, "Erro ao alocar memória para ler arquivo\n");
    fclose(fp);
    return NULL;
  }

  size_t read_size = fread(content, 1, size, fp);
  if ((long int)read_size != size) {
    fprintf(stderr, "Erro ao ler arquivo %s\n", filename_txt);
    free(content);
    fclose(fp);
    return NULL;
  }

  content[size] = '\0';
  fclose(fp);

  return content;
}

hash_t *load_hash(const char *filename) {
  if (!filename) {
    fprintf(stderr, "Erro: filename é nulo\n");
    return NULL;
  }

  FILE *fp = fopen(filename, "rb");
  if (!fp) {
    fprintf(stderr, "Erro ao abrir arquivo %s para leitura\n", filename);
    return NULL;
  }

  hash_t *gh = hash_new();
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
    if (fread(&wlen, sizeof(size_t), 1, fp) != 1)
      break;

    char *word = (char *)malloc(wlen + 1);
    if (!word) {
      hash_free(gh);
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

    hash_add(gh, word, 0.0);

    // Encontrar a entrada recém-adicionada e setar o IDF
    // Precisamos fazer hash da palavra para encontrá-la
    size_t hash = 5381;
    for (size_t i = 0; i < wlen; i++) {
      hash = ((hash << 5) + hash) + (unsigned char)word[i];
    }
    size_t bucket = hash % gh->cap;

    for (HashEntry *e = gh->buckets[bucket]; e; e = e->next) {
      if (e->wlen == wlen && memcmp(e->word, word, wlen) == 0) {
        e->value = idf;
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

hash_t **load_hash_array(const char *filename, long int *num_hashes_out) {
  if (!filename) {
    fprintf(stderr, "Erro: filename é nulo\n");
    return NULL;
  }

  FILE *fp = fopen(filename, "rb");
  if (!fp) {
    fprintf(stderr, "Erro ao abrir arquivo %s para leitura\n", filename);
    return NULL;
  }

  // Ler número de hashes
  long int num_hashes;
  if (fread(&num_hashes, sizeof(long int), 1, fp) != 1) {
    fclose(fp);
    return NULL;
  }

  if (num_hashes_out)
    *num_hashes_out = num_hashes;

  // Alocar array de ponteiros
  hash_t **hashes = (hash_t **)calloc(num_hashes, sizeof(hash_t *));
  if (!hashes) {
    fclose(fp);
    return NULL;
  }

  // Carregar cada hash
  for (long int i = 0; i < num_hashes; i++) {
    size_t cap, size;
    if (fread(&cap, sizeof(size_t), 1, fp) != 1) {
      // Erro de leitura - liberar tudo
      for (long int j = 0; j < i; j++) {
        if (hashes[j])
          hash_free(hashes[j]);
      }
      free(hashes);
      fclose(fp);
      return NULL;
    }

    fread(&size, sizeof(size_t), 1, fp);

    // Se capacidade é 0, hash é nulo
    if (cap == 0) {
      hashes[i] = NULL;
      continue;
    }

    // Criar novo hash
    hashes[i] = hash_new();
    if (!hashes[i]) {
      for (long int j = 0; j < i; j++) {
        if (hashes[j])
          hash_free(hashes[j]);
      }
      free(hashes);
      fclose(fp);
      return NULL;
    }

    // Ler cada entrada
    for (size_t j = 0; j < size; j++) {
      size_t wlen;
      if (fread(&wlen, sizeof(size_t), 1, fp) != 1)
        break;

      char *word = (char *)malloc(wlen + 1);
      if (!word)
        break;

      if (fread(word, sizeof(char), wlen, fp) != wlen) {
        free(word);
        break;
      }
      word[wlen] = '\0';

      double value;
      if (fread(&value, sizeof(double), 1, fp) != 1) {
        free(word);
        break;
      }

      hash_add(hashes[i], word, 0.0);

      // Encontrar a entrada recém-adicionada e setar o valor
      size_t hash = hash_str(word, wlen);
      size_t bucket = hash % hashes[i]->cap;
      for (HashEntry *e = hashes[i]->buckets[bucket]; e; e = e->next) {
        if (e->wlen == wlen && memcmp(e->word, word, wlen) == 0) {
          e->value = value;
          break;
        }
      }

      free(word);
    }
  }

  fclose(fp);
  printf("global_tf carregado de %s (%ld hashes)\n", filename, num_hashes);
  return hashes;
}

double **load_doc_vecs(const char *filename, long int *num_docs_out,
                       size_t *vocab_size_out) {
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
      for (long int j = 0; j < i; j++)
        free(doc_vecs[j]);
      free(doc_vecs);
      fclose(fp);
      return NULL;
    }
    fread(doc_vecs[i], sizeof(double), vocab_size, fp);
  }

  fclose(fp);
  printf("global_doc_vec carregado de %s (%ld docs x %zu palavras)\n", filename,
         num_docs, vocab_size);

  if (num_docs_out)
    *num_docs_out = num_docs;
  if (vocab_size_out)
    *vocab_size_out = vocab_size;

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

  if (num_docs_out)
    *num_docs_out = num_docs;

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
      for (size_t j = 0; j < i; j++)
        free((void *)vocab[j]);
      free(vocab);
      fclose(fp);
      return NULL;
    }
    i++;
  }

  fclose(fp);
  printf("global_vocab carregado de %s (%zu palavras)\n", filename, count);

  if (vocab_size_out)
    *vocab_size_out = count;

  return vocab;
}
