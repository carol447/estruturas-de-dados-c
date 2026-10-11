#include "ed/patricia.h"

#include <string.h>

#include "ed/memoria.h"

#define EXTERNO (-1)

typedef struct No {
    int bit;                /* índice do bit testado; EXTERNO nas folhas */
    struct No *esq, *dir;   /* nós internos */
    char *chave;            /* folhas */
    size_t tamanho;
    int64_t valor;
} No;

struct Patricia {
    No *raiz;
    size_t n;
};

/* Bit i da chave (bit 0 = mais significativo do primeiro byte). Depois do fim vale 0. */
static int bit_de(const char *chave, size_t tamanho, size_t i)
{
    size_t byte = i >> 3;
    if (byte >= tamanho)  /* o '\0' e o que vem depois são zeros */
        return 0;
    return ((unsigned char)chave[byte] >> (7 - (i & 7))) & 1;
}

/* Primeiro bit em que duas chaves distintas diferem. */
static size_t primeiro_bit_diferente(const char *a, size_t na, const char *b, size_t nb)
{
    size_t byte = 0;
    size_t limite = (na > nb ? na : nb) + 1;
    while (byte < limite) {
        unsigned char x = byte < na ? (unsigned char)a[byte] : 0;
        unsigned char y = byte < nb ? (unsigned char)b[byte] : 0;
        if (x != y) {
            unsigned char d = x ^ y;
            size_t i = 0;
            while (!(d & (0x80 >> i)))
                i++;
            return byte * 8 + i;
        }
        byte++;
    }
    return SIZE_MAX;  /* iguais */
}

static No *nova_folha(const char *chave, size_t tamanho, int64_t valor)
{
    No *f = ed_calloc(1, sizeof *f);
    f->bit = EXTERNO;
    f->chave = ed_strndup(chave, tamanho);
    f->tamanho = tamanho;
    f->valor = valor;
    return f;
}

Patricia *pat_criar(void)
{
    return ed_calloc(1, sizeof(Patricia));
}

static void destruir_no(No *no)
{
    if (no == NULL)
        return;
    if (no->bit == EXTERNO) {
        ed_free(no->chave);
    } else {
        destruir_no(no->esq);
        destruir_no(no->dir);
    }
    ed_free(no);
}

void pat_destruir(Patricia *p)
{
    if (p) {
        destruir_no(p->raiz);
        ed_free(p);
    }
}

/* Desce pelos bits até uma folha (a única candidata a ser igual à chave). */
static No *descer(No *no, const char *chave, size_t tamanho)
{
    while (no != NULL && no->bit != EXTERNO)
        no = bit_de(chave, tamanho, (size_t)no->bit) ? no->dir : no->esq;
    return no;
}

int pat_buscar(const Patricia *p, const char *chave, int64_t *valor)
{
    size_t tamanho = strlen(chave);
    No *f = descer(p->raiz, chave, tamanho);
    if (f == NULL || f->tamanho != tamanho || memcmp(f->chave, chave, tamanho) != 0)
        return 0;
    if (valor)
        *valor = f->valor;
    return 1;
}

int pat_inserir(Patricia *p, const char *chave, int64_t valor)
{
    size_t tamanho = strlen(chave);
    if (p->raiz == NULL) {
        p->raiz = nova_folha(chave, tamanho, valor);
        p->n = 1;
        return 1;
    }
    /* 1) A folha alcançada pelos bits da chave compartilha com ela o maior prefixo possível. */
    No *f = descer(p->raiz, chave, tamanho);
    size_t d = primeiro_bit_diferente(chave, tamanho, f->chave, f->tamanho);
    if (d == SIZE_MAX) {
        f->valor = valor;
        return 0;
    }
    /* 2) Desce de novo e para no primeiro ponto cujo bit é maior que d (ou numa folha):
     *    o novo nó interno, que testa o bit d, entra ali. */
    No **elo = &p->raiz;
    while ((*elo)->bit != EXTERNO && (size_t)(*elo)->bit < d)
        elo = bit_de(chave, tamanho, (size_t)(*elo)->bit) ? &(*elo)->dir : &(*elo)->esq;
    No *interno = ed_calloc(1, sizeof *interno);
    interno->bit = (int)d;
    No *folha = nova_folha(chave, tamanho, valor);
    if (bit_de(chave, tamanho, d)) {
        interno->esq = *elo;
        interno->dir = folha;
    } else {
        interno->esq = folha;
        interno->dir = *elo;
    }
    *elo = interno;
    p->n++;
    return 1;
}

int pat_remover(Patricia *p, const char *chave)
{
    size_t tamanho = strlen(chave);
    if (p->raiz == NULL)
        return 0;
    No **elo_pai = NULL, **elo = &p->raiz;
    while ((*elo)->bit != EXTERNO) {
        elo_pai = elo;
        elo = bit_de(chave, tamanho, (size_t)(*elo)->bit) ? &(*elo)->dir : &(*elo)->esq;
    }
    No *f = *elo;
    if (f->tamanho != tamanho || memcmp(f->chave, chave, tamanho) != 0)
        return 0;
    if (elo_pai == NULL) {
        p->raiz = NULL;
    } else {
        /* O pai fica com um filho só: é substituído pelo irmão da folha removida. */
        No *pai = *elo_pai;
        *elo_pai = pai->esq == f ? pai->dir : pai->esq;
        ed_free(pai);
    }
    ed_free(f->chave);
    ed_free(f);
    p->n--;
    return 1;
}

size_t pat_tamanho(const Patricia *p)
{
    return p->n;
}

static int altura(const No *no)
{
    if (no == NULL)
        return 0;
    if (no->bit == EXTERNO)
        return 1;
    int a = altura(no->esq), b = altura(no->dir);
    return 1 + (a > b ? a : b);
}

int pat_altura(const Patricia *p)
{
    return altura(p->raiz);
}

size_t pat_nos_internos(const Patricia *p)
{
    return p->n ? p->n - 1 : 0;
}

/* ---------------------------------------------------------------- percurso */
static int percorrer(const No *no, pat_visitante visitar, void *ctx, size_t *contagem, const char *prefixo,
                     size_t tam_prefixo)
{
    if (no->bit == EXTERNO) {
        if (no->tamanho < tam_prefixo || memcmp(no->chave, prefixo, tam_prefixo) != 0)
            return 0;
        (*contagem)++;
        return visitar ? visitar(no->chave, no->valor, ctx) : 0;
    }
    return percorrer(no->esq, visitar, ctx, contagem, prefixo, tam_prefixo) ||
           percorrer(no->dir, visitar, ctx, contagem, prefixo, tam_prefixo);
}

size_t pat_prefixo(const Patricia *p, const char *prefixo, pat_visitante visitar, void *ctx)
{
    size_t contagem = 0, tam = strlen(prefixo);
    const No *no = p->raiz;
    if (no == NULL)
        return 0;
    /* Desce enquanto o bit testado estiver dentro do prefixo; abaixo desse ponto todas as
     * chaves concordam com o prefixo ou nenhuma concorda (basta conferir uma folha). */
    while (no->bit != EXTERNO && (size_t)no->bit < 8 * tam)
        no = bit_de(prefixo, tam, (size_t)no->bit) ? no->dir : no->esq;
    const No *qualquer = no;
    while (qualquer->bit != EXTERNO)
        qualquer = qualquer->esq;
    if (qualquer->tamanho < tam || memcmp(qualquer->chave, prefixo, tam) != 0)
        return 0;
    percorrer(no, visitar, ctx, &contagem, prefixo, tam);
    return contagem;
}

/* ---------------------------------------------------------------- verificação e memória */
typedef struct {
    size_t folhas, internos;
    int ok;
} Estado;

/* Todas as chaves da subárvore devem concordar com `modelo` nos bits < limite. */
static void verificar_no(const No *no, int bit_pai, const char *modelo, size_t tam_modelo, size_t limite,
                         Estado *e)
{
    if (no == NULL) {
        e->ok = 0;
        return;
    }
    if (no->bit == EXTERNO) {
        e->folhas++;
        if (strlen(no->chave) != no->tamanho)
            e->ok = 0;
        if (modelo && primeiro_bit_diferente(no->chave, no->tamanho, modelo, tam_modelo) < limite)
            e->ok = 0;
        return;
    }
    e->internos++;
    if (no->bit <= bit_pai || no->esq == NULL || no->dir == NULL)
        e->ok = 0;
    if (!e->ok)
        return;
    /* Folhas à esquerda têm o bit = 0, à direita = 1, e concordam em tudo antes dele. */
    const No *l = no->esq, *r = no->dir;
    while (l->bit != EXTERNO)
        l = l->esq;
    while (r->bit != EXTERNO)
        r = r->esq;
    if (bit_de(l->chave, l->tamanho, (size_t)no->bit) != 0 || bit_de(r->chave, r->tamanho, (size_t)no->bit) != 1)
        e->ok = 0;
    if (primeiro_bit_diferente(l->chave, l->tamanho, r->chave, r->tamanho) != (size_t)no->bit)
        e->ok = 0;
    verificar_no(no->esq, no->bit, l->chave, l->tamanho, (size_t)no->bit + 1, e);
    verificar_no(no->dir, no->bit, r->chave, r->tamanho, (size_t)no->bit + 1, e);
}

int pat_verificar(const Patricia *p)
{
    if (p->raiz == NULL)
        return p->n == 0;
    Estado e = {0, 0, 1};
    verificar_no(p->raiz, -1, NULL, 0, 0, &e);
    return e.ok && e.folhas == p->n && e.internos == p->n - 1;
}

static size_t memoria(const No *no)
{
    if (no == NULL)
        return 0;
    if (no->bit == EXTERNO)
        return sizeof *no + no->tamanho + 1;
    return sizeof *no + memoria(no->esq) + memoria(no->dir);
}

size_t pat_memoria(const Patricia *p)
{
    return sizeof *p + memoria(p->raiz);
}
