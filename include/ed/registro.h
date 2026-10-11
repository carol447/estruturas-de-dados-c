#ifndef ED_REGISTRO_H
#define ED_REGISTRO_H

/*
 * Registro de aluno usado pelo arquivo de dados, pela ordenação externa e
 * pelo índice B+ (chave = matrícula, valor = posição do registro no arquivo).
 *
 * No disco cada registro ocupa ALUNO_TAMANHO bytes em formato fixo e
 * little-endian, independente do compilador: escrever a struct com fwrite
 * dependeria de padding e da ordem dos bytes da máquina.
 */

#include <stdint.h>
#include <stdio.h>

#define ALUNO_NOME 40
#define ALUNO_TAMANHO 56   /* 4 (matrícula) + 40 (nome) + 3 × 4 (notas) */

typedef struct {
    int32_t matricula;
    char nome[ALUNO_NOME];  /* sempre terminado em '\0' */
    float notas[3];
} Aluno;

void aluno_serializar(const Aluno *a, uint8_t buf[ALUNO_TAMANHO]);
void aluno_desserializar(const uint8_t buf[ALUNO_TAMANHO], Aluno *a);

/* Lê/escreve o registro de índice `posicao` (0, 1, 2, ...) de um arquivo de dados. */
int aluno_ler(FILE *f, int64_t posicao, Aluno *a);
int aluno_escrever(FILE *f, int64_t posicao, const Aluno *a);
int64_t aluno_contar(FILE *f);

float aluno_media(const Aluno *a);

/* ---------------------------------------------------------------- gerador pseudoaleatório */
/* splitmix64: rápido, de boa qualidade estatística e determinístico (testes reproduzíveis). */
typedef struct {
    uint64_t estado;
} Rng;

void rng_iniciar(Rng *r, uint64_t semente);
uint64_t rng_proximo(Rng *r);
uint64_t rng_ate(Rng *r, uint64_t limite);   /* uniforme em [0, limite), sem viés de módulo */
void rng_embaralhar(Rng *r, int32_t *v, size_t n);

void aluno_aleatorio(Rng *r, int32_t matricula, Aluno *a);

#endif
