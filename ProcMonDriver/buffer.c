/*
 * buffer.c — Реализация потокобезопасного кольцевого буфера.
 *
 * Буфер работает по принципу FIFO (очередь).
 * При переполнении новое событие перезаписывает самое старое (tail сдвигается).
 * Это гарантирует, что буфер никогда не вызовет проблем с памятью,
 * даже если клиент долго не читает события.
 *
 * IRQL: BufferPush может вызываться до DISPATCH_LEVEL (из callback ядра).
 *       BufferRead вызывается на PASSIVE_LEVEL (из IOCTL-обработчика).
 *       KSPIN_LOCK корректно работает в обоих случаях.
 */

#include "buffer.h"

/*
 * BufferInit — инициализация кольцевого буфера.
 * Обнуляет все поля и инициализирует спинлок.
 */
VOID BufferInit(_Out_ PRING_BUFFER Buffer)
{
    RtlZeroMemory(Buffer, sizeof(RING_BUFFER));
    KeInitializeSpinLock(&Buffer->Lock);
}

/*
 * BufferPush — добавление события в буфер.
 *
 * Копирует событие в текущую позицию Head.
 * Если буфер полон — перезаписывает самое старое событие (сдвигает Tail).
 * Head продвигается по кругу: (Head + 1) % RING_BUFFER_SIZE.
 */
VOID BufferPush(_Inout_ PRING_BUFFER Buffer, _In_ PPROCMON_EVENT Event)
{
    KIRQL OldIrql;

    /* Захватываем спинлок. OldIrql сохраняет предыдущий IRQL для восстановления. */
    KeAcquireSpinLock(&Buffer->Lock, &OldIrql);

    /* Копируем событие в текущую ячейку Head */
    RtlCopyMemory(&Buffer->Entries[Buffer->Head], Event, sizeof(PROCMON_EVENT));

    /* Продвигаем Head по кругу */
    Buffer->Head = (Buffer->Head + 1) % RING_BUFFER_SIZE;

    if (Buffer->Count < RING_BUFFER_SIZE) {
        /* Буфер ещё не заполнен — просто увеличиваем счётчик */
        Buffer->Count++;
    } else {
        /* Буфер полон — самое старое событие перезаписано, сдвигаем Tail */
        Buffer->Tail = (Buffer->Tail + 1) % RING_BUFFER_SIZE;
    }

    KeReleaseSpinLock(&Buffer->Lock, OldIrql);
}

/*
 * BufferRead — извлечение событий из буфера.
 *
 * Копирует до MaxEvents событий из буфера в массив OutEvents.
 * Каждое прочитанное событие удаляется из буфера (Tail продвигается).
 * Возвращает количество фактически скопированных событий.
 */
ULONG BufferRead(
    _Inout_ PRING_BUFFER Buffer,
    _Out_writes_(MaxEvents) PPROCMON_EVENT OutEvents,
    _In_ ULONG MaxEvents)
{
    KIRQL OldIrql;
    ULONG ReadCount = 0;
    ULONG i;

    KeAcquireSpinLock(&Buffer->Lock, &OldIrql);

    /* Читаем минимум из (запрошено, доступно) */
    for (i = 0; i < MaxEvents && Buffer->Count > 0; i++) {
        RtlCopyMemory(&OutEvents[i], &Buffer->Entries[Buffer->Tail], sizeof(PROCMON_EVENT));

        /* Продвигаем Tail по кругу */
        Buffer->Tail = (Buffer->Tail + 1) % RING_BUFFER_SIZE;
        Buffer->Count--;
        ReadCount++;
    }

    KeReleaseSpinLock(&Buffer->Lock, OldIrql);

    return ReadCount;
}
