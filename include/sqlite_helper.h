#ifndef SQLITE_HELPER_H
#define SQLITE_HELPER_H

long int get_single_int(const char *db, const char *query,
                        const char *table);
char **get_str_arr(const char *db, const char *query, long int start,
                   long int count, const char *table);
char **get_documents_by_ids(const char *db, const char *table,
                            const long int *doc_ids, long int k);

#endif
