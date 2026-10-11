#ifndef ED_TESTE_H
#define ED_TESTE_H

/* Mini-framework de testes: cada arquivo de teste é um executável. */

#include <stdio.h>
#include <stdlib.h>

#include "ed/memoria.h"

static int teste_falhas = 0;
static int teste_verificacoes = 0;

#define VERIFICAR(cond)                                                                    \
    do {                                                                                   \
        teste_verificacoes++;                                                              \
        if (!(cond)) {                                                                     \
            teste_falhas++;                                                                \
            fprintf(stderr, "  FALHOU %s:%d: %s\n", __FILE__, __LINE__, #cond);           \
        }                                                                                  \
    } while (0)

#define VERIFICAR_IGUAL(a, b)                                                              \
    do {                                                                                   \
        long long va_ = (long long)(a), vb_ = (long long)(b);                              \
        teste_verificacoes++;                                                              \
        if (va_ != vb_) {                                                                  \
            teste_falhas++;                                                                \
            fprintf(stderr, "  FALHOU %s:%d: %s == %s (%lld != %lld)\n", __FILE__, __LINE__, \
                    #a, #b, va_, vb_);                                                     \
        }                                                                                  \
    } while (0)

/* Executa um teste e confere que ele não vazou memória. */
#define TESTE(fn)                                                                          \
    do {                                                                                   \
        long antes_ = ed_memoria_blocos();                                                 \
        int falhas_ = teste_falhas;                                                        \
        fn();                                                                              \
        long depois_ = ed_memoria_blocos();                                                \
        if (depois_ != antes_) {                                                           \
            teste_falhas++;                                                                \
            fprintf(stderr, "  FALHOU %s: %ld bloco(s) de memória não liberado(s)\n", #fn, \
                    depois_ - antes_);                                                     \
        }                                                                                  \
        printf("%s %s\n", teste_falhas == falhas_ ? "ok   " : "FALHA", #fn);                \
    } while (0)

static int teste_resultado(void)
{
    printf("\n%d verificações, %d falha(s)\n", teste_verificacoes, teste_falhas);
    return teste_falhas ? EXIT_FAILURE : EXIT_SUCCESS;
}

#endif
