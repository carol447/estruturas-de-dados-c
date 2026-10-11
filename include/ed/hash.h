#ifndef ED_HASH_H
#define ED_HASH_H

/*
 * Tabela hash de chaves texto -> int64, com quatro formas de tratar colisões:
 *
 *   HASH_ENCADEAMENTO   listas ligadas por posição (encadeamento separado)
 *   HASH_LINEAR         endereçamento aberto, sondagem h, h+1, h+2, ...
 *   HASH_QUADRATICA     endereçamento aberto, h + i(i+1)/2 (números triangulares)
 *   HASH_DUPLO          endereçamento aberto, h1 + i·h2 com h2 ímpar
 *
 * Capacidades são potências de 2: com uma boa função hash (FNV-1a de 64 bits,
 * com mistura final) isso não prejudica a distribuição, e garante que as
 * sondagens quadrática (triangular) e dupla (passo ímpar) visitam TODAS as
 * posições, ou seja, a inserção nunca falha com espaço livre.
 *
 * Remoção no endereçamento aberto usa marcadores ("lápides"): apagar a posição
 * quebraria a sequência de sondagem de outras chaves. Lápides contam como
 * ocupadas para o fator de carga e são descartadas no próximo
 * redimensionamento.
 *
 * Correções em relação à versão original: a inserção "sem colisão"
 * sobrescrevia a posição e vazava memória; não havia remoção nem verificação
 * de duplicatas; o hash de strings tinha overflow de int com sinal
 * (comportamento indefinido); a "dobra" usava TABLE_SIZE−1 como máscara sem
 * exigir potência de 2.
 */

#include <stddef.h>
#include <stdint.h>

typedef enum {
    HASH_ENCADEAMENTO,
    HASH_LINEAR,
    HASH_QUADRATICA,
    HASH_DUPLO,
} HashEstrategia;

typedef enum {
    HASH_FNV1A,         /* padrão */
    HASH_POLINOMIAL31,  /* h = 31·h + c, a do original (agora sem sinal) */
} HashFuncao;

typedef struct Hash Hash;

typedef struct {
    size_t quantidade;
    size_t capacidade;
    size_t lapides;
    double fator_carga;
    size_t maior_cadeia;          /* encadeamento: maior lista; aberto: maior sequência de sondagem */
    uint64_t sondagens;           /* acumulado desde a criação ou do último hash_zerar_sondagens */
    uint64_t operacoes;
    size_t redimensionamentos;
} HashInfo;

/* `carga_maxima`: dispara o crescimento (0 = nunca cresce; usado nos experimentos). */
Hash *hash_criar(HashEstrategia estrategia, size_t capacidade_inicial, double carga_maxima);
void hash_definir_funcao(Hash *h, HashFuncao f);   /* só com a tabela vazia */
void hash_destruir(Hash *h);

int hash_inserir(Hash *h, const char *chave, int64_t valor);   /* 1 = nova, 0 = atualizada, -1 = cheia */
int hash_buscar(Hash *h, const char *chave, int64_t *valor);
int hash_remover(Hash *h, const char *chave);

/* Percorre todos os pares (ordem da tabela). Retornar nonzero interrompe. */
void hash_percorrer(const Hash *h, int (*visitar)(const char *, int64_t, void *), void *ctx);

void hash_info(const Hash *h, HashInfo *info);
void hash_zerar_sondagens(Hash *h);
/* Confere contagens, lápides e que toda chave é encontrável a partir da sua posição. */
int hash_verificar(const Hash *h);
const char *hash_nome(HashEstrategia e);

uint64_t hash_fnv1a(const char *s);
uint64_t hash_polinomial31(const char *s);

#endif
