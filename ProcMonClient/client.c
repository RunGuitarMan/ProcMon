/*
 * client.c — Консольный клиент для драйвера ProcMon.
 *
 * Открывает устройство \\.\ProcMon через CreateFileW.
 * В цикле каждые 500мс отправляет IOCTL_PROCMON_GET_EVENTS.
 * Выводит события в читаемом формате.
 *
 * Требует запуска от имени администратора (доступ к драйверу ядра).
 * Для остановки — нажать Ctrl+C.
 *
 * Компиляция: cl.exe client.c /Fe:ProcMonClient.exe
 */

#include <windows.h>
#include <winioctl.h>
#include <stdio.h>

/*
 * shared.h использует типы ULONG, BOOLEAN, LARGE_INTEGER, CHAR.
 * В kernel-mode они определены в ntddk.h.
 * В user-mode они уже определены в windows.h (подключён выше).
 */
#include "../common/shared.h"

/* Размер буфера для приёма событий (вмещает ~64 события) */
#define BUFFER_SIZE  (sizeof(ULONG) + 64 * sizeof(PROCMON_EVENT))

/*
 * FormatTimestamp — конвертирует LARGE_INTEGER (системное время ядра)
 * в читаемую строку формата HH:MM:SS.mmm.
 *
 * Системное время ядра — 100-наносекундные интервалы с 1 января 1601 года.
 * FileTimeToSystemTime конвертирует его в структуру SYSTEMTIME.
 */
static void FormatTimestamp(LARGE_INTEGER timestamp, char *buffer, size_t bufferSize)
{
    FILETIME   ft;
    SYSTEMTIME st;

    ft.dwLowDateTime  = timestamp.LowPart;
    ft.dwHighDateTime = (DWORD)timestamp.HighPart;

    if (FileTimeToSystemTime(&ft, &st)) {
        /* Конвертируем UTC -> локальное время */
        SYSTEMTIME localSt;
        if (SystemTimeToTzSpecificLocalTime(NULL, &st, &localSt)) {
            _snprintf(buffer, bufferSize, "%02d:%02d:%02d.%03d",
                      localSt.wHour, localSt.wMinute,
                      localSt.wSecond, localSt.wMilliseconds);
        } else {
            _snprintf(buffer, bufferSize, "%02d:%02d:%02d.%03d",
                      st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
        }
    } else {
        _snprintf(buffer, bufferSize, "??:??:??.???");
    }

    buffer[bufferSize - 1] = '\0';
}

int main(void)
{
    HANDLE hDevice;
    BYTE   buffer[BUFFER_SIZE];
    DWORD  bytesReturned;
    BOOL   success;
    PPROCMON_EVENT_RESPONSE response;
    ULONG  i;
    char   timeStr[32];

    printf("=== ProcMon Client ===\n");
    printf("Подключение к драйверу...\n\n");

    /*
     * Открываем устройство драйвера.
     * "\\\\.\\ProcMon" — это путь к символической ссылке \\DosDevices\\ProcMon.
     * GENERIC_READ — нам нужно только чтение.
     */
    hDevice = CreateFileW(
        L"\\\\.\\ProcMon",          /* Имя устройства */
        GENERIC_READ,               /* Доступ на чтение */
        FILE_SHARE_READ | FILE_SHARE_WRITE, /* Разрешаем совместный доступ */
        NULL,                        /* Безопасность по умолчанию */
        OPEN_EXISTING,               /* Устройство должно существовать */
        FILE_ATTRIBUTE_NORMAL,       /* Обычные атрибуты */
        NULL                         /* Без шаблона */
    );

    if (hDevice == INVALID_HANDLE_VALUE) {
        printf("Ошибка открытия устройства: %lu\n", GetLastError());
        printf("\nВозможные причины:\n");
        printf("  - Драйвер не загружен (sc start ProcMon)\n");
        printf("  - Программа запущена не от администратора\n");
        return 1;
    }

    printf("Устройство открыто успешно!\n");
    printf("Мониторинг процессов (Ctrl+C для остановки)...\n");
    printf("%-14s %-8s %8s %8s  %s\n",
           "Время", "Тип", "PID", "PPID", "Имя процесса");
    printf("--------------------------------------------------------------\n");

    /* Главный цикл опроса */
    while (1) {
        /*
         * Отправляем IOCTL драйверу.
         * Драйвер заполняет buffer[] событиями из кольцевого буфера.
         */
        success = DeviceIoControl(
            hDevice,                        /* Хэндл устройства */
            IOCTL_PROCMON_GET_EVENTS,       /* IOCTL-код */
            NULL,                           /* Входной буфер (не нужен) */
            0,                              /* Размер входного буфера */
            buffer,                         /* Выходной буфер */
            BUFFER_SIZE,                    /* Размер выходного буфера */
            &bytesReturned,                 /* [out] Сколько байт вернулось */
            NULL                            /* Синхронная операция */
        );

        if (!success) {
            printf("Ошибка DeviceIoControl: %lu\n", GetLastError());
            break;
        }

        /* Разбираем ответ */
        response = (PPROCMON_EVENT_RESPONSE)buffer;

        for (i = 0; i < response->EventCount; i++) {
            PPROCMON_EVENT event = &response->Events[i];

            FormatTimestamp(event->Timestamp, timeStr, sizeof(timeStr));

            printf("%-14s %-8s %8lu %8lu  %s\n",
                   timeStr,
                   event->IsCreate ? "CREATE" : "EXIT",
                   event->ProcessId,
                   event->ParentProcessId,
                   event->ImageName);
        }

        /* Пауза 500мс между опросами */
        Sleep(500);
    }

    CloseHandle(hDevice);
    printf("\nКлиент завершён.\n");
    return 0;
}
