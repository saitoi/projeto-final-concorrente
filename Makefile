CC = cc
CFLAGS = -Wall -Wextra -I./include
LDFLAGS = -lsqlite3 -lpthread

SRC = src/main.c src/hash_t.c src/sqlite_helper.c
OBJ = $(SRC:.c=.o)
TARGET = app

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(OBJ) -o $(TARGET) $(LDFLAGS)

lint:
	@command -v clang-tidy >/dev/null 2>&1 && clang-tidy $(SRC) -- $(CFLAGS) || echo "clang-tidy not found, skipping"

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(TARGET)

.PHONY: all clean lint
