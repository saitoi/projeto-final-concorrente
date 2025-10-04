#ifndef SQLITE_HELPER_H
#define SQLITE_HELPER_H

long int get_single_int(const char *filename_db, const char *query);
char **get_str_arr(const char *filename_db, const char *query, long int start,
                   long int count);

#endif
