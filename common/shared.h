#ifndef PROCMON_SHARED_H
#define PROCMON_SHARED_H

/*
 * shared.h — Общие определения для драйвера и клиента.
 * Этот файл включается и в kernel-mode (драйвер), и в user-mode (клиент).
 */

/* Максимальная длина имени процесса */
#define PROCMON_MAX_IMAGE_NAME  260

/* Размер MD5-хеша в байтах */
#define PROCMON_HASH_SIZE         16

/* Максимальная длина пути к файлу драйвера */
#define PROCMON_MAX_DRIVER_PATH   520

/* Максимальная длина серийного номера устройства */
#define PROCMON_MAX_SERIAL        128

/* Максимальная длина Hardware ID */
#define PROCMON_MAX_HWID          260

/*
 * IOCTL-код для получения событий из драйвера.
 * CTL_CODE(DeviceType, Function, Method, Access)
 *   FILE_DEVICE_UNKNOWN = 0x22
 *   Function = 0x800 (начало пользовательского диапазона)
 *   METHOD_BUFFERED = 0 (буфер копируется ядром)
 *   FILE_READ_ACCESS = 1
 */
#define IOCTL_PROCMON_GET_EVENTS \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_READ_ACCESS)

/* IOCTL для получения списка установленных драйверов (из реестра Services) */
#define IOCTL_PROCMON_GET_INSTALLED_DRIVERS \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_READ_ACCESS)

/* IOCTL для получения списка загруженных драйверов (из ядра) */
#define IOCTL_PROCMON_GET_LOADED_DRIVERS \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x802, METHOD_BUFFERED, FILE_READ_ACCESS)

/* IOCTL для получения списка активных устройств (из реестра Enum) */
#define IOCTL_PROCMON_GET_DEVICES \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x803, METHOD_BUFFERED, FILE_READ_ACCESS)

/*
 * Структура одного события мониторинга процесса.
 * Заполняется в callback ядра, читается клиентом через IOCTL.
 */
typedef struct _PROCMON_EVENT {
    ULONG     ProcessId;                          /* PID процесса */
    ULONG     ParentProcessId;                    /* PID родительского процесса */
    BOOLEAN   IsCreate;                           /* TRUE = создание, FALSE = завершение */
    LARGE_INTEGER Timestamp;                      /* Время события (системное) */
    CHAR      ImageName[PROCMON_MAX_IMAGE_NAME];  /* Имя исполняемого файла (ANSI) */
    UCHAR     FileHash[PROCMON_HASH_SIZE];        /* MD5 хеш исполняемого файла */
    BOOLEAN   HashValid;                          /* TRUE если хеш вычислен */
} PROCMON_EVENT, *PPROCMON_EVENT;

/*
 * Структура ответа на IOCTL_PROCMON_GET_EVENTS.
 * Содержит количество событий и массив событий.
 */
typedef struct _PROCMON_EVENT_RESPONSE {
    ULONG         EventCount;   /* Количество событий в массиве */
    PROCMON_EVENT Events[1];    /* Гибкий массив событий (C89-совместимый) */
} PROCMON_EVENT_RESPONSE, *PPROCMON_EVENT_RESPONSE;

/*
 * Информация об одном драйвере (установленном или загруженном).
 */
typedef struct _DRIVER_INFO {
    CHAR      DriverName[PROCMON_MAX_IMAGE_NAME];  /* Имя/DisplayName */
    CHAR      ImagePath[PROCMON_MAX_DRIVER_PATH];  /* Путь к файлу */
    ULONG_PTR BaseAddress;   /* Базовый адрес (для загруженных, 0 для установленных) */
    ULONG     ImageSize;     /* Размер в памяти (для загруженных) */
    ULONG     StartType;     /* Тип запуска (0-4) для установленных */
    UCHAR     FileHash[PROCMON_HASH_SIZE];
    BOOLEAN   HashValid;
} DRIVER_INFO, *PDRIVER_INFO;

/*
 * Ответ на IOCTL_PROCMON_GET_INSTALLED_DRIVERS / IOCTL_PROCMON_GET_LOADED_DRIVERS.
 */
typedef struct _DRIVER_INFO_RESPONSE {
    ULONG       TotalCount;     /* Всего найдено */
    ULONG       ReturnedCount;  /* Сколько поместилось в буфер */
    DRIVER_INFO Drivers[1];
} DRIVER_INFO_RESPONSE, *PDRIVER_INFO_RESPONSE;

/*
 * Информация об одном устройстве.
 */
typedef struct _DEVICE_INFO {
    CHAR      DeviceName[PROCMON_MAX_IMAGE_NAME];   /* FriendlyName или DeviceDesc */
    CHAR      InstanceId[PROCMON_MAX_IMAGE_NAME];   /* Путь экземпляра устройства */
    CHAR      HardwareId[PROCMON_MAX_HWID];         /* Hardware ID */
    CHAR      SerialNumber[PROCMON_MAX_SERIAL];     /* Серийный номер */
    CHAR      Service[PROCMON_MAX_IMAGE_NAME];      /* Имя связанного драйвера */
} DEVICE_INFO, *PDEVICE_INFO;

/*
 * Ответ на IOCTL_PROCMON_GET_DEVICES.
 */
typedef struct _DEVICE_INFO_RESPONSE {
    ULONG       TotalCount;
    ULONG       ReturnedCount;
    DEVICE_INFO Devices[1];
} DEVICE_INFO_RESPONSE, *PDEVICE_INFO_RESPONSE;

#endif /* PROCMON_SHARED_H */
