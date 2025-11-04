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
5. Verifique se a biblioteca libstemmer do SnowballStemmer está instalada.
6. Compile e execute:

    ```bash
    cc main.c -o app -lsqlite3
    ./app # Parâmetros padrões já definidos
    ./app --nthreads 4 --db alguma_coisa.db --filename_tfidf marcos.bin
    ```

7. **(Preferencialmente) Use o Makefile:**

    ```bash
    make clean && make
    ./app # Parâmetros padrões já definidos
    ./app --nthreads 4 --db alguma_coisa.db --filename_tfidf marcos.bin
    ```

## Próximos Passos

Começar a processar 100 primeiros artigos somente para facilitar.

Leia `instrucoes-projeto.pdf`.
- [x] Estrutura de Hash (`hash_t.h`).
- [x] Separar funções auxiliares do SQLite (`sqlite_helper.h`).
- [x] Makefile
- [x] Parâmetros nomeados CLI.
- [x] Criação e passagem de argumentos para as threads.
- [x] Pré-processamento no DuckDB:
    - [x] Lower -> Remover acentos -> Restringir para ASCII (sem caracteres de controle)
    - [x] Tratamento especial de apóstrofes, vírgulas e dash.
    - [x] Substituir os seguintes caracteres por espaço: '#', ':', '"', '()', '[]', '{}', '~', '=', '\_', '|', ';', '<>', '\*', '$', '\`', '\\\'
- [x] Pré-processamento em C:
    - [x] Tokenização.
    - [x] Remoção de Stopwords.
    - [x] Stemmer.
    - [x] Popular hash local.
- [x] Mergir hashes e montar estrutura do TF-IDF com `hash_t`. 
- [x] Pré-computar:
    - [x] Vocabulário.
    - [x] IDF.
    - [x] Sincronizar set_idf e generic_hash_to_vec com barrier.
    - [x] Vetores dos documentos.
    - [x] Normas dos vetores.
    - [x] Salvamento em ordem em um binário.
- [x] NÃO ALOCAR VETOR DO VOCABULÁRIO JAMAIS
- [x] Global_doc_vec ABSURDAMENTE GRANDE
- [x] Substituir "'s" no pré-processamento.
- [ ] Consulta:
    - [x] Pré-processamento da consulta do usuário.
    - [ ] Replicar scripts de regex do preprocess/**.py em C
    - [x] Cálculo da similaridade com os documentos.
- [ ] Pré-processamento da consulta:
    - [x] Processamento de texto (parecido com DuckDB).
    - [x] Protótipo funcional da consulta com o corpus retornado.
    - [ ] Delimitadores.
- [ ] Adicionar macros LOG e variável VERBOSE em `sqlite_helper.c`.

### Menos Relevantes

- [x] Inverter a TF (atual: word -> DocId, nova: DocId -> word).
  - [x] Substituir completamente tf_hash por generic_hash**.
  - [x] Adicionar campo double adicional no generic_hash para conter 
- [ ] Executar `make check` e corrigir warnings.
- [ ] Unificar pipeline do SQlite.
- [ ] Reorganizar funções e estrutura dos arquivos.
- [ ] Remover prints de debug excessivos.
- [ ] Possivelmente converter variável `VERBOSE` para uma flag de compilação

### Tarefas Futuras

- [ ] Abordagem de chunks para pré-processamento de coleções maiores.

### Questionamentos

- Considero palavras com uma única letra?

## Documentação

A documentação do código é gerada automaticamente usando **Doxygen**.

### Gerar documentação

Para gerar a documentação HTML:

```bash
doxygen Doxyfile
```

A documentação será gerada no diretório `docs/html/`. Para visualizar:

```bash
# Abra o arquivo principal
firefox docs/html/index.html
# ou
xdg-open docs/html/index.html
```

### Estrutura da documentação

- **src/main.c**: Documentação completa do fluxo principal, estruturas e funções
- Todas as funções públicas estão documentadas com parâmetros, retornos e notas
- Grupos de variáveis globais organizados por categoria (sincronização, dados, configuração)

### Atualizar documentação

Após modificar comentários Doxygen no código, regenere a documentação:

```bash
doxygen Doxyfile
```

## Interface Visual

O projeto inclui uma interface gráfica desenvolvida com **Clay UI** e **Raylib**, proporcionando uma experiência interativa para realizar buscas TF-IDF.

### Compilação e Execução

```bash
make ui
./app_ui
```

Alternativamente, utilize o script de demonstração que verifica pré-requisitos:

```bash
make demo
```

### Funcionalidades

A interface oferece os seguintes recursos:

- **Busca Interativa**: Sistema de busca em tempo real com processamento TF-IDF
- **Controle de Threads**: Ajuste dinâmico do número de threads (1-16) para processamento paralelo
- **Seleção de Banco de Dados**: Alternância entre diferentes bases de dados:
  - `wiki-small` (97.549 documentos)
  - `test` (conjuntos de teste)
  - `food-reviews` (568.454 documentos)
- **Visualização de Resultados**: Exibição ranqueada com scores de similaridade
- **Controles Intuitivos**: Atalhos de teclado e botões para navegação
- **Design Moderno**: Interface com gradientes, efeitos de hover e tipografia otimizada

### Geração de Modelos TF-IDF

Para utilizar a interface com todos os bancos de dados disponíveis, é necessário gerar previamente os modelos TF-IDF:

```bash
./generate_models.sh
```

O script realizará automaticamente:

1. Geração de modelos para `wiki-small` (97k documentos)
2. Geração de modelos para `food-reviews` (568k documentos)
3. Geração de modelos para tabelas de teste
4. Verificação e reutilização de modelos existentes

**Observação**: O processo de geração pode demandar alguns minutos, especialmente para bases de dados maiores. Para mais informações, consulte [MODELS.md](MODELS.md).

### Controles da Interface

- **ESC**: Sair da aplicação
- **Roda do Mouse**: Scroll vertical
- **↑/↓**: Ajustar número de threads
- **Botões +/-**: Controle de threads e documentos
- **Enter**: Confirmar busca