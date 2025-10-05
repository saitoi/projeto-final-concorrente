CC = cc
CFLAGS = -Wall -Wextra -I./include
LDFLAGS = -lsqlite3 -lpthread
FCLANG = --checks=-clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling

SRC = src/main.c src/hash_t.c src/sqlite_helper.c src/preprocess.c
OBJ = $(SRC:.c=.o)
TARGET = app

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(OBJ) -o $(TARGET) $(LDFLAGS)

lint:
	@command -v clang-tidy >/dev/null 2>&1 && clang-tidy $(SRC) $(FCLANG) -- $(CFLAGS) || echo "clang-tidy not found, skipping"

format:
	@command -v clang-format >/dev/null 2>&1 && clang-format -i $(SRC) include/*.h || echo "clang-format not found, skipping"

check: lint format

run: clean all
	./$(TARGET) --entries 100 --verbose

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(TARGET)

.PHONY: all clean lint format check run
