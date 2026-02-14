/*
 * enum_devices.c — Перечисление PnP-устройств через реестр.
 *
 * Обходит \Registry\Machine\System\CurrentControlSet\Enum
 * в три уровня: Bus \ DeviceId \ InstanceId.
 * Для каждого экземпляра читает FriendlyName, HardwareID, Service, etc.
 */

#include "driver.h"
#include "enum_devices.h"
#include <ntstrsafe.h>

/*
 * ReadDeviceRegistryString — чтение строки (REG_SZ/EXPAND_SZ/MULTI_SZ) из реестра в ANSI.
 * Дублирует логику из enum_drivers.c, но локально для enum_devices.c.
 */
static NTSTATUS ReadDeviceRegistryString(HANDLE KeyHandle, PCWSTR ValueName,
                                         CHAR *outBuffer, ULONG outBufferSize)
{
    NTSTATUS                       status;
    UNICODE_STRING                 valueName;
    UCHAR                          stackBuf[512];
    PKEY_VALUE_PARTIAL_INFORMATION info = (PKEY_VALUE_PARTIAL_INFORMATION)stackBuf;
    PKEY_VALUE_PARTIAL_INFORMATION heapInfo = NULL;
    ULONG                          resultLength;
    UNICODE_STRING                 uniStr;
    ANSI_STRING                    ansiStr;
    ULONG                          copyLen;

    outBuffer[0] = '\0';

    RtlInitUnicodeString(&valueName, ValueName);

    status = ZwQueryValueKey(KeyHandle, &valueName, KeyValuePartialInformation,
                             info, sizeof(stackBuf), &resultLength);

    if (status == STATUS_BUFFER_OVERFLOW || status == STATUS_BUFFER_TOO_SMALL) {
        heapInfo = (PKEY_VALUE_PARTIAL_INFORMATION)ExAllocatePoolWithTag(
            PagedPool, resultLength, POOL_TAG);
        if (heapInfo == NULL) return STATUS_INSUFFICIENT_RESOURCES;
        info = heapInfo;
        status = ZwQueryValueKey(KeyHandle, &valueName, KeyValuePartialInformation,
                                 info, resultLength, &resultLength);
        if (!NT_SUCCESS(status)) {
            ExFreePoolWithTag(heapInfo, POOL_TAG);
            return status;
        }
    } else if (!NT_SUCCESS(status)) {
        return status;
    }

    if (info->Type == REG_SZ || info->Type == REG_EXPAND_SZ) {
        uniStr.Buffer = (PWCH)info->Data;
        uniStr.Length = (USHORT)info->DataLength;
        if (uniStr.Length >= sizeof(WCHAR) &&
            uniStr.Buffer[uniStr.Length / sizeof(WCHAR) - 1] == L'\0') {
            uniStr.Length -= sizeof(WCHAR);
        }
        uniStr.MaximumLength = uniStr.Length;

        status = RtlUnicodeStringToAnsiString(&ansiStr, &uniStr, TRUE);
        if (NT_SUCCESS(status)) {
            copyLen = ansiStr.Length;
            if (copyLen >= outBufferSize)
                copyLen = outBufferSize - 1;
            RtlCopyMemory(outBuffer, ansiStr.Buffer, copyLen);
            outBuffer[copyLen] = '\0';
            RtlFreeAnsiString(&ansiStr);
        }
    } else if (info->Type == REG_MULTI_SZ) {
        PWCH firstStr = (PWCH)info->Data;
        USHORT firstLen = 0;
        while (firstLen < info->DataLength / sizeof(WCHAR) && firstStr[firstLen] != L'\0') {
            firstLen++;
        }
        uniStr.Buffer = firstStr;
        uniStr.Length = firstLen * sizeof(WCHAR);
        uniStr.MaximumLength = uniStr.Length + sizeof(WCHAR);

        status = RtlUnicodeStringToAnsiString(&ansiStr, &uniStr, TRUE);
        if (NT_SUCCESS(status)) {
            copyLen = ansiStr.Length;
            if (copyLen >= outBufferSize)
                copyLen = outBufferSize - 1;
            RtlCopyMemory(outBuffer, ansiStr.Buffer, copyLen);
            outBuffer[copyLen] = '\0';
            RtlFreeAnsiString(&ansiStr);
        }
    } else {
        status = STATUS_OBJECT_TYPE_MISMATCH;
    }

    if (heapInfo != NULL) {
        ExFreePoolWithTag(heapInfo, POOL_TAG);
    }

    return status;
}

/*
 * EnumerateSubKeys — вспомогательная: получить количество подключей.
 */
static NTSTATUS GetSubKeyCount(HANDLE KeyHandle, PULONG Count)
{
    NTSTATUS status;
    UCHAR buf[sizeof(KEY_FULL_INFORMATION) + 256];
    PKEY_FULL_INFORMATION fullInfo = (PKEY_FULL_INFORMATION)buf;
    ULONG resultLen;

    status = ZwQueryKey(KeyHandle, KeyFullInformation, fullInfo, sizeof(buf), &resultLen);
    if (NT_SUCCESS(status)) {
        *Count = fullInfo->SubKeys;
    }
    return status;
}

/*
 * ExtractSerialFromInstanceId — извлечь серийный номер из Instance ID.
 * Для USB-устройств Instance ID часто выглядит как "Bus\VID_xxxx&PID_xxxx\SerialNumber".
 * Мы берём последний компонент после '\'.
 */
static void ExtractSerialFromInstanceId(const CHAR *instanceId, CHAR *serial, ULONG serialSize)
{
    const CHAR *lastSlash = NULL;
    const CHAR *p;
    ULONG len;

    serial[0] = '\0';

    for (p = instanceId; *p != '\0'; p++) {
        if (*p == '\\') {
            lastSlash = p;
        }
    }

    if (lastSlash != NULL && *(lastSlash + 1) != '\0') {
        lastSlash++; /* Пропускаем сам '\' */
        len = (ULONG)strlen(lastSlash);
        if (len >= serialSize)
            len = serialSize - 1;
        RtlCopyMemory(serial, lastSlash, len);
        serial[len] = '\0';
    }
}

/*
 * EnumerateDevices — перечисление PnP-устройств из реестра.
 *
 * Трёхуровневая рекурсия:
 * Level 1: Шины (PCI, USB, HDAUDIO, ...)
 * Level 2: Device ID (VID_xxxx&PID_xxxx, ...)
 * Level 3: Instance ID (серийник, индекс, ...)
 */
NTSTATUS EnumerateDevices(
    PDEVICE_INFO OutputBuffer,
    ULONG MaxEntries,
    PULONG TotalCount,
    PULONG ReturnedCount)
{
    NTSTATUS          status;
    HANDLE            enumKey = NULL;
    UNICODE_STRING    enumPath;
    OBJECT_ATTRIBUTES enumAttr;
    ULONG             busIndex;
    ULONG             total = 0, returned = 0;
    UCHAR            *keyBuf = NULL;
    ULONG             keyBufSize = 512;

    *TotalCount = 0;
    *ReturnedCount = 0;

    RtlInitUnicodeString(&enumPath,
        L"\\Registry\\Machine\\System\\CurrentControlSet\\Enum");

    InitializeObjectAttributes(&enumAttr, &enumPath,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               NULL, NULL);

    status = ZwOpenKey(&enumKey, KEY_READ, &enumAttr);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    keyBuf = (UCHAR *)ExAllocatePoolWithTag(PagedPool, keyBufSize, POOL_TAG);
    if (keyBuf == NULL) {
        ZwClose(enumKey);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /* Level 1: Перебираем шины */
    for (busIndex = 0; ; busIndex++) {
        PKEY_BASIC_INFORMATION busInfo = (PKEY_BASIC_INFORMATION)keyBuf;
        ULONG resultLen;
        HANDLE busKey = NULL;
        UNICODE_STRING busName;
        OBJECT_ATTRIBUTES busAttr;
        ULONG devIndex;

        /* ANSI-представления для построения Instance ID */
        CHAR busNameAnsi[128];
        ANSI_STRING busAnsi;
        UNICODE_STRING busUniName;

        status = ZwEnumerateKey(enumKey, busIndex, KeyBasicInformation,
                                busInfo, keyBufSize, &resultLen);

        if (status == STATUS_NO_MORE_ENTRIES) break;
        if (status == STATUS_BUFFER_OVERFLOW || status == STATUS_BUFFER_TOO_SMALL) {
            ExFreePoolWithTag(keyBuf, POOL_TAG);
            keyBufSize = resultLen + 64;
            keyBuf = (UCHAR *)ExAllocatePoolWithTag(PagedPool, keyBufSize, POOL_TAG);
            if (keyBuf == NULL) { ZwClose(enumKey); return STATUS_INSUFFICIENT_RESOURCES; }
            busIndex--;
            continue;
        }
        if (!NT_SUCCESS(status)) continue;

        /* Конвертируем имя шины в ANSI для построения InstanceId */
        busUniName.Buffer = busInfo->Name;
        busUniName.Length = (USHORT)busInfo->NameLength;
        busUniName.MaximumLength = busUniName.Length;
        if (NT_SUCCESS(RtlUnicodeStringToAnsiString(&busAnsi, &busUniName, TRUE))) {
            ULONG cl = busAnsi.Length;
            if (cl >= sizeof(busNameAnsi)) cl = sizeof(busNameAnsi) - 1;
            RtlCopyMemory(busNameAnsi, busAnsi.Buffer, cl);
            busNameAnsi[cl] = '\0';
            RtlFreeAnsiString(&busAnsi);
        } else {
            busNameAnsi[0] = '\0';
        }

        /* Открываем ключ шины */
        busName.Buffer = busInfo->Name;
        busName.Length = (USHORT)busInfo->NameLength;
        busName.MaximumLength = busName.Length;
        InitializeObjectAttributes(&busAttr, &busName,
                                   OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                                   enumKey, NULL);
        status = ZwOpenKey(&busKey, KEY_READ, &busAttr);
        if (!NT_SUCCESS(status)) continue;

        /* Level 2: Device IDs */
        for (devIndex = 0; ; devIndex++) {
            PKEY_BASIC_INFORMATION devInfo;
            HANDLE devKey = NULL;
            UNICODE_STRING devName;
            OBJECT_ATTRIBUTES devAttr;
            ULONG instIndex;
            CHAR devIdAnsi[256];
            ANSI_STRING devAnsi;
            UNICODE_STRING devUniName;

            /* Переиспользуем keyBuf (вложенный уровень) — выделяем отдельный буфер */
            UCHAR devBuf[512];
            devInfo = (PKEY_BASIC_INFORMATION)devBuf;

            status = ZwEnumerateKey(busKey, devIndex, KeyBasicInformation,
                                    devInfo, sizeof(devBuf), &resultLen);

            if (status == STATUS_NO_MORE_ENTRIES) break;
            if (!NT_SUCCESS(status)) continue;

            /* Конвертируем Device ID в ANSI */
            devUniName.Buffer = devInfo->Name;
            devUniName.Length = (USHORT)devInfo->NameLength;
            devUniName.MaximumLength = devUniName.Length;
            if (NT_SUCCESS(RtlUnicodeStringToAnsiString(&devAnsi, &devUniName, TRUE))) {
                ULONG cl = devAnsi.Length;
                if (cl >= sizeof(devIdAnsi)) cl = sizeof(devIdAnsi) - 1;
                RtlCopyMemory(devIdAnsi, devAnsi.Buffer, cl);
                devIdAnsi[cl] = '\0';
                RtlFreeAnsiString(&devAnsi);
            } else {
                devIdAnsi[0] = '\0';
            }

            devName.Buffer = devInfo->Name;
            devName.Length = (USHORT)devInfo->NameLength;
            devName.MaximumLength = devName.Length;
            InitializeObjectAttributes(&devAttr, &devName,
                                       OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                                       busKey, NULL);
            status = ZwOpenKey(&devKey, KEY_READ, &devAttr);
            if (!NT_SUCCESS(status)) continue;

            /* Level 3: Instance IDs */
            for (instIndex = 0; ; instIndex++) {
                PKEY_BASIC_INFORMATION instInfo;
                HANDLE instKey = NULL;
                UNICODE_STRING instName;
                OBJECT_ATTRIBUTES instAttr;
                CHAR instIdAnsi[128];
                ANSI_STRING instAnsi;
                UNICODE_STRING instUniName;
                CHAR serviceName[PROCMON_MAX_IMAGE_NAME];

                UCHAR instBuf[512];
                instInfo = (PKEY_BASIC_INFORMATION)instBuf;

                status = ZwEnumerateKey(devKey, instIndex, KeyBasicInformation,
                                        instInfo, sizeof(instBuf), &resultLen);

                if (status == STATUS_NO_MORE_ENTRIES) break;
                if (!NT_SUCCESS(status)) continue;

                /* Конвертируем Instance ID в ANSI */
                instUniName.Buffer = instInfo->Name;
                instUniName.Length = (USHORT)instInfo->NameLength;
                instUniName.MaximumLength = instUniName.Length;
                if (NT_SUCCESS(RtlUnicodeStringToAnsiString(&instAnsi, &instUniName, TRUE))) {
                    ULONG cl = instAnsi.Length;
                    if (cl >= sizeof(instIdAnsi)) cl = sizeof(instIdAnsi) - 1;
                    RtlCopyMemory(instIdAnsi, instAnsi.Buffer, cl);
                    instIdAnsi[cl] = '\0';
                    RtlFreeAnsiString(&instAnsi);
                } else {
                    instIdAnsi[0] = '\0';
                }

                instName.Buffer = instInfo->Name;
                instName.Length = (USHORT)instInfo->NameLength;
                instName.MaximumLength = instName.Length;
                InitializeObjectAttributes(&instAttr, &instName,
                                           OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                                           devKey, NULL);
                status = ZwOpenKey(&instKey, KEY_READ, &instAttr);
                if (!NT_SUCCESS(status)) continue;

                /* Фильтр: пропускаем устройства без Service */
                serviceName[0] = '\0';
                ReadDeviceRegistryString(instKey, L"Service",
                                         serviceName, sizeof(serviceName));
                if (serviceName[0] == '\0') {
                    ZwClose(instKey);
                    continue;
                }

                total++;

                if (returned < MaxEntries) {
                    PDEVICE_INFO dinfo = &OutputBuffer[returned];
                    CHAR fullInstanceId[PROCMON_MAX_IMAGE_NAME];

                    RtlZeroMemory(dinfo, sizeof(DEVICE_INFO));

                    /* Service */
                    RtlCopyMemory(dinfo->Service, serviceName, strlen(serviceName) + 1);

                    /* DeviceName: FriendlyName, потом DeviceDesc */
                    if (!NT_SUCCESS(ReadDeviceRegistryString(instKey, L"FriendlyName",
                                        dinfo->DeviceName, PROCMON_MAX_IMAGE_NAME)) ||
                        dinfo->DeviceName[0] == '\0') {
                        ReadDeviceRegistryString(instKey, L"DeviceDesc",
                                                 dinfo->DeviceName, PROCMON_MAX_IMAGE_NAME);
                    }

                    /* HardwareID */
                    ReadDeviceRegistryString(instKey, L"HardwareID",
                                             dinfo->HardwareId, PROCMON_MAX_HWID);

                    /* InstanceId = Bus\DeviceId\InstanceId */
                    RtlStringCbPrintfA(fullInstanceId, sizeof(fullInstanceId),
                                       "%s\\%s\\%s", busNameAnsi, devIdAnsi, instIdAnsi);
                    {
                        ULONG idLen = (ULONG)strlen(fullInstanceId);
                        if (idLen >= PROCMON_MAX_IMAGE_NAME)
                            idLen = PROCMON_MAX_IMAGE_NAME - 1;
                        RtlCopyMemory(dinfo->InstanceId, fullInstanceId, idLen);
                        dinfo->InstanceId[idLen] = '\0';
                    }

                    /* SerialNumber: извлекаем из Instance ID */
                    ExtractSerialFromInstanceId(fullInstanceId,
                                                dinfo->SerialNumber, PROCMON_MAX_SERIAL);

                    returned++;
                }

                ZwClose(instKey);
            }

            ZwClose(devKey);
        }

        ZwClose(busKey);
    }

    *TotalCount = total;
    *ReturnedCount = returned;

    ExFreePoolWithTag(keyBuf, POOL_TAG);
    ZwClose(enumKey);
    return STATUS_SUCCESS;
}
