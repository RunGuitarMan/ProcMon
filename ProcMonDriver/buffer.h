#ifndef PROCMON_BUFFER_H
#define PROCMON_BUFFER_H

/*
 * buffer.h — Потокобезопасный кольцевой буфер для событий.
 * Используется для передачи данных из callback ядра (IRQL <= APC_LEVEL)
 * в IOCTL-обработчик (IRQL = PASSIVE_LEVEL).
 * Синхронизация через KSPIN_LOCK (безопасна на любом IRQL <= DISPATCH_LEVEL).
 */

#include <ntddk.h>
#include "../common/shared.h"

/* Размер кольцевого буфера (количество записей). Должен быть степенью двойки. */
#define RING_BUFFER_SIZE  512

/*
 * Структура кольцевого буфера.
 * Head — индекс для записи (следующая свободная ячейка).
 * Tail — индекс для чтения (следующее непрочитанное событие).
 * Count — текущее количество непрочитанных событий.
 */
typedef struct _RING_BUFFER {
    PROCMON_EVENT  Entries[RING_BUFFER_SIZE];  /* Массив событий */
    ULONG          Head;                       /* Индекс записи */
    ULONG          Tail;                       /* Индекс чтения */
    ULONG          Count;                      /* Количество непрочитанных */
    KSPIN_LOCK     Lock;                       /* Спинлок для синхронизации */
} RING_BUFFER, *PRING_BUFFER;

/* Инициализация буфера. Вызывается один раз при загрузке драйвера. */
VOID BufferInit(_Out_ PRING_BUFFER Buffer);

/* Добавить событие в буфер. Вызывается из callback ядра. */
VOID BufferPush(_Inout_ PRING_BUFFER Buffer, _In_ PPROCMON_EVENT Event);

/*
 * Извлечь события из буфера.
 * MaxEvents — максимальное количество событий для извлечения.
 * OutEvents — массив для записи событий (выделен вызывающей стороной).
 * Возвращает количество фактически извлечённых событий.
 */
ULONG BufferRead(
    _Inout_ PRING_BUFFER Buffer,
    _Out_writes_(MaxEvents) PPROCMON_EVENT OutEvents,
    _In_ ULONG MaxEvents
);

#endif /* PROCMON_BUFFER_H */
