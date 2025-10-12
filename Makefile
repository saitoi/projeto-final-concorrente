# Detecta o sistema operacional
ifeq ($(OS),Windows_NT)
    # Configurações para Windows
    RM = del /Q
    RM_DIR = rmdir /S /Q
    TARGET = app.exe
    PATH_SEP = \\
    NULL_DEVICE = NUL
    LDFLAGS = -lsqlite3 -lpthread -lstemmer
else
    # Configurações para Linux/Unix
    RM = rm -f
    RM_DIR = rm -rf
    TARGET = app
    PATH_SEP = /
    NULL_DEVICE = /dev/null
    LDFLAGS = -lsqlite3 -lpthread -lstemmer
endif

CC = cc
CFLAGS = -Wall -Wextra -I.$(PATH_SEP)include
FCLANG = --checks=-clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling

SRC = src$(PATH_SEP)main.c src$(PATH_SEP)hash_t.c src$(PATH_SEP)sqlite_helper.c src$(PATH_SEP)preprocess.c
OBJ = $(SRC:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(OBJ) -o $(TARGET) $(LDFLAGS)

lint:
	@command -v clang-tidy >$(NULL_DEVICE) 2>&1 && clang-tidy $(SRC) $(FCLANG) -- $(CFLAGS) || echo "clang-tidy not found, skipping"

format:
	@command -v clang-format >$(NULL_DEVICE) 2>&1 && clang-format -i $(SRC) include$(PATH_SEP)*.h || echo "clang-format not found, skipping"

check: lint format

run: clean all
	.$(PATH_SEP)$(TARGET) --entries 100 --verbose

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	$(RM) $(OBJ) $(TARGET)

.PHONY: all clean lint format check run