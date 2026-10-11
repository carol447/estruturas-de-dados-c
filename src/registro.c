#include "ed/registro.h"

#include "ed/arquivo.h"

#include <string.h>

static void escrever_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static uint32_t ler_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void escrever_f32(uint8_t *p, float f)
{
    uint32_t v;
    memcpy(&v, &f, sizeof v);  /* IEEE 754 em 32 bits */
    escrever_u32(p, v);
}

static float ler_f32(const uint8_t *p)
{
    uint32_t v = ler_u32(p);
    float f;
    memcpy(&f, &v, sizeof f);
    return f;
}

void aluno_serializar(const Aluno *a, uint8_t buf[ALUNO_TAMANHO])
{
    escrever_u32(buf, (uint32_t)a->matricula);
    memset(buf + 4, 0, ALUNO_NOME);
    size_t n = strnlen(a->nome, ALUNO_NOME - 1);
    memcpy(buf + 4, a->nome, n);
    for (int i = 0; i < 3; i++)
        escrever_f32(buf + 44 + 4 * i, a->notas[i]);
}

void aluno_desserializar(const uint8_t buf[ALUNO_TAMANHO], Aluno *a)
{
    a->matricula = (int32_t)ler_u32(buf);
    memcpy(a->nome, buf + 4, ALUNO_NOME);
    a->nome[ALUNO_NOME - 1] = '\0';
    for (int i = 0; i < 3; i++)
        a->notas[i] = ler_f32(buf + 44 + 4 * i);
}

int aluno_ler(FILE *f, int64_t posicao, Aluno *a)
{
    uint8_t buf[ALUNO_TAMANHO];
    if (ed_fseek(f, posicao * ALUNO_TAMANHO) != 0 || fread(buf, ALUNO_TAMANHO, 1, f) != 1)
        return 0;
    aluno_desserializar(buf, a);
    return 1;
}

int aluno_escrever(FILE *f, int64_t posicao, const Aluno *a)
{
    uint8_t buf[ALUNO_TAMANHO];
    aluno_serializar(a, buf);
    return ed_fseek(f, posicao * ALUNO_TAMANHO) == 0 && fwrite(buf, ALUNO_TAMANHO, 1, f) == 1;
}

int64_t aluno_contar(FILE *f)
{
    int64_t fim = ed_tamanho_arquivo(f);
    return fim < 0 ? -1 : fim / ALUNO_TAMANHO;
}

float aluno_media(const Aluno *a)
{
    return (a->notas[0] + a->notas[1] + a->notas[2]) / 3.0f;
}

/* ---------------------------------------------------------------- splitmix64 */
void rng_iniciar(Rng *r, uint64_t semente)
{
    r->estado = semente;
}

uint64_t rng_proximo(Rng *r)
{
    uint64_t z = (r->estado += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

uint64_t rng_ate(Rng *r, uint64_t limite)
{
    /* Rejeita a "sobra" para que todos os valores tenham a mesma probabilidade. */
    uint64_t teto = UINT64_MAX - UINT64_MAX % limite;
    uint64_t x;
    do {
        x = rng_proximo(r);
    } while (x >= teto);
    return x % limite;
}

void rng_embaralhar(Rng *r, int32_t *v, size_t n)
{
    for (size_t i = n; i > 1; i--) {  /* Fisher–Yates */
        size_t j = (size_t)rng_ate(r, i);
        int32_t t = v[i - 1];
        v[i - 1] = v[j];
        v[j] = t;
    }
}

static const char *PRENOMES[] = {
    "Ana", "Bruno", "Camila", "Diego", "Elisa", "Fábio", "Gabriela", "Heitor", "Isadora", "João",
    "Larissa", "Marcos", "Natália", "Otávio", "Paula", "Rafael", "Sofia", "Tiago", "Vitória", "Yuri",
};
static const char *SOBRENOMES[] = {
    "Almeida", "Barbosa", "Cardoso", "Duarte", "Esteves", "Ferraz", "Guimarães", "Hashimoto", "Ishikawa",
    "Junqueira", "Kowalski", "Lacerda", "Monteiro", "Nogueira", "Okafor", "Prado", "Quintana", "Rezende",
};

void aluno_aleatorio(Rng *r, int32_t matricula, Aluno *a)
{
    memset(a, 0, sizeof *a);
    a->matricula = matricula;
    snprintf(a->nome, ALUNO_NOME, "%s %s",
             PRENOMES[rng_ate(r, sizeof PRENOMES / sizeof *PRENOMES)],
             SOBRENOMES[rng_ate(r, sizeof SOBRENOMES / sizeof *SOBRENOMES)]);
    for (int i = 0; i < 3; i++)
        a->notas[i] = (float)rng_ate(r, 101) / 10.0f;  /* 0,0 a 10,0 */
}
