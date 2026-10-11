/*
 * Testes da PATRICIA (bits) e da árvore radix (caracteres). As duas recebem as mesmas
 * operações aleatórias e são comparadas com uma referência ingênua (vetor ordenado).
 */
#include <string.h>

#include "ed/patricia.h"
#include "ed/radix.h"
#include "ed/registro.h"
#include "teste.h"

/* As palavras do exemplo original (com acentos em UTF-8). */
static const char *PALAVRAS[] = {"bola", "estudante", "cadeira", "amor", "bola", "bolacha", "estuda", "amar",
                                 "bolo", "escola", "cadeia", "amaria", "bonita", "maçã", "maca"};
#define QTD_PALAVRAS (sizeof PALAVRAS / sizeof *PALAVRAS)

typedef struct {
    char itens[64][32];
    int n;
} Coleta;

static int coletar(const char *chave, int64_t valor, void *ctx)
{
    (void)valor;
    Coleta *c = ctx;
    if (c->n < 64)
        snprintf(c->itens[c->n++], 32, "%s", chave);
    return 0;
}

static void teste_exemplo_original(void)
{
    Patricia *p = pat_criar();
    Radix *r = rad_criar();
    int novas = 0;
    for (size_t i = 0; i < QTD_PALAVRAS; i++) {
        int np = pat_inserir(p, PALAVRAS[i], (int64_t)i);
        int nr = rad_inserir(r, PALAVRAS[i], (int64_t)i);
        VERIFICAR_IGUAL(np, nr);
        novas += np;
    }
    VERIFICAR_IGUAL(novas, 14);  /* "bola" aparece duas vezes */
    VERIFICAR_IGUAL(pat_tamanho(p), 14);
    VERIFICAR_IGUAL(rad_tamanho(r), 14);
    VERIFICAR_IGUAL(pat_nos_internos(p), 13);  /* n chaves ⇒ n − 1 nós internos */
    VERIFICAR(pat_verificar(p) && rad_verificar(r));
    VERIFICAR(pat_buscar(p, "maçã", NULL) && rad_buscar(r, "maçã", NULL));
    VERIFICAR(!pat_buscar(p, "bol", NULL) && !rad_buscar(r, "bol", NULL));
    VERIFICAR(!pat_buscar(p, "bolachas", NULL) && !rad_buscar(r, "bolachas", NULL));

    /* Ordem lexicográfica por bytes nas duas estruturas. */
    Coleta cp = {0}, cr = {0};
    pat_prefixo(p, "", coletar, &cp);
    rad_prefixo(r, "", coletar, &cr);
    const char *ordem[] = {"amar", "amaria", "amor", "bola", "bolacha", "bolo", "bonita", "cadeia", "cadeira",
                           "escola", "estuda", "estudante", "maca", "maçã"};
    VERIFICAR_IGUAL(cp.n, 14);
    VERIFICAR_IGUAL(cr.n, 14);
    for (int i = 0; i < 14; i++) {
        VERIFICAR(strcmp(cp.itens[i], ordem[i]) == 0);
        VERIFICAR(strcmp(cr.itens[i], ordem[i]) == 0);
    }

    /* Prefixos, inclusive terminando no meio de um rótulo da radix. */
    VERIFICAR_IGUAL(pat_prefixo(p, "bol", NULL, NULL), 3);
    VERIFICAR_IGUAL(rad_prefixo(r, "bol", NULL, NULL), 3);
    VERIFICAR_IGUAL(pat_prefixo(p, "estud", NULL, NULL), 2);
    VERIFICAR_IGUAL(rad_prefixo(r, "estud", NULL, NULL), 2);
    VERIFICAR_IGUAL(rad_prefixo(r, "estuda", NULL, NULL), 2);
    VERIFICAR_IGUAL(pat_prefixo(p, "x", NULL, NULL), 0);
    VERIFICAR_IGUAL(rad_prefixo(r, "bolx", NULL, NULL), 0);
    VERIFICAR_IGUAL(pat_prefixo(p, "ma", NULL, NULL), 2);

    /* As remoções do main original. */
    const char *remover[] = {"maçã", "maca", "escola", "estuda", "cadeira", "bolacha", "bonita", "amor", "amar"};
    for (size_t i = 0; i < 9; i++) {
        VERIFICAR(pat_remover(p, remover[i]));
        VERIFICAR(rad_remover(r, remover[i]));
        VERIFICAR(pat_verificar(p) && rad_verificar(r));
    }
    VERIFICAR(!pat_remover(p, "amar") && !rad_remover(r, "amar"));
    Coleta fp = {0}, fr = {0};
    pat_prefixo(p, "", coletar, &fp);
    rad_prefixo(r, "", coletar, &fr);
    const char *restantes[] = {"amaria", "bola", "bolo", "cadeia", "estudante"};
    VERIFICAR_IGUAL(fp.n, 5);
    VERIFICAR_IGUAL(fr.n, 5);
    for (int i = 0; i < 5; i++)
        VERIFICAR(strcmp(fp.itens[i], restantes[i]) == 0 && strcmp(fr.itens[i], restantes[i]) == 0);
    /* Na radix, "estud" + "a"/"ante" voltou a ser um único nó "estudante" depois das remoções:
     * restam amaria, bol (filhos a, o), cadeia e estudante. */
    VERIFICAR_IGUAL(rad_nos(r), 6);
    pat_destruir(p);
    rad_destruir(r);
}

static void teste_chave_vazia_e_prefixos_entre_si(void)
{
    Patricia *p = pat_criar();
    Radix *r = rad_criar();
    const char *ks[] = {"", "a", "ab", "abc", "abcd", "b"};
    for (int i = 0; i < 6; i++) {
        VERIFICAR_IGUAL(pat_inserir(p, ks[i], i), 1);
        VERIFICAR_IGUAL(rad_inserir(r, ks[i], i), 1);
    }
    VERIFICAR(pat_verificar(p) && rad_verificar(r));
    for (int i = 0; i < 6; i++) {
        int64_t v1 = -1, v2 = -1;
        VERIFICAR(pat_buscar(p, ks[i], &v1) && rad_buscar(r, ks[i], &v2));
        VERIFICAR_IGUAL(v1, i);
        VERIFICAR_IGUAL(v2, i);
    }
    VERIFICAR_IGUAL(pat_prefixo(p, "ab", NULL, NULL), 3);
    VERIFICAR_IGUAL(rad_prefixo(r, "ab", NULL, NULL), 3);
    VERIFICAR(pat_remover(p, "ab") && rad_remover(r, "ab"));
    VERIFICAR(pat_buscar(p, "abc", NULL) && rad_buscar(r, "abc", NULL));
    VERIFICAR(pat_verificar(p) && rad_verificar(r));
    VERIFICAR(pat_remover(p, "") && rad_remover(r, ""));
    VERIFICAR_IGUAL(pat_tamanho(p), 4);
    pat_destruir(p);
    rad_destruir(r);
}

/* ---------------------------------------------------------------- aleatório contra referência */
#define UNIVERSO 1500

static char universo[UNIVERSO][16];
static int presente[UNIVERSO];
static int64_t valor_ref[UNIVERSO];

static void gerar_universo(Rng *rng)
{
    /* Alfabeto pequeno e comprimentos variados: muitos prefixos compartilhados. */
    for (int i = 0; i < UNIVERSO; i++) {
        int tam;
        do {
            tam = 1 + (int)rng_ate(rng, 9);
            for (int j = 0; j < tam; j++)
                universo[i][j] = "abc\xc3\xa7"[rng_ate(rng, 5)];  /* inclui os bytes de "ç" */
            universo[i][tam] = '\0';
            int repetida = 0;
            for (int k = 0; k < i && !repetida; k++)
                repetida = strcmp(universo[k], universo[i]) == 0;
            if (!repetida)
                break;
        } while (1);
    }
}

static int comparar_str(const void *a, const void *b)
{
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

typedef struct {
    const char **esperado;
    int n, pos, erros;
} Conferencia;

static int conferir(const char *chave, int64_t valor, void *ctx)
{
    (void)valor;
    Conferencia *c = ctx;
    if (c->pos >= c->n || strcmp(c->esperado[c->pos], chave) != 0)
        c->erros++;
    c->pos++;
    return 0;
}

static void conferir_prefixo(Patricia *p, Radix *r, const char *prefixo)
{
    const char *esperado[UNIVERSO];
    int n = 0;
    size_t tp = strlen(prefixo);
    for (int i = 0; i < UNIVERSO; i++)
        if (presente[i] && strncmp(universo[i], prefixo, tp) == 0)
            esperado[n++] = universo[i];
    qsort(esperado, (size_t)n, sizeof *esperado, comparar_str);
    Conferencia cp = {esperado, n, 0, 0}, cr = {esperado, n, 0, 0};
    VERIFICAR_IGUAL(pat_prefixo(p, prefixo, conferir, &cp), n);
    VERIFICAR_IGUAL(rad_prefixo(r, prefixo, conferir, &cr), n);
    VERIFICAR_IGUAL(cp.erros, 0);
    VERIFICAR_IGUAL(cr.erros, 0);
}

static void teste_aleatorio_contra_referencia(void)
{
    Rng rng;
    rng_iniciar(&rng, 42);
    gerar_universo(&rng);
    memset(presente, 0, sizeof presente);
    Patricia *p = pat_criar();
    Radix *r = rad_criar();
    int falhas_antes = teste_falhas;
    for (int op = 0; op < 40000 && teste_falhas == falhas_antes; op++) {
        int i = (int)rng_ate(&rng, UNIVERSO);
        int tipo = (int)rng_ate(&rng, 10);
        if (tipo < 5) {
            int64_t v = (int64_t)rng_ate(&rng, 1000000);
            VERIFICAR_IGUAL(pat_inserir(p, universo[i], v), !presente[i]);
            VERIFICAR_IGUAL(rad_inserir(r, universo[i], v), !presente[i]);
            presente[i] = 1;
            valor_ref[i] = v;
        } else if (tipo < 8) {
            VERIFICAR_IGUAL(pat_remover(p, universo[i]), presente[i]);
            VERIFICAR_IGUAL(rad_remover(r, universo[i]), presente[i]);
            presente[i] = 0;
        } else {
            int64_t v1 = -1, v2 = -1;
            VERIFICAR_IGUAL(pat_buscar(p, universo[i], &v1), presente[i]);
            VERIFICAR_IGUAL(rad_buscar(r, universo[i], &v2), presente[i]);
            if (presente[i]) {
                VERIFICAR_IGUAL(v1, valor_ref[i]);
                VERIFICAR_IGUAL(v2, valor_ref[i]);
            }
        }
        if (op % 500 == 0) {
            VERIFICAR(pat_verificar(p));
            VERIFICAR(rad_verificar(r));
            char prefixo[4] = {0};
            int tp = (int)rng_ate(&rng, 4);
            for (int j = 0; j < tp; j++)
                prefixo[j] = "abc"[rng_ate(&rng, 3)];
            conferir_prefixo(p, r, prefixo);
        }
    }
    int n = 0;
    for (int i = 0; i < UNIVERSO; i++)
        n += presente[i];
    VERIFICAR_IGUAL(pat_tamanho(p), n);
    VERIFICAR_IGUAL(rad_tamanho(r), n);
    VERIFICAR_IGUAL(pat_nos_internos(p), n ? n - 1 : 0);
    conferir_prefixo(p, r, "");
    pat_destruir(p);
    rad_destruir(r);
}

static void teste_altura_patricia_limitada_pelos_bits(void)
{
    /* Chaves de comprimento L: altura ≤ 8·(L+1) + 1, mesmo com muitas chaves. */
    Patricia *p = pat_criar();
    char chave[8];
    for (int i = 0; i < 4096; i++) {
        snprintf(chave, sizeof chave, "%04x", i);
        pat_inserir(p, chave, i);
    }
    VERIFICAR(pat_verificar(p));
    VERIFICAR(pat_altura(p) <= 8 * 5 + 1);
    VERIFICAR_IGUAL(pat_nos_internos(p), 4095);
    for (int i = 0; i < 4096; i += 2) {
        snprintf(chave, sizeof chave, "%04x", i);
        VERIFICAR(pat_remover(p, chave));
    }
    VERIFICAR(pat_verificar(p));
    VERIFICAR_IGUAL(pat_tamanho(p), 2048);
    pat_destruir(p);
}

int main(void)
{
    TESTE(teste_exemplo_original);
    TESTE(teste_chave_vazia_e_prefixos_entre_si);
    TESTE(teste_aleatorio_contra_referencia);
    TESTE(teste_altura_patricia_limitada_pelos_bits);
    return teste_resultado();
}
