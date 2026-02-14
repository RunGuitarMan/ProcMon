/*
 * ioctl.c — Обработка IRP-запросов от user-mode клиента.
 *
 * DispatchCreateClose — обрабатывает открытие/закрытие хэндла устройства.
 *   Клиент вызывает CreateFile("\\.\ProcMon") → ядро отправляет IRP_MJ_CREATE.
 *   Клиент вызывает CloseHandle() → ядро отправляет IRP_MJ_CLOSE.
 *   Оба IRP завершаем со STATUS_SUCCESS (ничего делать не нужно).
 *
 * DispatchDeviceControl — обрабатывает IOCTL-запросы.
 *   Клиент вызывает DeviceIoControl() → ядро отправляет IRP_MJ_DEVICE_CONTROL.
 *   Мы обрабатываем IOCTL_PROCMON_GET_EVENTS: читаем события из кольцевого буфера
 *   и копируем их в выходной буфер клиента.
 */

#include "driver.h"
#include "enum_drivers.h"
#include "enum_devices.h"

/*
 * DispatchCreateClose — обработчик открытия/закрытия устройства.
 *
 * Минимальная реализация: просто завершаем IRP успешно.
 * Без этого обработчика CreateFile в клиенте вернёт ошибку.
 */
NTSTATUS DispatchCreateClose(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    UNREFERENCED_PARAMETER(DeviceObject);

    /* Устанавливаем статус завершения IRP */
    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;

    /* Завершаем IRP — возвращаем результат вызывающей стороне */
    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    return STATUS_SUCCESS;
}

/*
 * DispatchDeviceControl — обработчик IOCTL-запросов.
 *
 * Для METHOD_BUFFERED ядро выделяет один буфер (Irp->AssociatedIrp.SystemBuffer),
 * который используется и для входных, и для выходных данных.
 * Это безопасно: ядро само копирует данные между user/kernel пространствами.
 *
 * Алгоритм для IOCTL_PROCMON_GET_EVENTS:
 * 1. Получаем размер выходного буфера.
 * 2. Проверяем, что буфер достаточно большой хотя бы для заголовка.
 * 3. Вычисляем, сколько событий поместится.
 * 4. Читаем события из кольцевого буфера.
 * 5. Устанавливаем Information = реальный размер возвращённых данных.
 */
NTSTATUS DispatchDeviceControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION  irpSp;
    NTSTATUS            status = STATUS_SUCCESS;
    ULONG               outputLength;
    ULONG               ioctlCode;
    ULONG               maxEvents;
    ULONG               readCount;
    PPROCMON_EVENT_RESPONSE response;
    PDEVICE_EXTENSION   extension;
    ULONG               bytesReturned = 0;

    irpSp = IoGetCurrentIrpStackLocation(Irp);
    ioctlCode = irpSp->Parameters.DeviceIoControl.IoControlCode;
    outputLength = irpSp->Parameters.DeviceIoControl.OutputBufferLength;

    extension = (PDEVICE_EXTENSION)DeviceObject->DeviceExtension;

    switch (ioctlCode) {
    case IOCTL_PROCMON_GET_EVENTS:

        /*
         * Минимальный размер выходного буфера — заголовок PROCMON_EVENT_RESPONSE
         * (поле EventCount). Без хотя бы одного Events[] вернём пустой ответ.
         */
        if (outputLength < sizeof(PROCMON_EVENT_RESPONSE)) {
            /*
             * Если буфер слишком мал даже для заголовка + одного события,
             * но достаточен для одного ULONG (EventCount) — вернём 0 событий.
             */
            if (outputLength >= sizeof(ULONG)) {
                response = (PPROCMON_EVENT_RESPONSE)Irp->AssociatedIrp.SystemBuffer;
                response->EventCount = 0;
                bytesReturned = sizeof(ULONG);
                status = STATUS_SUCCESS;
            } else {
                status = STATUS_BUFFER_TOO_SMALL;
            }
            break;
        }

        response = (PPROCMON_EVENT_RESPONSE)Irp->AssociatedIrp.SystemBuffer;

        /*
         * Вычисляем, сколько событий поместится в выходной буфер.
         * outputLength = sizeof(ULONG) + maxEvents * sizeof(PROCMON_EVENT)
         * => maxEvents = (outputLength - sizeof(ULONG)) / sizeof(PROCMON_EVENT)
         */
        maxEvents = (outputLength - FIELD_OFFSET(PROCMON_EVENT_RESPONSE, Events))
                    / sizeof(PROCMON_EVENT);

        if (maxEvents == 0) {
            response->EventCount = 0;
            bytesReturned = sizeof(ULONG);
            status = STATUS_SUCCESS;
            break;
        }

        /* Читаем события из кольцевого буфера */
        readCount = BufferRead(&extension->RingBuffer, response->Events, maxEvents);
        response->EventCount = readCount;

        /*
         * bytesReturned = размер заголовка + размер фактических данных.
         * Это значение ядро использует для копирования данных в user-space.
         */
        bytesReturned = FIELD_OFFSET(PROCMON_EVENT_RESPONSE, Events)
                        + readCount * sizeof(PROCMON_EVENT);

        status = STATUS_SUCCESS;
        break;

    case IOCTL_PROCMON_GET_INSTALLED_DRIVERS:
    case IOCTL_PROCMON_GET_LOADED_DRIVERS:
    {
        PDRIVER_INFO_RESPONSE drvResponse;
        ULONG drvMaxEntries;
        ULONG drvTotal = 0, drvReturned = 0;

        /* Минимальный буфер: TotalCount + ReturnedCount */
        if (outputLength < (ULONG)FIELD_OFFSET(DRIVER_INFO_RESPONSE, Drivers)) {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }

        drvResponse = (PDRIVER_INFO_RESPONSE)Irp->AssociatedIrp.SystemBuffer;
        drvMaxEntries = (outputLength - (ULONG)FIELD_OFFSET(DRIVER_INFO_RESPONSE, Drivers))
                        / sizeof(DRIVER_INFO);

        if (ioctlCode == IOCTL_PROCMON_GET_INSTALLED_DRIVERS) {
            status = EnumerateInstalledDrivers(drvResponse->Drivers, drvMaxEntries,
                                               &drvTotal, &drvReturned);
        } else {
            status = EnumerateLoadedDrivers(drvResponse->Drivers, drvMaxEntries,
                                            &drvTotal, &drvReturned);
        }

        if (NT_SUCCESS(status)) {
            drvResponse->TotalCount = drvTotal;
            drvResponse->ReturnedCount = drvReturned;
            bytesReturned = FIELD_OFFSET(DRIVER_INFO_RESPONSE, Drivers)
                            + drvReturned * sizeof(DRIVER_INFO);
        }
        break;
    }

    case IOCTL_PROCMON_GET_DEVICES:
    {
        PDEVICE_INFO_RESPONSE devResponse;
        ULONG devMaxEntries;
        ULONG devTotal = 0, devReturned = 0;

        if (outputLength < (ULONG)FIELD_OFFSET(DEVICE_INFO_RESPONSE, Devices)) {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }

        devResponse = (PDEVICE_INFO_RESPONSE)Irp->AssociatedIrp.SystemBuffer;
        devMaxEntries = (outputLength - (ULONG)FIELD_OFFSET(DEVICE_INFO_RESPONSE, Devices))
                        / sizeof(DEVICE_INFO);

        status = EnumerateDevices(devResponse->Devices, devMaxEntries,
                                  &devTotal, &devReturned);

        if (NT_SUCCESS(status)) {
            devResponse->TotalCount = devTotal;
            devResponse->ReturnedCount = devReturned;
            bytesReturned = FIELD_OFFSET(DEVICE_INFO_RESPONSE, Devices)
                            + devReturned * sizeof(DEVICE_INFO);
        }
        break;
    }

    default:
        /* Неизвестный IOCTL-код */
        status = STATUS_INVALID_DEVICE_REQUEST;
        DbgPrint("[ProcMon] Неизвестный IOCTL: 0x%08X\n", ioctlCode);
        break;
    }

    /* Завершаем IRP */
    Irp->IoStatus.Status = status;
    Irp->IoStatus.Information = bytesReturned;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    return status;
}
