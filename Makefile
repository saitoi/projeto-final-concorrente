# Detecta o sistema operacional
ifeq ($(OS),Windows_NT)
    # Configurações para Windows
    RM = del /Q
    RM_DIR = rmdir /S /Q
    TARGET = app.exe
    PATH_SEP = \\
    NULL_DEVICE = NUL
    LDFLAGS = -lsqlite3 -lpthread -lstemmer -lm
else
    # Configurações para Linux/Unix
    RM = rm -f
    RM_DIR = rm -rf
    TARGET = app
    PATH_SEP = /
    NULL_DEVICE = /dev/null
    LDFLAGS = -lsqlite3 -lpthread -lstemmer -lm
endif

CC = cc
CFLAGS = -Wall -Wextra -I.$(PATH_SEP)include
FCLANG = --checks=-clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling

# Parâmetros do programa (podem ser sobrescritos: make run ENTRIES=50 VERBOSE=1)
ENTRIES ?= 100
VERBOSE ?= 1

SRC = src$(PATH_SEP)main.c src$(PATH_SEP)hash_t.c src$(PATH_SEP)sqlite_helper.c src$(PATH_SEP)preprocess.c src$(PATH_SEP)file_io.c src$(PATH_SEP)preprocess_query.c
OBJ = $(SRC:.c=.o)

all: $(TARGET)

help:
	@echo "Targets disponíveis:"
	@echo "  make all          - Compila o projeto (padrão)"
	@echo "  make run          - Limpa, compila e executa o programa"
	@echo "                      Variáveis default: ENTRIES=$(ENTRIES) VERBOSE=$(VERBOSE)"
	@echo "  make clean        - Remove arquivos de compilação (.o e executável)"
	@echo "  make clean_models - Remove arquivos binários em ./models/"
	@echo "  make lint         - Executa clang-tidy para análise estática"
	@echo "  make format       - Formata código com clang-format"
	@echo "  make check        - Executa lint + format"
	@echo "  make help         - Exibe esta mensagem"
	@echo ""
	@echo "Exemplos:"
	@echo "  make run ENTRIES=50 VERBOSE=0"
	@echo "  make run ENTRIES=1000 VERBOSE=1"

$(TARGET): $(OBJ)
	$(CC) $(OBJ) -o $(TARGET) $(LDFLAGS)

lint:
	@command -v clang-tidy >$(NULL_DEVICE) 2>&1 && clang-tidy $(SRC) $(FCLANG) -- $(CFLAGS) || echo "clang-tidy not found, skipping"

format:
	@command -v clang-format >$(NULL_DEVICE) 2>&1 && clang-format -i $(SRC) include$(PATH_SEP)*.h || echo "clang-format not found, skipping"

check: lint format

run: clean all
	.$(PATH_SEP)$(TARGET) --entries $(ENTRIES) $(if $(filter 1,$(VERBOSE)),--verbose,)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	$(RM) $(OBJ) $(TARGET)

clean_models:
ifeq ($(OS),Windows_NT)
	@for %%f in (models\*.bin models\*.txt) do if exist "%%f" del "%%f"
else
	@find models -type f ! -name '.gitkeep' -delete 2>$(NULL_DEVICE) || true
endif
	echo 'Models cleaned..'

.PHONY: all clean clean_models lint format check run help
