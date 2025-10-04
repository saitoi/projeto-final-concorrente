#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <semaphore.h>

#include "../include/sqlite_helper.h"

int VERBOSE = 0;

#define LOG(output, fmt, ...) \
	do { if (VERBOSE) fprintf(output, "[VERBOSE] " fmt "\n", ##__VA_ARGS__); } while (0)

// Ordem decrescente de tam
typedef struct {
    long int start;
    long int end;
    long int id;
    const char *filename_db;
} thread_args;

sem_t mutex;

void *preprocess(void *arg) {
    thread_args *t = (thread_args*) arg;

    LOG(stdout, "Thread %ld iniciou:\n"
                "\tstart: %ld\n"
                "\tchunk: %ld\n"
                "\tfilename_db: %s\n", t->id, t->start, t->end, t->filename_db);

    // Passo-a-passo
    // 1. Inicializar conexão com o banco
    // 2. Executar consulta para obter 

    char **article_texts = get_str_arr(t->filename_db, "select article_text from sample_articles where article_id between ? and ?", t->start, t->end);
    if (!article_texts) {
        fprintf(stderr, "Erro ao obter dados do banco\n");
        pthread_exit(NULL);
    }

    LOG(stdout, "Primeiro artigo da thread %ld: %s\n", t->id, article_texts[0]);

    pthread_exit(NULL);
}

int main(int argc, char *argv[]) {
    const char *filename_tfidf = "tfidf.bin", *filename_db = "wiki-small.db";
    const char *query_user = "exemplo";
    int nthreads = 4;
    long int entries = 0;

    // CLI com parâmetros nomeados
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--nthreads") == 0 && i + 1 < argc)
            nthreads = atoi(argv[++i]);
        else if (strcmp(argv[i], "--entries") == 0 && i + 1 < argc)
            entries = atol(argv[++i]);
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
                            "--entries: Quantidade de entradas para pré-processamento (default: Toda tabela 'sample_articles')\n"
                            "--filename_db: Nome do arquivo Sqlite (default: 'wiki-small.db')\n"
                            "--filename_tfidf: Nome do arquivo com estrutura do TF-IDF (default: 'tfidf.bin')\n"
                            "--query_user: Consulta do usuário (default: 'exemplo')\n", argv[0]);
            return 1;
        }
    }

    LOG(stdout, "Parâmetros nomeados:\n"
		"\targc: %d\n"
       	"\tnthreads: %d\n"
        "\tentries: %ld\n"
       	"\tfilename_tfidf: %s\n"
       	"\tfilename_db: %s\n"
       	"\tquery_user: %s", argc, nthreads, entries, filename_tfidf, filename_db, query_user);

    // Caso o arquivo não exista: Pré-processamento
    if (access(filename_tfidf, F_OK) == -1) {
        pthread_t *tids;
        const char *query_count = "select count(*) from sample_articles;";

        // Sobreescreve count=0
        // Quantos registros na tabela 'sample_articles'
        if (!entries)
            entries = get_single_int(filename_db, query_count);

        printf("Qtd. artigos: %ld\n", entries);

        tids = (pthread_t*) malloc(nthreads * sizeof(pthread_t));
        if (!tids) {
            LOG(stderr, "Erro ao alocar memória para identificador das threads\n");
            return 1;
        }

        long int base = entries / nthreads;
        long int rem = entries % nthreads;
        for (long int i = 0; i < nthreads; ++i) {
            thread_args *arg = (thread_args*) malloc(sizeof(thread_args));
            if (!arg) {
                fprintf(stderr, "Erro ao alocar memória para argumentos da thread\n");
                free(tids);
                return 1;
            }
            arg->id = i;
            arg->filename_db = filename_db;
            arg->start = i * base + (i < rem ? i : rem);
            arg->end = arg->start + base + (i < rem);
            if (pthread_create(&tids[i], NULL, preprocess, (void*) arg)) {
                fprintf(stderr, "Erro ao criar thread %ld\n", i);
                free(tids);
                return 1;
            }
        }

        for (long int i = 0; i < nthreads; ++i) {
            pthread_join(tids[i], NULL);
        }

    } else {
        // Carregar estrutura hash TF-IDF do arquivo
        printf("Arquivo binário encontrado: %s\n", filename_tfidf);
    }

    sem_init(&mutex, 0, 1);

    
    sem_destroy(&mutex);

    return 0;
}
