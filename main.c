#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <semaphore.h>

int VERBOSE = 0;

#define LOG(output, fmt, ...) \
	do { if (VERBOSE) fprintf(output, "[VERBOSE] " fmt "\n", ##__VA_ARGS__); } while (0)

sem_t mutex;
sem_t cond_barreira;

void *preprocess(void *args) {
    
}

// Extraído do cods-lab7/barreira.c
void barreira(int nthreads) {
    static int bloqueadas = 0;
    sem_wait(&mutex);
    bloqueadas++;
    if (bloqueadas < nthreads) {
      sem_post(&mutex);
      sem_wait(&cond_barreira);
      bloqueadas--;
      if (bloqueadas==0) sem_post(&mutex);
      else sem_post(&cond_barreira); 
    } else {
      bloqueadas--;
      sem_post(&cond_barreira);
    }
}

int get_single_int(sqlite3 *db, const char *query) {
    sqlite3_stmt *stmt;
    int rc, result = -1;

    rc = sqlite3_prepare_v2(db, query, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Erro ao preparar o statement: %s\n", sqlite3_errmsg(db));
        return -1;
    }

    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW)
        result = sqlite3_column_int(stmt, 0);

    sqlite3_finalize(stmt);
    return result;
}

int main(int argc, char *argv[]) {
    sqlite3 *db;
    const char *filename_tfidf = "tfidf.bin", *filename_db = "wiki-small.db";
    const char *query_user = "exemplo";
    int nthreads = 4;

    // CLI com parâmetros nomeados
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--nthreads") == 0 && i + 1 < argc)
            nthreads = atoi(argv[++i]);
        else if (strcmp(argv[i], "--filename_db") == 0 && i + 1 < argc)
            filename_db = argv[++i];
        else if (strcmp(argv[i], "--filename_tfidf") == 0 && i + 1 < argc)
            filename_tfidf = argv[++i];
        else if (strcmp(argv[i], "--query_user") == 0 && i + 1 < argc)
            query_user = argv[++i];
        else if (strcmp(argv[i], "--verbose") == 0)
	        VERBOSE = 1;
        else {
            fprintf(stderr, "Uso: %s <parametros nomeados>\n"
                            "--nthreads: Número de threads (default: 4)\n"
                            "--filename_db: Nome do arquivo Sqlite (default: 'wiki-small.db')\n"
                            "--filename_tfidf: Nome do arquivo com estrutura do TF-IDF (default: 'tfidf.bin')\n"
                            "--query_user: Consulta do usuário (default: 'exemplo')\n", argv[0]);
            return 1;
        }
    }

    LOG(stdout, "Parâmetros nomeados:\n"
		"argc: %d\n"
		"argv: %s\n"
       		"nthreads: %d\n"
       		"filename_tfidf: %s\n"
       		"filename_db: %s\n"
       		"query_user: %s", argc, *argv, nthreads, filename_tfidf, filename_db, query_user);

    // Caso o arquivo não exista: Pré-processamento
    if (access(filename_tfidf, F_OK) == -1) {
        int rc,    // Variável auxiliar
            count; // Quantidade de registros do banco
        const char *query_count = "select count(*) from sample_articles;";

        // Abre conexão com o banco
        rc = sqlite3_open(filename_db, &db);
        if (rc) {
            fprintf(stderr, "Erro ao abrir banco: %s\n", sqlite3_errmsg(db));
            return 1;
        }

        // Quantos registros na tabela 'sample_articles'
        count = get_single_int(db, query_count);

        printf("Qtd. artigos: %d\n", count);

    } else {
        printf("Arquivo binário encontrado: %s\n", filename_tfidf);
    }

    sem_init(&mutex, 0, 1);
    sem_init(&cond_barreira, 0, 0);


    sem_destroy(&mutex);
    sem_destroy(&cond_barreira);

    return 0;
}
