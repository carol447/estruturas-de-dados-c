#ifndef ED_RADIX_H
#define ED_RADIX_H

/*
 * Árvore de prefixos compactada (radix tree) por bytes: versão corrigida da
 * "árvore Patrícia" original do trabalho, que na verdade era desta família.
 *
 * - Cada aresta carrega um rótulo (pedaço da palavra); um nó que só tem um
 *   filho e não termina palavra é fundido com o filho (compactação).
 * - Os filhos ficam numa lista ligada ordenada pelo primeiro byte do rótulo
 *   (a "lista de irmãos" do original), então o percurso sai em ordem.
 *
 * Correções em relação ao original: o resultado de realloc era descartado;
 * strcpy com origem e destino sobrepostos (comportamento indefinido); o
 * retorno da inserção recursiva no caso de irmão com mesma inicial se perdia;
 * a remoção não refundia nós; nada era liberado; printf de depuração.
 */

#include <stddef.h>
#include <stdint.h>

typedef struct Radix Radix;
typedef int (*rad_visitante)(const char *chave, int64_t valor, void *ctx);

Radix *rad_criar(void);
void rad_destruir(Radix *r);

int rad_inserir(Radix *r, const char *chave, int64_t valor);   /* 1 = nova, 0 = atualizada */
int rad_buscar(const Radix *r, const char *chave, int64_t *valor);
int rad_remover(Radix *r, const char *chave);

size_t rad_tamanho(const Radix *r);
size_t rad_nos(const Radix *r);          /* sem contar a raiz */
int rad_altura(const Radix *r);
size_t rad_prefixo(const Radix *r, const char *prefixo, rad_visitante visitar, void *ctx);
int rad_verificar(const Radix *r);
size_t rad_memoria(const Radix *r);

#endif
