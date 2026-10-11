#ifndef ED_ARQUIVO_H
#define ED_ARQUIVO_H

/* Posicionamento com 64 bits: no Windows `long` tem 32 bits e fseek pararia em 2 GiB. */

#include <stdint.h>
#include <stdio.h>

static inline int ed_fseek(FILE *f, int64_t pos)
{
#ifdef _WIN32
    return _fseeki64(f, pos, SEEK_SET);
#else
    return fseeko(f, (off_t)pos, SEEK_SET);
#endif
}

static inline int64_t ed_tamanho_arquivo(FILE *f)
{
#ifdef _WIN32
    if (_fseeki64(f, 0, SEEK_END) != 0)
        return -1;
    return _ftelli64(f);
#else
    if (fseeko(f, 0, SEEK_END) != 0)
        return -1;
    return (int64_t)ftello(f);
#endif
}

#endif
