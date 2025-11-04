# Detecta o sistema operacional
ifeq ($(OS),Windows_NT)
    RM = del /Q
    RM_DIR = rmdir /S /Q
    TARGET = app.exe
    PATH_SEP = \\
    NULL_DEVICE = NUL
    LDFLAGS = -lsqlite3 -lpthread -lstemmer -lm
else
    RM = rm -f
    RM_DIR = rm -rf
    TARGET = app
    PATH_SEP = /
    NULL_DEVICE = /dev/null
    LDFLAGS = -lsqlite3 -lpthread -lstemmer -lm
endif

CC = cc
CFLAGS = -Wall -Wextra -I.$(PATH_SEP)include -g -DLOG_USE_COLOR
FCLANG = --checks=-clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling

# Parâmetros configuráveis
QUERY ?=
DB ?=
QUERY_FILENAME ?=
TEST ?= -1
ENTRIES ?= 100
VERBOSE ?= 1
MANUAL ?= 0
NTHR ?= 4
SEQ ?= 0
K ?= 10

# Mapeamento TEST para TBL_NAME
ifeq ($(TEST),0)
	DB = ./data/test.db
    TBL = test_tbl_0
else ifeq ($(TEST),1)
	DB = ./data/test.db
    TBL = test_tbl_1
else ifeq ($(TEST),2)
	DB = ./data/test.db
    TBL = test_tbl_2
else ifeq ($(TEST),3)
	DB = ./data/test.db
    TBL = test_tbl_3
else ifeq ($(TEST),4)
    QUERY_FILENAME = ./tests/performance/queries/shakespeares_query.txt
else ifeq ($(TEST),5)
    DB = ./book-corpus.db
else
    TBL = sample_articles
endif


# Se MANUAL=1, usa includes e libs locais
ifeq ($(MANUAL),1)
    CFLAGS += -I./libstemmer/usr/include -I./libsqlite3/usr/include
    LDFLAGS = -L./libstemmer/usr/lib/x86_64-linux-gnu -L./libsqlite3/usr/lib/x86_64-linux-gnu -lstemmer -lsqlite3 -lpthread -lm
endif

ifeq ($(MANUAL),2)
    CFLAGS += -I/opt/homebrew/include -I./libsqlite3/usr/include
	LDFLAGS = -L/opt/homebrew/lib -lstemmer -lsqlite3 -lpthread -lm
endif

ifeq ($(SEQ),0)
    MAIN_SRC = src$(PATH_SEP)main.c
    TARGET = app
else
    MAIN_SRC = src$(PATH_SEP)main_seq.c
    TARGET = app_seq
endif

SRC = $(MAIN_SRC) src$(PATH_SEP)hash_t.c src$(PATH_SEP)sqlite_helper.c src$(PATH_SEP)preprocess.c src$(PATH_SEP)file_io.c src$(PATH_SEP)preprocess_query.c src$(PATH_SEP)log.c
OBJ = $(SRC:.c=.o)
HEADERS = include$(PATH_SEP)hash_t.h include$(PATH_SEP)file_io.h include$(PATH_SEP)preprocess.h include$(PATH_SEP)sqlite_helper.h include$(PATH_SEP)preprocess_query.h include$(PATH_SEP)log.h include$(PATH_SEP)clay.h

# UI com Clay + Raylib
UI_TARGET = app_ui
UI_SRC = src$(PATH_SEP)ui_clay.c
UI_LDFLAGS = -lraylib -lm

all: $(TARGET)

help:
	@echo "Targets disponíveis:"
	@echo "  make all             - Compila o projeto (padrão)"
	@echo "  make ui              - Compila e executa a interface visual (Clay + Raylib)"
	@echo "  make setup           - Configura o projeto (gera modelos se necessário)"
	@echo "  make models          - Gera todos os modelos TF-IDF necessários"
	@echo "  make demo            - Executa demo da interface (com verificações)"
	@echo "  make run             - Limpa, compila e executa o programa"
	@echo "                         Variáveis default: ENTRIES=$(ENTRIES) VERBOSE=$(VERBOSE)"
	@echo "  make test-correctness - Executa todos os testes de corretude do banco de dados"
	@echo "  make clean           - Remove arquivos de compilação (.o e executável)"
	@echo "  make clean_models    - Remove arquivos binários em ./models/"
	@echo "  make lint            - Executa clang-tidy para análise estática"
	@echo "  make format          - Formata código com clang-format"
	@echo "  make check           - Executa lint + format"
	@echo "  make help            - Exibe esta mensagem"
	@echo ""
	@echo "Exemplos:"
	@echo "  make setup           - Primeira vez: compila e gera modelos"
	@echo "  make demo            - Demonstração completa da interface ⭐"
	@echo "  make run ENTRIES=50 VERBOSE=0"
	@echo "  make run TEST=0 QUERY=\"marine sea species\""
	@echo "  make run MANUAL=1"
	@echo "  make test-correctness"
	@echo "  make ui              - Abre interface gráfica"

$(TARGET): $(OBJ)
	$(CC) $(OBJ) -o $(TARGET) $(LDFLAGS)

$(UI_TARGET): $(UI_SRC) $(HEADERS) src/sqlite_helper.c src/preprocess_query.c
	@echo "Compilando interface visual com Clay..."
	$(CC) $(CFLAGS) $(UI_SRC) src/sqlite_helper.c src/preprocess_query.c src/preprocess.c src/hash_t.c src/file_io.c src/log.c -o $(UI_TARGET) $(UI_LDFLAGS) -lsqlite3 -lstemmer

ui: $(UI_TARGET)
	@echo "Iniciando interface visual..."
	./$(UI_TARGET)

# Gera todos os modelos TF-IDF
models:
	@echo "Gerando modelos TF-IDF..."
	@chmod +x generate_models.sh
	@./generate_models.sh

# Demonstração completa da interface
demo:
	@echo "Executando demonstração da interface..."
	@chmod +x demo_interface.sh
	@./demo_interface.sh

# Setup inicial: compila e gera modelos se necessário
setup: all
	@echo "Configurando projeto..."
	@if [ ! -d "models" ]; then mkdir -p models; fi
	@MODEL_COUNT=$$(ls -1 models/*.bin 2>/dev/null | wc -l); \
	if [ "$$MODEL_COUNT" -lt 3 ]; then \
		echo "Modelos não encontrados. Gerando..."; \
		$(MAKE) models; \
	else \
		echo "Modelos já existem ($$MODEL_COUNT arquivos)"; \
	fi
	@echo "✓ Setup concluído!"

lint:
	@command -v clang-tidy >$(NULL_DEVICE) 2>&1 && clang-tidy $(SRC) $(FCLANG) -- $(CFLAGS) || echo "clang-tidy not found, skipping"

format:
	@command -v clang-format >$(NULL_DEVICE) 2>&1 && clang-format -i $(SRC) include$(PATH_SEP)*.h || echo "clang-format not found, skipping"

check: lint format

run:
	@echo "Cleaning old binaries.."
	@echo "Running.."
	@$(MAKE) --no-print-directory clean all >$(NULL_DEVICE) 2>&1
	@./$(TARGET) --entries $(ENTRIES) --nthreads $(NTHR) \
    $(if $(filter 1,$(VERBOSE)),--verbose,) \
    $(if $(TBL),--table "$(TBL)",) \
    $(if $(QUERY),--query_user "$(QUERY)",) \
    $(if $(QUERY_FILENAME),--query_filename $(QUERY_FILENAME),) \
    $(if $(DB),--db $(DB),) \
    $(if $(K),--k $(K),)

%.o: %.c $(HEADERS)
	@$(CC) $(CFLAGS) -c $< -o $@

clean:
	@echo 'Cleaning old binaries..'
	@$(RM) $(OBJ) $(TARGET) $(UI_TARGET)

clean_models:
ifeq ($(OS),Windows_NT)
	@for %%f in (models\*.bin models\*.txt) do if exist "%%f" del "%%f"
else
	@find models -type f ! -name '.gitkeep' -delete 2>$(NULL_DEVICE) || true
endif
	@echo 'Models cleaned..'

test-correctness: $(TARGET)
	@echo "=========================================="
	@echo "Executando testes de corretude TF-IDF"
	@echo "=========================================="
	@echo ""
	@sqlite3 data/test.db "SELECT table_id, query_id, query FROM queries ORDER BY \"table\", query_id" | while IFS='|' read -r tbl qid query; do \
		if [ -z "$$tbl" ] || [ -z "$$qid" ] || [ -z "$$query" ]; then \
			continue; \
		fi; \
		table_name="test_tbl_$$tbl"; \
		table_exists=$$(sqlite3 data/test.db "SELECT name FROM sqlite_master WHERE type='table' AND name='$$table_name';" 2>/dev/null); \
		if [ -z "$$table_exists" ]; then \
			echo "=========================================="; \
			echo "Tabela: $$table_name"; \
			echo "Query ID: $$qid"; \
			echo "Query: $$query"; \🔍 Buscar - Interface para realizar buscas TF-IDF

			echo "AVISO: Tabela não existe no banco de dados. Ignorando..."; \
			echo "=========================================="; \
			echo ""; \
			continue; \
		fi; \
		echo "=========================================="; \
		echo "Tabela: $$table_name"; \
		echo "Query ID: $$qid"; \
		echo "Query: $$query"; \
		echo "=========================================="; \
		./$(TARGET) --table "$$table_name" --query_user "$$query" --entries $(ENTRIES) --nthreads $(NTHR) $(if $(filter 1,$(VERBOSE)),--verbose,); \
		echo ""; \
	done
	@echo "=========================================="
	@echo "Testes concluídos!"
	@echo "=========================================="

.PHONY: all clean clean_models lint format check run help test-correctness ui models demo setup
