# Documentação do Projeto - Sistema de Busca TF-IDF

## Visão Geral

Este documento descreve a estrutura de documentação implementada para o projeto de busca de documentos usando TF-IDF e similaridade de cosseno.

## Ferramentas Utilizadas

- **Doxygen**: Gerador automático de documentação a partir de comentários no código
- Versão configurada: 1.9.8

## Arquivos de Configuração

### Doxyfile

Arquivo principal de configuração do Doxygen localizado na raiz do projeto.

**Principais configurações:**

- `PROJECT_NAME`: "Sistema de Busca TF-IDF"
- `PROJECT_BRIEF`: "Sistema de recuperação de informações usando TF-IDF e similaridade de cosseno"
- `OUTPUT_DIRECTORY`: `docs/`
- `INPUT`: `src include` (diretórios processados)
- `RECURSIVE`: `YES` (processa subdiretórios)
- `EXTRACT_ALL`: `YES` (extrai documentação de todos os elementos)

## Estrutura da Documentação

### src/main.c

Arquivo completamente documentado com:

#### 1. Cabeçalho do arquivo
- Descrição geral do sistema
- Lista de autores
- Funcionalidades principais

#### 2. Variáveis Globais (organizadas em grupos)

**@defgroup sync - Variáveis de Sincronização:**
- `pthread_mutex_t mutex`: Proteção de seções críticas
- `pthread_barrier_t barrier`: Sincronização entre threads
- `pthread_once_t once`: Execução única de compute_once()

**@defgroup global_data - Estruturas Globais:**
- `hash_t **global_tf`: Array de hashes TF por documento
- `hash_t *global_idf`: Hash IDF global
- `double *global_doc_norms`: Normas dos vetores
- `size_t global_vocab_size`: Tamanho do vocabulário
- `long int global_entries`: Número de documentos

**@defgroup config - Configuração:**
- `int VERBOSE`: Flag de verbosidade

#### 3. Estruturas de Dados

**thread_args:**
- Documentação de cada campo
- Propósito: passar argumentos para threads de pré-processamento

**Config:**
- Documentação completa de todos os parâmetros CLI
- Agrupa configurações do programa

#### 4. Funções Principais

**main():**
- Descrição completa do fluxo de execução
- Parâmetros documentados
- Retorno documentado

**compute_once():**
- Explicação do padrão pthread_once
- Tarefas executadas
- Notas sobre variáveis globais acessadas

**preprocess():**
- Pipeline de pré-processamento detalhado (11 etapas)
- Documentação de sincronização
- Notas sobre mutex e barreiras

**parse_cli():**
- Lista completa de parâmetros suportados
- Formato esperado
- Códigos de retorno

## Como Usar a Documentação

### Gerar

```bash
doxygen Doxyfile
```

### Visualizar

```bash
firefox docs/html/index.html
# ou
xdg-open docs/html/index.html
```

### Navegar

A documentação gerada inclui:

1. **Página inicial** (`index.html`): Visão geral do projeto
2. **Estruturas de Dados**: Detalhes de structs
3. **Arquivos**: Documentação por arquivo fonte
4. **Funções**: Todas as funções documentadas
5. **Grupos**: Variáveis organizadas por categoria
6. **Grafos**: Diagramas de dependências (se dot/graphviz estiver instalado)

## Estrutura de Comentários Doxygen

### Arquivo
```c
/**
 * @file nome.c
 * @brief Descrição breve
 *
 * Descrição detalhada...
 *
 * @authors Nome1, Nome2
 */
```

### Função
```c
/**
 * @brief Descrição breve
 *
 * Descrição detalhada...
 *
 * @param arg1 Descrição do parâmetro
 * @param arg2 Descrição do parâmetro
 * @return Descrição do retorno
 *
 * @note Observações importantes
 */
```

### Variáveis e Structs
```c
int var; /**< Descrição inline */

typedef struct {
  int field; /**< Descrição do campo */
} MyStruct;
```

### Grupos
```c
/** @defgroup nome_grupo Título do Grupo
 *  Descrição do grupo
 *  @{
 */
int var1; /**< Descrição */
int var2; /**< Descrição */
/** @} */
```

## Próximos Passos

Para expandir a documentação:

1. Adicionar comentários Doxygen em:
   - `src/hash_t.c` e `include/hash_t.h`
   - `src/preprocess.c` e `include/preprocess.h`
   - `src/sqlite_helper.c` e `include/sqlite_helper.h`
   - Demais arquivos do projeto

2. Adicionar exemplos de uso (`@example`)

3. Adicionar seções de código (`@code ... @endcode`)

4. Criar página principal customizada (Markdown)

5. Documentar algoritmos complexos com mais detalhes

## Referências

- [Documentação oficial do Doxygen](https://www.doxygen.nl/manual/)
- [Comandos especiais do Doxygen](https://www.doxygen.nl/manual/commands.html)
