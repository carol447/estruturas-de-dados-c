#ifndef ED_MEMORIA_H
#define ED_MEMORIA_H

/*
 * Alocação com contagem de blocos vivos.
 *
 * Todas as estruturas alocam por aqui. Os testes conferem que, depois de
 * destruir cada estrutura, ed_memoria_blocos() volta ao valor anterior:
 * qualquer vazamento faz o teste falhar (sem depender de valgrind/ASan).
 * Falta de memória encerra o programa com mensagem (não há como continuar
 * de forma útil nos exemplos).
 */

#include <stddef.h>

void *ed_malloc(size_t tamanho);
void *ed_calloc(size_t quantidade, size_t tamanho);
void *ed_realloc(void *p, size_t tamanho);
void ed_free(void *p);
char *ed_strdup(const char *s);
char *ed_strndup(const char *s, size_t n);

/* Blocos alocados e ainda não liberados. */
long ed_memoria_blocos(void);

#endif
