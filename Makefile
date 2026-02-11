CC := cc
CFLAGS := -std=c11 -Wall -Wextra -Wpedantic -O2 -Iinclude
LDFLAGS := -lm

SRC := $(wildcard src/*.c)
OBJ := $(SRC:.c=.o)
BIN := bin/dwaing

.PHONY: all clean run-example

all: $(BIN)

$(BIN): $(OBJ)
	@mkdir -p bin
	$(CC) $(OBJ) -o $@ $(LDFLAGS)

src/%.o: src/%.c
	$(CC) $(CFLAGS) -c $< -o $@

run-example: $(BIN)
	./$(BIN) validate examples/library.xml examples/panel.xml
	./$(BIN) summary examples/library.xml examples/panel.xml

clean:
	rm -f src/*.o
	rm -rf bin
