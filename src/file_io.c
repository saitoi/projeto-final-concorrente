/**
 * @file file_io.c
 * @brief Funções de I/O para serialização e carregamento de estruturas
 *
 * Este arquivo implementa funcionalidades de entrada/saída para:
 * - Gerenciamento de stopwords (carregamento e liberação)
 * - Serialização de estruturas (hash, arrays, vetores, normas)
 * - Desserialização (carregamento de arquivos binários)
 * - Leitura de arquivos de texto (queries, vocabulário)
 *
 * As funções save_* e load_* são usadas para persistir modelos
 * pré-processados em disco, evitando reprocessamento.
 */

#include "../include/file_io.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* -------------------- Stopwords -------------------- */

hash_t *global_stopwords = NULL; /**< Hash global de stopwords */

/**
 * @brief Carrega stopwords de arquivo para hash global
 *
 * Lê arquivo linha por linha e popula hash global de stopwords.
 * Usado para filtrar palavras irrelevantes durante pré-processamento.
 *
 * @param filename Caminho para arquivo de stopwords
 * @note Define global_stopwords
 */
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

/**
 * @brief Libera memória de stopwords globais
 */
void free_stopwords(void) {
  if (global_stopwords) {
    hash_free(global_stopwords);
    global_stopwords = NULL;
  }
}

/* -------------------- Funções de Serialização -------------------- */

/**
 * @brief Salva tabela hash em arquivo binário
 *
 * Formato: capacidade, tamanho, seguido de (wlen, word, value) para cada entrada.
 *
 * @param gh Tabela hash (tipicamente global_idf)
 * @param filename Caminho do arquivo de saída
 * @return 0 em sucesso, -1 em erro
 */
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

/**
 * @brief Salva array de hashes em arquivo binário
 *
 * @param hashes Array de hash_t* (tipicamente global_tf)
 * @param num_hashes Número de hashes no array
 * @param filename Caminho do arquivo de saída
 * @return 0 em sucesso, -1 em erro
 */
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

/* -------------------- Funções de Carregamento -------------------- */

/**
 * @brief Lê conteúdo completo de arquivo de texto
 *
 * Usado para carregar queries de arquivo.
 *
 * @param filename_txt Caminho para arquivo de texto
 * @return String com conteúdo completo (caller deve liberar), ou NULL em erro
 */
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

/**
 * @brief Carrega tabela hash de arquivo binário
 *
 * Reconstrói hash (global_idf) de arquivo salvo com save_hash().
 *
 * @param filename Caminho para arquivo binário
 * @return Ponteiro para hash_t carregado, ou NULL em erro
 */
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

/**
 * @brief Carrega array de hashes de arquivo binário
 *
 * @param filename Caminho para arquivo binário
 * @param num_hashes_out Ponteiro para receber número de hashes (pode ser NULL)
 * @return Array de hash_t*, ou NULL em erro
 */
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

/**
 * @brief Carrega normas de documentos de arquivo binário
 *
 * @param filename Caminho para arquivo binário
 * @param num_docs_out Ponteiro para receber número de documentos
 * @return Array de double com normas, ou NULL em erro
 */
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
