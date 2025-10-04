#include <sqlite3.h>
#include <stdio.h>

int get_single_int(const char *filename_db, const char *query) {
    int rc, result = -1;
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
        result = sqlite3_column_int(stmt, 0);

    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return result;
}
