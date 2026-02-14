/*
 * callback.c — Callback для мониторинга процессов.
 *
 * PsSetCreateProcessNotifyRoutineEx регистрирует функцию, которую ядро
 * вызывает при каждом создании или завершении процесса в системе.
 *
 * ВАЖНО: Этот callback вызывается на PASSIVE_LEVEL, но в контексте
 * создаваемого/завершающегося процесса. Нельзя блокировать надолго.
 *
 * ВАЖНО: Для PsSetCreateProcessNotifyRoutineEx драйвер ДОЛЖЕН быть
 * подписан или слинкован с /integritycheck. Иначе STATUS_ACCESS_DENIED.
 */

#include "driver.h"

/*
 * ProcessNotifyCallback — вызывается ядром при создании/завершении процесса.
 *
 * Параметры:
 *   Process    — указатель на EPROCESS (структура процесса ядра).
 *   ProcessId  — PID процесса.
 *   CreateInfo — информация о создании. NULL = процесс завершается.
 *
 * При создании (CreateInfo != NULL):
 *   - Заполняем PID, PPID, имя образа из CreateInfo->ImageFileName.
 *
 * При завершении (CreateInfo == NULL):
 *   - Заполняем PID, ставим метку "<exiting>".
 *   - PPID = 0 (недоступен при завершении).
 */
VOID ProcessNotifyCallback(
    _Inout_ PEPROCESS Process,
    _In_ HANDLE ProcessId,
    _Inout_opt_ PPS_CREATE_NOTIFY_INFO CreateInfo)
{
    PROCMON_EVENT     event;
    PDEVICE_EXTENSION extension;
    ANSI_STRING       ansiName;
    NTSTATUS          status;
    ULONG             copyLen;

    UNREFERENCED_PARAMETER(Process);

    /* Проверяем, что устройство существует */
    if (g_DeviceObject == NULL) {
        return;
    }

    extension = (PDEVICE_EXTENSION)g_DeviceObject->DeviceExtension;

    /* Обнуляем структуру события */
    RtlZeroMemory(&event, sizeof(PROCMON_EVENT));

    /* PID всегда доступен */
    event.ProcessId = (ULONG)(ULONG_PTR)ProcessId;

    /* Метка времени */
    KeQuerySystemTime(&event.Timestamp);

    if (CreateInfo != NULL) {
        /* === Процесс создаётся === */
        event.IsCreate = TRUE;
        event.ParentProcessId = (ULONG)(ULONG_PTR)CreateInfo->ParentProcessId;

        /*
         * CreateInfo->ImageFileName — PUNICODE_STRING.
         * Может быть NULL (например, для системных процессов).
         * Конвертируем Unicode -> ANSI для простоты хранения.
         */
        if (CreateInfo->ImageFileName != NULL) {
            status = RtlUnicodeStringToAnsiString(&ansiName, CreateInfo->ImageFileName, TRUE);
            if (NT_SUCCESS(status)) {
                /* Копируем не больше, чем помещается в буфер */
                copyLen = ansiName.Length;
                if (copyLen >= PROCMON_MAX_IMAGE_NAME) {
                    copyLen = PROCMON_MAX_IMAGE_NAME - 1;
                }
                RtlCopyMemory(event.ImageName, ansiName.Buffer, copyLen);
                event.ImageName[copyLen] = '\0';

                /* Освобождаем ANSI-строку, выделенную RtlUnicodeStringToAnsiString */
                RtlFreeAnsiString(&ansiName);
            } else {
                /* Если конвертация не удалась — ставим заглушку */
                RtlCopyMemory(event.ImageName, "<unknown>", 10);
            }

            /* Вычисляем MD5-хеш исполняемого файла */
            status = ComputeFileHash(CreateInfo->ImageFileName, event.FileHash);
            if (NT_SUCCESS(status)) {
                event.HashValid = TRUE;
            } else {
                event.HashValid = FALSE;
            }
        } else {
            RtlCopyMemory(event.ImageName, "<no name>", 10);
            event.HashValid = FALSE;
        }

        DbgPrint("[ProcMon] CREATE: PID=%lu PPID=%lu Image=%s Hash=%s\n",
                 event.ProcessId, event.ParentProcessId, event.ImageName,
                 event.HashValid ? "OK" : "N/A");

    } else {
        /* === Процесс завершается === */
        event.IsCreate = FALSE;
        event.ParentProcessId = 0;
        RtlCopyMemory(event.ImageName, "<exiting>", 10);

        DbgPrint("[ProcMon] EXIT: PID=%lu\n", event.ProcessId);
    }

    /* Добавляем событие в кольцевой буфер */
    BufferPush(&extension->RingBuffer, &event);
}

/*
 * RegisterProcessCallback — регистрирует callback в ядре.
 *
 * ВАЖНО: Драйвер должен быть слинкован с /integritycheck,
 * иначе PsSetCreateProcessNotifyRoutineEx вернёт STATUS_ACCESS_DENIED.
 */
NTSTATUS RegisterProcessCallback(VOID)
{
    NTSTATUS status;

    status = PsSetCreateProcessNotifyRoutineEx(ProcessNotifyCallback, FALSE);

    if (!NT_SUCCESS(status)) {
        DbgPrint("[ProcMon] PsSetCreateProcessNotifyRoutineEx failed: 0x%08X\n", status);

        if (status == STATUS_ACCESS_DENIED) {
            DbgPrint("[ProcMon] ПОДСКАЗКА: Убедитесь, что драйвер слинкован с /integritycheck\n");
        }
    } else {
        DbgPrint("[ProcMon] Process callback зарегистрирован\n");
    }

    return status;
}

/*
 * UnregisterProcessCallback — снимает callback.
 * Второй параметр TRUE означает "удалить регистрацию".
 */
VOID UnregisterProcessCallback(VOID)
{
    NTSTATUS status;

    status = PsSetCreateProcessNotifyRoutineEx(ProcessNotifyCallback, TRUE);

    if (!NT_SUCCESS(status)) {
        DbgPrint("[ProcMon] Ошибка снятия callback: 0x%08X\n", status);
    } else {
        DbgPrint("[ProcMon] Process callback снят\n");
    }
}
