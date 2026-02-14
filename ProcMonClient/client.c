/*
 * client.c — Консольный клиент для драйвера ProcMon.
 *
 * Multi-mode интерфейс:
 *   Режим 1: Мониторинг процессов (лог create/exit с MD5-хешами)
 *   Режим 2: Список установленных драйверов
 *   Режим 3: Загруженные драйверы (обновление по Enter)
 *   Режим 4: Активные устройства
 *
 * Требует запуска от имени администратора.
 */

#include <windows.h>
#include <winioctl.h>
#include <stdio.h>

#include "../common/shared.h"

/* Размер буфера для приёма событий процессов (~64 события) */
#define EVENT_BUFFER_SIZE  (sizeof(ULONG) + 64 * sizeof(PROCMON_EVENT))

/* Размер буфера для перечисления драйверов/устройств (256 KB) */
#define ENUM_BUFFER_SIZE   (256 * 1024)

/*
 * FormatTimestamp — конвертирует LARGE_INTEGER (системное время ядра)
 * в читаемую строку формата HH:MM:SS.mmm.
 */
static void FormatTimestamp(LARGE_INTEGER timestamp, char *buffer, size_t bufferSize)
{
    FILETIME   ft;
    SYSTEMTIME st;

    ft.dwLowDateTime  = timestamp.LowPart;
    ft.dwHighDateTime = (DWORD)timestamp.HighPart;

    if (FileTimeToSystemTime(&ft, &st)) {
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

/*
 * FormatHash — форматирует 16-байтовый MD5-хеш в hex-строку (32 символа).
 */
static void FormatHash(const unsigned char hash[16], char *outStr, size_t outSize)
{
    static const char hex[] = "0123456789abcdef";
    int i;

    if (outSize < 33) {
        if (outSize > 0) outStr[0] = '\0';
        return;
    }

    for (i = 0; i < 16; i++) {
        outStr[i * 2]     = hex[(hash[i] >> 4) & 0x0f];
        outStr[i * 2 + 1] = hex[hash[i] & 0x0f];
    }
    outStr[32] = '\0';
}

/*
 * OpenDevice — открыть устройство драйвера ProcMon.
 */
static HANDLE OpenDevice(void)
{
    HANDLE hDevice;

    hDevice = CreateFileW(
        L"\\\\.\\ProcMon",
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );

    if (hDevice == INVALID_HANDLE_VALUE) {
        printf("Ошибка открытия устройства: %lu\n", GetLastError());
        printf("\nВозможные причины:\n");
        printf("  - Драйвер не загружен (sc start ProcMon)\n");
        printf("  - Программа запущена не от администратора\n");
    }

    return hDevice;
}

/*
 * Режим 1: Мониторинг процессов (расширенный с MD5).
 */
static void ModeProcessMonitor(HANDLE hDevice)
{
    BYTE  buffer[EVENT_BUFFER_SIZE];
    DWORD bytesReturned;
    BOOL  success;
    PPROCMON_EVENT_RESPONSE response;
    ULONG i;
    char  timeStr[32];
    char  hashStr[33];

    printf("\nМониторинг процессов (Ctrl+C для остановки)...\n");
    printf("%-14s %-8s %8s %8s  %-34s %s\n",
           "Время", "Тип", "PID", "PPID", "MD5", "Имя процесса");
    printf("------------------------------------------"
           "------------------------------------------\n");

    while (1) {
        success = DeviceIoControl(
            hDevice,
            IOCTL_PROCMON_GET_EVENTS,
            NULL, 0,
            buffer, EVENT_BUFFER_SIZE,
            &bytesReturned,
            NULL
        );

        if (!success) {
            printf("Ошибка DeviceIoControl: %lu\n", GetLastError());
            break;
        }

        response = (PPROCMON_EVENT_RESPONSE)buffer;

        for (i = 0; i < response->EventCount; i++) {
            PPROCMON_EVENT event = &response->Events[i];

            FormatTimestamp(event->Timestamp, timeStr, sizeof(timeStr));

            if (event->HashValid) {
                FormatHash(event->FileHash, hashStr, sizeof(hashStr));
            } else {
                _snprintf(hashStr, sizeof(hashStr), "N/A");
                hashStr[sizeof(hashStr) - 1] = '\0';
            }

            printf("%-14s %-8s %8lu %8lu  %-34s %s\n",
                   timeStr,
                   event->IsCreate ? "CREATE" : "EXIT",
                   event->ProcessId,
                   event->ParentProcessId,
                   hashStr,
                   event->ImageName);
        }

        Sleep(500);
    }
}

/*
 * Режим 2: Все установленные драйверы.
 */
static void ModeInstalledDrivers(HANDLE hDevice)
{
    BYTE *buffer;
    DWORD bytesReturned;
    BOOL  success;
    PDRIVER_INFO_RESPONSE response;
    ULONG i;
    char  hashStr[33];

    buffer = (BYTE *)malloc(ENUM_BUFFER_SIZE);
    if (buffer == NULL) {
        printf("Ошибка выделения памяти\n");
        return;
    }

    printf("\nЗапрос установленных драйверов...\n\n");

    success = DeviceIoControl(
        hDevice,
        IOCTL_PROCMON_GET_INSTALLED_DRIVERS,
        NULL, 0,
        buffer, ENUM_BUFFER_SIZE,
        &bytesReturned,
        NULL
    );

    if (!success) {
        printf("Ошибка DeviceIoControl: %lu\n", GetLastError());
        free(buffer);
        return;
    }

    response = (PDRIVER_INFO_RESPONSE)buffer;

    printf("%-24s %-50s %-8s %s\n",
           "Имя", "Путь", "Запуск", "MD5");
    printf("--------------------------------------------"
           "--------------------------------------------\n");

    for (i = 0; i < response->ReturnedCount; i++) {
        PDRIVER_INFO drv = &response->Drivers[i];

        if (drv->HashValid) {
            FormatHash(drv->FileHash, hashStr, sizeof(hashStr));
        } else {
            _snprintf(hashStr, sizeof(hashStr), "N/A");
            hashStr[sizeof(hashStr) - 1] = '\0';
        }

        printf("%-24.24s %-50.50s %-8lu %s\n",
               drv->DriverName,
               drv->ImagePath,
               drv->StartType,
               hashStr);
    }

    printf("\nВсего: %lu драйверов (показано: %lu)\n",
           response->TotalCount, response->ReturnedCount);

    free(buffer);
}

/*
 * Режим 3: Загруженные драйверы (с обновлением по Enter).
 */
static void ModeLoadedDrivers(HANDLE hDevice)
{
    BYTE *buffer;
    DWORD bytesReturned;
    BOOL  success;
    PDRIVER_INFO_RESPONSE response;
    ULONG i;
    char  hashStr[33];
    int   ch;

    buffer = (BYTE *)malloc(ENUM_BUFFER_SIZE);
    if (buffer == NULL) {
        printf("Ошибка выделения памяти\n");
        return;
    }

    while (1) {
        printf("\nЗапрос загруженных драйверов...\n\n");

        success = DeviceIoControl(
            hDevice,
            IOCTL_PROCMON_GET_LOADED_DRIVERS,
            NULL, 0,
            buffer, ENUM_BUFFER_SIZE,
            &bytesReturned,
            NULL
        );

        if (!success) {
            printf("Ошибка DeviceIoControl: %lu\n", GetLastError());
            break;
        }

        response = (PDRIVER_INFO_RESPONSE)buffer;

        printf("%-24s %-20s %-12s %s\n",
               "Имя", "Базовый адрес", "Размер", "MD5");
        printf("--------------------------------------------"
               "--------------------------------------------\n");

        for (i = 0; i < response->ReturnedCount; i++) {
            PDRIVER_INFO drv = &response->Drivers[i];

            if (drv->HashValid) {
                FormatHash(drv->FileHash, hashStr, sizeof(hashStr));
            } else {
                _snprintf(hashStr, sizeof(hashStr), "N/A");
                hashStr[sizeof(hashStr) - 1] = '\0';
            }

            printf("%-24.24s 0x%016llX   0x%08X %s\n",
                   drv->DriverName,
                   (unsigned long long)drv->BaseAddress,
                   drv->ImageSize,
                   hashStr);
        }

        printf("\nЗагружено: %lu драйверов (показано: %lu)\n",
               response->TotalCount, response->ReturnedCount);
        printf("Нажмите Enter для обновления, Q для выхода.\n");

        ch = getchar();
        if (ch == 'q' || ch == 'Q') {
            break;
        }
    }

    free(buffer);
}

/*
 * Режим 4: Активные устройства.
 */
static void ModeDevices(HANDLE hDevice)
{
    BYTE *buffer;
    DWORD bytesReturned;
    BOOL  success;
    PDEVICE_INFO_RESPONSE response;
    ULONG i;

    buffer = (BYTE *)malloc(ENUM_BUFFER_SIZE);
    if (buffer == NULL) {
        printf("Ошибка выделения памяти\n");
        return;
    }

    printf("\nЗапрос активных устройств...\n\n");

    success = DeviceIoControl(
        hDevice,
        IOCTL_PROCMON_GET_DEVICES,
        NULL, 0,
        buffer, ENUM_BUFFER_SIZE,
        &bytesReturned,
        NULL
    );

    if (!success) {
        printf("Ошибка DeviceIoControl: %lu\n", GetLastError());
        free(buffer);
        return;
    }

    response = (PDEVICE_INFO_RESPONSE)buffer;

    printf("%-32s %-20s %-32s %s\n",
           "Устройство", "Серийник", "Hardware ID", "Драйвер");
    printf("--------------------------------------------"
           "------------------------------------------------------\n");

    for (i = 0; i < response->ReturnedCount; i++) {
        PDEVICE_INFO dev = &response->Devices[i];

        printf("%-32.32s %-20.20s %-32.32s %s\n",
               dev->DeviceName[0] ? dev->DeviceName : "-",
               dev->SerialNumber[0] ? dev->SerialNumber : "-",
               dev->HardwareId[0] ? dev->HardwareId : "-",
               dev->Service[0] ? dev->Service : "-");
    }

    printf("\nВсего: %lu устройств (показано: %lu)\n",
           response->TotalCount, response->ReturnedCount);

    free(buffer);
}

int main(void)
{
    HANDLE hDevice;
    int    mode;
    char   input[16];

    printf("=== ProcMon Anti-Cheat Monitor ===\n");
    printf("Выберите режим:\n");
    printf("  1. Мониторинг процессов (лог create/exit)\n");
    printf("  2. Все установленные драйверы\n");
    printf("  3. Загруженные драйверы (обновление по Enter)\n");
    printf("  4. Активные устройства\n");
    printf("Режим [1-4]: ");

    if (fgets(input, sizeof(input), stdin) == NULL) {
        return 1;
    }

    mode = atoi(input);
    if (mode < 1 || mode > 4) {
        printf("Неверный режим: %d\n", mode);
        return 1;
    }

    printf("Подключение к драйверу...\n");

    hDevice = OpenDevice();
    if (hDevice == INVALID_HANDLE_VALUE) {
        return 1;
    }

    printf("Устройство открыто успешно!\n");

    switch (mode) {
    case 1:
        ModeProcessMonitor(hDevice);
        break;
    case 2:
        ModeInstalledDrivers(hDevice);
        break;
    case 3:
        ModeLoadedDrivers(hDevice);
        break;
    case 4:
        ModeDevices(hDevice);
        break;
    }

    CloseHandle(hDevice);
    printf("\nКлиент завершён.\n");
    return 0;
}
