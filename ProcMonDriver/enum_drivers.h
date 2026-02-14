#ifndef PROCMON_ENUM_DRIVERS_H
#define PROCMON_ENUM_DRIVERS_H

/*
 * enum_drivers.h — Перечисление установленных и загруженных драйверов.
 */

#include <ntddk.h>
#include "../common/shared.h"

/*
 * EnumerateLoadedDrivers — перечислить загруженные драйверы ядра.
 * Использует ZwQuerySystemInformation(SystemModuleInformation).
 */
NTSTATUS EnumerateLoadedDrivers(
    PDRIVER_INFO OutputBuffer,
    ULONG MaxEntries,
    PULONG TotalCount,
    PULONG ReturnedCount
);

/*
 * EnumerateInstalledDrivers — перечислить установленные драйверы из реестра.
 * Перебирает HKLM\System\CurrentControlSet\Services,
 * фильтрует по Type == 1 (SERVICE_KERNEL_DRIVER) или Type == 2 (SERVICE_FILE_SYSTEM_DRIVER).
 */
NTSTATUS EnumerateInstalledDrivers(
    PDRIVER_INFO OutputBuffer,
    ULONG MaxEntries,
    PULONG TotalCount,
    PULONG ReturnedCount
);

#endif /* PROCMON_ENUM_DRIVERS_H */
