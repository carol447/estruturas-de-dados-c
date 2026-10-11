#include "ed/radix.h"

#include <string.h>

#include "ed/memoria.h"

typedef struct No {
    char *rotulo;           /* não terminado em '\0'; tamanho em `tam` */
    size_t tam;
    int terminal;           /* uma palavra termina aqui */
    int64_t valor;
    struct No *filho;       /* primeiro filho */
    struct No *irmao;       /* próximo irmão (ordem crescente do primeiro byte) */
} No;

struct Radix {
    No raiz;                /* rótulo vazio */
    size_t n;
};

static unsigned char primeiro(const No *no)
{
    return (unsigned char)no->rotulo[0];
}

static No *novo_no(const char *rotulo, size_t tam)
{
    No *no = ed_calloc(1, sizeof *no);
    no->rotulo = ed_strndup(rotulo, tam);
    no->tam = tam;
    return no;
}

static void destruir_no(No *no)
{
    while (no != NULL) {
        No *irmao = no->irmao;
        destruir_no(no->filho);
        ed_free(no->rotulo);
        ed_free(no);
        no = irmao;
    }
}

Radix *rad_criar(void)
{
    return ed_calloc(1, sizeof(Radix));
}

void rad_destruir(Radix *r)
{
    if (r) {
        destruir_no(r->raiz.filho);
        ed_free(r);
    }
}

static size_t prefixo_comum(const char *a, size_t na, const char *b, size_t nb)
{
    size_t i = 0;
    while (i < na && i < nb && a[i] == b[i])
        i++;
    return i;
}

/* Elo (ponteiro para ponteiro) onde está ou deveria estar o filho que começa com `c`. */
static No **elo_filho(No *pai, unsigned char c)
{
    No **elo = &pai->filho;
    while (*elo && primeiro(*elo) < c)
        elo = &(*elo)->irmao;
    return elo;
}

int rad_inserir(Radix *r, const char *chave, int64_t valor)
{
    No *atual = &r->raiz;
    const char *s = chave;
    size_t resto = strlen(chave);
    for (;;) {
        if (resto == 0) {
            int nova = !atual->terminal;
            atual->terminal = 1;
            atual->valor = valor;
            r->n += (size_t)nova;
            return nova;
        }
        No **elo = elo_filho(atual, (unsigned char)s[0]);
        if (*elo == NULL || primeiro(*elo) != (unsigned char)s[0]) {
            No *folha = novo_no(s, resto);
            folha->terminal = 1;
            folha->valor = valor;
            folha->irmao = *elo;
            *elo = folha;
            r->n++;
            return 1;
        }
        No *f = *elo;
        size_t comum = prefixo_comum(f->rotulo, f->tam, s, resto);
        if (comum < f->tam) {
            /* Divide a aresta: um nó intermediário com a parte comum passa a ser pai de f. */
            No *meio = novo_no(f->rotulo, comum);
            char *sufixo = ed_strndup(f->rotulo + comum, f->tam - comum);  /* nova área: sem sobreposição */
            ed_free(f->rotulo);
            f->rotulo = sufixo;
            f->tam -= comum;
            meio->irmao = f->irmao;
            f->irmao = NULL;
            meio->filho = f;
            *elo = meio;
            f = meio;
        }
        atual = f;
        s += comum;
        resto -= comum;
    }
}

static const No *encontrar(const Radix *r, const char *chave)
{
    const No *atual = &r->raiz;
    size_t resto = strlen(chave);
    while (resto > 0) {
        const No *f = atual->filho;
        while (f && primeiro(f) < (unsigned char)chave[0])
            f = f->irmao;
        if (f == NULL || primeiro(f) != (unsigned char)chave[0] || f->tam > resto ||
            memcmp(f->rotulo, chave, f->tam) != 0)
            return NULL;
        chave += f->tam;
        resto -= f->tam;
        atual = f;
    }
    return atual->terminal ? atual : NULL;
}

int rad_buscar(const Radix *r, const char *chave, int64_t *valor)
{
    const No *no = encontrar(r, chave);
    if (no && valor)
        *valor = no->valor;
    return no != NULL;
}

/* Depois de uma remoção na subárvore de *elo, desfaz nós que ficaram inúteis. */
static void normalizar(No **elo)
{
    No *f = *elo;
    if (f->terminal)
        return;
    if (f->filho == NULL) {               /* não termina palavra nem tem filhos: some */
        *elo = f->irmao;
        ed_free(f->rotulo);
        ed_free(f);
    } else if (f->filho->irmao == NULL) { /* um único filho: funde os rótulos */
        No *unico = f->filho;
        char *rotulo = ed_malloc(f->tam + unico->tam + 1);
        memcpy(rotulo, f->rotulo, f->tam);
        memcpy(rotulo + f->tam, unico->rotulo, unico->tam);
        rotulo[f->tam + unico->tam] = '\0';
        ed_free(unico->rotulo);
        unico->rotulo = rotulo;
        unico->tam += f->tam;
        unico->irmao = f->irmao;
        *elo = unico;
        ed_free(f->rotulo);
        ed_free(f);
    }
}

static int remover_rec(No **elo, const char *s, size_t resto)
{
    No *f = *elo;
    if (f->tam > resto || memcmp(f->rotulo, s, f->tam) != 0)
        return 0;
    s += f->tam;
    resto -= f->tam;
    if (resto == 0) {
        if (!f->terminal)
            return 0;
        f->terminal = 0;
    } else {
        No **filho = elo_filho(f, (unsigned char)s[0]);
        if (*filho == NULL || primeiro(*filho) != (unsigned char)s[0] || !remover_rec(filho, s, resto))
            return 0;
    }
    normalizar(elo);
    return 1;
}

int rad_remover(Radix *r, const char *chave)
{
    size_t resto = strlen(chave);
    int removido;
    if (resto == 0) {
        removido = r->raiz.terminal;
        r->raiz.terminal = 0;
    } else {
        No **elo = elo_filho(&r->raiz, (unsigned char)chave[0]);
        removido = *elo && primeiro(*elo) == (unsigned char)chave[0] && remover_rec(elo, chave, resto);
    }
    r->n -= (size_t)removido;
    return removido;
}

size_t rad_tamanho(const Radix *r)
{
    return r->n;
}

static size_t contar(const No *no)
{
    size_t total = 0;
    for (; no; no = no->irmao)
        total += 1 + contar(no->filho);
    return total;
}

size_t rad_nos(const Radix *r)
{
    return contar(r->raiz.filho);
}

static int altura(const No *no)
{
    int maior = 0;
    for (; no; no = no->irmao) {
        int a = 1 + altura(no->filho);
        if (a > maior)
            maior = a;
    }
    return maior;
}

int rad_altura(const Radix *r)
{
    return altura(r->raiz.filho);
}

/* ---------------------------------------------------------------- percurso */
typedef struct {
    char *buf;
    size_t tam, cap;
    rad_visitante visitar;
    void *ctx;
    size_t contagem;
    int parar;
} Percurso;

static void empilhar(Percurso *p, const char *s, size_t n)
{
    if (p->tam + n + 1 > p->cap) {
        p->cap = (p->tam + n + 1) * 2;
        p->buf = ed_realloc(p->buf, p->cap);
    }
    memcpy(p->buf + p->tam, s, n);
    p->tam += n;
    p->buf[p->tam] = '\0';
}

static void percorrer(const No *no, Percurso *p)
{
    for (; no && !p->parar; no = no->irmao) {
        size_t antes = p->tam;
        empilhar(p, no->rotulo, no->tam);
        if (no->terminal) {
            p->contagem++;
            if (p->visitar && p->visitar(p->buf, no->valor, p->ctx))
                p->parar = 1;
        }
        percorrer(no->filho, p);
        p->tam = antes;
        p->buf[antes] = '\0';
    }
}

size_t rad_prefixo(const Radix *r, const char *prefixo, rad_visitante visitar, void *ctx)
{
    Percurso p = {NULL, 0, 0, visitar, ctx, 0, 0};
    empilhar(&p, "", 0);
    const No *atual = &r->raiz;
    const char *s = prefixo;
    size_t resto = strlen(prefixo);
    while (resto > 0) {
        const No *f = atual->filho;
        while (f && primeiro(f) < (unsigned char)s[0])
            f = f->irmao;
        if (f == NULL || primeiro(f) != (unsigned char)s[0]) {
            ed_free(p.buf);
            return 0;
        }
        size_t n = f->tam < resto ? f->tam : resto;
        if (memcmp(f->rotulo, s, n) != 0) {
            ed_free(p.buf);
            return 0;
        }
        empilhar(&p, f->rotulo, f->tam);
        s += n;
        resto -= n;
        atual = f;
    }
    /* `atual` cobre o prefixo inteiro (o prefixo pode terminar no meio do rótulo dele). */
    if (atual != &r->raiz && atual->terminal) {
        p.contagem++;
        if (visitar && visitar(p.buf, atual->valor, ctx))
            p.parar = 1;
    } else if (atual == &r->raiz && atual->terminal) {
        p.contagem++;
        if (visitar && visitar("", atual->valor, ctx))
            p.parar = 1;
    }
    percorrer(atual->filho, &p);
    ed_free(p.buf);
    return p.contagem;
}

/* ---------------------------------------------------------------- verificação e memória */
static int verificar_no(const No *no, size_t *terminais)
{
    int ultimo = -1;
    for (; no; no = no->irmao) {
        if (no->tam == 0 || (int)primeiro(no) <= ultimo)
            return 0;  /* rótulo vazio ou irmãos fora de ordem / com mesma inicial */
        ultimo = primeiro(no);
        if (!no->terminal && (no->filho == NULL || no->filho->irmao == NULL))
            return 0;  /* nó que deveria ter sido removido ou fundido */
        *terminais += (size_t)no->terminal;
        if (!verificar_no(no->filho, terminais))
            return 0;
    }
    return 1;
}

int rad_verificar(const Radix *r)
{
    size_t terminais = (size_t)r->raiz.terminal;
    return verificar_no(r->raiz.filho, &terminais) && terminais == r->n;
}

static size_t memoria(const No *no)
{
    size_t total = 0;
    for (; no; no = no->irmao)
        total += sizeof *no + no->tam + 1 + memoria(no->filho);
    return total;
}

size_t rad_memoria(const Radix *r)
{
    return sizeof *r + memoria(r->raiz.filho);
}
