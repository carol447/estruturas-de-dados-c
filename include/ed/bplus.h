#ifndef ED_BPLUS_H
#define ED_BPLUS_H

/*
 * Árvore B+ armazenada em arquivo, em páginas de BP_PAGINA bytes.
 *
 * - Cada nó ocupa uma página. Página 0 é o cabeçalho (raiz, altura, contagens,
 *   lista de páginas livres). Páginas liberadas por fusões são reaproveitadas.
 * - Todas as chaves ficam nas folhas, que formam uma lista duplamente ligada:
 *   consultas por intervalo descem uma vez e depois só seguem os ponteiros.
 * - Nós internos guardam separadores: no filho i ficam chaves < K[i] e no
 *   filho i+1 chaves >= K[i].
 * - "ordem" = número máximo de chaves por nó (folha ou interno). Ocupação
 *   mínima de nós não raiz: folhas ⌊(ordem+1)/2⌋, internos ⌊ordem/2⌋.
 * - Remoção completa: empréstimo do irmão e, se não der, fusão; a raiz
 *   encolhe quando fica sem chaves.
 * - Contadores de leitura/escrita de páginas mostram o custo em E/S.
 *
 * Valor associado: tipicamente a posição do registro num arquivo de dados.
 */

#include <stdint.h>
#include <stdio.h>

#define BP_PAGINA 4096
#define BP_ORDEM_MIN 3
#define BP_ORDEM_MAX 340   /* cabe numa página: 16 + 340 × (4 + 8) = 4096 bytes */

typedef int32_t bp_chave;
typedef int64_t bp_valor;

enum {
    BP_OK = 0,
    BP_DUPLICADA = 1,
    BP_NAO_ENCONTRADA = 2,
    BP_ERRO_IO = -1,
    BP_ERRO_FORMATO = -2,
    BP_ERRO_ARGUMENTO = -3,
    BP_ERRO_INVARIANTE = -4,
};

typedef struct BPlus BPlus;

typedef struct {
    int ordem;
    int min_folha;
    int min_interno;
    int altura;               /* 1 = só a raiz-folha */
    int64_t chaves;
    uint32_t paginas;         /* inclui o cabeçalho */
    uint32_t paginas_livres;
    uint32_t folhas;
    uint64_t leituras;        /* páginas lidas desde a abertura ou do último bp_zerar_contadores */
    uint64_t escritas;
} BPlusInfo;

/* Retorna nonzero para interromper o percurso. */
typedef int (*bp_visitante)(bp_chave chave, bp_valor valor, void *ctx);
/* Fornece o próximo par (em ordem crescente); retorna 0 quando acabou. */
typedef int (*bp_fonte)(void *ctx, bp_chave *chave, bp_valor *valor);

int bp_criar(const char *arquivo, int ordem, BPlus **arvore);
int bp_abrir(const char *arquivo, BPlus **arvore);
int bp_fechar(BPlus *t);
int bp_sincronizar(BPlus *t);

int bp_inserir(BPlus *t, bp_chave chave, bp_valor valor);
int bp_buscar(BPlus *t, bp_chave chave, bp_valor *valor);
int bp_atualizar(BPlus *t, bp_chave chave, bp_valor valor);
int bp_remover(BPlus *t, bp_chave chave);

/* Visita em ordem todas as chaves em [inicio, fim]; devolve quantas visitou (ou erro < 0). */
int64_t bp_intervalo(BPlus *t, bp_chave inicio, bp_chave fim, bp_visitante visitar, void *ctx);

/* Carga em lote numa árvore vazia: n pares em ordem estritamente crescente.
 * `preenchimento` (0,5 a 1,0) é a fração de cada nó a ocupar (deixa folga para inserções). */
int bp_carregar(BPlus *t, int64_t n, bp_fonte fonte, void *ctx, double preenchimento);

/* Confere todas as invariantes; em caso de falha descreve o problema em `msg`. */
int bp_verificar(BPlus *t, char *msg, size_t tamanho);

void bp_info(BPlus *t, BPlusInfo *info);
void bp_zerar_contadores(BPlus *t);
/* Desenha os níveis da árvore (no máximo `max_nos` nós por nível). */
int bp_imprimir(BPlus *t, FILE *saida, int max_nos);

const char *bp_mensagem(int codigo);

#endif
