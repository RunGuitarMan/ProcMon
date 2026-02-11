/*
 * driver.c — Точка входа и выгрузка драйвера ProcMon.
 *
 * DriverEntry создаёт устройство, символическую ссылку, инициализирует буфер,
 * регистрирует dispatch-функции и callback для мониторинга процессов.
 *
 * DriverUnload выполняет очистку в обратном порядке.
 *
 * Паттерн обработки ошибок: каждый шаг проверяет NTSTATUS.
 * При ошибке откатываются все предыдущие шаги (goto cleanup).
 */

#include "driver.h"

/* Глобальный указатель на устройство (нужен callback-у) */
PDEVICE_OBJECT g_DeviceObject = NULL;

/*
 * DriverEntry — точка входа драйвера.
 *
 * Аналог main() для user-mode программы, но вызывается ядром.
 * Параметры:
 *   DriverObject — объект драйвера, созданный ядром.
 *   RegistryPath — путь в реестре к параметрам драйвера.
 * Возвращает STATUS_SUCCESS при успехе, иначе код ошибки.
 */
NTSTATUS DriverEntry(
    _In_ PDRIVER_OBJECT  DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    NTSTATUS       status;
    PDEVICE_OBJECT deviceObject = NULL;
    UNICODE_STRING deviceName;
    UNICODE_STRING symlinkName;
    PDEVICE_EXTENSION extension;
    BOOLEAN        symlinkCreated = FALSE;

    UNREFERENCED_PARAMETER(RegistryPath);

    DbgPrint("[ProcMon] DriverEntry: загрузка драйвера...\n");

    /* Шаг 1: Создание объекта устройства */
    RtlInitUnicodeString(&deviceName, DEVICE_NAME);

    status = IoCreateDevice(
        DriverObject,                    /* Объект драйвера */
        sizeof(DEVICE_EXTENSION),        /* Размер расширения */
        &deviceName,                     /* Имя устройства */
        FILE_DEVICE_UNKNOWN,             /* Тип устройства */
        FILE_DEVICE_SECURE_OPEN,         /* Характеристики */
        FALSE,                           /* Не эксклюзивное */
        &deviceObject                    /* [out] Созданный объект */
    );

    if (!NT_SUCCESS(status)) {
        DbgPrint("[ProcMon] Ошибка IoCreateDevice: 0x%08X\n", status);
        return status;
    }

    DbgPrint("[ProcMon] Устройство создано: %wZ\n", &deviceName);

    /* Сохраняем глобальный указатель (для callback) */
    g_DeviceObject = deviceObject;

    /* Инициализируем расширение устройства */
    extension = (PDEVICE_EXTENSION)deviceObject->DeviceExtension;
    RtlZeroMemory(extension, sizeof(DEVICE_EXTENSION));
    BufferInit(&extension->RingBuffer);

    /* Шаг 2: Создание символической ссылки для user-mode доступа */
    RtlInitUnicodeString(&symlinkName, SYMLINK_NAME);

    status = IoCreateSymbolicLink(&symlinkName, &deviceName);
    if (!NT_SUCCESS(status)) {
        DbgPrint("[ProcMon] Ошибка IoCreateSymbolicLink: 0x%08X\n", status);
        goto cleanup;
    }

    symlinkCreated = TRUE;
    DbgPrint("[ProcMon] Символическая ссылка создана: %wZ\n", &symlinkName);

    /* Шаг 3: Регистрация dispatch-функций */
    DriverObject->MajorFunction[IRP_MJ_CREATE]         = DispatchCreateClose;
    DriverObject->MajorFunction[IRP_MJ_CLOSE]          = DispatchCreateClose;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = DispatchDeviceControl;
    DriverObject->DriverUnload                          = DriverUnload;

    /* Шаг 4: Регистрация callback для мониторинга процессов */
    status = RegisterProcessCallback();
    if (!NT_SUCCESS(status)) {
        DbgPrint("[ProcMon] Ошибка RegisterProcessCallback: 0x%08X\n", status);
        goto cleanup;
    }

    extension->CallbackRegistered = TRUE;

    DbgPrint("[ProcMon] Драйвер успешно загружен!\n");
    return STATUS_SUCCESS;

cleanup:
    /*
     * Откат в обратном порядке создания.
     * Callback ещё не был зарегистрирован (ошибка выше или сам callback не удался).
     */
    if (symlinkCreated) {
        IoDeleteSymbolicLink(&symlinkName);
    }

    if (deviceObject != NULL) {
        IoDeleteDevice(deviceObject);
        g_DeviceObject = NULL;
    }

    DbgPrint("[ProcMon] DriverEntry завершился с ошибкой: 0x%08X\n", status);
    return status;
}

/*
 * DriverUnload — вызывается ядром при выгрузке драйвера.
 *
 * Очистка ресурсов строго в обратном порядке создания:
 * 1. Снять callback (чтобы новые события не писались в буфер)
 * 2. Удалить символическую ссылку
 * 3. Удалить устройство
 */
VOID DriverUnload(_In_ PDRIVER_OBJECT DriverObject)
{
    UNICODE_STRING    symlinkName;
    PDEVICE_EXTENSION extension;

    DbgPrint("[ProcMon] DriverUnload: выгрузка драйвера...\n");

    if (DriverObject->DeviceObject != NULL) {
        extension = (PDEVICE_EXTENSION)DriverObject->DeviceObject->DeviceExtension;

        /* Шаг 1: Снять callback */
        if (extension->CallbackRegistered) {
            UnregisterProcessCallback();
            extension->CallbackRegistered = FALSE;
            DbgPrint("[ProcMon] Callback снят\n");
        }

        /* Шаг 2: Удалить символическую ссылку */
        RtlInitUnicodeString(&symlinkName, SYMLINK_NAME);
        IoDeleteSymbolicLink(&symlinkName);
        DbgPrint("[ProcMon] Символическая ссылка удалена\n");

        /* Шаг 3: Удалить устройство */
        IoDeleteDevice(DriverObject->DeviceObject);
        g_DeviceObject = NULL;
        DbgPrint("[ProcMon] Устройство удалено\n");
    }

    DbgPrint("[ProcMon] Драйвер успешно выгружен!\n");
}
