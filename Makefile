# Estruturas de dados em C: árvore B+ em disco, PATRICIA, radix, tabela hash e ordenação externa.
#
#   make              compila a biblioteca, o programa `ed` e os testes
#   make teste        roda todos os testes
#   make sanitizado   testes com AddressSanitizer e UndefinedBehaviorSanitizer (Linux/macOS)
#   make wasm         compila para WebAssembly/WASI (requer zig) e roda os testes no Node
#   make limpar

CC      ?= gcc
CFLAGS  ?= -O2 -g
AVISOS  := -std=c11 -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion
CPPFLAGS += -Iinclude -D_POSIX_C_SOURCE=200809L
LDLIBS  += -lm

EXE :=
ifeq ($(OS),Windows_NT)
  EXE := .exe
endif

FONTES  := $(wildcard src/*.c)
OBJETOS := $(FONTES:src/%.c=build/%.o)
TESTES  := bplus tries hash ordenacao
BIN_TESTES := $(TESTES:%=build/teste_%$(EXE))

.PHONY: all teste sanitizado wasm limpar

all: build/ed$(EXE) $(BIN_TESTES)

build:
	mkdir -p build

build/%.o: src/%.c $(wildcard include/ed/*.h) | build
	$(CC) $(AVISOS) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

build/libed.a: $(OBJETOS)
	$(AR) rcs $@ $^

build/ed$(EXE): app/ed.c build/libed.a
	$(CC) $(AVISOS) $(CFLAGS) $(CPPFLAGS) $< build/libed.a -o $@ $(LDLIBS)

build/teste_%$(EXE): tests/teste_%.c tests/teste.h build/libed.a
	$(CC) $(AVISOS) $(CFLAGS) $(CPPFLAGS) $< build/libed.a -o $@ $(LDLIBS)

teste: $(BIN_TESTES)
	@cd build && for t in $(TESTES); do echo "== $$t"; ./teste_$$t$(EXE) || exit 1; done

sanitizado:
	$(MAKE) limpar
	$(MAKE) teste CFLAGS="-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all"

# WebAssembly: o mesmo código, executado dentro do Node (útil onde executáveis nativos
# novos são bloqueados, como no Windows com Smart App Control).
ZIG  ?= zig
NODE ?= node
WASM_FLAGS := -target wasm32-wasi -O2 $(AVISOS) $(CPPFLAGS) -Wl,-z,stack-size=8388608

wasm: | build
	@for t in $(TESTES); do \
	  $(ZIG) cc $(WASM_FLAGS) $(FONTES) tests/teste_$$t.c -o build/teste_$$t.wasm -lm || exit 1; done
	$(ZIG) cc $(WASM_FLAGS) $(FONTES) app/ed.c -o build/ed.wasm -lm
	@cd build && for t in $(TESTES); do echo "== $$t"; \
	  $(NODE) --no-warnings ../ferramentas/executar-wasi.mjs teste_$$t.wasm || exit 1; done

limpar:
	rm -rf build
