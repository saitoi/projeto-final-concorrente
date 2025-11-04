#!/bin/bash

# 🎨 Script de Demonstração da Interface TF-IDF
# Autor: Milton Salgado
# Data: 03/11/2024

set -e

# Cores para output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
PURPLE='\033[0;35m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

echo -e "${PURPLE}"
echo "╔═══════════════════════════════════════════════════════════╗"
echo "║                                                           ║"
echo "║         🎨 Interface Visual TF-IDF                       ║"
echo "║         Busca de Documentos com Clay UI                  ║"
echo "║                                                           ║"
echo "╚═══════════════════════════════════════════════════════════╝"
echo -e "${NC}"

# Verificações de pré-requisitos
echo -e "${CYAN}[1/5]${NC} Verificando pré-requisitos..."

# Verifica stopwords
if [ ! -f "assets/stopwords.txt" ]; then
    echo -e "${RED}✗${NC} assets/stopwords.txt não encontrado!"
    exit 1
else
    STOPWORDS_COUNT=$(wc -l < assets/stopwords.txt)
    echo -e "${GREEN}✓${NC} Stopwords: ${STOPWORDS_COUNT} termos"
fi

# Verifica modelos
echo -e "${CYAN}[2/5]${NC} Verificando modelos..."
if [ ! -d "models" ]; then
    echo -e "${RED}✗${NC} Pasta models/ não encontrada!"
    exit 1
fi

MODELS_COUNT=$(ls -1 models/*.bin 2>/dev/null | wc -l)
if [ "$MODELS_COUNT" -eq 0 ]; then
    echo -e "${RED}✗${NC} Nenhum modelo .bin encontrado em models/"
    exit 1
else
    echo -e "${GREEN}✓${NC} Modelos: ${MODELS_COUNT} arquivos .bin"
    ls -lh models/*.bin | awk '{printf "  - %s (%s)\n", $9, $5}'
fi

# Verifica bancos de dados
echo -e "${CYAN}[3/5]${NC} Verificando bancos de dados..."
if [ ! -d "data" ]; then
    echo -e "${RED}✗${NC} Pasta data/ não encontrada!"
    exit 1
fi

DBS_COUNT=$(ls -1 data/*.db 2>/dev/null | wc -l)
if [ "$DBS_COUNT" -eq 0 ]; then
    echo -e "${RED}✗${NC} Nenhum banco .db encontrado em data/"
    exit 1
else
    echo -e "${GREEN}✓${NC} Bancos de dados: ${DBS_COUNT} arquivos .db"
    ls -lh data/*.db | awk '{printf "  - %s (%s)\n", $9, $5}'
fi

# Verifica executável
echo -e "${CYAN}[4/5]${NC} Verificando executável..."
if [ ! -f "app_ui" ]; then
    echo -e "${YELLOW}⚠${NC} app_ui não encontrado, compilando..."
    make ui
    if [ $? -ne 0 ]; then
        echo -e "${RED}✗${NC} Falha na compilação!"
        exit 1
    fi
else
    APP_SIZE=$(ls -lh app_ui | awk '{print $5}')
    echo -e "${GREEN}✓${NC} Executável: app_ui (${APP_SIZE})"
fi

# Resumo final
echo -e "${CYAN}[5/5]${NC} Resumo da configuração:"
echo -e "  ${BLUE}•${NC} Stopwords: ${GREEN}${STOPWORDS_COUNT} termos${NC}"
echo -e "  ${BLUE}•${NC} Modelos: ${GREEN}${MODELS_COUNT} arquivos${NC}"
echo -e "  ${BLUE}•${NC} Bancos: ${GREEN}${DBS_COUNT} databases${NC}"
echo -e "  ${BLUE}•${NC} Threads: ${GREEN}1-16 configuráveis${NC}"
echo ""

# Instruções de uso
echo -e "${YELLOW}╔═══════════════════════════════════════════════════════════╗${NC}"
echo -e "${YELLOW}║  📖 INSTRUÇÕES DE USO                                     ║${NC}"
echo -e "${YELLOW}╠═══════════════════════════════════════════════════════════╣${NC}"
echo -e "${YELLOW}║                                                           ║${NC}"
echo -e "${YELLOW}║  ⌨️  TECLADO:                                             ║${NC}"
echo -e "${YELLOW}║    • Setas ↑↓  : Aumentar/diminuir threads (1-16)        ║${NC}"
echo -e "${YELLOW}║    • Setas ←→  : Navegar entre bancos de dados           ║${NC}"
echo -e "${YELLOW}║    • Enter     : Executar busca                          ║${NC}"
echo -e "${YELLOW}║    • ESC       : Fechar aplicação                        ║${NC}"
echo -e "${YELLOW}║                                                           ║${NC}"
echo -e "${YELLOW}║  🖱️  MOUSE:                                               ║${NC}"
echo -e "${YELLOW}║    • Click no campo: Ativar entrada de texto             ║${NC}"
echo -e "${YELLOW}║    • Botões +/-    : Ajustar threads                     ║${NC}"
echo -e "${YELLOW}║    • Botão Buscar  : Executar busca TF-IDF               ║${NC}"
echo -e "${YELLOW}║                                                           ║${NC}"
echo -e "${YELLOW}║  📝 EXEMPLOS DE CONSULTAS:                                ║${NC}"
echo -e "${YELLOW}║    • machine learning algorithms                         ║${NC}"
echo -e "${YELLOW}║    • natural language processing                         ║${NC}"
echo -e "${YELLOW}║    • artificial intelligence neural networks             ║${NC}"
echo -e "${YELLOW}║    • computer vision deep learning                       ║${NC}"
echo -e "${YELLOW}║                                                           ║${NC}"
echo -e "${YELLOW}╚═══════════════════════════════════════════════════════════╝${NC}"
echo ""

# Pergunta se deve executar
read -p "$(echo -e ${GREEN}Deseja executar a interface agora? [S/n]: ${NC})" -n 1 -r
echo
if [[ ! $REPLY =~ ^[Nn]$ ]]; then
    echo ""
    echo -e "${PURPLE}🚀 Iniciando interface...${NC}"
    echo ""
    sleep 1
    ./app_ui
else
    echo -e "${BLUE}Para executar manualmente, use: ${CYAN}./app_ui${NC}"
fi

echo ""
echo -e "${GREEN}✨ Demonstração concluída!${NC}"
echo -e "${CYAN}📚 Para mais informações, veja: ${YELLOW}INTERFACE_GUIDE.md${NC}"
