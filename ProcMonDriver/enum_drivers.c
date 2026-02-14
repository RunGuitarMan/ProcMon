/*
 * enum_drivers.c — Перечисление установленных и загруженных драйверов.
 *
 * Загруженные: ZwQuerySystemInformation(SystemModuleInformation)
 * Установленные: перебор реестра \Registry\Machine\System\CurrentControlSet\Services
 */

#include "driver.h"
#include "hash.h"
#include "enum_drivers.h"

/* SystemModuleInformation = 11 */
#define SystemModuleInformation 11

/* Структуры для ZwQuerySystemInformation (не экспортированы в WDK headers) */
typedef struct _RTL_PROCESS_MODULE_INFORMATION {
    HANDLE  Section;
    PVOID   MappedBase;
    PVOID   ImageBase;
    ULONG   ImageSize;
    ULONG   Flags;
    USHORT  LoadOrderIndex;
    USHORT  InitOrderIndex;
    USHORT  LoadCount;
    USHORT  OffsetToFileName;
    UCHAR   FullPathName[256];
} RTL_PROCESS_MODULE_INFORMATION, *PRTL_PROCESS_MODULE_INFORMATION;

typedef struct _RTL_PROCESS_MODULES {
    ULONG NumberOfModules;
    RTL_PROCESS_MODULE_INFORMATION Modules[1];
} RTL_PROCESS_MODULES, *PRTL_PROCESS_MODULES;

/* Прототип ZwQuerySystemInformation */
NTSYSAPI NTSTATUS NTAPI ZwQuerySystemInformation(
    ULONG SystemInformationClass,
    PVOID SystemInformation,
    ULONG SystemInformationLength,
    PULONG ReturnLength
);

/*
 * HasPrefixCaseInsensitive — проверка unicode-префикса (case-insensitive).
 */
static BOOLEAN HasPrefixCaseInsensitive(PCUNICODE_STRING String, PCWSTR Prefix, USHORT PrefixChars)
{
    UNICODE_STRING prefixStr;
    prefixStr.Buffer = (PWCH)Prefix;
    prefixStr.Length = PrefixChars * sizeof(WCHAR);
    prefixStr.MaximumLength = prefixStr.Length;
    return RtlPrefixUnicodeString(&prefixStr, (PUNICODE_STRING)String, TRUE);
}

/*
 * ResolveDriverPath — преобразовать путь драйвера из формата ядра
 * (\SystemRoot\..., system32\drivers\...) в NT-путь (\??\C:\Windows\...).
 *
 * resultPath — выходной UNICODE_STRING. Буфер выделяется через ExAllocatePoolWithTag(POOL_TAG).
 * Вызывающий должен освободить resultPath->Buffer через ExFreePoolWithTag(..., POOL_TAG).
 */
static NTSTATUS ResolveDriverPath(const CHAR *kernelPath, PUNICODE_STRING resultPath)
{
    ANSI_STRING    ansiPath;
    UNICODE_STRING uniPath;
    NTSTATUS       status;
    PWCH           outBuf;
    USHORT         outLen;

    static const WCHAR sysRootPrefix[] = L"\\SystemRoot\\";
    static const WCHAR ntPrefix[]      = L"\\??\\";
    static const WCHAR sys32Prefix[]   = L"system32\\";
    static const WCHAR winDir[]        = L"\\??\\C:\\Windows\\";

    #define SYSROOT_PREFIX_CHARS 12  /* wcslen(L"\\SystemRoot\\") */
    #define NT_PREFIX_CHARS       4  /* wcslen(L"\\??\\") */
    #define SYS32_PREFIX_CHARS    9  /* wcslen(L"system32\\") */
    #define WINDIR_CHARS         15  /* wcslen(L"\\??\\C:\\Windows\\") */

    RtlZeroMemory(resultPath, sizeof(UNICODE_STRING));

    RtlInitAnsiString(&ansiPath, kernelPath);
    status = RtlAnsiStringToUnicodeString(&uniPath, &ansiPath, TRUE);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    /* \SystemRoot\... -> \??\C:\Windows\... */
    if (HasPrefixCaseInsensitive(&uniPath, sysRootPrefix, SYSROOT_PREFIX_CHARS)) {
        USHORT suffixLen = uniPath.Length - SYSROOT_PREFIX_CHARS * sizeof(WCHAR);
        outLen = WINDIR_CHARS * sizeof(WCHAR) + suffixLen;

        outBuf = (PWCH)ExAllocatePoolWithTag(PagedPool, outLen + sizeof(WCHAR), POOL_TAG);
        if (outBuf == NULL) {
            RtlFreeUnicodeString(&uniPath);
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        RtlCopyMemory(outBuf, winDir, WINDIR_CHARS * sizeof(WCHAR));
        RtlCopyMemory((PUCHAR)outBuf + WINDIR_CHARS * sizeof(WCHAR),
                       uniPath.Buffer + SYSROOT_PREFIX_CHARS, suffixLen);
        outBuf[outLen / sizeof(WCHAR)] = L'\0';

        resultPath->Buffer = outBuf;
        resultPath->Length = outLen;
        resultPath->MaximumLength = outLen + sizeof(WCHAR);

        RtlFreeUnicodeString(&uniPath);
        return STATUS_SUCCESS;
    }

    /* \??\... — уже NT-путь, копируем в наш буфер */
    if (HasPrefixCaseInsensitive(&uniPath, ntPrefix, NT_PREFIX_CHARS)) {
        outLen = uniPath.Length;
        outBuf = (PWCH)ExAllocatePoolWithTag(PagedPool, outLen + sizeof(WCHAR), POOL_TAG);
        if (outBuf == NULL) {
            RtlFreeUnicodeString(&uniPath);
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        RtlCopyMemory(outBuf, uniPath.Buffer, outLen);
        outBuf[outLen / sizeof(WCHAR)] = L'\0';

        resultPath->Buffer = outBuf;
        resultPath->Length = outLen;
        resultPath->MaximumLength = outLen + sizeof(WCHAR);

        RtlFreeUnicodeString(&uniPath);
        return STATUS_SUCCESS;
    }

    /* system32\... -> \??\C:\Windows\system32\... */
    if (HasPrefixCaseInsensitive(&uniPath, sys32Prefix, SYS32_PREFIX_CHARS)) {
        outLen = WINDIR_CHARS * sizeof(WCHAR) + uniPath.Length;

        outBuf = (PWCH)ExAllocatePoolWithTag(PagedPool, outLen + sizeof(WCHAR), POOL_TAG);
        if (outBuf == NULL) {
            RtlFreeUnicodeString(&uniPath);
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        RtlCopyMemory(outBuf, winDir, WINDIR_CHARS * sizeof(WCHAR));
        RtlCopyMemory((PUCHAR)outBuf + WINDIR_CHARS * sizeof(WCHAR),
                       uniPath.Buffer, uniPath.Length);
        outBuf[outLen / sizeof(WCHAR)] = L'\0';

        resultPath->Buffer = outBuf;
        resultPath->Length = outLen;
        resultPath->MaximumLength = outLen + sizeof(WCHAR);

        RtlFreeUnicodeString(&uniPath);
        return STATUS_SUCCESS;
    }

    /* Другой формат — копируем как есть */
    outLen = uniPath.Length;
    outBuf = (PWCH)ExAllocatePoolWithTag(PagedPool, outLen + sizeof(WCHAR), POOL_TAG);
    if (outBuf == NULL) {
        RtlFreeUnicodeString(&uniPath);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlCopyMemory(outBuf, uniPath.Buffer, outLen);
    outBuf[outLen / sizeof(WCHAR)] = L'\0';

    resultPath->Buffer = outBuf;
    resultPath->Length = outLen;
    resultPath->MaximumLength = outLen + sizeof(WCHAR);

    RtlFreeUnicodeString(&uniPath);
    return STATUS_SUCCESS;

    #undef SYSROOT_PREFIX_CHARS
    #undef NT_PREFIX_CHARS
    #undef SYS32_PREFIX_CHARS
    #undef WINDIR_CHARS
}

/*
 * EnumerateLoadedDrivers — перечисление загруженных модулей ядра.
 */
NTSTATUS EnumerateLoadedDrivers(
    PDRIVER_INFO OutputBuffer,
    ULONG MaxEntries,
    PULONG TotalCount,
    PULONG ReturnedCount)
{
    NTSTATUS            status;
    PRTL_PROCESS_MODULES modules = NULL;
    ULONG               needed = 0;
    ULONG               i, count, returned;

    *TotalCount = 0;
    *ReturnedCount = 0;

    /* Узнаём необходимый размер буфера */
    status = ZwQuerySystemInformation(SystemModuleInformation, NULL, 0, &needed);
    if (status != STATUS_INFO_LENGTH_MISMATCH) {
        return (NT_SUCCESS(status)) ? STATUS_UNSUCCESSFUL : status;
    }

    modules = (PRTL_PROCESS_MODULES)ExAllocatePoolWithTag(PagedPool, needed, POOL_TAG);
    if (modules == NULL) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    status = ZwQuerySystemInformation(SystemModuleInformation, modules, needed, &needed);
    if (!NT_SUCCESS(status)) {
        ExFreePoolWithTag(modules, POOL_TAG);
        return status;
    }

    count = modules->NumberOfModules;
    *TotalCount = count;
    returned = 0;

    for (i = 0; i < count && returned < MaxEntries; i++) {
        PRTL_PROCESS_MODULE_INFORMATION mod = &modules->Modules[i];
        PDRIVER_INFO info = &OutputBuffer[returned];
        ULONG nameLen;
        const char *fileName;

        RtlZeroMemory(info, sizeof(DRIVER_INFO));

        /* Имя файла — последний компонент пути */
        fileName = (const char *)&mod->FullPathName[mod->OffsetToFileName];
        nameLen = (ULONG)strlen(fileName);
        if (nameLen >= PROCMON_MAX_IMAGE_NAME)
            nameLen = PROCMON_MAX_IMAGE_NAME - 1;
        RtlCopyMemory(info->DriverName, fileName, nameLen);
        info->DriverName[nameLen] = '\0';

        /* Полный путь */
        nameLen = (ULONG)strlen((const char *)mod->FullPathName);
        if (nameLen >= PROCMON_MAX_DRIVER_PATH)
            nameLen = PROCMON_MAX_DRIVER_PATH - 1;
        RtlCopyMemory(info->ImagePath, mod->FullPathName, nameLen);
        info->ImagePath[nameLen] = '\0';

        info->BaseAddress = (ULONG_PTR)mod->ImageBase;
        info->ImageSize = mod->ImageSize;
        info->StartType = 0;

        /* Вычисляем MD5-хеш файла */
        {
            UNICODE_STRING resolvedPath;
            status = ResolveDriverPath((const CHAR *)mod->FullPathName, &resolvedPath);
            if (NT_SUCCESS(status) && resolvedPath.Buffer != NULL) {
                if (NT_SUCCESS(ComputeFileHash(&resolvedPath, info->FileHash))) {
                    info->HashValid = TRUE;
                }
                ExFreePoolWithTag(resolvedPath.Buffer, POOL_TAG);
            }
        }

        returned++;
    }

    *ReturnedCount = returned;
    ExFreePoolWithTag(modules, POOL_TAG);
    return STATUS_SUCCESS;
}

/*
 * ReadRegistryDword — чтение DWORD-значения из реестра.
 */
static NTSTATUS ReadRegistryDword(HANDLE KeyHandle, PCWSTR ValueName, PULONG Value)
{
    NTSTATUS                       status;
    UNICODE_STRING                 valueName;
    UCHAR                          buffer[sizeof(KEY_VALUE_PARTIAL_INFORMATION) + sizeof(ULONG)];
    PKEY_VALUE_PARTIAL_INFORMATION info = (PKEY_VALUE_PARTIAL_INFORMATION)buffer;
    ULONG                          resultLength;

    RtlInitUnicodeString(&valueName, ValueName);

    status = ZwQueryValueKey(KeyHandle, &valueName, KeyValuePartialInformation,
                             info, sizeof(buffer), &resultLength);
    if (NT_SUCCESS(status) && info->Type == REG_DWORD && info->DataLength == sizeof(ULONG)) {
        *Value = *(PULONG)info->Data;
    } else if (NT_SUCCESS(status)) {
        status = STATUS_OBJECT_TYPE_MISMATCH;
    }

    return status;
}

/*
 * ReadRegistryString — чтение строки (REG_SZ/REG_EXPAND_SZ/REG_MULTI_SZ) из реестра в ANSI.
 */
static NTSTATUS ReadRegistryString(HANDLE KeyHandle, PCWSTR ValueName,
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
        /* Берём первую строку из REG_MULTI_SZ */
        PWCH firstStr = (PWCH)info->Data;
        USHORT firstLen = 0;
        /* Находим длину первой строки (до первого null) */
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
 * EnumerateInstalledDrivers — перечисление драйверов из реестра Services.
 */
NTSTATUS EnumerateInstalledDrivers(
    PDRIVER_INFO OutputBuffer,
    ULONG MaxEntries,
    PULONG TotalCount,
    PULONG ReturnedCount)
{
    NTSTATUS       status;
    HANDLE         servicesKey = NULL;
    UNICODE_STRING servicesPath;
    OBJECT_ATTRIBUTES objAttr;
    ULONG          index;
    ULONG          total = 0, returned = 0;
    UCHAR         *keyInfoBuf = NULL;
    ULONG          keyInfoBufSize = 512;

    *TotalCount = 0;
    *ReturnedCount = 0;

    RtlInitUnicodeString(&servicesPath,
        L"\\Registry\\Machine\\System\\CurrentControlSet\\Services");

    InitializeObjectAttributes(&objAttr, &servicesPath,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               NULL, NULL);

    status = ZwOpenKey(&servicesKey, KEY_READ, &objAttr);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    keyInfoBuf = (UCHAR *)ExAllocatePoolWithTag(PagedPool, keyInfoBufSize, POOL_TAG);
    if (keyInfoBuf == NULL) {
        ZwClose(servicesKey);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    for (index = 0; ; index++) {
        PKEY_BASIC_INFORMATION keyInfo = (PKEY_BASIC_INFORMATION)keyInfoBuf;
        ULONG resultLength;
        HANDLE subKey = NULL;
        UNICODE_STRING subKeyName;
        OBJECT_ATTRIBUTES subAttr;
        ULONG driverType = 0;

        status = ZwEnumerateKey(servicesKey, index, KeyBasicInformation,
                                keyInfo, keyInfoBufSize, &resultLength);

        if (status == STATUS_NO_MORE_ENTRIES) {
            status = STATUS_SUCCESS;
            break;
        }

        if (status == STATUS_BUFFER_OVERFLOW || status == STATUS_BUFFER_TOO_SMALL) {
            ExFreePoolWithTag(keyInfoBuf, POOL_TAG);
            keyInfoBufSize = resultLength + 64;
            keyInfoBuf = (UCHAR *)ExAllocatePoolWithTag(PagedPool, keyInfoBufSize, POOL_TAG);
            if (keyInfoBuf == NULL) {
                ZwClose(servicesKey);
                return STATUS_INSUFFICIENT_RESOURCES;
            }
            index--;
            continue;
        }

        if (!NT_SUCCESS(status)) {
            continue;
        }

        /* Открываем подключ */
        subKeyName.Buffer = keyInfo->Name;
        subKeyName.Length = (USHORT)keyInfo->NameLength;
        subKeyName.MaximumLength = subKeyName.Length;

        InitializeObjectAttributes(&subAttr, &subKeyName,
                                   OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                                   servicesKey, NULL);

        status = ZwOpenKey(&subKey, KEY_READ, &subAttr);
        if (!NT_SUCCESS(status)) {
            continue;
        }

        /* Фильтруем: Type == 1 (KERNEL_DRIVER) или Type == 2 (FILE_SYSTEM_DRIVER) */
        status = ReadRegistryDword(subKey, L"Type", &driverType);
        if (!NT_SUCCESS(status) || (driverType != 1 && driverType != 2)) {
            ZwClose(subKey);
            continue;
        }

        total++;

        if (returned < MaxEntries) {
            PDRIVER_INFO info = &OutputBuffer[returned];
            ANSI_STRING  ansiKeyName;
            UNICODE_STRING uniSubKeyName;

            RtlZeroMemory(info, sizeof(DRIVER_INFO));

            /* Имя ключа -> DriverName */
            uniSubKeyName.Buffer = keyInfo->Name;
            uniSubKeyName.Length = (USHORT)keyInfo->NameLength;
            uniSubKeyName.MaximumLength = uniSubKeyName.Length;

            status = RtlUnicodeStringToAnsiString(&ansiKeyName, &uniSubKeyName, TRUE);
            if (NT_SUCCESS(status)) {
                ULONG copyLen = ansiKeyName.Length;
                if (copyLen >= PROCMON_MAX_IMAGE_NAME)
                    copyLen = PROCMON_MAX_IMAGE_NAME - 1;
                RtlCopyMemory(info->DriverName, ansiKeyName.Buffer, copyLen);
                info->DriverName[copyLen] = '\0';
                RtlFreeAnsiString(&ansiKeyName);
            }

            /* DisplayName (перезаписывает имя ключа, если есть и не MUI-ссылка) */
            {
                CHAR displayName[PROCMON_MAX_IMAGE_NAME];
                displayName[0] = '\0';
                if (NT_SUCCESS(ReadRegistryString(subKey, L"DisplayName",
                                                  displayName, sizeof(displayName)))) {
                    if (displayName[0] != '\0' && displayName[0] != '@') {
                        ULONG len = (ULONG)strlen(displayName);
                        if (len >= PROCMON_MAX_IMAGE_NAME)
                            len = PROCMON_MAX_IMAGE_NAME - 1;
                        RtlCopyMemory(info->DriverName, displayName, len);
                        info->DriverName[len] = '\0';
                    }
                }
            }

            /* ImagePath */
            ReadRegistryString(subKey, L"ImagePath",
                               info->ImagePath, PROCMON_MAX_DRIVER_PATH);

            /* Start type */
            ReadRegistryDword(subKey, L"Start", &info->StartType);

            info->BaseAddress = 0;
            info->ImageSize = 0;

            /* Вычисляем MD5-хеш файла драйвера */
            if (info->ImagePath[0] != '\0') {
                UNICODE_STRING resolvedPath;
                if (NT_SUCCESS(ResolveDriverPath(info->ImagePath, &resolvedPath)) &&
                    resolvedPath.Buffer != NULL) {
                    if (NT_SUCCESS(ComputeFileHash(&resolvedPath, info->FileHash))) {
                        info->HashValid = TRUE;
                    }
                    ExFreePoolWithTag(resolvedPath.Buffer, POOL_TAG);
                }
            }

            returned++;
        }

        ZwClose(subKey);
    }

    *TotalCount = total;
    *ReturnedCount = returned;

    ExFreePoolWithTag(keyInfoBuf, POOL_TAG);
    ZwClose(servicesKey);
    return STATUS_SUCCESS;
}
