#ifndef PROCMON_ENUM_DEVICES_H
#define PROCMON_ENUM_DEVICES_H

/*
 * enum_devices.h — Перечисление активных PnP-устройств.
 */

#include <ntddk.h>
#include "../common/shared.h"

/*
 * EnumerateDevices — перечислить PnP-устройства через реестр Enum.
 * Трёхуровневый обход: Bus\DeviceId\InstanceId.
 * Фильтрует по наличию Service (активные устройства).
 */
NTSTATUS EnumerateDevices(
    PDEVICE_INFO OutputBuffer,
    ULONG MaxEntries,
    PULONG TotalCount,
    PULONG ReturnedCount
);

#endif /* PROCMON_ENUM_DEVICES_H */
