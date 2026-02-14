/*
 * hash.c — Самодостаточная реализация MD5 (RFC 1321) для kernel mode.
 *
 * Не зависит от CRT, CNG или BCrypt.
 * Используется для вычисления контрольных сумм исполняемых файлов.
 */

#include "hash.h"

/* Максимальный размер файла для хеширования (4 MB) */
#define HASH_MAX_FILE_SIZE  (4 * 1024 * 1024)

/* Размер блока чтения */
#define HASH_READ_BLOCK     4096

/* Pool tag */
#define HASH_POOL_TAG       'hsaH'

/* --- MD5 вспомогательные макросы --- */

#define F(x, y, z) (((x) & (y)) | ((~(x)) & (z)))
#define G(x, y, z) (((x) & (z)) | ((y) & (~(z))))
#define H(x, y, z) ((x) ^ (y) ^ (z))
#define I(x, y, z) ((y) ^ ((x) | (~(z))))

#define ROTL(x, n) (((x) << (n)) | ((x) >> (32 - (n))))

#define FF(a, b, c, d, x, s, ac) { \
    (a) += F((b), (c), (d)) + (x) + (ULONG)(ac); \
    (a) = ROTL((a), (s)); \
    (a) += (b); \
}
#define GG(a, b, c, d, x, s, ac) { \
    (a) += G((b), (c), (d)) + (x) + (ULONG)(ac); \
    (a) = ROTL((a), (s)); \
    (a) += (b); \
}
#define HH(a, b, c, d, x, s, ac) { \
    (a) += H((b), (c), (d)) + (x) + (ULONG)(ac); \
    (a) = ROTL((a), (s)); \
    (a) += (b); \
}
#define II(a, b, c, d, x, s, ac) { \
    (a) += I((b), (c), (d)) + (x) + (ULONG)(ac); \
    (a) = ROTL((a), (s)); \
    (a) += (b); \
}

/* Декодировать 4 байта (little-endian) в ULONG */
static __inline ULONG Decode32(const UCHAR *input)
{
    return ((ULONG)input[0])
         | ((ULONG)input[1] << 8)
         | ((ULONG)input[2] << 16)
         | ((ULONG)input[3] << 24);
}

/* Закодировать ULONG в 4 байта (little-endian) */
static __inline void Encode32(UCHAR *output, ULONG input)
{
    output[0] = (UCHAR)(input & 0xff);
    output[1] = (UCHAR)((input >> 8) & 0xff);
    output[2] = (UCHAR)((input >> 16) & 0xff);
    output[3] = (UCHAR)((input >> 24) & 0xff);
}

/*
 * Md5Transform — обработка одного 64-байтового блока.
 * 4 раунда по 16 операций.
 */
static void Md5Transform(ULONG state[4], const UCHAR block[64])
{
    ULONG a = state[0], b = state[1], c = state[2], d = state[3];
    ULONG x[16];
    ULONG i;

    for (i = 0; i < 16; i++) {
        x[i] = Decode32(block + i * 4);
    }

    /* Round 1 */
    FF(a, b, c, d, x[ 0],  7, 0xd76aa478);
    FF(d, a, b, c, x[ 1], 12, 0xe8c7b756);
    FF(c, d, a, b, x[ 2], 17, 0x242070db);
    FF(b, c, d, a, x[ 3], 22, 0xc1bdceee);
    FF(a, b, c, d, x[ 4],  7, 0xf57c0faf);
    FF(d, a, b, c, x[ 5], 12, 0x4787c62a);
    FF(c, d, a, b, x[ 6], 17, 0xa8304613);
    FF(b, c, d, a, x[ 7], 22, 0xfd469501);
    FF(a, b, c, d, x[ 8],  7, 0x698098d8);
    FF(d, a, b, c, x[ 9], 12, 0x8b44f7af);
    FF(c, d, a, b, x[10], 17, 0xffff5bb1);
    FF(b, c, d, a, x[11], 22, 0x895cd7be);
    FF(a, b, c, d, x[12],  7, 0x6b901122);
    FF(d, a, b, c, x[13], 12, 0xfd987193);
    FF(c, d, a, b, x[14], 17, 0xa679438e);
    FF(b, c, d, a, x[15], 22, 0x49b40821);

    /* Round 2 */
    GG(a, b, c, d, x[ 1],  5, 0xf61e2562);
    GG(d, a, b, c, x[ 6],  9, 0xc040b340);
    GG(c, d, a, b, x[11], 14, 0x265e5a51);
    GG(b, c, d, a, x[ 0], 20, 0xe9b6c7aa);
    GG(a, b, c, d, x[ 5],  5, 0xd62f105d);
    GG(d, a, b, c, x[10],  9, 0x02441453);
    GG(c, d, a, b, x[15], 14, 0xd8a1e681);
    GG(b, c, d, a, x[ 4], 20, 0xe7d3fbc8);
    GG(a, b, c, d, x[ 9],  5, 0x21e1cde6);
    GG(d, a, b, c, x[14],  9, 0xc33707d6);
    GG(c, d, a, b, x[ 3], 14, 0xf4d50d87);
    GG(b, c, d, a, x[ 8], 20, 0x455a14ed);
    GG(a, b, c, d, x[13],  5, 0xa9e3e905);
    GG(d, a, b, c, x[ 2],  9, 0xfcefa3f8);
    GG(c, d, a, b, x[ 7], 14, 0x676f02d9);
    GG(b, c, d, a, x[12], 20, 0x8d2a4c8a);

    /* Round 3 */
    HH(a, b, c, d, x[ 5],  4, 0xfffa3942);
    HH(d, a, b, c, x[ 8], 11, 0x8771f681);
    HH(c, d, a, b, x[11], 16, 0x6d9d6122);
    HH(b, c, d, a, x[14], 23, 0xfde5380c);
    HH(a, b, c, d, x[ 1],  4, 0xa4beea44);
    HH(d, a, b, c, x[ 4], 11, 0x4bdecfa9);
    HH(c, d, a, b, x[ 7], 16, 0xf6bb4b60);
    HH(b, c, d, a, x[10], 23, 0xbebfbc70);
    HH(a, b, c, d, x[13],  4, 0x289b7ec6);
    HH(d, a, b, c, x[ 0], 11, 0xeaa127fa);
    HH(c, d, a, b, x[ 3], 16, 0xd4ef3085);
    HH(b, c, d, a, x[ 6], 23, 0x04881d05);
    HH(a, b, c, d, x[ 9],  4, 0xd9d4d039);
    HH(d, a, b, c, x[12], 11, 0xe6db99e5);
    HH(c, d, a, b, x[15], 16, 0x1fa27cf8);
    HH(b, c, d, a, x[ 2], 23, 0xc4ac5665);

    /* Round 4 */
    II(a, b, c, d, x[ 0],  6, 0xf4292244);
    II(d, a, b, c, x[ 7], 10, 0x432aff97);
    II(c, d, a, b, x[14], 15, 0xab9423a7);
    II(b, c, d, a, x[ 5], 21, 0xfc93a039);
    II(a, b, c, d, x[12],  6, 0x655b59c3);
    II(d, a, b, c, x[ 3], 10, 0x8f0ccc92);
    II(c, d, a, b, x[10], 15, 0xffeff47d);
    II(b, c, d, a, x[ 1], 21, 0x85845dd1);
    II(a, b, c, d, x[ 8],  6, 0x6fa87e4f);
    II(d, a, b, c, x[15], 10, 0xfe2ce6e0);
    II(c, d, a, b, x[ 6], 15, 0xa3014314);
    II(b, c, d, a, x[13], 21, 0x4e0811a1);
    II(a, b, c, d, x[ 4],  6, 0xf7537e82);
    II(d, a, b, c, x[11], 10, 0xbd3af235);
    II(c, d, a, b, x[ 2], 15, 0x2ad7d2bb);
    II(b, c, d, a, x[ 9], 21, 0xeb86d391);

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;

    /* Очистка локальных данных */
    RtlZeroMemory(x, sizeof(x));
}

/* Padding: первый байт 0x80, остальные 0x00 */
static const UCHAR MD5_PADDING[64] = {
    0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
       0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
       0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
       0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

VOID Md5Init(MD5_CTX *ctx)
{
    ctx->Count = 0;
    ctx->State[0] = 0x67452301;
    ctx->State[1] = 0xefcdab89;
    ctx->State[2] = 0x98badcfe;
    ctx->State[3] = 0x10325476;
    RtlZeroMemory(ctx->Buffer, 64);
}

VOID Md5Update(MD5_CTX *ctx, const UCHAR *data, ULONG len)
{
    ULONG index, partLen, i;

    /* Индекс в текущем буфере (сколько байт уже буферизовано) */
    index = (ULONG)(ctx->Count & 0x3f);

    ctx->Count += len;

    partLen = 64 - index;

    i = 0;
    if (len >= partLen) {
        /* Дозаполняем текущий блок и обрабатываем */
        RtlCopyMemory(&ctx->Buffer[index], data, partLen);
        Md5Transform(ctx->State, ctx->Buffer);
        i = partLen;

        /* Обрабатываем полные 64-байтовые блоки */
        for (; i + 63 < len; i += 64) {
            Md5Transform(ctx->State, &data[i]);
        }

        index = 0;
    }

    /* Буферизуем остаток */
    if (i < len) {
        RtlCopyMemory(&ctx->Buffer[index], &data[i], len - i);
    }
}

VOID Md5Final(MD5_CTX *ctx, UCHAR digest[16])
{
    UCHAR bits[8];
    ULONG index, padLen;
    ULONG64 bitCount;

    /* Сохраняем длину в битах (little-endian) */
    bitCount = ctx->Count * 8;
    Encode32(bits, (ULONG)(bitCount & 0xffffffff));
    Encode32(bits + 4, (ULONG)(bitCount >> 32));

    /* Padding: дополняем до 56 mod 64 */
    index = (ULONG)(ctx->Count & 0x3f);
    padLen = (index < 56) ? (56 - index) : (120 - index);
    Md5Update(ctx, MD5_PADDING, padLen);

    /* Добавляем длину */
    Md5Update(ctx, bits, 8);

    /* Выводим результат */
    Encode32(digest,      ctx->State[0]);
    Encode32(digest + 4,  ctx->State[1]);
    Encode32(digest + 8,  ctx->State[2]);
    Encode32(digest + 12, ctx->State[3]);

    /* Очистка контекста */
    RtlZeroMemory(ctx, sizeof(MD5_CTX));
}

/*
 * ComputeFileHash — вычисляет MD5-хеш файла.
 *
 * FilePath — NT-путь к файлу (UNICODE_STRING).
 * Hash — буфер для 16-байтового MD5-дайджеста.
 *
 * Читает файл блоками по 4KB, до 4MB максимум.
 * Вызывать только на PASSIVE_LEVEL.
 */
NTSTATUS ComputeFileHash(PCUNICODE_STRING FilePath, UCHAR Hash[16])
{
    NTSTATUS          status;
    HANDLE            fileHandle = NULL;
    OBJECT_ATTRIBUTES objAttr;
    IO_STATUS_BLOCK   ioStatus;
    UCHAR            *readBuffer = NULL;
    MD5_CTX           ctx;
    ULONG             totalRead = 0;
    LARGE_INTEGER     byteOffset;

    if (FilePath == NULL || FilePath->Length == 0) {
        return STATUS_INVALID_PARAMETER;
    }

    InitializeObjectAttributes(&objAttr, (PUNICODE_STRING)FilePath,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               NULL, NULL);

    status = ZwCreateFile(
        &fileHandle,
        FILE_READ_DATA | SYNCHRONIZE,
        &objAttr,
        &ioStatus,
        NULL,
        FILE_ATTRIBUTE_NORMAL,
        FILE_SHARE_READ | FILE_SHARE_DELETE,
        FILE_OPEN,
        FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE,
        NULL, 0);

    if (!NT_SUCCESS(status)) {
        return status;
    }

    readBuffer = (UCHAR *)ExAllocatePoolWithTag(PagedPool, HASH_READ_BLOCK, HASH_POOL_TAG);
    if (readBuffer == NULL) {
        ZwClose(fileHandle);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Md5Init(&ctx);
    byteOffset.QuadPart = 0;

    while (totalRead < HASH_MAX_FILE_SIZE) {
        status = ZwReadFile(
            fileHandle, NULL, NULL, NULL,
            &ioStatus,
            readBuffer,
            HASH_READ_BLOCK,
            &byteOffset,
            NULL);

        if (status == STATUS_END_OF_FILE || ioStatus.Information == 0) {
            status = STATUS_SUCCESS;
            break;
        }

        if (!NT_SUCCESS(status)) {
            break;
        }

        Md5Update(&ctx, readBuffer, (ULONG)ioStatus.Information);
        totalRead += (ULONG)ioStatus.Information;
        byteOffset.QuadPart += ioStatus.Information;
    }

    if (NT_SUCCESS(status)) {
        Md5Final(&ctx, Hash);
    }

    ExFreePoolWithTag(readBuffer, HASH_POOL_TAG);
    ZwClose(fileHandle);

    return status;
}
