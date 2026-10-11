/*
 * Testes da árvore B+ em disco. A referência é um vetor de presença: cada operação
 * aleatória é aplicada às duas estruturas e a árvore é verificada com bp_verificar.
 */
#include <string.h>

#include "ed/bplus.h"
#include "ed/registro.h"
#include "teste.h"

#define ARQ "teste_bplus.idx"
#define UNIVERSO 3000

static char msg[256];

static void verificar_arvore(BPlus *t)
{
    int r = bp_verificar(t, msg, sizeof msg);
    VERIFICAR_IGUAL(r, BP_OK);
    if (r != BP_OK)
        fprintf(stderr, "    %s\n", msg);
}

static void teste_vazia_e_basico(void)
{
    BPlus *t;
    VERIFICAR_IGUAL(bp_criar(ARQ, 2, &t), BP_ERRO_ARGUMENTO);
    VERIFICAR_IGUAL(bp_criar(ARQ, BP_ORDEM_MAX + 1, &t), BP_ERRO_ARGUMENTO);
    VERIFICAR_IGUAL(bp_criar(ARQ, 3, &t), BP_OK);
    bp_valor v;
    VERIFICAR_IGUAL(bp_buscar(t, 5, &v), BP_NAO_ENCONTRADA);
    VERIFICAR_IGUAL(bp_remover(t, 5), BP_NAO_ENCONTRADA);
    VERIFICAR_IGUAL(bp_intervalo(t, 0, 100, NULL, NULL), 0);
    verificar_arvore(t);
    VERIFICAR_IGUAL(bp_inserir(t, 10, 100), BP_OK);
    VERIFICAR_IGUAL(bp_inserir(t, 10, 999), BP_DUPLICADA);
    VERIFICAR_IGUAL(bp_buscar(t, 10, &v), BP_OK);
    VERIFICAR_IGUAL(v, 100);
    VERIFICAR_IGUAL(bp_atualizar(t, 10, 7), BP_OK);
    bp_buscar(t, 10, &v);
    VERIFICAR_IGUAL(v, 7);
    VERIFICAR_IGUAL(bp_remover(t, 10), BP_OK);
    verificar_arvore(t);
    VERIFICAR_IGUAL(bp_fechar(t), BP_OK);
    remove(ARQ);
}

static void teste_crescimento_pela_raiz(void)
{
    BPlus *t;
    bp_criar(ARQ, 3, &t);
    BPlusInfo info;
    for (int k = 1; k <= 4; k++)
        bp_inserir(t, k * 10, k);
    bp_info(t, &info);
    VERIFICAR_IGUAL(info.altura, 2);  /* 4 chaves com ordem 3: a folha divide, a raiz vira interna */
    VERIFICAR_IGUAL(info.folhas, 2);
    for (int k = 5; k <= 200; k++)
        bp_inserir(t, k * 10, k);
    verificar_arvore(t);
    bp_info(t, &info);
    VERIFICAR(info.altura >= 4);
    /* Remover tudo devolve a árvore a uma única folha e todas as páginas à lista de livres. */
    for (int k = 1; k <= 200; k++)
        VERIFICAR_IGUAL(bp_remover(t, k * 10), BP_OK);
    verificar_arvore(t);
    bp_info(t, &info);
    VERIFICAR_IGUAL(info.altura, 1);
    VERIFICAR_IGUAL(info.chaves, 0);
    VERIFICAR_IGUAL(info.paginas_livres, info.paginas - 2);
    bp_fechar(t);
    remove(ARQ);
}

static void operacoes_aleatorias(int ordem, uint64_t semente, int operacoes)
{
    BPlus *t;
    VERIFICAR_IGUAL(bp_criar(ARQ, ordem, &t), BP_OK);
    static signed char presente[UNIVERSO];
    memset(presente, 0, sizeof presente);
    Rng rng;
    rng_iniciar(&rng, semente);
    int erros_antes = teste_falhas;
    for (int op = 0; op < operacoes && teste_falhas == erros_antes; op++) {
        int k = (int)rng_ate(&rng, UNIVERSO);
        int tipo = (int)rng_ate(&rng, 10);
        if (tipo < 5) {
            int r = bp_inserir(t, k, (bp_valor)k * 7);
            VERIFICAR_IGUAL(r, presente[k] ? BP_DUPLICADA : BP_OK);
            presente[k] = 1;
        } else if (tipo < 8) {
            int r = bp_remover(t, k);
            VERIFICAR_IGUAL(r, presente[k] ? BP_OK : BP_NAO_ENCONTRADA);
            presente[k] = 0;
        } else {
            bp_valor v = -1;
            int r = bp_buscar(t, k, &v);
            VERIFICAR_IGUAL(r, presente[k] ? BP_OK : BP_NAO_ENCONTRADA);
            if (presente[k])
                VERIFICAR_IGUAL(v, (bp_valor)k * 7);
        }
        if (op % 97 == 0)
            verificar_arvore(t);
    }
    verificar_arvore(t);
    BPlusInfo info;
    bp_info(t, &info);
    int esperado = 0;
    for (int k = 0; k < UNIVERSO; k++)
        esperado += presente[k];
    VERIFICAR_IGUAL(info.chaves, esperado);

    /* Persistência: fechar, reabrir e conferir tudo de novo. */
    VERIFICAR_IGUAL(bp_fechar(t), BP_OK);
    VERIFICAR_IGUAL(bp_abrir(ARQ, &t), BP_OK);
    verificar_arvore(t);
    for (int k = 0; k < UNIVERSO; k++)
        VERIFICAR_IGUAL(bp_buscar(t, k, NULL), presente[k] ? BP_OK : BP_NAO_ENCONTRADA);
    bp_fechar(t);
    remove(ARQ);
}

static void teste_aleatorio_ordem_3(void) { operacoes_aleatorias(3, 1, 20000); }
static void teste_aleatorio_ordem_4(void) { operacoes_aleatorias(4, 2, 20000); }
static void teste_aleatorio_ordem_5(void) { operacoes_aleatorias(5, 3, 20000); }
static void teste_aleatorio_ordem_16(void) { operacoes_aleatorias(16, 4, 20000); }
static void teste_aleatorio_ordem_max(void) { operacoes_aleatorias(BP_ORDEM_MAX, 5, 20000); }

typedef struct {
    int64_t n;
    int32_t atual;
    int32_t passo;
} Seq;

static int fonte_seq(void *ctx, bp_chave *c, bp_valor *v)
{
    Seq *s = ctx;
    if (s->n-- <= 0)
        return 0;
    *c = s->atual;
    *v = s->atual * 2;
    s->atual += s->passo;
    return 1;
}

typedef struct {
    int32_t esperado;
    int32_t passo;
    int erros;
} Coleta;

static int conferir_visita(bp_chave c, bp_valor v, void *ctx)
{
    Coleta *col = ctx;
    if (c != col->esperado || v != (bp_valor)c * 2)
        col->erros++;
    col->esperado += col->passo;
    return 0;
}

static void teste_carga_em_lote_e_intervalo(void)
{
    int ordens[] = {3, 4, 7, 64};
    int64_t tamanhos[] = {1, 2, 3, 4, 5, 9, 100, 1001, 50000};
    double preench[] = {0.5, 0.7, 1.0};
    for (size_t o = 0; o < sizeof ordens / sizeof *ordens; o++)
        for (size_t s = 0; s < sizeof tamanhos / sizeof *tamanhos; s++)
            for (size_t p = 0; p < 3; p++) {
                BPlus *t;
                bp_criar(ARQ, ordens[o], &t);
                Seq seq = {tamanhos[s], 0, 3};
                int r = bp_carregar(t, tamanhos[s], fonte_seq, &seq, preench[p]);
                VERIFICAR_IGUAL(r, BP_OK);
                if (bp_verificar(t, msg, sizeof msg) != BP_OK) {
                    fprintf(stderr, "    ordem %d, n %lld, preench %.1f: %s\n", ordens[o],
                            (long long)tamanhos[s], preench[p], msg);
                    teste_falhas++;
                }
                Coleta col = {30, 3, 0};
                int64_t visitados = bp_intervalo(t, 29, 3 * 40 + 1, conferir_visita, &col);
                int64_t esperado_vis = tamanhos[s] > 10 ? (tamanhos[s] > 40 ? 31 : tamanhos[s] - 10) : 0;
                VERIFICAR_IGUAL(visitados, esperado_vis);
                VERIFICAR_IGUAL(col.erros, 0);
                /* Depois da carga a árvore aceita inserções e remoções normalmente. */
                VERIFICAR_IGUAL(bp_inserir(t, 1, 2), BP_OK);
                VERIFICAR_IGUAL(bp_remover(t, 0), BP_OK);
                verificar_arvore(t);
                bp_fechar(t);
            }
    remove(ARQ);
}

static void teste_carga_rejeita_entrada_fora_de_ordem(void)
{
    BPlus *t;
    bp_criar(ARQ, 4, &t);
    Seq seq = {10, 100, -1};  /* decrescente */
    VERIFICAR_IGUAL(bp_carregar(t, 10, fonte_seq, &seq, 1.0), BP_ERRO_ARGUMENTO);
    bp_fechar(t);
    bp_criar(ARQ, 4, &t);
    bp_inserir(t, 1, 1);
    Seq seq2 = {10, 0, 1};
    VERIFICAR_IGUAL(bp_carregar(t, 10, fonte_seq, &seq2, 1.0), BP_ERRO_ARGUMENTO);  /* árvore não vazia */
    bp_fechar(t);
    remove(ARQ);
}

static void teste_custo_de_busca_logaritmico(void)
{
    BPlus *t;
    bp_criar(ARQ, 100, &t);
    Seq seq = {200000, 0, 1};
    bp_carregar(t, 200000, fonte_seq, &seq, 1.0);
    BPlusInfo info;
    bp_info(t, &info);
    VERIFICAR_IGUAL(info.altura, 3);  /* 200 mil chaves, até 101 filhos por nó */
    bp_zerar_contadores(t);
    for (int k = 0; k < 1000; k++)
        bp_buscar(t, k * 199, NULL);
    bp_info(t, &info);
    VERIFICAR_IGUAL(info.leituras, 3000);  /* exatamente uma página por nível */
    bp_fechar(t);
    remove(ARQ);
}

static void teste_arquivo_invalido(void)
{
    FILE *f = fopen(ARQ, "wb");
    fputs("isto não é um índice", f);
    fclose(f);
    BPlus *t;
    VERIFICAR_IGUAL(bp_abrir(ARQ, &t), BP_ERRO_FORMATO);
    VERIFICAR_IGUAL(bp_abrir("nao_existe.idx", &t), BP_ERRO_IO);
    remove(ARQ);
}

static void teste_ocupacao_minima_documentada(void)
{
    /* Folhas: ⌊(ordem+1)/2⌋; internos: ⌊ordem/2⌋ (garantem que fusões cabem num nó). */
    int ordens[] = {3, 4, 5, 6, 64, 65};
    for (int i = 0; i < 6; i++) {
        BPlus *t;
        bp_criar(ARQ, ordens[i], &t);
        BPlusInfo info;
        bp_info(t, &info);
        VERIFICAR_IGUAL(info.min_folha, (ordens[i] + 1) / 2);
        VERIFICAR_IGUAL(info.min_interno, ordens[i] / 2);
        VERIFICAR(2 * info.min_folha - 1 <= ordens[i]);       /* folha subocupada + irmã mínima cabem */
        VERIFICAR(2 * info.min_interno <= ordens[i]);         /* idem para internos, com o separador */
        bp_fechar(t);
    }
    remove(ARQ);
}

static void teste_verificador_detecta_corrupcao(void)
{
    BPlus *t;
    bp_criar(ARQ, 4, &t);
    for (int k = 0; k < 100; k++)
        bp_inserir(t, k, k);
    bp_fechar(t);
    /* Troca duas chaves de lugar dentro da página 1 (uma folha): ordem quebrada. */
    FILE *f = fopen(ARQ, "r+b");
    unsigned char a[4], b[4];
    fseek(f, 4096 + 16, SEEK_SET);
    fread(a, 1, 4, f);
    fread(b, 1, 4, f);
    fseek(f, 4096 + 16, SEEK_SET);
    fwrite(b, 1, 4, f);
    fwrite(a, 1, 4, f);
    fclose(f);
    VERIFICAR_IGUAL(bp_abrir(ARQ, &t), BP_OK);
    VERIFICAR_IGUAL(bp_verificar(t, msg, sizeof msg), BP_ERRO_INVARIANTE);
    VERIFICAR(strstr(msg, "fora de ordem") != NULL);
    bp_fechar(t);
    remove(ARQ);
}

int main(void)
{
    TESTE(teste_vazia_e_basico);
    TESTE(teste_crescimento_pela_raiz);
    TESTE(teste_aleatorio_ordem_3);
    TESTE(teste_aleatorio_ordem_4);
    TESTE(teste_aleatorio_ordem_5);
    TESTE(teste_aleatorio_ordem_16);
    TESTE(teste_aleatorio_ordem_max);
    TESTE(teste_carga_em_lote_e_intervalo);
    TESTE(teste_carga_rejeita_entrada_fora_de_ordem);
    TESTE(teste_custo_de_busca_logaritmico);
    TESTE(teste_arquivo_invalido);
    TESTE(teste_ocupacao_minima_documentada);
    TESTE(teste_verificador_detecta_corrupcao);
    return teste_resultado();
}
