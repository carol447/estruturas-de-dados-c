/*
 * ed: linha de comando que junta as estruturas.
 *
 *   ed gerar <dados.bin> <n> [semente]
 *   ed ordenar <entrada.bin> <saida.bin> [--memoria M] [--metodo quicksort|selecao] [--vias K]
 *   ed indexar <dados.bin> <indice.idx> [--ordem N] [--preenchimento F]
 *   ed consultar <dados.bin> <indice.idx> <matrícula>
 *   ed faixa <dados.bin> <indice.idx> <de> <até>
 *   ed bplus <indice.idx> info | verificar | imprimir | buscar K | inserir K V | remover K
 *   ed palavras <texto.txt> [prefixo]
 *   ed hash-experimento [n]
 *   ed demo
 */
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#endif

#include "ed/bplus.h"
#include "ed/hash.h"
#include "ed/memoria.h"
#include "ed/ordenacao_externa.h"
#include "ed/patricia.h"
#include "ed/radix.h"
#include "ed/registro.h"

/* Tempo decorrido em segundos (relógio monotônico; no Windows, clock() já mede tempo real). */
static double agora(void)
{
#ifdef _WIN32
    return (double)clock() / CLOCKS_PER_SEC;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
#endif
}

static int erro(const char *msg)
{
    fprintf(stderr, "erro: %s\n", msg);
    return EXIT_FAILURE;
}

static int erro_bp(const char *contexto, int codigo)
{
    fprintf(stderr, "erro: %s: %s\n", contexto, bp_mensagem(codigo));
    return EXIT_FAILURE;
}

static const char *opcao(int argc, char **argv, const char *nome, const char *padrao)
{
    for (int i = 0; i + 1 < argc; i++)
        if (strcmp(argv[i], nome) == 0)
            return argv[i + 1];
    return padrao;
}

/* Imprime `s` ocupando `largura` colunas: printf conta bytes, e em UTF-8 "á" ocupa 2 bytes e 1 coluna. */
static void imprimir_alinhado(const char *s, int largura)
{
    int colunas = 0;
    for (const unsigned char *p = (const unsigned char *)s; *p; p++)
        colunas += (*p & 0xC0) != 0x80;  /* conta só o primeiro byte de cada caractere */
    fputs(s, stdout);
    for (; colunas < largura; colunas++)
        putchar(' ');
}

static void imprimir_aluno(const Aluno *a, int64_t posicao)
{
    printf("  %d  ", a->matricula);
    imprimir_alinhado(a->nome, 24);
    printf(" notas %4.1f %4.1f %4.1f  média %4.1f  (registro %lld)\n", (double)a->notas[0], (double)a->notas[1],
           (double)a->notas[2], (double)aluno_media(a), (long long)posicao);
}

static void imprimir_info(BPlus *t)
{
    BPlusInfo i;
    bp_info(t, &i);
    printf("  ordem %d (folhas com %d a %d chaves, internos com %d a %d), altura %d\n", i.ordem, i.min_folha, i.ordem,
           i.min_interno, i.ordem, i.altura);
    printf("  %lld chaves em %u folhas; %u páginas de %d bytes (%u livres)\n", (long long)i.chaves, i.folhas,
           i.paginas, BP_PAGINA, i.paginas_livres);
    if (i.folhas)
        printf("  ocupação média das folhas: %.0f%%\n", 100.0 * (double)i.chaves / ((double)i.folhas * i.ordem));
}

/* ---------------------------------------------------------------- dados e ordenação */
static int cmd_gerar(int argc, char **argv)
{
    if (argc < 2)
        return erro("uso: ed gerar <dados.bin> <n> [semente]");
    int64_t n = atoll(argv[1]);
    uint64_t semente = argc > 2 ? strtoull(argv[2], NULL, 10) : 1;
    double t0 = agora();
    if (oe_gerar(argv[0], n, semente) != 0)
        return erro("não foi possível gravar o arquivo");
    printf("%lld alunos gravados em %s (%lld bytes, %d por registro) em %.2f s\n", (long long)n, argv[0],
           (long long)n * ALUNO_TAMANHO, ALUNO_TAMANHO, agora() - t0);
    return EXIT_SUCCESS;
}

static int cmd_ordenar(int argc, char **argv)
{
    if (argc < 2)
        return erro("uso: ed ordenar <entrada.bin> <saida.bin> [--memoria M] [--metodo quicksort|selecao] [--vias K]");
    int64_t memoria = atoll(opcao(argc, argv, "--memoria", "10000"));
    const char *metodo = opcao(argc, argv, "--metodo", "selecao");
    int vias = atoi(opcao(argc, argv, "--vias", "8"));
    OeMetodo m = strcmp(metodo, "quicksort") == 0 ? OE_QUICKSORT : OE_SELECAO;
    OeEstatisticas e;
    double t0 = agora();
    if (oe_ordenar(argv[0], argv[1], memoria, m, vias, &e) != 0)
        return erro("falha na ordenação (arquivo, memória >= 1 e vias >= 2)");
    printf("%lld registros ordenados em %.2f s com %lld registros na memória (%s, %d vias)\n", (long long)e.registros,
           agora() - t0, (long long)memoria, m == OE_QUICKSORT ? "quicksort" : "seleção por substituição", vias);
    printf("  corridas iniciais: %lld (tamanho médio %.0f = %.2f × memória)\n", (long long)e.corridas_iniciais,
           e.maior_corrida_media, e.maior_corrida_media / (double)memoria);
    printf("  passadas de intercalação: %d (⌈log_%d %lld⌉)\n", e.passadas, vias, (long long)e.corridas_iniciais);
    printf("  E/S: %lld registros lidos, %lld gravados\n", (long long)e.registros_lidos, (long long)e.registros_escritos);
    return oe_verificar(argv[1], NULL) ? EXIT_SUCCESS : erro("saída não ficou ordenada");
}

/* ---------------------------------------------------------------- índice B+ */
typedef struct {
    FILE *f;
    int64_t pos;
} FonteDados;

static int proximo_par(void *ctx, bp_chave *c, bp_valor *v)
{
    FonteDados *fd = ctx;
    uint8_t buf[ALUNO_TAMANHO];
    if (fread(buf, ALUNO_TAMANHO, 1, fd->f) != 1)
        return 0;
    Aluno a;
    aluno_desserializar(buf, &a);
    *c = a.matricula;
    *v = fd->pos++;
    return 1;
}

static int cmd_indexar(int argc, char **argv)
{
    if (argc < 2)
        return erro("uso: ed indexar <dados.bin> <indice.idx> [--ordem N] [--preenchimento F]");
    int ordem = atoi(opcao(argc, argv, "--ordem", "128"));
    double preench = atof(opcao(argc, argv, "--preenchimento", "1.0"));
    int64_t n;
    int ordenado = oe_verificar(argv[0], &n);
    BPlus *t;
    int r = bp_criar(argv[1], ordem, &t);
    if (r != BP_OK)
        return erro_bp("criar índice", r);
    FILE *f = fopen(argv[0], "rb");
    if (f == NULL) {
        bp_fechar(t);
        return erro("não foi possível abrir o arquivo de dados");
    }
    double t0 = agora();
    if (ordenado) {
        FonteDados fd = {f, 0};
        r = bp_carregar(t, n, proximo_par, &fd, preench);
        printf("arquivo ordenado: carga em lote de %lld chaves", (long long)n);
    } else {
        FonteDados fd = {f, 0};
        bp_chave c;
        bp_valor v;
        r = BP_OK;
        while (r == BP_OK && proximo_par(&fd, &c, &v))
            r = bp_inserir(t, c, v);
        printf("arquivo fora de ordem: %lld inserções individuais", (long long)n);
    }
    fclose(f);
    if (r != BP_OK) {
        bp_fechar(t);
        return erro_bp("\nindexar", r);
    }
    BPlusInfo info;
    bp_info(t, &info);
    printf(" em %.2f s (%llu páginas gravadas)\n", agora() - t0, (unsigned long long)info.escritas);
    imprimir_info(t);
    return bp_fechar(t) == BP_OK ? EXIT_SUCCESS : erro("falha ao gravar o índice");
}

static int cmd_consultar(int argc, char **argv)
{
    if (argc < 3)
        return erro("uso: ed consultar <dados.bin> <indice.idx> <matrícula>");
    BPlus *t;
    int r = bp_abrir(argv[1], &t);
    if (r != BP_OK)
        return erro_bp(argv[1], r);
    bp_valor pos;
    r = bp_buscar(t, (bp_chave)atol(argv[2]), &pos);
    BPlusInfo info;
    bp_info(t, &info);
    bp_fechar(t);
    if (r == BP_NAO_ENCONTRADA) {
        printf("matrícula %s não encontrada (%llu páginas do índice lidas)\n", argv[2], (unsigned long long)info.leituras);
        return EXIT_FAILURE;
    }
    FILE *f = fopen(argv[0], "rb");
    Aluno a;
    if (f == NULL || !aluno_ler(f, pos, &a)) {
        if (f)
            fclose(f);
        return erro("registro apontado pelo índice não existe no arquivo de dados");
    }
    fclose(f);
    imprimir_aluno(&a, pos);
    printf("  custo: %llu páginas do índice (= altura) + 1 leitura no arquivo de dados\n",
           (unsigned long long)info.leituras);
    return EXIT_SUCCESS;
}

typedef struct {
    FILE *dados;
    int64_t mostrados;
    double soma;
} Faixa;

static int visitar_faixa(bp_chave c, bp_valor v, void *ctx)
{
    (void)c;
    Faixa *fx = ctx;
    Aluno a;
    if (aluno_ler(fx->dados, v, &a)) {
        fx->soma += (double)aluno_media(&a);
        if (fx->mostrados++ < 10)
            imprimir_aluno(&a, v);
    }
    return 0;
}

static int cmd_faixa(int argc, char **argv)
{
    if (argc < 4)
        return erro("uso: ed faixa <dados.bin> <indice.idx> <de> <até>");
    BPlus *t;
    int r = bp_abrir(argv[1], &t);
    if (r != BP_OK)
        return erro_bp(argv[1], r);
    FILE *f = fopen(argv[0], "rb");
    if (f == NULL) {
        bp_fechar(t);
        return erro("não foi possível abrir o arquivo de dados");
    }
    Faixa fx = {f, 0, 0};
    int64_t n = bp_intervalo(t, (bp_chave)atol(argv[2]), (bp_chave)atol(argv[3]), visitar_faixa, &fx);
    BPlusInfo info;
    bp_info(t, &info);
    fclose(f);
    bp_fechar(t);
    if (n > 10)
        printf("  … e mais %lld\n", (long long)(n - 10));
    printf("%lld alunos com matrícula entre %s e %s; média geral %.2f\n", (long long)n, argv[2], argv[3],
           n ? fx.soma / (double)n : 0.0);
    printf("  páginas do índice lidas: %llu (desce uma vez e segue a lista de folhas)\n",
           (unsigned long long)info.leituras);
    return EXIT_SUCCESS;
}

static int cmd_bplus(int argc, char **argv)
{
    if (argc < 2)
        return erro("uso: ed bplus <indice.idx> info|verificar|imprimir|buscar K|inserir K V|remover K");
    BPlus *t;
    int r = bp_abrir(argv[0], &t);
    if (r != BP_OK)
        return erro_bp(argv[0], r);
    const char *acao = argv[1];
    int status = EXIT_SUCCESS;
    char msg[256];
    if (strcmp(acao, "info") == 0) {
        imprimir_info(t);
    } else if (strcmp(acao, "verificar") == 0) {
        r = bp_verificar(t, msg, sizeof msg);
        if (r == BP_OK)
            printf("ok: todas as invariantes conferem\n");
        else {
            printf("PROBLEMA: %s\n", msg);
            status = EXIT_FAILURE;
        }
    } else if (strcmp(acao, "imprimir") == 0) {
        bp_imprimir(t, stdout, argc > 2 ? atoi(argv[2]) : 12);
    } else if (strcmp(acao, "buscar") == 0 && argc > 2) {
        bp_valor v;
        r = bp_buscar(t, (bp_chave)atol(argv[2]), &v);
        if (r == BP_OK)
            printf("%s → %lld\n", argv[2], (long long)v);
        else {
            printf("%s\n", bp_mensagem(r));
            status = EXIT_FAILURE;
        }
    } else if (strcmp(acao, "inserir") == 0 && argc > 3) {
        r = bp_inserir(t, (bp_chave)atol(argv[2]), atoll(argv[3]));
        printf("%s\n", bp_mensagem(r));
        status = r == BP_OK ? EXIT_SUCCESS : EXIT_FAILURE;
    } else if (strcmp(acao, "remover") == 0 && argc > 2) {
        r = bp_remover(t, (bp_chave)atol(argv[2]));
        printf("%s\n", bp_mensagem(r));
        status = r == BP_OK ? EXIT_SUCCESS : EXIT_FAILURE;
    } else {
        status = erro("ação desconhecida");
    }
    if (bp_fechar(t) != BP_OK)
        status = erro("falha ao gravar o índice");
    return status;
}

/* ---------------------------------------------------------------- palavras: PATRICIA × radix × hash */
static int mostrar_palavra(const char *chave, int64_t valor, void *ctx)
{
    int *n = ctx;
    if ((*n)++ < 15)
        printf("    %-20s (%lld)\n", chave, (long long)valor);
    return 0;
}

typedef struct {
    const char *chave;
    int64_t valor;
} Par;

static int coletar_par(const char *chave, int64_t valor, void *ctx)
{
    Par *top = ctx;  /* top[0..4], ordenado por valor decrescente */
    for (int i = 0; i < 5; i++) {
        if (top[i].chave == NULL || valor > top[i].valor) {
            memmove(top + i + 1, top + i, (size_t)(4 - i) * sizeof *top);
            top[i].chave = chave;
            top[i].valor = valor;
            break;
        }
    }
    return 0;
}

static int cmd_palavras(int argc, char **argv)
{
    if (argc < 1)
        return erro("uso: ed palavras <texto.txt> [prefixo]");
    FILE *f = fopen(argv[0], "rb");
    if (f == NULL)
        return erro("não foi possível abrir o texto");
    Patricia *p = pat_criar();
    Radix *r = rad_criar();
    Hash *h = hash_criar(HASH_DUPLO, 64, 0.7);
    char palavra[128];
    size_t n = 0, total = 0;
    int c;
    do {
        c = fgetc(f);
        /* Letras ASCII, dígitos e bytes de UTF-8 (acentos) formam palavras. */
        if (c != EOF && (isalnum(c) || c >= 0x80 || c == '-')) {
            if (n + 1 < sizeof palavra)
                palavra[n++] = (char)(c < 0x80 ? tolower(c) : c);
        } else if (n > 0) {
            palavra[n] = '\0';
            int64_t freq = 0;
            hash_buscar(h, palavra, &freq);
            hash_inserir(h, palavra, freq + 1);
            pat_inserir(p, palavra, freq + 1);
            rad_inserir(r, palavra, freq + 1);
            total++;
            n = 0;
        }
    } while (c != EOF);
    fclose(f);

    printf("%zu palavras, %zu distintas\n\n", total, pat_tamanho(p));
    printf("  %-34s %10s %10s %12s\n", "estrutura", "nós", "altura", "memória");
    printf("  %-34s %10zu %10d %10zu B\n", "PATRICIA (bits)", 2 * pat_tamanho(p) - (pat_tamanho(p) > 0), pat_altura(p),
           pat_memoria(p));
    printf("  %-34s %10zu %10d %10zu B\n", "radix (prefixos por caractere)", rad_nos(r), rad_altura(r), rad_memoria(r));
    HashInfo hi;
    hash_info(h, &hi);
    printf("  %-34s %10zu %10s %12s\n", "hash duplo (posições)", hi.capacidade, "-", "-");
    printf("  conferência: %s\n\n", pat_verificar(p) && rad_verificar(r) && hash_verificar(h) ? "ok" : "FALHOU");

    Par top[6] = {{0}};
    hash_percorrer(h, coletar_par, top);
    printf("  mais frequentes (hash):");
    for (int i = 0; i < 5 && top[i].chave; i++)
        printf(" %s (%lld)", top[i].chave, (long long)top[i].valor);
    printf("\n");

    if (argc > 1) {
        int mostrados = 0;
        printf("\n  com prefixo \"%s\" (PATRICIA):\n", argv[1]);
        size_t q = pat_prefixo(p, argv[1], mostrar_palavra, &mostrados);
        size_t q2 = rad_prefixo(r, argv[1], NULL, NULL);
        if (q > 15)
            printf("    … %zu no total\n", q);
        printf("  (a radix encontrou %zu: %s)\n", q2, q == q2 ? "igual" : "DIFERENTE");
    }
    pat_destruir(p);
    rad_destruir(r);
    hash_destruir(h);
    return EXIT_SUCCESS;
}

/* ---------------------------------------------------------------- experimento de hash */
static double teoria(HashEstrategia e, double a, int sucesso)
{
    switch (e) {
    case HASH_ENCADEAMENTO: return sucesso ? 1 + a / 2 : a;
    case HASH_LINEAR: return sucesso ? 0.5 * (1 + 1 / (1 - a)) : 0.5 * (1 + 1 / ((1 - a) * (1 - a)));
    case HASH_QUADRATICA: return sucesso ? 1 - log(1 - a) - a / 2 : 1 / (1 - a) - a - log(1 - a);
    case HASH_DUPLO: return sucesso ? (1 / a) * log(1 / (1 - a)) : 1 / (1 - a);
    }
    return 0;
}

static int cmd_hash_experimento(int argc, char **argv)
{
    int expoente = argc > 0 ? atoi(argv[0]) : 16;
    if (expoente < 8 || expoente > 22)
        return erro("use um expoente entre 8 e 22 (capacidade 2^n)");
    size_t cap = (size_t)1 << expoente;
    const HashEstrategia est[] = {HASH_ENCADEAMENTO, HASH_LINEAR, HASH_QUADRATICA, HASH_DUPLO};
    const double cargas[] = {0.25, 0.5, 0.75, 0.9, 0.95};
    char k[32];
    printf("Sondagens médias por busca, tabela com %zu posições (medido / fórmula de Knuth)\n\n", cap);
    printf("  ");
    imprimir_alinhado("estratégia", 14);
    printf("      α          com sucesso          sem sucesso  maior cadeia\n");
    for (int e = 0; e < 4; e++) {
        for (int c = 0; c < 5; c++) {
            int n = (int)(cargas[c] * (double)cap);
            Hash *h = hash_criar(est[e], cap, 0);
            for (int i = 0; i < n; i++) {
                snprintf(k, sizeof k, "chave-%d", i);
                hash_inserir(h, k, i);
            }
            HashInfo info;
            hash_zerar_sondagens(h);
            for (int i = 0; i < n; i++) {
                snprintf(k, sizeof k, "chave-%d", i);
                hash_buscar(h, k, NULL);
            }
            hash_info(h, &info);
            double s = (double)info.sondagens / (double)info.operacoes;
            hash_zerar_sondagens(h);
            for (int i = 0; i < 20000; i++) {
                snprintf(k, sizeof k, "ausente-%d", i);
                hash_buscar(h, k, NULL);
            }
            hash_info(h, &info);
            double u = (double)info.sondagens / (double)info.operacoes;
            printf("  ");
            imprimir_alinhado(c ? "" : hash_nome(est[e]), 14);
            printf(" %6.2f  %8.2f / %-8.2f  %8.2f / %-8.2f  %12zu\n", cargas[c], s, teoria(est[e], cargas[c], 1), u,
                   teoria(est[e], cargas[c], 0), info.maior_cadeia);
            hash_destruir(h);
        }
    }
    printf("\nA sondagem linear sofre agrupamento primário: com α = 0,9 uma busca sem sucesso\n"
           "examina ~50 posições, contra ~10 no hash duplo. Por isso o crescimento automático\n"
           "da tabela mantém α ≤ 0,7.\n");
    return EXIT_SUCCESS;
}

/* ---------------------------------------------------------------- demonstração completa */
static int executar(const char *titulo, int (*cmd)(int, char **), int argc, char **argv)
{
    printf("\n$ ed %s", titulo);
    for (int i = 0; i < argc; i++)
        printf(" %s", argv[i]);
    printf("\n");
    return cmd(argc, argv);
}

static int cmd_demo(void)
{
    char *gerar[] = {"alunos.bin", "200000", "7"};
    char *ordenar[] = {"alunos.bin", "alunos_ordenados.bin", "--memoria", "10000", "--metodo", "selecao",
                       "--vias", "8"};
    char *indexar[] = {"alunos_ordenados.bin", "alunos.idx", "--ordem", "128", "--preenchimento", "0.9"};
    char *consultar[] = {"alunos_ordenados.bin", "alunos.idx", "400000"};
    char *faixa[] = {"alunos_ordenados.bin", "alunos.idx", "300000", "300300"};
    char *verificar[] = {"alunos.idx", "verificar"};
    char *imprimir[] = {"alunos.idx", "imprimir", "6"};
    if (executar("gerar", cmd_gerar, 3, gerar) || executar("ordenar", cmd_ordenar, 8, ordenar) ||
        executar("indexar", cmd_indexar, 6, indexar) || executar("consultar", cmd_consultar, 3, consultar) ||
        executar("faixa", cmd_faixa, 4, faixa) || executar("bplus", cmd_bplus, 2, verificar) ||
        executar("bplus", cmd_bplus, 3, imprimir))
        return EXIT_FAILURE;

    printf("\n$ (remove 150 mil matrículas, uma a uma, e confere a árvore)\n");
    BPlus *t;
    if (bp_abrir("alunos.idx", &t) != BP_OK)
        return erro("abrir índice");
    double t0 = agora();
    bp_zerar_contadores(t);
    for (int32_t i = 0; i < 150000; i++)
        bp_remover(t, 100000 + i * 3);
    BPlusInfo info;
    bp_info(t, &info);
    printf("  %.2f s, %.1f páginas lidas e %.1f gravadas por remoção\n", agora() - t0,
           (double)info.leituras / 150000, (double)info.escritas / 150000);
    imprimir_info(t);
    char msg[256];
    printf("  verificação: %s\n", bp_verificar(t, msg, sizeof msg) == BP_OK ? "ok" : msg);
    bp_fechar(t);
    remove("alunos.bin");
    remove("alunos_ordenados.bin");
    remove("alunos.idx");
    return EXIT_SUCCESS;
}

static void ajuda(void)
{
    puts("uso: ed <comando> [argumentos]\n"
         "\n"
         "  gerar <dados.bin> <n> [semente]          n alunos com matrículas distintas em ordem aleatória\n"
         "  ordenar <entrada> <saída> [--memoria M] [--metodo quicksort|selecao] [--vias K]\n"
         "                                           ordenação externa por matrícula\n"
         "  indexar <dados.bin> <índice> [--ordem N] [--preenchimento F]\n"
         "                                           cria o índice B+ (carga em lote se ordenado)\n"
         "  consultar <dados.bin> <índice> <mat>     busca pelo índice\n"
         "  faixa <dados.bin> <índice> <de> <até>     consulta por intervalo\n"
         "  bplus <índice> info|verificar|imprimir [n]|buscar K|inserir K V|remover K\n"
         "  palavras <texto.txt> [prefixo]           PATRICIA × radix × hash sobre as palavras do texto\n"
         "  hash-experimento [expoente]              sondagens medidas × fórmulas de Knuth\n"
         "  demo                                     roteiro completo com 200 mil alunos");
}

int main(int argc, char **argv)
{
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    if (argc < 2) {
        ajuda();
        return EXIT_FAILURE;
    }
    const char *c = argv[1];
    int n = argc - 2;
    char **a = argv + 2;
    int r;
    if (strcmp(c, "gerar") == 0) r = cmd_gerar(n, a);
    else if (strcmp(c, "ordenar") == 0) r = cmd_ordenar(n, a);
    else if (strcmp(c, "indexar") == 0) r = cmd_indexar(n, a);
    else if (strcmp(c, "consultar") == 0) r = cmd_consultar(n, a);
    else if (strcmp(c, "faixa") == 0) r = cmd_faixa(n, a);
    else if (strcmp(c, "bplus") == 0) r = cmd_bplus(n, a);
    else if (strcmp(c, "palavras") == 0) r = cmd_palavras(n, a);
    else if (strcmp(c, "hash-experimento") == 0) r = cmd_hash_experimento(n, a);
    else if (strcmp(c, "demo") == 0) r = cmd_demo();
    else {
        ajuda();
        r = EXIT_FAILURE;
    }
    if (ed_memoria_blocos() != 0)
        fprintf(stderr, "aviso: %ld bloco(s) de memória não liberado(s)\n", ed_memoria_blocos());
    return r;
}
