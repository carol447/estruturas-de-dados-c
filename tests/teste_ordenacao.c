/* Testes da ordenação externa e do quicksort interno. */
#include <string.h>

#include "ed/ordenacao_externa.h"
#include "teste.h"

#define ENTRADA "teste_entrada.bin"
#define SAIDA "teste_saida.bin"

static void teste_quicksort(void)
{
    Rng rng;
    rng_iniciar(&rng, 9);
    int tamanhos[] = {0, 1, 2, 17, 100, 5000};
    for (int t = 0; t < 6; t++) {
        int n = tamanhos[t];
        Aluno *v = ed_malloc((size_t)(n ? n : 1) * sizeof *v);
        for (int padrao = 0; padrao < 4; padrao++) {
            for (int i = 0; i < n; i++) {
                int32_t m = padrao == 0 ? (int32_t)rng_ate(&rng, 1000)        /* com repetições */
                          : padrao == 1 ? i                                     /* já ordenado */
                          : padrao == 2 ? n - i                                 /* invertido */
                                        : 7;                                    /* todos iguais */
                aluno_aleatorio(&rng, m, &v[i]);
            }
            oe_quicksort(v, n);
            int ordenado = 1;
            for (int i = 1; i < n; i++)
                ordenado &= v[i - 1].matricula <= v[i].matricula;
            VERIFICAR(ordenado);
        }
        ed_free(v);
    }
}

static int conferir_conteudo(int64_t n)
{
    /* Mesmos registros (nome e notas) da entrada, agora em ordem. */
    FILE *a = fopen(ENTRADA, "rb"), *b = fopen(SAIDA, "rb");
    Aluno *orig = ed_malloc((size_t)(n ? n : 1) * sizeof *orig);
    for (int64_t i = 0; i < n; i++)
        aluno_ler(a, i, &orig[i]);
    oe_quicksort(orig, n);
    int ok = 1;
    for (int64_t i = 0; i < n && ok; i++) {
        Aluno x;
        ok = aluno_ler(b, i, &x) && x.matricula == orig[i].matricula && strcmp(x.nome, orig[i].nome) == 0 &&
             memcmp(x.notas, orig[i].notas, sizeof x.notas) == 0;
    }
    ed_free(orig);
    fclose(a);
    fclose(b);
    return ok;
}

static void teste_ordenacao_externa(void)
{
    int64_t tamanhos[] = {0, 1, 7, 100, 2345};
    int64_t memorias[] = {1, 3, 64, 5000};
    int vias[] = {2, 3, 16};
    for (int t = 0; t < 5; t++) {
        VERIFICAR_IGUAL(oe_gerar(ENTRADA, tamanhos[t], (uint64_t)t), 0);
        for (int m = 0; m < 4; m++)
            for (int v = 0; v < 3; v++)
                for (int metodo = 0; metodo < 2; metodo++) {
                    OeEstatisticas e;
                    int r = oe_ordenar(ENTRADA, SAIDA, memorias[m], (OeMetodo)metodo, vias[v], &e);
                    VERIFICAR_IGUAL(r, 0);
                    int64_t n = -1;
                    VERIFICAR(oe_verificar(SAIDA, &n));
                    VERIFICAR_IGUAL(n, tamanhos[t]);
                    VERIFICAR_IGUAL(e.registros, tamanhos[t]);
                    if (metodo == OE_QUICKSORT && tamanhos[t] > 0)
                        VERIFICAR_IGUAL(e.corridas_iniciais, (tamanhos[t] + memorias[m] - 1) / memorias[m]);
                }
        VERIFICAR(conferir_conteudo(tamanhos[t]));
    }
    remove(ENTRADA);
    remove(SAIDA);
}

static void teste_selecao_gera_corridas_de_2m(void)
{
    /* Em entrada aleatória a seleção por substituição produz corridas de ≈ 2 × memória. */
    VERIFICAR_IGUAL(oe_gerar(ENTRADA, 60000, 99), 0);
    OeEstatisticas q, s;
    VERIFICAR_IGUAL(oe_ordenar(ENTRADA, SAIDA, 1000, OE_QUICKSORT, 8, &q), 0);
    VERIFICAR_IGUAL(oe_ordenar(ENTRADA, SAIDA, 1000, OE_SELECAO, 8, &s), 0);
    VERIFICAR_IGUAL(q.corridas_iniciais, 60);
    VERIFICAR(s.maior_corrida_media > 1.8 * 1000 && s.maior_corrida_media < 2.2 * 1000);
    VERIFICAR(s.passadas <= q.passadas);
    /* Cada passada lê e grava tudo uma vez: E/S = (1 + passadas) × n registros em cada sentido. */
    VERIFICAR_IGUAL(q.registros_lidos, (1 + q.passadas) * 60000);
    VERIFICAR_IGUAL(q.registros_escritos, (1 + q.passadas) * 60000);
    VERIFICAR(oe_verificar(SAIDA, NULL));
    remove(ENTRADA);
    remove(SAIDA);
}

static void teste_entrada_ja_ordenada_gera_uma_corrida(void)
{
    VERIFICAR_IGUAL(oe_gerar(ENTRADA, 5000, 1), 0);
    VERIFICAR_IGUAL(oe_ordenar(ENTRADA, SAIDA, 100, OE_QUICKSORT, 4, NULL), 0);
    OeEstatisticas s;
    VERIFICAR_IGUAL(oe_ordenar(SAIDA, ENTRADA, 100, OE_SELECAO, 4, &s), 0);
    VERIFICAR_IGUAL(s.corridas_iniciais, 1);  /* nada sai de ordem: uma corrida só */
    VERIFICAR_IGUAL(s.passadas, 0);
    remove(ENTRADA);
    remove(SAIDA);
}

static void teste_argumentos_invalidos(void)
{
    VERIFICAR_IGUAL(oe_ordenar("nao_existe.bin", SAIDA, 10, OE_QUICKSORT, 2, NULL), -1);
    VERIFICAR_IGUAL(oe_ordenar(ENTRADA, SAIDA, 0, OE_QUICKSORT, 2, NULL), -1);
    VERIFICAR_IGUAL(oe_ordenar(ENTRADA, SAIDA, 10, OE_QUICKSORT, 1, NULL), -1);
}

int main(void)
{
    TESTE(teste_quicksort);
    TESTE(teste_ordenacao_externa);
    TESTE(teste_selecao_gera_corridas_de_2m);
    TESTE(teste_entrada_ja_ordenada_gera_uma_corrida);
    TESTE(teste_argumentos_invalidos);
    return teste_resultado();
}
