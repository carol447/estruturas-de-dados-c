#ifndef ED_ORDENACAO_EXTERNA_H
#define ED_ORDENACAO_EXTERNA_H

/*
 * Ordenação externa de um arquivo binário de registros de aluno (pela
 * matrícula), usando só `memoria` registros na RAM.
 *
 * 1. Geração de corridas (runs) ordenadas:
 *    - OE_QUICKSORT: lê `memoria` registros, ordena com quicksort e grava;
 *      corridas de tamanho exatamente `memoria`.
 *    - OE_SELECAO: seleção por substituição com um heap de `memoria`
 *      registros; em entrada aleatória as corridas têm em média 2 × memoria
 *      (menos corridas ⇒ menos passadas de intercalação).
 * 2. Intercalação de `vias` corridas por vez com um heap mínimo, em passadas,
 *    até restar uma única corrida.
 *
 * O "QuickSort externo" da versão original ordenava os registros dentro de um
 * único nó em memória: isso é ordenação interna. Aqui o quicksort é usado do
 * jeito certo, para formar as corridas iniciais.
 */

#include <stdint.h>

#include "ed/registro.h"

typedef enum { OE_QUICKSORT, OE_SELECAO } OeMetodo;

typedef struct {
    int64_t registros;
    int64_t corridas_iniciais;
    int passadas;               /* passadas de intercalação */
    int64_t registros_lidos;    /* E/S total, em registros */
    int64_t registros_escritos;
    double maior_corrida_media; /* tamanho médio das corridas iniciais */
} OeEstatisticas;

/* Gera `n` alunos com matrículas distintas em ordem aleatória. */
int oe_gerar(const char *arquivo, int64_t n, uint64_t semente);

/* Ordena `entrada` em `saida`. Arquivos temporários ficam ao lado de `saida`. 0 = ok. */
int oe_ordenar(const char *entrada, const char *saida, int64_t memoria, OeMetodo metodo, int vias,
               OeEstatisticas *estatisticas);

/* 1 se o arquivo está em ordem crescente de matrícula sem repetições. */
int oe_verificar(const char *arquivo, int64_t *registros);

/* Quicksort interno usado na geração de corridas (mediana de três + inserção nos pequenos). */
void oe_quicksort(Aluno *v, int64_t n);

#endif
