#include "ed/hash.h"

#include <string.h>

#include "ed/memoria.h"

/* ---------------------------------------------------------------- funções hash */
uint64_t hash_fnv1a(const char *s)
{
    uint64_t h = 0xcbf29ce484222325ULL;
    for (; *s; s++) {
        h ^= (unsigned char)*s;
        h *= 0x100000001b3ULL;
    }
    /* Mistura final (de splitmix64): espalha a influência de todos os bits pelos bits baixos,
     * que são os usados pela máscara de uma tabela com capacidade potência de 2. */
    h ^= h >> 30;
    h *= 0xbf58476d1ce4e5b9ULL;
    h ^= h >> 27;
    h *= 0x94d049bb133111ebULL;
    return h ^ (h >> 31);
}

uint64_t hash_polinomial31(const char *s)
{
    uint32_t h = 7;  /* aritmética sem sinal: o estouro é bem definido (módulo 2^32) */
    for (; *s; s++)
        h = 31u * h + (unsigned char)*s;
    return h;
}

/* ---------------------------------------------------------------- estrutura */
typedef struct Entrada {
    char *chave;
    int64_t valor;
    uint64_t hash;
    struct Entrada *prox;   /* encadeamento */
} Entrada;

static char LAPIDE_MARCA;
#define LAPIDE ((Entrada *)&LAPIDE_MARCA)

struct Hash {
    HashEstrategia estrategia;
    HashFuncao funcao;
    Entrada **posicoes;
    size_t capacidade;      /* potência de 2 */
    size_t quantidade;
    size_t lapides;
    double carga_maxima;
    uint64_t sondagens;
    uint64_t operacoes;
    size_t redimensionamentos;
};

static uint64_t calcular_hash(const Hash *h, const char *chave)
{
    return h->funcao == HASH_FNV1A ? hash_fnv1a(chave) : hash_polinomial31(chave);
}

static size_t potencia_de_2(size_t n)
{
    size_t p = 8;
    while (p < n)
        p <<= 1;
    return p;
}

Hash *hash_criar(HashEstrategia estrategia, size_t capacidade_inicial, double carga_maxima)
{
    Hash *h = ed_calloc(1, sizeof *h);
    h->estrategia = estrategia;
    h->funcao = HASH_FNV1A;
    h->capacidade = potencia_de_2(capacidade_inicial);
    h->posicoes = ed_calloc(h->capacidade, sizeof *h->posicoes);
    h->carga_maxima = carga_maxima;
    return h;
}

void hash_definir_funcao(Hash *h, HashFuncao f)
{
    if (h->quantidade == 0 && h->lapides == 0)
        h->funcao = f;
}

static void liberar_entrada(Entrada *e)
{
    ed_free(e->chave);
    ed_free(e);
}

void hash_destruir(Hash *h)
{
    if (h == NULL)
        return;
    for (size_t i = 0; i < h->capacidade; i++) {
        Entrada *e = h->posicoes[i];
        if (h->estrategia == HASH_ENCADEAMENTO) {
            while (e) {
                Entrada *prox = e->prox;
                liberar_entrada(e);
                e = prox;
            }
        } else if (e && e != LAPIDE) {
            liberar_entrada(e);
        }
    }
    ed_free(h->posicoes);
    ed_free(h);
}

/* i-ésima posição da sequência de sondagem (i = 0, 1, 2, ...). */
static size_t sondar(const Hash *h, uint64_t hash, size_t i)
{
    size_t mascara = h->capacidade - 1;
    size_t base = (size_t)hash & mascara;
    switch (h->estrategia) {
    case HASH_LINEAR:
        return (base + i) & mascara;
    case HASH_QUADRATICA:
        return (base + i * (i + 1) / 2) & mascara;   /* 0, 1, 3, 6, 10, ...: cobre tudo se m = 2^k */
    case HASH_DUPLO: {
        /* Segunda função hash obtida remisturando a primeira (vale mesmo para hashes de 32 bits).
         * Passo ímpar ⇒ coprimo com 2^k ⇒ a sequência cobre todas as posições. */
        size_t passo = (size_t)((hash * 0x9E3779B97F4A7C15ULL) >> 32) | 1;
        return (base + i * passo) & mascara;
    }
    default:
        return base;
    }
}

static int iguais(const Entrada *e, uint64_t hash, const char *chave)
{
    return e->hash == hash && strcmp(e->chave, chave) == 0;
}

/* Endereçamento aberto: posição da chave, ou SIZE_MAX. `livre` recebe a primeira posição
 * reutilizável (vazia ou lápide) vista no caminho, para a inserção. */
static size_t localizar_aberto(Hash *h, const char *chave, uint64_t hash, size_t *livre)
{
    size_t primeira_livre = SIZE_MAX;
    for (size_t i = 0; i < h->capacidade; i++) {
        size_t p = sondar(h, hash, i);
        Entrada *e = h->posicoes[p];
        h->sondagens++;
        if (e == NULL) {
            if (primeira_livre == SIZE_MAX)
                primeira_livre = p;
            break;
        }
        if (e == LAPIDE) {
            if (primeira_livre == SIZE_MAX)
                primeira_livre = p;
        } else if (iguais(e, hash, chave)) {
            if (livre)
                *livre = primeira_livre;
            return p;
        }
    }
    if (livre)
        *livre = primeira_livre;
    return SIZE_MAX;
}

static void reinserir(Hash *h, Entrada *e)
{
    if (h->estrategia == HASH_ENCADEAMENTO) {
        size_t p = (size_t)e->hash & (h->capacidade - 1);
        e->prox = h->posicoes[p];
        h->posicoes[p] = e;
        return;
    }
    for (size_t i = 0;; i++) {
        size_t p = sondar(h, e->hash, i);
        if (h->posicoes[p] == NULL) {
            h->posicoes[p] = e;
            return;
        }
    }
}

static void redimensionar(Hash *h, size_t nova_capacidade)
{
    Entrada **antigas = h->posicoes;
    size_t cap_antiga = h->capacidade;
    h->posicoes = ed_calloc(nova_capacidade, sizeof *h->posicoes);
    h->capacidade = nova_capacidade;
    h->lapides = 0;
    for (size_t i = 0; i < cap_antiga; i++) {
        Entrada *e = antigas[i];
        if (h->estrategia == HASH_ENCADEAMENTO) {
            while (e) {
                Entrada *prox = e->prox;
                reinserir(h, e);
                e = prox;
            }
        } else if (e && e != LAPIDE) {
            reinserir(h, e);
        }
    }
    ed_free(antigas);
    h->redimensionamentos++;
}

static void talvez_crescer(Hash *h)
{
    if (h->carga_maxima <= 0)
        return;
    /* Lápides ocupam posições: entram no cálculo da carga do endereçamento aberto. */
    double ocupacao = (double)(h->quantidade + h->lapides + 1) / (double)h->capacidade;
    if (ocupacao <= h->carga_maxima)
        return;
    size_t nova = h->capacidade;
    if ((double)(h->quantidade + 1) / (double)h->capacidade > h->carga_maxima / 2)
        nova *= 2;  /* senão, só limpa as lápides mantendo o tamanho */
    redimensionar(h, nova);
}

int hash_inserir(Hash *h, const char *chave, int64_t valor)
{
    uint64_t hash = calcular_hash(h, chave);
    h->operacoes++;
    if (h->estrategia == HASH_ENCADEAMENTO) {
        size_t p = (size_t)hash & (h->capacidade - 1);
        for (Entrada *e = h->posicoes[p]; e; e = e->prox) {
            h->sondagens++;
            if (iguais(e, hash, chave)) {
                e->valor = valor;
                return 0;
            }
        }
        talvez_crescer(h);
        Entrada *novo = ed_malloc(sizeof *novo);
        novo->chave = ed_strdup(chave);
        novo->valor = valor;
        novo->hash = hash;
        reinserir(h, novo);
        h->quantidade++;
        return 1;
    }
    size_t livre;
    size_t p = localizar_aberto(h, chave, hash, &livre);
    if (p != SIZE_MAX) {
        h->posicoes[p]->valor = valor;
        return 0;
    }
    if (h->carga_maxima > 0 &&
        (double)(h->quantidade + h->lapides + 1) / (double)h->capacidade > h->carga_maxima) {
        talvez_crescer(h);
        localizar_aberto(h, chave, hash, &livre);
    }
    if (livre == SIZE_MAX)
        return -1;  /* tabela cheia (só sem crescimento automático) */
    if (h->posicoes[livre] == LAPIDE)
        h->lapides--;
    Entrada *novo = ed_malloc(sizeof *novo);
    novo->chave = ed_strdup(chave);
    novo->valor = valor;
    novo->hash = hash;
    novo->prox = NULL;
    h->posicoes[livre] = novo;
    h->quantidade++;
    return 1;
}

int hash_buscar(Hash *h, const char *chave, int64_t *valor)
{
    uint64_t hash = calcular_hash(h, chave);
    h->operacoes++;
    if (h->estrategia == HASH_ENCADEAMENTO) {
        for (Entrada *e = h->posicoes[(size_t)hash & (h->capacidade - 1)]; e; e = e->prox) {
            h->sondagens++;
            if (iguais(e, hash, chave)) {
                if (valor)
                    *valor = e->valor;
                return 1;
            }
        }
        return 0;
    }
    size_t p = localizar_aberto(h, chave, hash, NULL);
    if (p == SIZE_MAX)
        return 0;
    if (valor)
        *valor = h->posicoes[p]->valor;
    return 1;
}

int hash_remover(Hash *h, const char *chave)
{
    uint64_t hash = calcular_hash(h, chave);
    h->operacoes++;
    if (h->estrategia == HASH_ENCADEAMENTO) {
        Entrada **elo = &h->posicoes[(size_t)hash & (h->capacidade - 1)];
        for (; *elo; elo = &(*elo)->prox) {
            h->sondagens++;
            if (iguais(*elo, hash, chave)) {
                Entrada *e = *elo;
                *elo = e->prox;
                liberar_entrada(e);
                h->quantidade--;
                return 1;
            }
        }
        return 0;
    }
    size_t p = localizar_aberto(h, chave, hash, NULL);
    if (p == SIZE_MAX)
        return 0;
    liberar_entrada(h->posicoes[p]);
    h->posicoes[p] = LAPIDE;
    h->quantidade--;
    h->lapides++;
    return 1;
}

void hash_percorrer(const Hash *h, int (*visitar)(const char *, int64_t, void *), void *ctx)
{
    for (size_t i = 0; i < h->capacidade; i++) {
        Entrada *e = h->posicoes[i];
        if (e == LAPIDE)
            continue;
        for (; e; e = h->estrategia == HASH_ENCADEAMENTO ? e->prox : NULL)
            if (visitar(e->chave, e->valor, ctx))
                return;
    }
}

/* ---------------------------------------------------------------- estatísticas e verificação */
static size_t comprimento_sondagem(const Hash *h, const Entrada *alvo)
{
    for (size_t i = 0; i < h->capacidade; i++)
        if (h->posicoes[sondar(h, alvo->hash, i)] == alvo)
            return i + 1;
    return SIZE_MAX;
}

void hash_info(const Hash *h, HashInfo *info)
{
    info->quantidade = h->quantidade;
    info->capacidade = h->capacidade;
    info->lapides = h->lapides;
    info->fator_carga = (double)h->quantidade / (double)h->capacidade;
    info->sondagens = h->sondagens;
    info->operacoes = h->operacoes;
    info->redimensionamentos = h->redimensionamentos;
    info->maior_cadeia = 0;
    for (size_t i = 0; i < h->capacidade; i++) {
        size_t c = 0;
        if (h->estrategia == HASH_ENCADEAMENTO) {
            for (Entrada *e = h->posicoes[i]; e; e = e->prox)
                c++;
        } else if (h->posicoes[i] && h->posicoes[i] != LAPIDE) {
            c = comprimento_sondagem(h, h->posicoes[i]);
        }
        if (c > info->maior_cadeia)
            info->maior_cadeia = c;
    }
}

void hash_zerar_sondagens(Hash *h)
{
    h->sondagens = h->operacoes = 0;
}

int hash_verificar(const Hash *h)
{
    size_t contadas = 0, lapides = 0;
    for (size_t i = 0; i < h->capacidade; i++) {
        Entrada *e = h->posicoes[i];
        if (h->estrategia == HASH_ENCADEAMENTO) {
            for (; e; e = e->prox) {
                if (((size_t)e->hash & (h->capacidade - 1)) != i || e->hash != calcular_hash(h, e->chave))
                    return 0;
                contadas++;
            }
            continue;
        }
        if (e == LAPIDE) {
            lapides++;
            continue;
        }
        if (e == NULL)
            continue;
        contadas++;
        if (e->hash != calcular_hash(h, e->chave))
            return 0;
        /* Nenhuma posição VAZIA pode existir antes da chave na sua sequência de sondagem,
         * senão a busca pararia cedo e não a encontraria. */
        for (size_t k = 0;; k++) {
            Entrada *x = h->posicoes[sondar(h, e->hash, k)];
            if (x == e)
                break;
            if (x == NULL || k >= h->capacidade)
                return 0;
        }
    }
    return contadas == h->quantidade && (h->estrategia == HASH_ENCADEAMENTO || lapides == h->lapides);
}

const char *hash_nome(HashEstrategia e)
{
    switch (e) {
    case HASH_ENCADEAMENTO: return "encadeamento";
    case HASH_LINEAR: return "linear";
    case HASH_QUADRATICA: return "quadrática";
    case HASH_DUPLO: return "hash duplo";
    }
    return "?";
}
