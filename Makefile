# Builds the shared library, a static library and the command-line tool.
#   make              -> build/libtdbg.{so,dylib}, build/libtdbg.a, build/tdbg
#   make python       -> copies the shared library into python/tdbg/
#   make test         -> runs tests/test_tdbg.py
# On Windows use CMake (or MinGW: make SHLIB_EXT=dll).

CC      ?= cc
CFLAGS  ?= -O2
# required flags are kept separate so `make CFLAGS=...` cannot drop them
TDBG_FLAGS := -std=c99 -Wall -Wextra -pedantic -Iinclude
SRC     := src/common.c src/qr.c src/datamatrix.c src/aztec.c src/maxicode.c
BUILD   := build

UNAME_S := $(shell uname -s 2>/dev/null)
ifeq ($(UNAME_S),Darwin)
  SHLIB_EXT ?= dylib
  SHFLAGS   := -dynamiclib -install_name @rpath/libtdbg.dylib
else
  SHLIB_EXT ?= so
  SHFLAGS   := -shared
endif
SHLIB := $(BUILD)/libtdbg.$(SHLIB_EXT)

OBJ_PIC    := $(SRC:src/%.c=$(BUILD)/pic/%.o)
OBJ_STATIC := $(SRC:src/%.c=$(BUILD)/static/%.o)

all: $(SHLIB) $(BUILD)/libtdbg.a $(BUILD)/tdbg

$(BUILD)/pic/%.o: src/%.c include/tdbg.h src/internal.h
	@mkdir -p $(dir $@)
	$(CC) $(TDBG_FLAGS) $(CFLAGS) -fPIC -fvisibility=hidden -DTDBG_BUILD -c $< -o $@

$(BUILD)/static/%.o: src/%.c include/tdbg.h src/internal.h
	@mkdir -p $(dir $@)
	$(CC) $(TDBG_FLAGS) $(CFLAGS) -DTDBG_STATIC -c $< -o $@

$(SHLIB): $(OBJ_PIC)
	$(CC) $(SHFLAGS) -o $@ $^ -lm

$(BUILD)/libtdbg.a: $(OBJ_STATIC)
	ar rcs $@ $^

$(BUILD)/tdbg: cli/main.c $(BUILD)/libtdbg.a
	$(CC) $(TDBG_FLAGS) $(CFLAGS) -DTDBG_STATIC -o $@ $< $(BUILD)/libtdbg.a -lm

python: $(SHLIB)
	cp $(SHLIB) python/tdbg/

test: python
	PYTHONPATH=python python3 tests/test_tdbg.py
	PYTHONPATH=python python3 tests/test_maxicode_orientation.py

clean:
	rm -rf $(BUILD) python/tdbg/libtdbg.*

.PHONY: all python test clean
