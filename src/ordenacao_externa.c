#include "ed/ordenacao_externa.h"

#include <stdio.h>
#include <string.h>

#include "ed/memoria.h"

/* ---------------------------------------------------------------- leitura e escrita sequenciais */
typedef struct {
    FILE *f;
    uint8_t buf[ALUNO_TAMANHO];
    int64_t *contador;
} Fluxo;

static int ler(Fluxo *fl, Aluno *a)
{
    if (fread(fl->buf, ALUNO_TAMANHO, 1, fl->f) != 1)
        return 0;
    aluno_desserializar(fl->buf, a);
    (*fl->contador)++;
    return 1;
}

static int escrever(Fluxo *fl, const Aluno *a)
{
    aluno_serializar(a, fl->buf);
    if (fwrite(fl->buf, ALUNO_TAMANHO, 1, fl->f) != 1)
        return 0;
    (*fl->contador)++;
    return 1;
}

int oe_gerar(const char *arquivo, int64_t n, uint64_t semente)
{
    if (n < 0 || n > INT32_MAX)
        return -1;
    FILE *f = fopen(arquivo, "wb");
    if (f == NULL)
        return -1;
    int32_t *matriculas = ed_malloc((size_t)(n ? n : 1) * sizeof *matriculas);
    for (int64_t i = 0; i < n; i++)
        matriculas[i] = (int32_t)(100000 + i * 3);  /* distintas e não contíguas */
    Rng rng;
    rng_iniciar(&rng, semente);
    rng_embaralhar(&rng, matriculas, (size_t)n);
    int64_t escritos = 0;
    Fluxo fl = {f, {0}, &escritos};
    int ok = 1;
    for (int64_t i = 0; i < n && ok; i++) {
        Aluno a;
        aluno_aleatorio(&rng, matriculas[i], &a);
        ok = escrever(&fl, &a);
    }
    ed_free(matriculas);
    if (fclose(f) != 0)
        ok = 0;
    return ok ? 0 : -1;
}

/* ---------------------------------------------------------------- quicksort interno */
static void troca(Aluno *a, Aluno *b)
{
    Aluno t = *a;
    *a = *b;
    *b = t;
}

static void insercao(Aluno *v, int64_t n)
{
    for (int64_t i = 1; i < n; i++) {
        Aluno x = v[i];
        int64_t j = i - 1;
        while (j >= 0 && v[j].matricula > x.matricula) {
            v[j + 1] = v[j];
            j--;
        }
        v[j + 1] = x;
    }
}

void oe_quicksort(Aluno *v, int64_t n)
{
    while (n > 16) {
        /* Mediana de três: evita o pior caso em entradas já ordenadas. */
        int64_t m = n / 2;
        if (v[m].matricula < v[0].matricula)
            troca(&v[m], &v[0]);
        if (v[n - 1].matricula < v[0].matricula)
            troca(&v[n - 1], &v[0]);
        if (v[n - 1].matricula < v[m].matricula)
            troca(&v[n - 1], &v[m]);
        int32_t pivo = v[m].matricula;
        int64_t i = 0, j = n - 1;
        while (i <= j) {  /* partição de Hoare */
            while (v[i].matricula < pivo)
                i++;
            while (v[j].matricula > pivo)
                j--;
            if (i <= j) {
                troca(&v[i], &v[j]);
                i++;
                j--;
            }
        }
        /* Recursão no lado menor e laço no maior: pilha O(log n). */
        if (j + 1 < n - i) {
            oe_quicksort(v, j + 1);
            v += i;
            n -= i;
        } else {
            oe_quicksort(v + i, n - i);
            n = j + 1;
        }
    }
    insercao(v, n);
}

/* ---------------------------------------------------------------- heap mínimo */
typedef struct {
    Aluno aluno;
    int64_t chave_secundaria;   /* número da corrida (seleção) ou índice da via (intercalação) */
} ItemHeap;

static int menor(const ItemHeap *a, const ItemHeap *b)
{
    if (a->chave_secundaria != b->chave_secundaria)
        return a->chave_secundaria < b->chave_secundaria;
    return a->aluno.matricula < b->aluno.matricula;
}

static int menor_por_matricula(const ItemHeap *a, const ItemHeap *b)
{
    return a->aluno.matricula < b->aluno.matricula;
}

typedef int (*Comparador)(const ItemHeap *, const ItemHeap *);

static void descer(ItemHeap *h, int64_t n, int64_t i, Comparador cmp)
{
    for (;;) {
        int64_t e = 2 * i + 1, d = e + 1, m = i;
        if (e < n && cmp(&h[e], &h[m]))
            m = e;
        if (d < n && cmp(&h[d], &h[m]))
            m = d;
        if (m == i)
            return;
        ItemHeap t = h[i];
        h[i] = h[m];
        h[m] = t;
        i = m;
    }
}

/* ---------------------------------------------------------------- corridas */
typedef struct {
    char **nomes;
    int64_t n, cap;
} Corridas;

static char *nome_temporario(const char *base, int passada, int64_t k)
{
    char *nome = ed_malloc(strlen(base) + 48);
    sprintf(nome, "%s.tmp.%d.%lld", base, passada, (long long)k);
    return nome;
}

static void adicionar_corrida(Corridas *c, char *nome)
{
    if (c->n == c->cap) {
        c->cap = c->cap ? c->cap * 2 : 16;
        c->nomes = ed_realloc(c->nomes, (size_t)c->cap * sizeof *c->nomes);
    }
    c->nomes[c->n++] = nome;
}

static void limpar_corridas(Corridas *c, int apagar)
{
    for (int64_t i = 0; i < c->n; i++) {
        if (apagar)
            remove(c->nomes[i]);
        ed_free(c->nomes[i]);
    }
    ed_free(c->nomes);
    c->nomes = NULL;
    c->n = c->cap = 0;
}

static int corridas_quicksort(Fluxo *entrada, const char *base, int64_t memoria, Corridas *c, int64_t *escritos)
{
    Aluno *buf = ed_malloc((size_t)memoria * sizeof *buf);
    int ok = 1;
    for (;;) {
        int64_t n = 0;
        while (n < memoria && ler(entrada, &buf[n]))
            n++;
        if (n == 0)
            break;
        oe_quicksort(buf, n);
        char *nome = nome_temporario(base, 0, c->n);
        FILE *f = fopen(nome, "wb");
        adicionar_corrida(c, nome);
        if (f == NULL) {
            ok = 0;
            break;
        }
        Fluxo saida = {f, {0}, escritos};
        for (int64_t i = 0; i < n && ok; i++)
            ok = escrever(&saida, &buf[i]);
        if (fclose(f) != 0)
            ok = 0;
        if (!ok || n < memoria)
            break;
    }
    ed_free(buf);
    return ok;
}

static int corridas_selecao(Fluxo *entrada, const char *base, int64_t memoria, Corridas *c, int64_t *escritos)
{
    /* Seleção por substituição: o heap ordena por (corrida, matrícula). Quem sai é gravado na
     * corrida atual; quem entra fica na mesma corrida se for >= ao último gravado, senão vai
     * para a próxima (não pode mais entrar nesta sem quebrar a ordem). */
    ItemHeap *heap = ed_malloc((size_t)memoria * sizeof *heap);
    int64_t n = 0;
    while (n < memoria && ler(entrada, &heap[n].aluno))
        heap[n++].chave_secundaria = 0;
    for (int64_t i = n / 2 - 1; i >= 0; i--)
        descer(heap, n, i, menor);
    int ok = 1;
    int64_t corrida_atual = -1;
    FILE *f = NULL;
    Fluxo saida = {NULL, {0}, escritos};
    while (n > 0 && ok) {
        ItemHeap topo = heap[0];
        if (topo.chave_secundaria != corrida_atual) {
            if (f && fclose(f) != 0)
                ok = 0;
            char *nome = nome_temporario(base, 0, c->n);
            f = fopen(nome, "wb");
            adicionar_corrida(c, nome);
            if (f == NULL) {
                ok = 0;
                break;
            }
            saida.f = f;
            corrida_atual = topo.chave_secundaria;
        }
        ok = escrever(&saida, &topo.aluno);
        Aluno novo;
        if (ler(entrada, &novo)) {
            heap[0].aluno = novo;
            heap[0].chave_secundaria = novo.matricula >= topo.aluno.matricula ? corrida_atual : corrida_atual + 1;
        } else {
            heap[0] = heap[--n];
        }
        descer(heap, n, 0, menor);
    }
    if (f && fclose(f) != 0)
        ok = 0;
    ed_free(heap);
    return ok;
}

/* ---------------------------------------------------------------- intercalação */
static int intercalar(char **entradas, int k, const char *destino, int64_t *lidos, int64_t *escritos)
{
    Fluxo *fl = ed_calloc((size_t)k, sizeof *fl);
    ItemHeap *heap = ed_malloc((size_t)k * sizeof *heap);
    int64_t n = 0;
    int ok = 1;
    for (int i = 0; i < k; i++) {
        fl[i].f = fopen(entradas[i], "rb");
        fl[i].contador = lidos;
        if (fl[i].f == NULL) {
            ok = 0;
            continue;
        }
        if (ler(&fl[i], &heap[n].aluno))
            heap[n++].chave_secundaria = i;
    }
    FILE *out = ok ? fopen(destino, "wb") : NULL;
    ok = ok && out != NULL;
    Fluxo saida = {out, {0}, escritos};
    for (int64_t i = n / 2 - 1; i >= 0; i--)
        descer(heap, n, i, menor_por_matricula);
    while (n > 0 && ok) {
        int via = (int)heap[0].chave_secundaria;
        ok = escrever(&saida, &heap[0].aluno);
        if (!ler(&fl[via], &heap[0].aluno))
            heap[0] = heap[--n];
        descer(heap, n, 0, menor_por_matricula);
    }
    for (int i = 0; i < k; i++)
        if (fl[i].f)
            fclose(fl[i].f);
    if (out && fclose(out) != 0)
        ok = 0;
    ed_free(fl);
    ed_free(heap);
    return ok;
}

static int copiar(const char *origem, const char *destino)
{
    FILE *a = fopen(origem, "rb"), *b = fopen(destino, "wb");
    int ok = a && b;
    char buf[1 << 15];
    size_t n;
    while (ok && (n = fread(buf, 1, sizeof buf, a)) > 0)
        ok = fwrite(buf, 1, n, b) == n;
    if (a)
        fclose(a);
    if (b && fclose(b) != 0)
        ok = 0;
    return ok;
}

int oe_ordenar(const char *entrada, const char *saida, int64_t memoria, OeMetodo metodo, int vias,
               OeEstatisticas *est)
{
    if (memoria < 1 || vias < 2)
        return -1;
    OeEstatisticas e = {0};
    FILE *f = fopen(entrada, "rb");
    if (f == NULL)
        return -1;
    Fluxo in = {f, {0}, &e.registros_lidos};
    Corridas atual = {0};
    int ok = metodo == OE_QUICKSORT ? corridas_quicksort(&in, saida, memoria, &atual, &e.registros_escritos)
                                    : corridas_selecao(&in, saida, memoria, &atual, &e.registros_escritos);
    fclose(f);
    e.registros = e.registros_lidos;
    e.corridas_iniciais = atual.n;
    e.maior_corrida_media = atual.n ? (double)e.registros / (double)atual.n : 0.0;

    int passada = 0;
    while (ok && atual.n > 1) {
        passada++;
        Corridas proxima = {0};
        for (int64_t i = 0; i < atual.n && ok; i += vias) {
            int k = (int)(atual.n - i < vias ? atual.n - i : vias);
            char *nome = nome_temporario(saida, passada, proxima.n);
            adicionar_corrida(&proxima, nome);
            ok = intercalar(atual.nomes + i, k, nome, &e.registros_lidos, &e.registros_escritos);
        }
        limpar_corridas(&atual, 1);
        atual = proxima;
    }
    e.passadas = passada;
    if (ok) {
        remove(saida);
        if (atual.n == 0) {
            FILE *vazio = fopen(saida, "wb");  /* entrada vazia: saída vazia */
            ok = vazio != NULL && fclose(vazio) == 0;
        } else if (rename(atual.nomes[0], saida) != 0) {
            ok = copiar(atual.nomes[0], saida);
        }
    }
    limpar_corridas(&atual, 1);
    if (est)
        *est = e;
    return ok ? 0 : -1;
}

int oe_verificar(const char *arquivo, int64_t *registros)
{
    FILE *f = fopen(arquivo, "rb");
    if (f == NULL)
        return 0;
    int64_t lidos = 0;
    Fluxo fl = {f, {0}, &lidos};
    Aluno a;
    int64_t anterior = INT64_MIN;
    int ok = 1;
    while (ler(&fl, &a)) {
        if (a.matricula <= anterior)
            ok = 0;
        anterior = a.matricula;
    }
    fclose(f);
    if (registros)
        *registros = lidos;
    return ok;
}
