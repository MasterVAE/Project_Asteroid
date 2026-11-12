set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

# The POSIX thread variant supports std::thread. Ubuntu also ships a win32 variant.
find_program(ASTEROID_MINGW_C NAMES x86_64-w64-mingw32-gcc-posix x86_64-w64-mingw32-gcc REQUIRED)
find_program(ASTEROID_MINGW_CXX NAMES x86_64-w64-mingw32-g++-posix x86_64-w64-mingw32-g++ REQUIRED)
find_program(ASTEROID_MINGW_RC NAMES x86_64-w64-mingw32-windres REQUIRED)
set(CMAKE_C_COMPILER "${ASTEROID_MINGW_C}")
set(CMAKE_CXX_COMPILER "${ASTEROID_MINGW_CXX}")
set(CMAKE_RC_COMPILER "${ASTEROID_MINGW_RC}")

# Discover the target SDK without hardcoding Homebrew or Linux install paths.
execute_process(COMMAND "${CMAKE_C_COMPILER}" -print-file-name=libkernel32.a
    OUTPUT_VARIABLE ASTEROID_MINGW_KERNEL32 OUTPUT_STRIP_TRAILING_WHITESPACE
    COMMAND_ERROR_IS_FATAL ANY)
if(NOT IS_ABSOLUTE "${ASTEROID_MINGW_KERNEL32}" OR NOT EXISTS "${ASTEROID_MINGW_KERNEL32}")
    message(FATAL_ERROR "MinGW's Windows SDK (libkernel32.a) was not found")
endif()
get_filename_component(ASTEROID_MINGW_LIBDIR "${ASTEROID_MINGW_KERNEL32}" DIRECTORY)
get_filename_component(ASTEROID_MINGW_ROOT "${ASTEROID_MINGW_LIBDIR}" DIRECTORY)
list(APPEND CMAKE_FIND_ROOT_PATH "${ASTEROID_MINGW_ROOT}")

# Never link a target binary against libraries installed for the host OS.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
