/*
 * Testes da tabela hash: as quatro estratégias recebem as mesmas operações aleatórias e
 * são comparadas com uma referência; depois, o número médio de sondagens é comparado com
 * as fórmulas de Knuth (The Art of Computer Programming, vol. 3, seção 6.4).
 */
#include <math.h>
#include <string.h>

#include "ed/hash.h"
#include "ed/registro.h"
#include "teste.h"

static const HashEstrategia TODAS[] = {HASH_ENCADEAMENTO, HASH_LINEAR, HASH_QUADRATICA, HASH_DUPLO};

static void chave_de(int i, char *buf)
{
    snprintf(buf, 24, "aluno-%d", i);
}

static void teste_basico_e_exemplo_original(void)
{
    /* Os alunos do main original; antes, sem tratamento de colisão, um podia sobrescrever outro. */
    struct {
        int matricula;
        const char *nome;
    } alunos[] = {{12352, "Andre"}, {7894, "Ricardo"}, {3451, "Bianca"}, {5293, "Ana"}};
    for (int e = 0; e < 4; e++) {
        Hash *h = hash_criar(TODAS[e], 8, 0.75);
        for (int i = 0; i < 4; i++)
            VERIFICAR_IGUAL(hash_inserir(h, alunos[i].nome, alunos[i].matricula), 1);
        VERIFICAR_IGUAL(hash_inserir(h, "Ana", 1), 0);  /* atualiza, não duplica */
        int64_t v;
        VERIFICAR(hash_buscar(h, "Bianca", &v) && v == 3451);
        VERIFICAR(hash_buscar(h, "Ana", &v) && v == 1);
        VERIFICAR(!hash_buscar(h, "Carla", &v));
        VERIFICAR(hash_remover(h, "Ricardo"));
        VERIFICAR(!hash_remover(h, "Ricardo"));
        VERIFICAR(!hash_buscar(h, "Ricardo", NULL));
        VERIFICAR(hash_buscar(h, "Andre", NULL));
        VERIFICAR(hash_verificar(h));
        hash_destruir(h);
    }
}

static void teste_aleatorio_contra_referencia(void)
{
    enum { U = 5000 };
    static int presente[U];
    static int64_t valor[U];
    char k[24];
    for (int e = 0; e < 4; e++) {
        memset(presente, 0, sizeof presente);
        Hash *h = hash_criar(TODAS[e], 8, 0.7);
        Rng rng;
        rng_iniciar(&rng, 7 + (uint64_t)e);
        int falhas_antes = teste_falhas;
        for (int op = 0; op < 60000 && teste_falhas == falhas_antes; op++) {
            int i = (int)rng_ate(&rng, U);
            int tipo = (int)rng_ate(&rng, 10);
            chave_de(i, k);
            if (tipo < 5) {
                int64_t v = (int64_t)rng_proximo(&rng);
                VERIFICAR_IGUAL(hash_inserir(h, k, v), !presente[i]);
                presente[i] = 1;
                valor[i] = v;
            } else if (tipo < 8) {
                VERIFICAR_IGUAL(hash_remover(h, k), presente[i]);
                presente[i] = 0;
            } else {
                int64_t v = 0;
                VERIFICAR_IGUAL(hash_buscar(h, k, &v), presente[i]);
                if (presente[i])
                    VERIFICAR(v == valor[i]);
            }
            if (op % 1000 == 0)
                VERIFICAR(hash_verificar(h));
        }
        HashInfo info;
        hash_info(h, &info);
        int n = 0;
        for (int i = 0; i < U; i++)
            n += presente[i];
        VERIFICAR_IGUAL(info.quantidade, n);
        VERIFICAR(info.fator_carga <= 0.7 + 1e-9);
        VERIFICAR(info.redimensionamentos > 0);
        VERIFICAR(hash_verificar(h));
        hash_destruir(h);
    }
}

static void teste_tabela_cheia_sem_crescimento(void)
{
    /* Sem crescimento automático, todas as estratégias de endereçamento aberto preenchem
     * as 64 posições: a sondagem cobre a tabela inteira (potência de 2). */
    char k[24];
    for (int e = 1; e < 4; e++) {
        Hash *h = hash_criar(TODAS[e], 64, 0);
        for (int i = 0; i < 64; i++) {
            chave_de(i, k);
            VERIFICAR_IGUAL(hash_inserir(h, k, i), 1);
        }
        VERIFICAR_IGUAL(hash_inserir(h, "mais uma", 0), -1);
        for (int i = 0; i < 64; i++) {
            chave_de(i, k);
            VERIFICAR(hash_buscar(h, k, NULL));
        }
        VERIFICAR(hash_verificar(h));
        hash_destruir(h);
    }
}

static void teste_lapides(void)
{
    char k[24];
    Hash *h = hash_criar(HASH_LINEAR, 16, 0.75);
    for (int i = 0; i < 10; i++) {
        chave_de(i, k);
        hash_inserir(h, k, i);
    }
    for (int i = 0; i < 10; i += 2) {
        chave_de(i, k);
        hash_remover(h, k);
    }
    HashInfo info;
    hash_info(h, &info);
    VERIFICAR_IGUAL(info.lapides, 5);
    /* As chaves ímpares, que podem estar depois de uma lápide na sondagem, continuam lá. */
    for (int i = 1; i < 10; i += 2) {
        chave_de(i, k);
        VERIFICAR(hash_buscar(h, k, NULL));
    }
    /* Reinserir reaproveita lápide; muitas lápides disparam limpeza no redimensionamento. */
    for (int rodada = 0; rodada < 200; rodada++) {
        chave_de(1000 + rodada, k);
        hash_inserir(h, k, rodada);
        hash_remover(h, k);
    }
    hash_info(h, &info);
    VERIFICAR(info.lapides + info.quantidade <= (size_t)(0.75 * (double)info.capacidade) + 1);
    VERIFICAR(hash_verificar(h));
    hash_destruir(h);
}

static void teste_funcao_original_sem_overflow(void)
{
    /* O valor de 31·h + c em 32 bits sem sinal é bem definido; conferido à mão para "ab". */
    VERIFICAR_IGUAL(hash_polinomial31("ab"), (7u * 31u + 'a') * 31u + 'b');
    char longa[2000];
    memset(longa, 'z', sizeof longa - 1);
    longa[sizeof longa - 1] = '\0';
    VERIFICAR(hash_polinomial31(longa) <= UINT32_MAX);
    Hash *h = hash_criar(HASH_DUPLO, 8, 0.7);
    hash_definir_funcao(h, HASH_POLINOMIAL31);
    char k[24];
    for (int i = 0; i < 1000; i++) {
        chave_de(i, k);
        hash_inserir(h, k, i);
    }
    VERIFICAR(hash_verificar(h));
    hash_destruir(h);
}

/* ---------------------------------------------------------------- sondagens × teoria */
static double teoria_sucesso(HashEstrategia e, double a)
{
    switch (e) {
    case HASH_ENCADEAMENTO: return 1 + a / 2;
    case HASH_LINEAR: return 0.5 * (1 + 1 / (1 - a));
    case HASH_QUADRATICA: return 1 - log(1 - a) - a / 2;
    case HASH_DUPLO: return (1 / a) * log(1 / (1 - a));
    }
    return 0;
}

static double teoria_fracasso(HashEstrategia e, double a)
{
    switch (e) {
    case HASH_ENCADEAMENTO: return a;
    case HASH_LINEAR: return 0.5 * (1 + 1 / ((1 - a) * (1 - a)));
    case HASH_QUADRATICA: return 1 / (1 - a) - a - log(1 - a);
    case HASH_DUPLO: return 1 / (1 - a);
    }
    return 0;
}

static void teste_sondagens_conferem_com_knuth(void)
{
    const double cargas[] = {0.25, 0.5, 0.75, 0.9};
    char k[24];
    for (int e = 0; e < 4; e++)
        for (int c = 0; c < 4; c++) {
            size_t cap = 1 << 16;
            int n = (int)(cargas[c] * (double)cap);
            Hash *h = hash_criar(TODAS[e], cap, 0);
            for (int i = 0; i < n; i++) {
                chave_de(i, k);
                hash_inserir(h, k, i);
            }
            hash_zerar_sondagens(h);
            for (int i = 0; i < n; i++) {
                chave_de(i, k);
                hash_buscar(h, k, NULL);
            }
            HashInfo info;
            hash_info(h, &info);
            double sucesso = (double)info.sondagens / (double)info.operacoes;
            hash_zerar_sondagens(h);
            for (int i = 0; i < 20000; i++) {
                snprintf(k, sizeof k, "ausente-%d", i);
                hash_buscar(h, k, NULL);
            }
            hash_info(h, &info);
            double fracasso = (double)info.sondagens / (double)info.operacoes;
            double ts = teoria_sucesso(TODAS[e], cargas[c]), tf = teoria_fracasso(TODAS[e], cargas[c]);
            /* Encadeamento conta só as chaves comparadas; aberto conta também a posição vazia final
             * (as fórmulas de Knuth seguem a mesma convenção). */
            int ok = fabs(sucesso - ts) / ts < 0.12 && fabs(fracasso - tf) / (tf > 0.5 ? tf : 0.5) < 0.2;
            VERIFICAR(ok);
            if (!ok)
                fprintf(stderr, "    %s α=%.2f: sucesso %.3f (teoria %.3f), fracasso %.3f (teoria %.3f)\n",
                        hash_nome(TODAS[e]), cargas[c], sucesso, ts, fracasso, tf);
            hash_destruir(h);
        }
}

int main(void)
{
    TESTE(teste_basico_e_exemplo_original);
    TESTE(teste_aleatorio_contra_referencia);
    TESTE(teste_tabela_cheia_sem_crescimento);
    TESTE(teste_lapides);
    TESTE(teste_funcao_original_sem_overflow);
    TESTE(teste_sondagens_conferem_com_knuth);
    return teste_resultado();
}
