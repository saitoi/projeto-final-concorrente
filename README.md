### <ins>**Observação**: Apenas comitem código testado e funcional.</ins> 

## Desenvolvimento

1. Clone o repositório: `git clone https://github.com/saitoi/projeto-final-concorrente.git`
2. Instale git lfs: https://git-lfs.com/
3. Recupere o banco:

    ```bash
    git lfs install
    git lfs fetch
    git lfs pull
    # Verifique se o arquivo está correto
    file wiki-small.db
    # Retorno: wiki-small.db: SQLite 3.x database, last written using SQLite version 3040001, file counter 1, database pages 93800, cookie 0x1, schema 4, UTF-8, version-valid-for 1
    ```

4. Verifique se sqlite3 os dev package está instalada.
5. Compile e execute:

    ```bash
    cc main.c -o app -lsqlite3
    ./app # Parâmetros padrões já definidos
    ./app --nthreads 4 --filename_db alguma_coisa.db --filename_tfidf marcos.bin
    ```

6. **(Preferencialmente) Use o Makefile:**

    ```bash
    make clean && make
    ./app # Parâmetros padrões já definidos
    ./app --nthreads 4 --filename_db alguma_coisa.db --filename_tfidf marcos.bin
    ```

## Próximos Passos

Começar a processar 100 primeiros artigos somente para facilitar.

Leia `instrucoes-projeto.pdf`.

- [x] Estrutura de Hash (`hash_t.h`).
- [x] Separar funções auxiliares do SQLite (`sqlite_helper.h`).
- [x] Makefil- [x] Parâmetros nomeados CLI.
- [x] Criação e passagem de argumentos para as threads.
- [x] Pré-processamento no DuckDB:
    - [x] Lower -> Remover acentos -> Restringir para ASCII (sem caracteres de controle)
    - [x] Tratamento especial de apóstrofes, vírgulas e dash.
    - [x] Substituir os seguintes caracteres por espaço: '#', ':', '"', '()', '[]', '{}', '~', '=', '\_', '|', ';', '<>', '\*', '$', '\`', '\\\'
- [x] Pré-processamento em C:
    - [x] Tokenização.
    - [x] Remoção de Stopwords.
    - [x] Stemmer.
- [ ] Montar a estrutura do TF-IDF com `hash_t`. 
- Pré-computar:
    - [ ] IDF.
    - [ ] Vocabulário.
    - [ ] Vetores dos documentos.
    - [ ] Normas dos vetores.
- [ ] Pré-processamento da consulta:
    - [ ] Processamento de texto (parecido com DuckDB).
    - [ ] Delimitadores.
- [ ] Adicionar macros LOG e variável VERBOSE em `sqlite_helper.c`.

### Questionamentos

- Considero palavras com uma única letra?