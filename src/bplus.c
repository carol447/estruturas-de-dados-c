#include "ed/bplus.h"

#include <inttypes.h>
#include <math.h>
#include <stdarg.h>
#include <string.h>

#include "ed/arquivo.h"
#include "ed/memoria.h"

#define DIVIDIU 10          /* retorno interno: o nó foi dividido */
#define TIPO_LIVRE 0
#define TIPO_FOLHA 1
#define TIPO_INTERNO 2
#define VERSAO 1
static const char MAGICO[8] = {'E', 'D', 'B', 'P', 'L', 'U', 'S', '1'};

struct BPlus {
    FILE *f;
    int ordem;
    int min_folha;
    int min_interno;
    uint32_t raiz;
    int altura;
    uint32_t paginas;
    uint32_t livre;          /* primeira página da lista de livres (0 = nenhuma) */
    uint32_t qtd_livres;
    int64_t chaves;
    int cabecalho_sujo;
    uint64_t leituras;
    uint64_t escritas;
};

/* Nó em memória. Os vetores têm uma posição a mais para o estouro temporário antes da divisão. */
typedef struct {
    uint32_t pagina;
    int folha;
    int n;
    uint32_t prox;           /* folhas: próxima e anterior (0 = nenhuma) */
    uint32_t ant;
    bp_chave chaves[BP_ORDEM_MAX + 1];
    bp_valor valores[BP_ORDEM_MAX + 1];
    uint32_t filhos[BP_ORDEM_MAX + 2];
} No;

/* ---------------------------------------------------------------- bytes little-endian */
static void u32_para(uint8_t *p, uint32_t v)
{
    for (int i = 0; i < 4; i++)
        p[i] = (uint8_t)(v >> (8 * i));
}

static uint32_t u32_de(const uint8_t *p)
{
    uint32_t v = 0;
    for (int i = 0; i < 4; i++)
        v |= (uint32_t)p[i] << (8 * i);
    return v;
}

static void u64_para(uint8_t *p, uint64_t v)
{
    for (int i = 0; i < 8; i++)
        p[i] = (uint8_t)(v >> (8 * i));
}

static uint64_t u64_de(const uint8_t *p)
{
    uint64_t v = 0;
    for (int i = 0; i < 8; i++)
        v |= (uint64_t)p[i] << (8 * i);
    return v;
}

/* ---------------------------------------------------------------- páginas */
static int ler_pagina(BPlus *t, uint32_t id, uint8_t *buf)
{
    if (id >= t->paginas || ed_fseek(t->f, (int64_t)id * BP_PAGINA) != 0 || fread(buf, BP_PAGINA, 1, t->f) != 1)
        return BP_ERRO_IO;
    t->leituras++;
    return BP_OK;
}

static int escrever_pagina(BPlus *t, uint32_t id, const uint8_t *buf)
{
    if (ed_fseek(t->f, (int64_t)id * BP_PAGINA) != 0 || fwrite(buf, BP_PAGINA, 1, t->f) != 1)
        return BP_ERRO_IO;
    t->escritas++;
    return BP_OK;
}

static int salvar_cabecalho(BPlus *t)
{
    uint8_t buf[BP_PAGINA] = {0};
    memcpy(buf, MAGICO, 8);
    u32_para(buf + 8, VERSAO);
    u32_para(buf + 12, BP_PAGINA);
    u32_para(buf + 16, (uint32_t)t->ordem);
    u32_para(buf + 20, t->raiz);
    u32_para(buf + 24, (uint32_t)t->altura);
    u32_para(buf + 28, t->paginas);
    u32_para(buf + 32, t->livre);
    u32_para(buf + 36, t->qtd_livres);
    u64_para(buf + 40, (uint64_t)t->chaves);
    int r = escrever_pagina(t, 0, buf);
    if (r == BP_OK)
        t->cabecalho_sujo = 0;
    return r;
}

static int carregar(BPlus *t, uint32_t id, No *no)
{
    uint8_t buf[BP_PAGINA];
    int r = ler_pagina(t, id, buf);
    if (r != BP_OK)
        return r;
    if (buf[0] != TIPO_FOLHA && buf[0] != TIPO_INTERNO)
        return BP_ERRO_FORMATO;
    no->pagina = id;
    no->folha = buf[0] == TIPO_FOLHA;
    no->n = (int)u32_de(buf + 4);
    if (no->n < 0 || no->n > t->ordem)
        return BP_ERRO_FORMATO;
    no->prox = u32_de(buf + 8);
    no->ant = u32_de(buf + 12);
    const uint8_t *p = buf + 16;
    for (int i = 0; i < no->n; i++)
        no->chaves[i] = (bp_chave)u32_de(p + 4 * i);
    p += 4 * t->ordem;
    if (no->folha) {
        for (int i = 0; i < no->n; i++)
            no->valores[i] = (bp_valor)u64_de(p + 8 * i);
    } else {
        for (int i = 0; i <= no->n; i++)
            no->filhos[i] = u32_de(p + 4 * i);
    }
    return BP_OK;
}

static int salvar(BPlus *t, const No *no)
{
    uint8_t buf[BP_PAGINA] = {0};
    buf[0] = no->folha ? TIPO_FOLHA : TIPO_INTERNO;
    u32_para(buf + 4, (uint32_t)no->n);
    u32_para(buf + 8, no->prox);
    u32_para(buf + 12, no->ant);
    uint8_t *p = buf + 16;
    for (int i = 0; i < no->n; i++)
        u32_para(p + 4 * i, (uint32_t)no->chaves[i]);
    p += 4 * t->ordem;
    if (no->folha) {
        for (int i = 0; i < no->n; i++)
            u64_para(p + 8 * i, (uint64_t)no->valores[i]);
    } else {
        for (int i = 0; i <= no->n; i++)
            u32_para(p + 4 * i, no->filhos[i]);
    }
    return escrever_pagina(t, no->pagina, buf);
}

static uint32_t nova_pagina(BPlus *t)
{
    t->cabecalho_sujo = 1;
    if (t->livre != 0) {
        uint8_t buf[BP_PAGINA];
        uint32_t id = t->livre;
        if (ler_pagina(t, id, buf) != BP_OK)
            return 0;
        t->livre = u32_de(buf + 4);
        t->qtd_livres--;
        return id;
    }
    return t->paginas++;
}

static int liberar_pagina(BPlus *t, uint32_t id)
{
    uint8_t buf[BP_PAGINA] = {0};
    buf[0] = TIPO_LIVRE;
    u32_para(buf + 4, t->livre);
    t->livre = id;
    t->qtd_livres++;
    t->cabecalho_sujo = 1;
    return escrever_pagina(t, id, buf);
}

static void novo_no(No *no, uint32_t pagina, int folha)
{
    no->pagina = pagina;
    no->folha = folha;
    no->n = 0;
    no->prox = no->ant = 0;
}

/* ---------------------------------------------------------------- criar / abrir / fechar */
static void calcular_minimos(BPlus *t)
{
    t->min_folha = (t->ordem + 1) / 2;
    t->min_interno = t->ordem / 2;
}

int bp_criar(const char *arquivo, int ordem, BPlus **arvore)
{
    if (ordem < BP_ORDEM_MIN || ordem > BP_ORDEM_MAX)
        return BP_ERRO_ARGUMENTO;
    FILE *f = fopen(arquivo, "w+b");
    if (f == NULL)
        return BP_ERRO_IO;
    BPlus *t = ed_calloc(1, sizeof *t);
    t->f = f;
    t->ordem = ordem;
    calcular_minimos(t);
    t->paginas = 2;
    t->raiz = 1;
    t->altura = 1;
    No raiz;
    novo_no(&raiz, 1, 1);
    if (salvar_cabecalho(t) != BP_OK || salvar(t, &raiz) != BP_OK) {
        fclose(f);
        ed_free(t);
        return BP_ERRO_IO;
    }
    *arvore = t;
    return BP_OK;
}

int bp_abrir(const char *arquivo, BPlus **arvore)
{
    FILE *f = fopen(arquivo, "r+b");
    if (f == NULL)
        return BP_ERRO_IO;
    uint8_t buf[BP_PAGINA];
    if (fread(buf, BP_PAGINA, 1, f) != 1) {
        fclose(f);
        return BP_ERRO_FORMATO;
    }
    if (memcmp(buf, MAGICO, 8) != 0 || u32_de(buf + 8) != VERSAO || u32_de(buf + 12) != BP_PAGINA) {
        fclose(f);
        return BP_ERRO_FORMATO;
    }
    BPlus *t = ed_calloc(1, sizeof *t);
    t->f = f;
    t->ordem = (int)u32_de(buf + 16);
    t->raiz = u32_de(buf + 20);
    t->altura = (int)u32_de(buf + 24);
    t->paginas = u32_de(buf + 28);
    t->livre = u32_de(buf + 32);
    t->qtd_livres = u32_de(buf + 36);
    t->chaves = (int64_t)u64_de(buf + 40);
    int64_t tamanho = ed_tamanho_arquivo(f);
    if (t->ordem < BP_ORDEM_MIN || t->ordem > BP_ORDEM_MAX || t->raiz == 0 || t->raiz >= t->paginas ||
        tamanho < (int64_t)t->paginas * BP_PAGINA) {
        fclose(f);
        ed_free(t);
        return BP_ERRO_FORMATO;
    }
    calcular_minimos(t);
    *arvore = t;
    return BP_OK;
}

int bp_sincronizar(BPlus *t)
{
    int r = t->cabecalho_sujo ? salvar_cabecalho(t) : BP_OK;
    if (r == BP_OK && fflush(t->f) != 0)
        r = BP_ERRO_IO;
    return r;
}

int bp_fechar(BPlus *t)
{
    if (t == NULL)
        return BP_OK;
    int r = bp_sincronizar(t);
    if (fclose(t->f) != 0 && r == BP_OK)
        r = BP_ERRO_IO;
    ed_free(t);
    return r;
}

/* ---------------------------------------------------------------- busca */
/* Primeira posição com chave >= c. */
static int limite_inferior(const No *no, bp_chave c)
{
    int lo = 0, hi = no->n;
    while (lo < hi) {
        int m = lo + (hi - lo) / 2;
        if (no->chaves[m] < c)
            lo = m + 1;
        else
            hi = m;
    }
    return lo;
}

/* Filho a seguir: quantidade de separadores <= c. */
static int indice_filho(const No *no, bp_chave c)
{
    int lo = 0, hi = no->n;
    while (lo < hi) {
        int m = lo + (hi - lo) / 2;
        if (no->chaves[m] <= c)
            lo = m + 1;
        else
            hi = m;
    }
    return lo;
}

static int descer_ate_folha(BPlus *t, bp_chave c, No *no)
{
    int r = carregar(t, t->raiz, no);
    while (r == BP_OK && !no->folha)
        r = carregar(t, no->filhos[indice_filho(no, c)], no);
    return r;
}

int bp_buscar(BPlus *t, bp_chave chave, bp_valor *valor)
{
    No no;
    int r = descer_ate_folha(t, chave, &no);
    if (r != BP_OK)
        return r;
    int i = limite_inferior(&no, chave);
    if (i < no.n && no.chaves[i] == chave) {
        if (valor)
            *valor = no.valores[i];
        return BP_OK;
    }
    return BP_NAO_ENCONTRADA;
}

int bp_atualizar(BPlus *t, bp_chave chave, bp_valor valor)
{
    No no;
    int r = descer_ate_folha(t, chave, &no);
    if (r != BP_OK)
        return r;
    int i = limite_inferior(&no, chave);
    if (i >= no.n || no.chaves[i] != chave)
        return BP_NAO_ENCONTRADA;
    no.valores[i] = valor;
    return salvar(t, &no);
}

int64_t bp_intervalo(BPlus *t, bp_chave inicio, bp_chave fim, bp_visitante visitar, void *ctx)
{
    if (inicio > fim)
        return 0;
    No no;
    int r = descer_ate_folha(t, inicio, &no);
    if (r != BP_OK)
        return r;
    int64_t visitados = 0;
    int i = limite_inferior(&no, inicio);
    for (;;) {
        for (; i < no.n; i++) {
            if (no.chaves[i] > fim)
                return visitados;
            visitados++;
            if (visitar && visitar(no.chaves[i], no.valores[i], ctx))
                return visitados;
        }
        if (no.prox == 0)
            return visitados;
        if ((r = carregar(t, no.prox, &no)) != BP_OK)
            return r;
        i = 0;
    }
}

/* ---------------------------------------------------------------- inserção */
static int inserir_rec(BPlus *t, uint32_t pagina, bp_chave c, bp_valor v, bp_chave *promovida, uint32_t *nova)
{
    No no;
    int r = carregar(t, pagina, &no);
    if (r != BP_OK)
        return r;

    if (no.folha) {
        int pos = limite_inferior(&no, c);
        if (pos < no.n && no.chaves[pos] == c)
            return BP_DUPLICADA;
        memmove(no.chaves + pos + 1, no.chaves + pos, (size_t)(no.n - pos) * sizeof *no.chaves);
        memmove(no.valores + pos + 1, no.valores + pos, (size_t)(no.n - pos) * sizeof *no.valores);
        no.chaves[pos] = c;
        no.valores[pos] = v;
        no.n++;
        if (no.n <= t->ordem)
            return salvar(t, &no);

        /* Divisão da folha: a esquerda fica com ⌈(ordem+1)/2⌉ chaves e a primeira chave
         * da direita sobe como separador (ela continua na folha: é uma B+). */
        No dir;
        novo_no(&dir, nova_pagina(t), 1);
        if (dir.pagina == 0)
            return BP_ERRO_IO;
        int esq_n = (no.n + 1) / 2;
        dir.n = no.n - esq_n;
        memcpy(dir.chaves, no.chaves + esq_n, (size_t)dir.n * sizeof *dir.chaves);
        memcpy(dir.valores, no.valores + esq_n, (size_t)dir.n * sizeof *dir.valores);
        no.n = esq_n;
        dir.prox = no.prox;
        dir.ant = no.pagina;
        if (no.prox != 0) {
            No seguinte;
            if ((r = carregar(t, no.prox, &seguinte)) != BP_OK)
                return r;
            seguinte.ant = dir.pagina;
            if ((r = salvar(t, &seguinte)) != BP_OK)
                return r;
        }
        no.prox = dir.pagina;
        if ((r = salvar(t, &no)) != BP_OK || (r = salvar(t, &dir)) != BP_OK)
            return r;
        *promovida = dir.chaves[0];
        *nova = dir.pagina;
        return DIVIDIU;
    }

    int i = indice_filho(&no, c);
    bp_chave sep;
    uint32_t pag_nova;
    r = inserir_rec(t, no.filhos[i], c, v, &sep, &pag_nova);
    if (r != DIVIDIU)
        return r;
    memmove(no.chaves + i + 1, no.chaves + i, (size_t)(no.n - i) * sizeof *no.chaves);
    memmove(no.filhos + i + 2, no.filhos + i + 1, (size_t)(no.n - i) * sizeof *no.filhos);
    no.chaves[i] = sep;
    no.filhos[i + 1] = pag_nova;
    no.n++;
    if (no.n <= t->ordem)
        return salvar(t, &no);

    /* Divisão do nó interno: a chave do meio sobe e NÃO fica em nenhum dos dois lados. */
    No dir;
    novo_no(&dir, nova_pagina(t), 0);
    if (dir.pagina == 0)
        return BP_ERRO_IO;
    int meio = no.n / 2;
    dir.n = no.n - meio - 1;
    memcpy(dir.chaves, no.chaves + meio + 1, (size_t)dir.n * sizeof *dir.chaves);
    memcpy(dir.filhos, no.filhos + meio + 1, (size_t)(dir.n + 1) * sizeof *dir.filhos);
    *promovida = no.chaves[meio];
    no.n = meio;
    if ((r = salvar(t, &no)) != BP_OK || (r = salvar(t, &dir)) != BP_OK)
        return r;
    *nova = dir.pagina;
    return DIVIDIU;
}

int bp_inserir(BPlus *t, bp_chave chave, bp_valor valor)
{
    bp_chave sep;
    uint32_t nova;
    int r = inserir_rec(t, t->raiz, chave, valor, &sep, &nova);
    if (r == DIVIDIU) {
        /* A raiz dividiu: a árvore cresce um nível (sempre pela raiz, todas as folhas na mesma altura). */
        No raiz;
        novo_no(&raiz, nova_pagina(t), 0);
        if (raiz.pagina == 0)
            return BP_ERRO_IO;
        raiz.n = 1;
        raiz.chaves[0] = sep;
        raiz.filhos[0] = t->raiz;
        raiz.filhos[1] = nova;
        if ((r = salvar(t, &raiz)) != BP_OK)
            return r;
        t->raiz = raiz.pagina;
        t->altura++;
        r = BP_OK;
    }
    if (r == BP_OK) {
        t->chaves++;
        t->cabecalho_sujo = 1;
    }
    return r;
}

/* ---------------------------------------------------------------- remoção */
static void remover_do_pai(No *pai, int k)
{
    /* Remove o separador k e o filho k+1 (que foi fundido no filho k). */
    memmove(pai->chaves + k, pai->chaves + k + 1, (size_t)(pai->n - k - 1) * sizeof *pai->chaves);
    memmove(pai->filhos + k + 1, pai->filhos + k + 2, (size_t)(pai->n - k - 1) * sizeof *pai->filhos);
    pai->n--;
}

static int corrigir_folha(BPlus *t, No *pai, int i, No *filho)
{
    No esq, dir;
    esq.n = dir.n = 0;
    int r;
    if (i > 0) {
        if ((r = carregar(t, pai->filhos[i - 1], &esq)) != BP_OK)
            return r;
        if (esq.n > t->min_folha) {  /* empresta a última chave do irmão esquerdo */
            memmove(filho->chaves + 1, filho->chaves, (size_t)filho->n * sizeof *filho->chaves);
            memmove(filho->valores + 1, filho->valores, (size_t)filho->n * sizeof *filho->valores);
            filho->chaves[0] = esq.chaves[esq.n - 1];
            filho->valores[0] = esq.valores[esq.n - 1];
            filho->n++;
            esq.n--;
            pai->chaves[i - 1] = filho->chaves[0];
            if ((r = salvar(t, &esq)) != BP_OK || (r = salvar(t, filho)) != BP_OK)
                return r;
            return salvar(t, pai);
        }
    }
    if (i < pai->n) {
        if ((r = carregar(t, pai->filhos[i + 1], &dir)) != BP_OK)
            return r;
        if (dir.n > t->min_folha) {  /* empresta a primeira chave do irmão direito */
            filho->chaves[filho->n] = dir.chaves[0];
            filho->valores[filho->n] = dir.valores[0];
            filho->n++;
            memmove(dir.chaves, dir.chaves + 1, (size_t)(dir.n - 1) * sizeof *dir.chaves);
            memmove(dir.valores, dir.valores + 1, (size_t)(dir.n - 1) * sizeof *dir.valores);
            dir.n--;
            pai->chaves[i] = dir.chaves[0];
            if ((r = salvar(t, &dir)) != BP_OK || (r = salvar(t, filho)) != BP_OK)
                return r;
            return salvar(t, pai);
        }
    }
    /* Ninguém pode emprestar: funde duas folhas vizinhas (a da direita é liberada). */
    No *a, *b;
    int sep;
    if (i > 0) {
        a = &esq;
        b = filho;
        sep = i - 1;
    } else {
        a = filho;
        b = &dir;
        sep = i;
    }
    memcpy(a->chaves + a->n, b->chaves, (size_t)b->n * sizeof *b->chaves);
    memcpy(a->valores + a->n, b->valores, (size_t)b->n * sizeof *b->valores);
    a->n += b->n;
    a->prox = b->prox;
    if (b->prox != 0) {
        No seguinte;
        if ((r = carregar(t, b->prox, &seguinte)) != BP_OK)
            return r;
        seguinte.ant = a->pagina;
        if ((r = salvar(t, &seguinte)) != BP_OK)
            return r;
    }
    remover_do_pai(pai, sep);
    if ((r = salvar(t, a)) != BP_OK || (r = liberar_pagina(t, b->pagina)) != BP_OK)
        return r;
    return salvar(t, pai);
}

static int corrigir_interno(BPlus *t, No *pai, int i, No *filho)
{
    No esq, dir;
    esq.n = dir.n = 0;
    int r;
    if (i > 0) {
        if ((r = carregar(t, pai->filhos[i - 1], &esq)) != BP_OK)
            return r;
        if (esq.n > t->min_interno) {  /* rotação à direita passando pelo separador do pai */
            memmove(filho->chaves + 1, filho->chaves, (size_t)filho->n * sizeof *filho->chaves);
            memmove(filho->filhos + 1, filho->filhos, (size_t)(filho->n + 1) * sizeof *filho->filhos);
            filho->chaves[0] = pai->chaves[i - 1];
            filho->filhos[0] = esq.filhos[esq.n];
            filho->n++;
            pai->chaves[i - 1] = esq.chaves[esq.n - 1];
            esq.n--;
            if ((r = salvar(t, &esq)) != BP_OK || (r = salvar(t, filho)) != BP_OK)
                return r;
            return salvar(t, pai);
        }
    }
    if (i < pai->n) {
        if ((r = carregar(t, pai->filhos[i + 1], &dir)) != BP_OK)
            return r;
        if (dir.n > t->min_interno) {  /* rotação à esquerda */
            filho->chaves[filho->n] = pai->chaves[i];
            filho->filhos[filho->n + 1] = dir.filhos[0];
            filho->n++;
            pai->chaves[i] = dir.chaves[0];
            memmove(dir.chaves, dir.chaves + 1, (size_t)(dir.n - 1) * sizeof *dir.chaves);
            memmove(dir.filhos, dir.filhos + 1, (size_t)dir.n * sizeof *dir.filhos);
            dir.n--;
            if ((r = salvar(t, &dir)) != BP_OK || (r = salvar(t, filho)) != BP_OK)
                return r;
            return salvar(t, pai);
        }
    }
    /* Fusão: esquerda + separador do pai + direita. */
    No *a, *b;
    int sep;
    if (i > 0) {
        a = &esq;
        b = filho;
        sep = i - 1;
    } else {
        a = filho;
        b = &dir;
        sep = i;
    }
    a->chaves[a->n] = pai->chaves[sep];
    memcpy(a->chaves + a->n + 1, b->chaves, (size_t)b->n * sizeof *b->chaves);
    memcpy(a->filhos + a->n + 1, b->filhos, (size_t)(b->n + 1) * sizeof *b->filhos);
    a->n += b->n + 1;
    remover_do_pai(pai, sep);
    if ((r = salvar(t, a)) != BP_OK || (r = liberar_pagina(t, b->pagina)) != BP_OK)
        return r;
    return salvar(t, pai);
}

static int remover_rec(BPlus *t, No *no, bp_chave c, int *subocupado)
{
    int r;
    if (no->folha) {
        int pos = limite_inferior(no, c);
        if (pos >= no->n || no->chaves[pos] != c)
            return BP_NAO_ENCONTRADA;
        memmove(no->chaves + pos, no->chaves + pos + 1, (size_t)(no->n - pos - 1) * sizeof *no->chaves);
        memmove(no->valores + pos, no->valores + pos + 1, (size_t)(no->n - pos - 1) * sizeof *no->valores);
        no->n--;
        *subocupado = no->n < t->min_folha;
        return salvar(t, no);
    }
    int i = indice_filho(no, c);
    No filho;
    int sub = 0;
    if ((r = carregar(t, no->filhos[i], &filho)) != BP_OK)
        return r;
    if ((r = remover_rec(t, &filho, c, &sub)) != BP_OK)
        return r;
    if (sub && (r = (filho.folha ? corrigir_folha : corrigir_interno)(t, no, i, &filho)) != BP_OK)
        return r;
    *subocupado = no->n < t->min_interno;
    return BP_OK;
}

int bp_remover(BPlus *t, bp_chave chave)
{
    No raiz;
    int sub = 0;
    int r = carregar(t, t->raiz, &raiz);
    if (r != BP_OK)
        return r;
    if ((r = remover_rec(t, &raiz, chave, &sub)) != BP_OK)
        return r;
    t->chaves--;
    t->cabecalho_sujo = 1;
    if (!raiz.folha && raiz.n == 0) {
        /* A raiz interna ficou com um único filho: a árvore perde um nível. */
        uint32_t antiga = raiz.pagina;
        t->raiz = raiz.filhos[0];
        t->altura--;
        return liberar_pagina(t, antiga);
    }
    return BP_OK;
}

/* ---------------------------------------------------------------- carga em lote */
static int distribuir(int64_t total, int cap, int minimo, int maximo, int64_t *grupos)
{
    /* Quantidade de grupos: o menor número que respeita `cap`, reduzido se algum grupo
     * ficaria abaixo do mínimo. Tamanhos diferem em no máximo 1. */
    int64_t g = (total + cap - 1) / cap;
    while (g > 1 && total / g < minimo)
        g--;
    if ((total + g - 1) / g > maximo)
        return BP_ERRO_ARGUMENTO;
    *grupos = g;
    return BP_OK;
}

typedef struct {
    bp_chave primeira;
    uint32_t pagina;
} Entrada;

int bp_carregar(BPlus *t, int64_t n, bp_fonte fonte, void *ctx, double preenchimento)
{
    if (t->chaves != 0 || n < 0 || preenchimento < 0.5 || preenchimento > 1.0)
        return BP_ERRO_ARGUMENTO;
    if (n == 0)
        return BP_OK;
    int r;
    int cap_folha = (int)lround(t->ordem * preenchimento);
    int cap_interno = (int)lround(t->ordem * preenchimento);
    if (cap_folha < t->min_folha)
        cap_folha = t->min_folha;
    if (cap_interno < t->min_interno)
        cap_interno = t->min_interno;

    int64_t qtd_folhas;
    if ((r = distribuir(n, cap_folha, t->min_folha, t->ordem, &qtd_folhas)) != BP_OK)
        return r;
    if ((r = liberar_pagina(t, t->raiz)) != BP_OK)  /* a raiz-folha vazia será substituída */
        return r;

    Entrada *nivel = ed_malloc((size_t)qtd_folhas * sizeof *nivel);
    No atual, anterior;
    int tem_anterior = 0;
    bp_chave ultima = 0;
    int64_t lidos = 0;
    for (int64_t f = 0; f < qtd_folhas; f++) {
        int tam = (int)(n / qtd_folhas + (f < n % qtd_folhas));
        novo_no(&atual, nova_pagina(t), 1);
        for (int j = 0; j < tam; j++) {
            bp_chave c;
            bp_valor v;
            if (!fonte(ctx, &c, &v) || (lidos > 0 && c <= ultima)) {
                ed_free(nivel);
                return BP_ERRO_ARGUMENTO;  /* faltaram pares ou não estão em ordem estrita */
            }
            atual.chaves[j] = c;
            atual.valores[j] = v;
            ultima = c;
            lidos++;
        }
        atual.n = tam;
        if (tem_anterior) {
            anterior.prox = atual.pagina;
            atual.ant = anterior.pagina;
            if ((r = salvar(t, &anterior)) != BP_OK) {
                ed_free(nivel);
                return r;
            }
        }
        nivel[f].primeira = atual.chaves[0];
        nivel[f].pagina = atual.pagina;
        anterior = atual;
        tem_anterior = 1;
    }
    if ((r = salvar(t, &anterior)) != BP_OK) {
        ed_free(nivel);
        return r;
    }

    /* Níveis internos, de baixo para cima: cada nó recebe um grupo de filhos consecutivos
     * e o separador entre dois filhos é a primeira chave do filho da direita. */
    int64_t qtd = qtd_folhas;
    int altura = 1;
    while (qtd > 1) {
        int64_t grupos;
        if ((r = distribuir(qtd, cap_interno + 1, t->min_interno + 1, t->ordem + 1, &grupos)) != BP_OK) {
            ed_free(nivel);
            return r;
        }
        int64_t k = 0;
        for (int64_t g = 0; g < grupos; g++) {
            int filhos = (int)(qtd / grupos + (g < qtd % grupos));
            No no;
            novo_no(&no, nova_pagina(t), 0);
            bp_chave primeira = nivel[k].primeira;
            for (int j = 0; j < filhos; j++, k++) {
                no.filhos[j] = nivel[k].pagina;
                if (j > 0)
                    no.chaves[j - 1] = nivel[k].primeira;
            }
            no.n = filhos - 1;
            if ((r = salvar(t, &no)) != BP_OK) {
                ed_free(nivel);
                return r;
            }
            nivel[g].primeira = primeira;  /* reaproveita o vetor: g < k sempre */
            nivel[g].pagina = no.pagina;
        }
        qtd = grupos;
        altura++;
    }
    t->raiz = nivel[0].pagina;
    t->altura = altura;
    t->chaves = n;
    t->cabecalho_sujo = 1;
    ed_free(nivel);
    return BP_OK;
}

/* ---------------------------------------------------------------- verificação */
typedef struct {
    BPlus *t;
    char *msg;
    size_t tam;
    int altura_folhas;
    int64_t chaves;
    uint32_t nos;
    uint32_t folhas;
    uint32_t primeira_folha;
} Verificacao;

static int falha(Verificacao *v, const char *formato, ...)
{
    if (v->msg && v->tam) {
        va_list ap;
        va_start(ap, formato);
        vsnprintf(v->msg, v->tam, formato, ap);
        va_end(ap);
    }
    return BP_ERRO_INVARIANTE;
}

/* Confere o nó e sua subárvore; as chaves devem estar em [min, max) (int64 para representar ±∞). */
static int verificar_no(Verificacao *v, uint32_t pagina, int nivel, int64_t min, int64_t max, int raiz)
{
    No no;
    int r = carregar(v->t, pagina, &no);
    if (r != BP_OK)
        return falha(v, "página %" PRIu32 " ilegível ou com formato inválido", pagina);
    v->nos++;
    int minimo = no.folha ? v->t->min_folha : v->t->min_interno;
    if (!raiz && no.n < minimo)
        return falha(v, "página %" PRIu32 ": %d chaves, abaixo do mínimo %d", pagina, no.n, minimo);
    if (raiz && !no.folha && no.n < 1)
        return falha(v, "raiz interna sem chaves");
    for (int i = 0; i < no.n; i++) {
        if (i > 0 && no.chaves[i - 1] >= no.chaves[i])
            return falha(v, "página %" PRIu32 ": chaves fora de ordem", pagina);
        if (no.chaves[i] < min || no.chaves[i] >= max)
            return falha(v, "página %" PRIu32 ": chave %" PRId32 " fora do intervalo do separador", pagina, no.chaves[i]);
    }
    if (no.folha) {
        if (v->altura_folhas == 0)
            v->altura_folhas = nivel;
        else if (v->altura_folhas != nivel)
            return falha(v, "folhas em profundidades diferentes (%d e %d)", v->altura_folhas, nivel);
        if (v->folhas == 0)
            v->primeira_folha = pagina;
        v->folhas++;
        v->chaves += no.n;
        return BP_OK;
    }
    for (int i = 0; i <= no.n; i++) {
        int64_t lo = i == 0 ? min : no.chaves[i - 1];
        int64_t hi = i == no.n ? max : no.chaves[i];
        if ((r = verificar_no(v, no.filhos[i], nivel + 1, lo, hi, 0)) != BP_OK)
            return r;
    }
    return BP_OK;
}

int bp_verificar(BPlus *t, char *msg, size_t tamanho)
{
    Verificacao v = {t, msg, tamanho, 0, 0, 0, 0, 0};
    int r = verificar_no(&v, t->raiz, 1, INT64_MIN, INT64_MAX, 1);
    if (r != BP_OK)
        return r;
    if (v.altura_folhas != t->altura)
        return falha(&v, "altura registrada %d, real %d", t->altura, v.altura_folhas);
    if (v.chaves != t->chaves)
        return falha(&v, "cabeçalho diz %" PRId64 " chaves, árvore tem %" PRId64, t->chaves, v.chaves);

    /* A lista de folhas deve percorrer as mesmas folhas, em ordem, com os ponteiros "ant" coerentes. */
    No no;
    uint32_t pagina = v.primeira_folha, anterior = 0, folhas = 0;
    int64_t contadas = 0;
    int64_t ultima = INT64_MIN;
    while (pagina != 0) {
        if (carregar(t, pagina, &no) != BP_OK || !no.folha)
            return falha(&v, "lista de folhas aponta para página inválida %" PRIu32, pagina);
        if (no.ant != anterior)
            return falha(&v, "folha %" PRIu32 ": ponteiro anterior incorreto", pagina);
        for (int i = 0; i < no.n; i++) {
            if (no.chaves[i] <= ultima)
                return falha(&v, "lista de folhas fora de ordem na folha %" PRIu32, pagina);
            ultima = no.chaves[i];
        }
        contadas += no.n;
        folhas++;
        if (folhas > v.folhas)
            return falha(&v, "lista de folhas com ciclo ou folhas a mais");
        anterior = pagina;
        pagina = no.prox;
    }
    if (folhas != v.folhas || contadas != v.chaves)
        return falha(&v, "lista de folhas com %" PRIu32 " folhas, árvore com %" PRIu32, folhas, v.folhas);

    /* Toda página é cabeçalho, nó alcançável ou livre: nenhuma se perdeu. */
    uint32_t livres = 0;
    uint8_t buf[BP_PAGINA];
    for (uint32_t p = t->livre; p != 0; p = u32_de(buf + 4)) {
        if (ler_pagina(t, p, buf) != BP_OK || buf[0] != TIPO_LIVRE || ++livres > t->paginas)
            return falha(&v, "lista de páginas livres corrompida");
    }
    if (livres != t->qtd_livres)
        return falha(&v, "contagem de páginas livres incorreta");
    if (1 + v.nos + livres != t->paginas)
        return falha(&v, "%" PRIu32 " páginas no arquivo, mas %" PRIu32 " nós + %" PRIu32 " livres + cabeçalho",
                     t->paginas, v.nos, livres);
    return BP_OK;
}

/* ---------------------------------------------------------------- informação e impressão */
void bp_info(BPlus *t, BPlusInfo *info)
{
    info->ordem = t->ordem;
    info->min_folha = t->min_folha;
    info->min_interno = t->min_interno;
    info->altura = t->altura;
    info->chaves = t->chaves;
    info->paginas = t->paginas;
    info->paginas_livres = t->qtd_livres;
    info->leituras = t->leituras;
    info->escritas = t->escritas;
    /* Folhas: conta pela lista ligada a partir da folha mais à esquerda (sem afetar contadores). */
    uint64_t l = t->leituras;
    No no;
    uint32_t folhas = 0;
    if (carregar(t, t->raiz, &no) == BP_OK) {
        while (!no.folha && carregar(t, no.filhos[0], &no) == BP_OK) {
        }
        folhas = 1;
        while (no.prox != 0 && carregar(t, no.prox, &no) == BP_OK)
            folhas++;
    }
    t->leituras = l;
    info->folhas = folhas;
}

void bp_zerar_contadores(BPlus *t)
{
    t->leituras = t->escritas = 0;
}

typedef struct {
    uint32_t *v;
    size_t n, cap;
} Lista;

static void lista_add(Lista *l, uint32_t x)
{
    if (l->n == l->cap) {
        l->cap = l->cap ? l->cap * 2 : 16;
        l->v = ed_realloc(l->v, l->cap * sizeof *l->v);
    }
    l->v[l->n++] = x;
}

int bp_imprimir(BPlus *t, FILE *saida, int max_nos)
{
    Lista atual = {0}, proximo = {0};
    lista_add(&atual, t->raiz);
    int r = BP_OK;
    for (int nivel = 1; atual.n > 0 && r == BP_OK; nivel++) {
        fprintf(saida, "nível %d (%zu nó%s): ", nivel, atual.n, atual.n == 1 ? "" : "s");
        proximo.n = 0;
        for (size_t k = 0; k < atual.n; k++) {
            No no;
            if ((r = carregar(t, atual.v[k], &no)) != BP_OK)
                break;
            if (k < (size_t)max_nos) {
                /* Nós grandes aparecem abreviados: três primeiras chaves, "…", última. */
                fputc('[', saida);
                for (int i = 0; i < no.n; i++) {
                    if (no.n > 6 && i == 3) {
                        fprintf(saida, " … (%d chaves) …", no.n);
                        i = no.n - 1;
                    }
                    fprintf(saida, i ? " %" PRId32 : "%" PRId32, no.chaves[i]);
                }
                fputs("] ", saida);
            } else if (k == (size_t)max_nos) {
                fputs("…", saida);
            }
            if (!no.folha)
                for (int i = 0; i <= no.n; i++)
                    lista_add(&proximo, no.filhos[i]);
        }
        fputc('\n', saida);
        Lista tmp = atual;
        atual = proximo;
        proximo = tmp;
    }
    ed_free(atual.v);
    ed_free(proximo.v);
    return r;
}

const char *bp_mensagem(int codigo)
{
    switch (codigo) {
    case BP_OK: return "ok";
    case BP_DUPLICADA: return "chave já existe";
    case BP_NAO_ENCONTRADA: return "chave não encontrada";
    case BP_ERRO_IO: return "erro de leitura/escrita no arquivo";
    case BP_ERRO_FORMATO: return "arquivo não é um índice B+ válido";
    case BP_ERRO_ARGUMENTO: return "argumento inválido";
    case BP_ERRO_INVARIANTE: return "invariante da árvore violada";
    default: return "erro desconhecido";
    }
}
