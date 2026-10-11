#include "ed/memoria.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static long blocos_vivos = 0;

static void *verificar(void *p, size_t tamanho)
{
    if (p == NULL && tamanho > 0) {
        fprintf(stderr, "ed: memória insuficiente (%zu bytes)\n", tamanho);
        exit(EXIT_FAILURE);
    }
    return p;
}

void *ed_malloc(size_t tamanho)
{
    void *p = verificar(malloc(tamanho ? tamanho : 1), tamanho);
    blocos_vivos++;
    return p;
}

void *ed_calloc(size_t quantidade, size_t tamanho)
{
    void *p = verificar(calloc(quantidade ? quantidade : 1, tamanho ? tamanho : 1), quantidade * tamanho);
    blocos_vivos++;
    return p;
}

void *ed_realloc(void *p, size_t tamanho)
{
    if (p == NULL)
        return ed_malloc(tamanho);
    return verificar(realloc(p, tamanho ? tamanho : 1), tamanho);
}

void ed_free(void *p)
{
    if (p != NULL) {
        blocos_vivos--;
        free(p);
    }
}

char *ed_strndup(const char *s, size_t n)
{
    char *copia = ed_malloc(n + 1);
    memcpy(copia, s, n);
    copia[n] = '\0';
    return copia;
}

char *ed_strdup(const char *s)
{
    return ed_strndup(s, strlen(s));
}

long ed_memoria_blocos(void)
{
    return blocos_vivos;
}
