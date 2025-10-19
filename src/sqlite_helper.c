#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

long int get_single_int(const char *filename_db, const char *query,
                        const char *tablename) {
  int rc;
  long int result = -1;
  sqlite3 *db;
  sqlite3_stmt *stmt;

  rc = sqlite3_open(filename_db, &db);
  if (rc) {
    fprintf(stderr, "Erro ao abrir banco: %s\n", sqlite3_errmsg(db));
    return 1;
  }

  char *sql = sqlite3_mprintf(query, tablename);
  if (!sql) {
    fprintf(stderr, "Erro ao formatar statement: %s\n", sqlite3_errmsg(db));
    return -1;
  }

  printf("Executando query: %s\n", sql);

  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    fprintf(stderr, "Erro ao preparar o statement: %s\n", sqlite3_errmsg(db));
    return -1;
  }

  rc = sqlite3_step(stmt);
  if (rc == SQLITE_ROW)
    result = sqlite3_column_int64(stmt, 0);

  sqlite3_finalize(stmt);
  sqlite3_free(sql);
  sqlite3_close(db);
  return result;
}

char **get_str_arr(const char *filename_db, const char *query, long int start,
                   long int end, const char *tablename) {
  int rc;
  sqlite3 *db;
  sqlite3_stmt *stmt;
  char **result = NULL;
  long int i = 0;

  rc = sqlite3_open(filename_db, &db);
  if (rc) {
    fprintf(stderr, "Erro ao abrir banco: %s\n", sqlite3_errmsg(db));
    return NULL;
  }

  char *sql = sqlite3_mprintf(query, tablename);
  if (!sql) {
    fprintf(stderr, "Erro ao formatar statement: %s\n", sqlite3_errmsg(db));
    return NULL;
  }

  printf("Executando query: %s\n", sql);

  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    fprintf(stderr, "Erro ao preparar statement: %s\n", sqlite3_errmsg(db));
    sqlite3_close(db);
    return NULL;
  }

  sqlite3_bind_int64(stmt, 1, start);
  sqlite3_bind_int64(stmt, 2, end);

  long int array_size = end - start + 1;
  result = calloc(array_size, sizeof(char *));
  if (!result) {
    fprintf(stderr, "Erro ao alocar memória\n");
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return NULL;
  }

  while (sqlite3_step(stmt) == SQLITE_ROW && i < array_size) {
    const unsigned char *text = sqlite3_column_text(stmt, 0);
    if (text)
      result[i++] = strdup((const char *)text);
  }

  sqlite3_finalize(stmt);
  sqlite3_free(sql);
  sqlite3_close(db);
  return result;
}
char **get_documents_by_ids(const char *filename_db, const char *tablename,
                            const long int *doc_ids, long int k) {
  if (!filename_db || !tablename || !doc_ids || k <= 0) {
    return NULL;
  }

  int rc;
  sqlite3 *db;
  sqlite3_stmt *stmt;
  char **result = NULL;

  rc = sqlite3_open(filename_db, &db);
  if (rc) {
    fprintf(stderr, "Erro ao abrir banco: %s\n", sqlite3_errmsg(db));
    return NULL;
  }

  // Alocar array de resultados
  result = (char **)calloc(k, sizeof(char *));
  if (!result) {
    fprintf(stderr, "Erro ao alocar memória para resultados\n");
    sqlite3_close(db);
    return NULL;
  }

  // Para cada ID, buscar o documento
  for (long int i = 0; i < k; i++) {
    // Query: SELECT article_text FROM tablename WHERE article_id = ?
    char *sql = sqlite3_mprintf("SELECT article_text FROM \"%w\" WHERE article_id = ?", tablename);
    if (!sql) {
      fprintf(stderr, "Erro ao formatar statement\n");
      // Liberar resultados já alocados
      for (long int j = 0; j < i; j++) {
        free(result[j]);
      }
      free(result);
      sqlite3_close(db);
      return NULL;
    }

    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
      fprintf(stderr, "Erro ao preparar statement: %s\n", sqlite3_errmsg(db));
      sqlite3_free(sql);
      // Liberar resultados já alocados
      for (long int j = 0; j < i; j++) {
        free(result[j]);
      }
      free(result);
      sqlite3_close(db);
      return NULL;
    }

    // Bind do article_id (doc_ids[i] + 1 porque IDs começam em 1)
    sqlite3_bind_int64(stmt, 1, doc_ids[i]);

    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
      const unsigned char *text = sqlite3_column_text(stmt, 0);
      if (text) {
        result[i] = strdup((const char *)text);
      } else {
        result[i] = NULL;
      }
    } else {
      result[i] = NULL;
    }

    sqlite3_finalize(stmt);
    sqlite3_free(sql);
  }

  sqlite3_close(db);
  return result;
}
