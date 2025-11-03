#!/bin/bash

# Script de teste da funcionalidade de busca
# Verifica se a busca TF-IDF está funcionando corretamente

echo "🔍 Testando Busca TF-IDF..."
echo ""

# Cores
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m'

# Teste 1: Verificar executável
echo -n "✓ Verificando executável... "
if [ -f "./app_ui" ]; then
    echo -e "${GREEN}OK${NC}"
else
    echo -e "${RED}FALHOU${NC} - app_ui não encontrado"
    echo "Execute: make ui"
    exit 1
fi

# Teste 2: Verificar stopwords
echo -n "✓ Verificando stopwords... "
if [ -f "assets/stopwords.txt" ]; then
    COUNT=$(wc -l < assets/stopwords.txt)
    echo -e "${GREEN}OK${NC} (${COUNT} termos)"
else
    echo -e "${RED}FALHOU${NC} - stopwords.txt não encontrado"
    exit 1
fi

# Teste 3: Verificar modelos
echo -n "✓ Verificando modelos... "
IDF_FILE="models/idf_sample_articles_97549.bin"
TF_FILE="models/tf_sample_articles_97549.bin"
NORMS_FILE="models/doc_norms_sample_articles_97549.bin"

if [ -f "$IDF_FILE" ] && [ -f "$TF_FILE" ] && [ -f "$NORMS_FILE" ]; then
    echo -e "${GREEN}OK${NC}"
else
    echo -e "${RED}FALHOU${NC}"
    echo "Arquivos necessários:"
    echo "  - $IDF_FILE"
    echo "  - $TF_FILE"
    echo "  - $NORMS_FILE"
    exit 1
fi

# Teste 4: Verificar banco de dados
echo -n "✓ Verificando banco de dados... "
if [ -f "data/wiki-small.db" ]; then
    SIZE=$(ls -lh data/wiki-small.db | awk '{print $5}')
    echo -e "${GREEN}OK${NC} (${SIZE})"
else
    echo -e "${RED}FALHOU${NC} - data/wiki-small.db não encontrado"
    exit 1
fi

echo ""
echo -e "${GREEN}✅ Todos os testes passaram!${NC}"
echo ""
echo "📝 Queries de exemplo para testar:"
echo "  • machine learning"
echo "  • neural networks"
echo "  • artificial intelligence"
echo "  • computer science"
echo "  • natural language processing"
echo ""
echo -e "${YELLOW}Execute a interface com:${NC} ./app_ui"
echo ""
echo "⌨️  Instruções:"
echo "  1. Clique no campo de busca"
echo "  2. Digite uma query"
echo "  3. Pressione Enter ou clique em 'Buscar Documentos'"
echo "  4. Veja os resultados com scores e previews"
echo ""
