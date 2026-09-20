# Linux / macOS / MinGW build.   make            -> build/sam_say, build/sam (library CLI), libsam
#                                 make standalone -> build/sam_standalone (needs src/sam_data.c from tools/embed_data.py)
#                                 make test       -> build/lib_test, the library's own tests
CC ?= cc
CFLAGS ?= -std=c99 -O2 -Wall -Wextra -Wno-unused-parameter
LIB = src/sam.c src/sam_lex.c src/sam_morph.c src/sam_pos.c src/sam_norm.c src/sam_front.c
TTSLIB = $(LIB) src/sam4fx.c src/sam_tts.c
SOEXT ?= so

all: build/sam_say build/sam build/libsam.$(SOEXT) build/samsynth build/lextest

build:
	mkdir -p build

build/sam_say: $(LIB) src/sam4fx.c src/sam_say.c | build
	$(CC) $(CFLAGS) $^ -lm -o $@

# the screen-reader library (src/sam_tts.h) and its command line front end
build/sam: $(TTSLIB) src/sam_cli.c | build
	$(CC) $(CFLAGS) $^ -lm -o $@

build/libsam.$(SOEXT): $(TTSLIB) | build
	$(CC) $(CFLAGS) -shared -fPIC -fvisibility=hidden -DSAM_BUILD_DLL $^ -lm -o $@

build/samsynth: src/sam.c src/samsynth.c | build
	$(CC) $(CFLAGS) $^ -lm -o $@

build/lextest: src/sam_lex.c src/sam_morph.c src/lextest.c | build
	$(CC) $(CFLAGS) $^ -lm -o $@

standalone: build/sam_standalone

build/sam_standalone: $(LIB) src/sam4fx.c src/sam_say.c src/sam_data.c | build
	$(CC) $(CFLAGS) -DSAM_EMBEDDED $^ -lm -o $@

test: build/lib_test

build/lib_test: $(TTSLIB) tests/lib/lib_test.c | build
	$(CC) $(CFLAGS) $^ -lm -lpthread -o $@

clean:
	rm -rf build

.PHONY: all standalone test clean
