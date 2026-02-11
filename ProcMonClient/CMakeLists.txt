#
# CMakeLists.txt для user-mode клиента ProcMonClient.exe
#
# Обычное консольное приложение Windows.
# Это можно собрать и через MinGW, но для единообразия используем тот же MSVC.
#

add_executable(ProcMonClient client.c)

# Include-путь к общим определениям (shared.h)
target_include_directories(ProcMonClient PRIVATE
    "${CMAKE_SOURCE_DIR}/common"
)

# Windows API уже подключается автоматически в MSVC
# Но на всякий случай линкуем kernel32 (для CreateFileW, DeviceIoControl, etc.)
target_link_libraries(ProcMonClient PRIVATE kernel32)
