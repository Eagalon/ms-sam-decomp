# Linux / macOS / MinGW build.   make            -> build/sam_say
#                                 make standalone -> build/sam_standalone (needs src/sam_data.c from tools/embed_data.py)
CC ?= cc
CFLAGS ?= -std=c99 -O2 -Wall -Wextra -Wno-unused-parameter
LIB = src/sam.c src/sam_lex.c src/sam_morph.c src/sam_pos.c src/sam_norm.c src/sam_front.c

all: build/sam_say build/samsynth build/lextest

build:
	mkdir -p build

build/sam_say: $(LIB) src/sam4fx.c src/sam_say.c | build
	$(CC) $(CFLAGS) $^ -lm -o $@

build/samsynth: src/sam.c src/samsynth.c | build
	$(CC) $(CFLAGS) $^ -lm -o $@

build/lextest: src/sam_lex.c src/sam_morph.c src/lextest.c | build
	$(CC) $(CFLAGS) $^ -lm -o $@

standalone: build/sam_standalone

build/sam_standalone: $(LIB) src/sam4fx.c src/sam_say.c src/sam_data.c | build
	$(CC) $(CFLAGS) -DSAM_EMBEDDED $^ -lm -o $@

clean:
	rm -rf build

.PHONY: all standalone clean
