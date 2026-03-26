CC := cc
CFLAGS := -std=c11 -Wall -Wextra -Wpedantic -O2 -Iinclude
DEPFLAGS := -MMD -MP
LDFLAGS := -lm
SDL2_CFLAGS := $(shell pkg-config --cflags sdl2)
SDL2_LIBS := $(shell pkg-config --libs sdl2)
OPENGL_LIBS ?= -lGL -lGLU

CORE_OBJ := src/geom.o src/model.o src/parser.o src/layout.o src/rules.o
PANEL_OBJ := src/geom.o src/model.o src/parser.o src/layout.o src/rules.o
CLI_OBJ := src/main.o
VIEWER_OBJ := src/viewer.o

BIN := bin/dwaing
VIEWER_BIN := bin/dwaing_viewer

.PHONY: all clean run-example viewer run-viewer test

all: $(BIN)

$(BIN): $(CLI_OBJ) $(CORE_OBJ)
	@mkdir -p bin
	$(CC) $(CLI_OBJ) $(CORE_OBJ) -o $@ $(LDFLAGS)

$(VIEWER_BIN): $(VIEWER_OBJ) $(PANEL_OBJ)
	@mkdir -p bin
	$(CC) $(VIEWER_OBJ) $(PANEL_OBJ) -o $@ $(LDFLAGS) $(SDL2_LIBS) $(OPENGL_LIBS)

viewer: $(VIEWER_BIN)

run-viewer: $(VIEWER_BIN)
	./$(VIEWER_BIN) examples/panel.xml libraries/default/library.xml

src/viewer.o: src/viewer.c
	$(CC) $(CFLAGS) $(DEPFLAGS) $(SDL2_CFLAGS) -c $< -o $@

src/%.o: src/%.c
	$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

test: $(BIN)
	./tests/run.sh

run-example: test
	./$(BIN) validate libraries/default/library.xml examples/panel.xml
	./$(BIN) summary libraries/default/library.xml examples/panel.xml

clean:
	rm -f src/*.o src/*.d
	rm -rf bin

-include src/*.d
