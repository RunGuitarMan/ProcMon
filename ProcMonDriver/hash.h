#ifndef PROCMON_HASH_H
#define PROCMON_HASH_H

/*
 * hash.h — MD5-хеширование для kernel mode.
 * Самодостаточная реализация RFC 1321 без внешних зависимостей.
 */

#include <ntddk.h>

/* Контекст MD5-вычисления */
typedef struct _MD5_CTX {
    ULONG   State[4];    /* ABCD */
    ULONG64 Count;       /* Количество обработанных байт */
    UCHAR   Buffer[64];  /* Буфер для неполного блока */
} MD5_CTX;

VOID Md5Init(MD5_CTX *ctx);
VOID Md5Update(MD5_CTX *ctx, const UCHAR *data, ULONG len);
VOID Md5Final(MD5_CTX *ctx, UCHAR digest[16]);

/*
 * ComputeFileHash — вычислить MD5 файла по пути.
 * Читает файл блоками по 4KB, ограничение 4MB.
 * Должен вызываться на PASSIVE_LEVEL.
 */
NTSTATUS ComputeFileHash(PCUNICODE_STRING FilePath, UCHAR Hash[16]);

#endif /* PROCMON_HASH_H */
