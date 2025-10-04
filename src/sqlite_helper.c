#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

long int get_single_int(const char *filename_db, const char *query) {
    int rc;
    long int result = -1;
    sqlite3 *db;
    sqlite3_stmt *stmt;

    rc = sqlite3_open(filename_db, &db);
    if (rc) {
        fprintf(stderr, "Erro ao abrir banco: %s\n", sqlite3_errmsg(db));
        return 1;
    }

    rc = sqlite3_prepare_v2(db, query, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Erro ao preparar o statement: %s\n", sqlite3_errmsg(db));
        return -1;
    }

    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW)
        result = sqlite3_column_int64(stmt, 0);

    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return result;
}

char **get_str_arr(const char *filename_db, const char *query, long int start, long int count) {
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

    rc = sqlite3_prepare_v2(db, query, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Erro ao preparar statement: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return NULL;
    }

    sqlite3_bind_int(stmt, 1, start);
    sqlite3_bind_int(stmt, 2, count);
    
    result = malloc(count * sizeof(char*));
    if (!result) {
        fprintf(stderr, "Erro ao alocar memória\n");
        sqlite3_finalize(stmt);
        sqlite3_close(db);
        return NULL;
    }

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW && i < count) {
        const unsigned char *text = sqlite3_column_text(stmt, 0);
        if (text)
            result[i++] = strdup((const char*)text);
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return result;
}