# 🔧 Geração de Modelos TF-IDF

Este documento explica como os modelos TF-IDF são gerados e gerenciados no sistema.

## 📋 Visão Geral

Os modelos TF-IDF são arquivos binários pré-computados que contêm:
- **IDF (Inverse Document Frequency)**: Vocabulário global com pesos IDF
- **TF (Term Frequency)**: Frequências de termos por documento
- **Normas**: Normas dos vetores TF-IDF de cada documento

Estes arquivos permitem buscas rápidas sem necessidade de reprocessar toda a coleção.

## 🗂️ Estrutura de Arquivos

Os modelos são salvos em `models/` com a seguinte convenção de nomes:

```
models/
├── idf_{tabela}_{num_docs}.bin        # Vocabulário + IDF
├── tf_{tabela}_{num_docs}.bin          # TF de cada documento
└── doc_norms_{tabela}_{num_docs}.bin   # Normas dos vetores
```

**Exemplo:**
```
models/idf_sample_articles_97549.bin      # IDF para wiki-small (97549 docs)
models/tf_sample_articles_97549.bin       # TF para wiki-small
models/doc_norms_sample_articles_97549.bin # Normas para wiki-small
```

## 🚀 Geração Automática

### Script de Geração

Use o script `generate_models.sh` para gerar todos os modelos necessários:

```bash
./generate_models.sh
```

Este script:
- ✅ Verifica se os modelos já existem (evita reprocessamento)
- ✅ Gera modelos para todos os bancos configurados
- ✅ Mostra progresso e estatísticas
- ✅ Utiliza 8 threads por padrão para processamento paralelo

### Geração Manual

Para gerar modelos manualmente para um banco específico:

```bash
./app --db data/food-reviews.db \
      --table sample_articles \
      --entries 568454 \
      --nthreads 8 \
      --query_user "test" \
      --k 1
```

**Parâmetros:**
- `--db`: Caminho do banco de dados SQLite
- `--table`: Nome da tabela (deve ter colunas `article_id` e `article_text`)
- `--entries`: Número total de documentos
- `--nthreads`: Número de threads para processamento paralelo
- `--query_user`: Query de teste (necessária para executar, mas pode ser qualquer coisa)

## 🗄️ Bancos de Dados Suportados

### 1. Wiki-Small (97,549 documentos)
```bash
./app --db data/wiki-small.db --table sample_articles --entries 97549 --nthreads 8 --query_user "test" --k 1
```

### 2. Food Reviews (568,454 documentos)
```bash
./app --db data/food-reviews.db --table sample_articles --entries 568454 --nthreads 8 --query_user "test" --k 1
```

**Nota:** O banco `food-reviews.db` usa uma VIEW `sample_articles` que mapeia:
- `Id` → `article_id`
- `Text` → `article_text`

### 3. Test Tables
```bash
# test_tbl_0 (10 documentos)
./app --db data/test.db --table test_tbl_0 --entries 10 --nthreads 8 --query_user "test" --k 1

# test_tbl_1 (5 documentos)
./app --db data/test.db --table test_tbl_1 --entries 5 --nthreads 8 --query_user "test" --k 1

# test_tbl_2 (15 documentos)
./app --db data/test.db --table test_tbl_2 --entries 15 --nthreads 8 --query_user "test" --k 1

# test_tbl_3 (20 documentos)
./app --db data/test.db --table test_tbl_3 --entries 20 --nthreads 8 --query_user "test" --k 1
```

## 🔄 Atualização de Modelos

Os modelos devem ser regenerados quando:
- ❌ O banco de dados é atualizado
- ❌ O algoritmo de pré-processamento é modificado
- ❌ As stopwords são alteradas
- ❌ O stemmer é modificado

Para regenerar, simplesmente delete os arquivos `.bin` correspondentes e execute o script de geração novamente.

## 📊 Tamanhos Esperados

| Banco | Documentos | IDF | TF | Normas | Total |
|-------|-----------|-----|----|----|-------|
| test_tbl_0 | 10 | ~2KB | ~2KB | <1KB | ~5KB |
| test_tbl_1 | 5 | <1KB | <1KB | <1KB | ~2KB |
| test_tbl_2 | 15 | ~2KB | ~3KB | <1KB | ~6KB |
| test_tbl_3 | 20 | ~2KB | ~4KB | <1KB | ~7KB |
| wiki-small | 97,549 | ~31MB | ~358MB | ~763KB | ~390MB |
| food-reviews | 568,454 | ~6MB | ~428MB | ~4MB | ~438MB |

## 🐛 Troubleshooting

### Erro: "no such column: article_text"

**Problema:** O banco não tem as colunas esperadas.

**Solução:** Crie uma VIEW que mapeia suas colunas:
```sql
CREATE VIEW sample_articles AS 
SELECT Id as article_id, Text as article_text 
FROM MinhaTabela;
```

### Modelos não são encontrados

**Problema:** A interface não encontra os modelos.

**Solução:** Verifique se os arquivos existem em `models/` e se o nome segue o padrão correto:
```bash
ls -lh models/
```

### Processo muito lento

**Problema:** A geração está demorando muito.

**Solução:** 
- Aumente o número de threads: `--nthreads 16`
- Processe menos documentos inicialmente para testes
- Use SSD ao invés de HDD

## 💡 Dicas

1. **Primeira execução:** Sempre execute `./generate_models.sh` antes de usar a interface
2. **Desenvolvimento:** Para testes rápidos, use as tabelas de teste (10-20 docs)
3. **Produção:** Para demonstrações, use o wiki-small (97k docs)
4. **Benchmark:** Use o food-reviews para testes de performance (568k docs)

## 🔍 Verificação

Para verificar se os modelos foram gerados corretamente:

```bash
# Listar todos os modelos
ls -lh models/*.bin

# Contar modelos
ls -1 models/*.bin | wc -l

# Espaço total ocupado
du -sh models/
```

Você deve ter 3 arquivos (idf, tf, doc_norms) para cada configuração de banco/tabela.
