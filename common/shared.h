#ifndef PROCMON_SHARED_H
#define PROCMON_SHARED_H

/*
 * shared.h — Общие определения для драйвера и клиента.
 * Этот файл включается и в kernel-mode (драйвер), и в user-mode (клиент).
 */

/* Максимальная длина имени процесса */
#define PROCMON_MAX_IMAGE_NAME  260

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
} PROCMON_EVENT, *PPROCMON_EVENT;

/*
 * Структура ответа на IOCTL_PROCMON_GET_EVENTS.
 * Содержит количество событий и массив событий.
 * Размер массива определяется размером выходного буфера.
 */
typedef struct _PROCMON_EVENT_RESPONSE {
    ULONG         EventCount;   /* Количество событий в массиве */
    PROCMON_EVENT Events[1];    /* Гибкий массив событий (C89-совместимый) */
} PROCMON_EVENT_RESPONSE, *PPROCMON_EVENT_RESPONSE;

#endif /* PROCMON_SHARED_H */
