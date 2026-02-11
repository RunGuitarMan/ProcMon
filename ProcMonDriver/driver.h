#ifndef PROCMON_DRIVER_H
#define PROCMON_DRIVER_H

/*
 * driver.h — Заголовок драйвера ProcMon.
 * Содержит структуру расширения устройства, глобальные переменные и прототипы.
 */

#include <ntddk.h>
#include "../common/shared.h"
#include "buffer.h"

/* Имя устройства в пространстве имён ядра */
#define DEVICE_NAME     L"\\Device\\ProcMon"

/* Символическая ссылка для доступа из user-mode (\\.\ProcMon) */
#define SYMLINK_NAME    L"\\DosDevices\\ProcMon"

/* Тег для пула памяти (4 символа в обратном порядке: 'PMon') */
#define POOL_TAG        'noMP'

/*
 * DEVICE_EXTENSION — структура, ассоциированная с объектом устройства.
 * Содержит всё состояние драйвера.
 * Доступна через DeviceObject->DeviceExtension.
 */
typedef struct _DEVICE_EXTENSION {
    RING_BUFFER  RingBuffer;        /* Кольцевой буфер для событий */
    BOOLEAN      CallbackRegistered; /* Флаг: callback зарегистрирован? */
} DEVICE_EXTENSION, *PDEVICE_EXTENSION;

/*
 * Глобальный указатель на объект устройства.
 * Необходим, потому что PsSetCreateProcessNotifyRoutineEx не позволяет
 * передать контекст в callback — callback получает только фиксированные параметры.
 * Через g_DeviceObject мы получаем доступ к DeviceExtension и кольцевому буферу.
 */
extern PDEVICE_OBJECT g_DeviceObject;

/* === driver.c === */

/* Точка входа драйвера. Вызывается ядром при загрузке. */
DRIVER_INITIALIZE DriverEntry;

/* Вызывается ядром при выгрузке драйвера (sc stop). */
DRIVER_UNLOAD DriverUnload;

/* === callback.c === */

/*
 * Callback для уведомлений о создании/завершении процессов.
 * Вызывается ядром при каждом создании или завершении процесса.
 * IRQL: PASSIVE_LEVEL (гарантировано ядром).
 */
VOID ProcessNotifyCallback(
    _Inout_ PEPROCESS Process,
    _In_ HANDLE ProcessId,
    _Inout_opt_ PPS_CREATE_NOTIFY_INFO CreateInfo
);

/* Регистрация callback в ядре */
NTSTATUS RegisterProcessCallback(VOID);

/* Снятие callback */
VOID UnregisterProcessCallback(VOID);

/* === ioctl.c === */

/* Обработчик IRP_MJ_CREATE и IRP_MJ_CLOSE (открытие/закрытие хэндла устройства) */
_Dispatch_type_(IRP_MJ_CREATE)
_Dispatch_type_(IRP_MJ_CLOSE)
DRIVER_DISPATCH DispatchCreateClose;

/* Обработчик IRP_MJ_DEVICE_CONTROL (IOCTL-запросы от клиента) */
_Dispatch_type_(IRP_MJ_DEVICE_CONTROL)
DRIVER_DISPATCH DispatchDeviceControl;

#endif /* PROCMON_DRIVER_H */
