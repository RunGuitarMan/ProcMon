#
# CMakeLists.txt для kernel-mode драйвера ProcMon.sys
#
# Kernel-драйвер — это не обычный .exe и не .dll.
# Это .sys файл со специальными флагами компиляции:
#   - Нет стандартной библиотеки C (no CRT)
#   - Точка входа: DriverEntry (не main)
#   - Подсистема: NATIVE (не CONSOLE/WINDOWS)
#   - /integritycheck — обязателен для PsSetCreateProcessNotifyRoutineEx
#

# Собираем только для x64
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Драйвер поддерживает только x64")
endif()

#
# CMake в Debug-режиме автоматически добавляет /RTC1 (runtime checks).
# Runtime checks требуют CRT-функции (_RTC_InitBase, _RTC_Shutdown, _RTC_CheckStackVars).
# В kernel-mode нет CRT (/NODEFAULTLIB), поэтому /RTC1 ломает линковку.
# Убираем его из стандартных флагов.
#
string(REPLACE "/RTC1" "" CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG}")
# Также убираем /MDd (dynamic CRT) — в ядре нет CRT вообще
string(REPLACE "/MDd" "" CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG}")

#
# Убираем стандартные user-mode библиотеки (kernel32.lib, user32.lib, и т.д.)
# Они недоступны в kernel-mode и вызывают ошибку линковки.
# CMAKE_C_STANDARD_LIBRARIES задаётся до add_library, чтобы CMake
# не добавлял их к таргету автоматически.
#
set(CMAKE_C_STANDARD_LIBRARIES "")

# Исходники драйвера
set(DRIVER_SOURCES
    driver.c
    callback.c
    ioctl.c
    buffer.c
    hash.c
    enum_drivers.c
    enum_devices.c
)

# Создаём драйвер как библиотеку (MODULE = .sys для kernel)
# MODULE генерирует загружаемый модуль (аналог DLL, но для ядра)
add_library(ProcMon MODULE ${DRIVER_SOURCES})

# Указываем расширение .sys вместо .dll
set_target_properties(ProcMon PROPERTIES
    SUFFIX ".sys"
    OUTPUT_NAME "ProcMon"
)

# --- Include-пути ---
# km/ — kernel-mode заголовки WDK (ntddk.h, wdm.h, etc.)
# shared/ — общие заголовки Windows SDK (ntdef.h, ntstatus.h)
target_include_directories(ProcMon PRIVATE
    "${WDK_INC}/km"
    "${WDK_INC}/shared"
    "${WDK_INC}/km/crt"
    "${CMAKE_SOURCE_DIR}/common"
)

# --- Флаги компиляции ---
target_compile_options(ProcMon PRIVATE
    /kernel               # Режим компиляции ядра (запрещает float, исключения, и т.д.)
    /GS-                  # Отключить stack security check (нет CRT в ядре)
    /W4                   # Высокий уровень предупреждений
    /Zi                   # Отладочная информация (для WinDbg)
    /Od                   # Без оптимизаций (Debug). Заменить на /O2 для Release.
)

# Определения препроцессора для kernel-mode
target_compile_definitions(ProcMon PRIVATE
    _AMD64_                   # Архитектура x64
    _WIN64                    # 64-битная Windows
    NTDDI_VERSION=0x0A000000  # Windows 10+
)

# --- Флаги линковки ---
# Kernel-драйвер линкуется совсем не так, как user-mode программа.
target_link_options(ProcMon PRIVATE
    /DRIVER:WDM            # Тип драйвера: WDM
    /SUBSYSTEM:NATIVE      # Подсистема NATIVE (не CONSOLE)
    /ENTRY:DriverEntry      # Точка входа
    /INTEGRITYCHECK         # ОБЯЗАТЕЛЬНО для PsSetCreateProcessNotifyRoutineEx!
    /NODEFAULTLIB           # Не использовать стандартную CRT
    /MANIFEST:NO            # Нет манифеста
    /INCREMENTAL:NO         # Инкрементальная линковка несовместима с /INTEGRITYCHECK
)

# --- Библиотеки ядра ---
# ntoskrnl.lib — основная библиотека ядра (IoCreateDevice, PsSetCreate..., etc.)
# hal.lib      — Hardware Abstraction Layer
# BufferOverflowK.lib — __security_cookie для kernel (нужен даже с /GS-)
target_link_libraries(ProcMon PRIVATE
    "${WDK_LIB}/km/x64/ntoskrnl.lib"
    "${WDK_LIB}/km/x64/hal.lib"
    "${WDK_LIB}/km/x64/wmilib.lib"
    "${WDK_LIB}/km/x64/BufferOverflowK.lib"
)
