cmake_minimum_required(VERSION 3.20)
project(ProcMon C)

#
# Корневой CMakeLists.txt — собирает драйвер и клиент.
#
# ТРЕБОВАНИЯ:
#   1. Visual Studio Build Tools 2022 (или полная VS) с C++ workload
#   2. Windows Driver Kit (WDK)
#   3. В CLion: Settings → Build → Toolchains → добавить "Visual Studio"
#      и поставить его первым (или указать в этом CMake-профиле)
#
# ВАЖНО: MinGW НЕ подходит для драйвера! Только MSVC.
#

# Проверяем, что используется MSVC
if(NOT MSVC)
    message(FATAL_ERROR
        "Для сборки kernel-драйвера нужен MSVC!\n"
        "В CLion: Settings -> Build -> Toolchains -> добавьте 'Visual Studio'\n"
        "MinGW/GCC не поддерживает WDK."
    )
endif()

# --- Поиск WDK ---
# WDK обычно ставится сюда. Если путь другой — поменяйте.
set(WDK_ROOT "C:/Program Files (x86)/Windows Kits/10" CACHE PATH "Путь к Windows Kits")

# Найти последнюю версию WDK
file(GLOB WDK_VERSIONS "${WDK_ROOT}/Include/10.*")
if(NOT WDK_VERSIONS)
    message(FATAL_ERROR
        "WDK не найден в ${WDK_ROOT}\n"
        "Установите Windows Driver Kit: https://learn.microsoft.com/en-us/windows-hardware/drivers/download-the-wdk"
    )
endif()
list(SORT WDK_VERSIONS)
list(GET WDK_VERSIONS -1 WDK_LATEST)
get_filename_component(WDK_VERSION ${WDK_LATEST} NAME)
message(STATUS "Найден WDK версии: ${WDK_VERSION}")

set(WDK_INC "${WDK_ROOT}/Include/${WDK_VERSION}")
set(WDK_LIB "${WDK_ROOT}/Lib/${WDK_VERSION}")

# Проверяем наличие ntddk.h
if(NOT EXISTS "${WDK_INC}/km/ntddk.h")
    message(FATAL_ERROR "ntddk.h не найден в ${WDK_INC}/km/ — проверьте установку WDK")
endif()

# --- Подпроекты ---
add_subdirectory(ProcMonDriver)
add_subdirectory(ProcMonClient)
