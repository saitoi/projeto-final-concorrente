// #include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <semaphore.h>

/* Argumentos de Linha de Comando */
/* ./a.out <numero de threads> <texto> */

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
      sem_wait(&cond);
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
    const char *filename = "busca.bin";
    int nthreads;

    if (argc < 2) {
        fprintf(stderr, "Uso: %s <numero de threads> <texto>", argv[0]);
        return 1;
    }

    nthreads = (int) atoi(argv[1]);
    query = (const char*) argv[2];

    // Caso o arquivo não exista: Pré-processamento
    if (access(filename, F_OK) != -1) {
        int rc,    // Variável auxiliar
            count; // Quantidade de registros do banco
        const char *query_count = "select count(*) from sample_articles;";

        // Abre conexão com o banco
        int rc = sqlite3_open("wiki.db", &db);
        if (rc) {
            fprintf(stderr, "Erro ao abrir banco: %s\n", sqlite3_errmsg(db));
            return 1;
        }

        // Quantos registros na tabela 'sample_articles'
        count = get_single_int(db, query_count);

        printf("Qtd. artigos: %d\n", count);

    } else {
        printf("Arquivo binário encontrado: %s\n", filename);
    }

    sem_init(&mutex, 0, 1);
    sem_init(&cond_barreira, 0, 0);


    sem_destroy(&mutex);
    sem_destroy(&cond_barreira);

    return 0;
}
