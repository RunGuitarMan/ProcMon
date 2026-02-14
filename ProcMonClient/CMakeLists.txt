#
# CMakeLists.txt для user-mode клиента ProcMonClient.exe
#
# Обычное консольное приложение Windows.
# Это можно собрать и через MinGW, но для единообразия используем тот же MSVC.
#

add_executable(ProcMonClient client.c)

#
# Определяем путь к include-директории MSVC из пути к компилятору.
# Необходимо, т.к. CLion + Ninja могут не устанавливать INCLUDE env variable.
#
get_filename_component(MSVC_BIN_DIR "${CMAKE_C_COMPILER}" DIRECTORY)
get_filename_component(MSVC_TOOLS_DIR "${MSVC_BIN_DIR}/../../.." ABSOLUTE)

# Include-пути: MSVC CRT + Windows SDK (user-mode) + общие определения
target_include_directories(ProcMonClient PRIVATE
    "${CMAKE_SOURCE_DIR}/common"
    "${MSVC_TOOLS_DIR}/include"
    "${WDK_INC}/um"
    "${WDK_INC}/shared"
    "${WDK_INC}/ucrt"
)

# Пути к библиотекам
target_link_directories(ProcMonClient PRIVATE
    "${MSVC_TOOLS_DIR}/lib/x64"
    "${WDK_LIB}/um/x64"
    "${WDK_LIB}/ucrt/x64"
)

target_link_libraries(ProcMonClient PRIVATE kernel32)
