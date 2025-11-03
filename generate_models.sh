#!/bin/bash

# 🔧 Script de Geração de Modelos TF-IDF
# Este script gera todos os modelos necessários para os bancos de dados

set -e

# Cores para output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

echo -e "${CYAN}"
echo "╔═══════════════════════════════════════════════════════════╗"
echo "║                                                           ║"
echo "║         🔧 Gerador de Modelos TF-IDF                     ║"
echo "║                                                           ║"
echo "╚═══════════════════════════════════════════════════════════╝"
echo -e "${NC}"

# Verifica se o executável existe
if [ ! -f "./app" ]; then
    echo -e "${RED}✗${NC} Executável './app' não encontrado!"
    echo -e "${YELLOW}→${NC} Execute 'make' primeiro"
    exit 1
fi

# Cria diretório de modelos se não existir
mkdir -p models

# Função para gerar modelo
generate_model() {
    local db=$1
    local table=$2
    local entries=$3
    local display_name=$4
    
    echo -e "\n${BLUE}➜${NC} Gerando modelo para ${YELLOW}$display_name${NC}"
    echo -e "  DB: $db"
    echo -e "  Tabela: $table"
    echo -e "  Documentos: $entries"
    
    # Verifica se os modelos já existem
    local idf_file="models/idf_${table}_${entries}.bin"
    local tf_file="models/tf_${table}_${entries}.bin"
    local norms_file="models/doc_norms_${table}_${entries}.bin"
    
    if [ -f "$idf_file" ] && [ -f "$tf_file" ] && [ -f "$norms_file" ]; then
        echo -e "${GREEN}✓${NC} Modelos já existem, pulando..."
        return 0
    fi
    
    echo -e "${CYAN}⚙${NC} Processando..."
    
    # Executa o app para gerar os modelos
    ./app --db "$db" --table "$table" --entries "$entries" --nthreads 8 --query_user "test" --k 1 > /dev/null 2>&1
    
    if [ $? -eq 0 ]; then
        echo -e "${GREEN}✓${NC} Modelos gerados com sucesso!"
    else
        echo -e "${RED}✗${NC} Erro ao gerar modelos"
        return 1
    fi
}

# ========== WIKI-SMALL ==========
echo -e "\n${CYAN}[1/6]${NC} Wiki-Small Database"
generate_model "data/wiki-small.db" "sample_articles" 97549 "Wikipedia (97k docs)"

# ========== TEST TABLES ==========
echo -e "\n${CYAN}[2/6]${NC} Test Database - test_tbl_0"
generate_model "data/test.db" "test_tbl_0" 10 "Test Table 0 (10 docs)"

echo -e "\n${CYAN}[3/6]${NC} Test Database - test_tbl_1"
generate_model "data/test.db" "test_tbl_1" 5 "Test Table 1 (5 docs)"

echo -e "\n${CYAN}[4/6]${NC} Test Database - test_tbl_2"
generate_model "data/test.db" "test_tbl_2" 15 "Test Table 2 (15 docs)"

echo -e "\n${CYAN}[5/6]${NC} Test Database - test_tbl_3"
generate_model "data/test.db" "test_tbl_3" 20 "Test Table 3 (20 docs)"

# ========== FOOD REVIEWS ==========
echo -e "\n${CYAN}[6/6]${NC} Food Reviews Database"
generate_model "data/food-reviews.db" "sample_articles" 568454 "Amazon Food Reviews (568k docs)"

# Resumo
echo -e "\n${GREEN}"
echo "╔═══════════════════════════════════════════════════════════╗"
echo "║                                                           ║"
echo "║         ✓ Todos os modelos foram gerados!                ║"
echo "║                                                           ║"
echo "╚═══════════════════════════════════════════════════════════╝"
echo -e "${NC}"

echo -e "${CYAN}Modelos disponíveis:${NC}"
ls -lh models/*.bin | wc -l | xargs -I {} echo -e "  Total: {} arquivos"
du -sh models/ | awk '{print "  Tamanho total: " $1}'

echo -e "\n${GREEN}✓${NC} Pronto! Você pode executar a interface agora."
