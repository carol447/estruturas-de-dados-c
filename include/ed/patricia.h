#ifndef ED_PATRICIA_H
#define ED_PATRICIA_H

/*
 * Árvore PATRICIA (Morrison, 1968; formulação de Knuth/Ziviani com nós internos
 * e externos), sobre os bits das chaves.
 *
 * - Nós internos guardam só o ÍNDICE do bit que decide o caminho
 *   (0 vai para a esquerda, 1 para a direita). Nós externos guardam as chaves.
 * - Não há nós com um único filho: n chaves ⇒ exatamente n folhas e n − 1 nós
 *   internos, independente do comprimento das chaves.
 * - Os índices de bit crescem estritamente do topo para baixo; a busca testa
 *   no máximo um bit por nível e só compara a chave inteira uma vez, na folha.
 * - Cada chave é vista como a sequência de bits de seus bytes (mais
 *   significativo primeiro) seguida do '\0' e de zeros: assim "bola" e
 *   "bolacha" diferem em algum bit, e o percurso em ordem dá a ordem
 *   lexicográfica por bytes (UTF-8 inclusive).
 *
 * A versão original do trabalho era uma árvore de prefixos por caracteres
 * (radix); ela foi corrigida e está em radix.h.
 */

#include <stddef.h>
#include <stdint.h>

typedef struct Patricia Patricia;

/* Retorna nonzero para interromper. */
typedef int (*pat_visitante)(const char *chave, int64_t valor, void *ctx);

Patricia *pat_criar(void);
void pat_destruir(Patricia *p);

/* 1 se inseriu, 0 se a chave já existia (o valor é atualizado). */
int pat_inserir(Patricia *p, const char *chave, int64_t valor);
/* 1 se encontrou. */
int pat_buscar(const Patricia *p, const char *chave, int64_t *valor);
/* 1 se removeu. */
int pat_remover(Patricia *p, const char *chave);

size_t pat_tamanho(const Patricia *p);
int pat_altura(const Patricia *p);
size_t pat_nos_internos(const Patricia *p);

/* Visita em ordem lexicográfica as chaves com o prefixo dado ("" = todas). Devolve quantas. */
size_t pat_prefixo(const Patricia *p, const char *prefixo, pat_visitante visitar, void *ctx);

/* Confere: índices crescentes, chaves do lado certo de cada bit, contagens. 1 = ok. */
int pat_verificar(const Patricia *p);

/* Bytes ocupados pelos nós e chaves (para comparar com a radix). */
size_t pat_memoria(const Patricia *p);

#endif
