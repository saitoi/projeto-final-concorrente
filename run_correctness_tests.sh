#!/bin/bash

# Script para executar consultas de testes de corretude
# Lê queries do banco de dados e executa tf_idf.py para cada consulta

DB_FILE="data/wiki-small.db"

# Verifica se o banco de dados existe
if [ ! -f "$DB_FILE" ]; then
    echo "Erro: Banco de dados $DB_FILE não encontrado!"
    exit 1
fi

echo "=========================================="
echo "Executando testes de corretude TF-IDF"
echo "=========================================="
echo ""

# Lê queries do banco de dados
sqlite3 "$DB_FILE" "SELECT \"table\", query_id, query FROM queries ORDER BY \"table\", query_id" | while IFS='|' read -r table query_id query; do
    # Ignora linhas vazias
    if [ -z "$table" ] || [ -z "$query_id" ] || [ -z "$query" ]; then
        continue
    fi

    TABLE_NAME="test_tbl_${table}"

    # Verifica se a tabela existe no banco de dados
    TABLE_EXISTS=$(sqlite3 "$DB_FILE" "SELECT name FROM sqlite_master WHERE type='table' AND name='$TABLE_NAME';" 2>/dev/null)

    if [ -z "$TABLE_EXISTS" ]; then
        echo "=========================================="
        echo "Tabela: $TABLE_NAME"
        echo "Query ID: $query_id"
        echo "Query: $query"
        echo "AVISO: Tabela não existe no banco de dados. Ignorando..."
        echo "=========================================="
        echo ""
        continue
    fi

    echo "=========================================="
    echo "Tabela: $TABLE_NAME"
    echo "Query ID: $query_id"
    echo "Query: $query"
    echo "=========================================="

    # Executa o tf_idf.py
    uv run tf_idf.py -d "$DB_FILE" -t "$TABLE_NAME" -q "$query"

    echo ""
done

echo "=========================================="
echo "Testes concluídos!"
echo "=========================================="
