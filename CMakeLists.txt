cmake_minimum_required(VERSION 3.24)
project(ProjectAsteroid VERSION 0.1.0 LANGUAGES C CXX)

if(PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
    message(FATAL_ERROR "Use an out-of-source build: cmake -S . -B build/release")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
option(ASTEROID_FETCH_DEPENDENCIES "Download and build pinned SFML and Boost sources" OFF)
option(ASTEROID_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

include(cmake/Dependencies.cmake)
find_package(Threads REQUIRED)

add_executable(app backend/src/main.cpp backend/src/game.cpp
    backend/src/modules.cpp backend/src/render.cpp)
target_include_directories(app PRIVATE backend/include)
target_link_libraries(app PRIVATE SFML::Graphics SFML::Window SFML::System
    Boost::json Boost::filesystem Boost::process Threads::Threads)
set_target_properties(app PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin/$<CONFIG>"
    VS_DEBUGGER_WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")

if(MSVC)
    target_compile_options(app PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(app PRIVATE -Wall -Wextra)
endif()
if(WIN32)
    target_compile_definitions(app PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00)
    target_link_libraries(app PRIVATE ws2_32 mswsock)
    if(MINGW AND ASTEROID_FETCH_DEPENDENCIES)
        # Include the GCC/MinGW runtimes, including libwinpthread, in app.exe.
        target_link_options(app PRIVATE -static -static-libgcc -static-libstdc++)
    endif()
endif()
if(ASTEROID_SANITIZERS)
    if(WIN32 OR NOT CMAKE_CXX_COMPILER_ID MATCHES "^(AppleClang|Clang|GNU)$")
        message(FATAL_ERROR "ASTEROID_SANITIZERS requires GCC or Clang on macOS/Linux")
    endif()
    target_compile_options(app PRIVATE -g -O1 -fno-omit-frame-pointer
        -fno-optimize-sibling-calls -fsanitize=address,undefined -fno-sanitize-recover=all)
    target_link_options(app PRIVATE -fsanitize=address,undefined)
endif()
if(NOT CMAKE_CROSSCOMPILING)
    add_custom_target(run COMMAND "$<TARGET_FILE:app>" DEPENDS app
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}" USES_TERMINAL VERBATIM)
endif()

# Install Runtime only, so the distribution does not contain dependency SDKs.
install(TARGETS app RUNTIME DESTINATION . COMPONENT Runtime)
install(DIRECTORY scripts DESTINATION . COMPONENT Runtime
    FILES_MATCHING PATTERN "*.py" PATTERN "__pycache__" EXCLUDE)
install(FILES README.md DESTINATION . COMPONENT Runtime)
if(ASTEROID_FETCH_DEPENDENCIES)
    install(FILES "${boost_SOURCE_DIR}/LICENSE_1_0.txt"
        DESTINATION licenses/boost COMPONENT Runtime)
    install(FILES "${sfml_SOURCE_DIR}/license.md"
        DESTINATION licenses/sfml COMPONENT Runtime)
    if(DEFINED freetype_SOURCE_DIR)
        install(FILES "${freetype_SOURCE_DIR}/docs/FTL.TXT"
            DESTINATION licenses/freetype COMPONENT Runtime)
    endif()
endif()
set(CPACK_PACKAGE_NAME ProjectAsteroid)
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(ASTEROID_PACKAGE_ARCH "${CMAKE_SYSTEM_PROCESSOR}")
if(APPLE AND CMAKE_OSX_ARCHITECTURES)
    string(REPLACE ";" "-" ASTEROID_PACKAGE_ARCH "${CMAKE_OSX_ARCHITECTURES}")
endif()
set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}-${ASTEROID_PACKAGE_ARCH}")
set(CPACK_PACKAGE_DIRECTORY "${PROJECT_BINARY_DIR}/packages")
set(CPACK_COMPONENTS_ALL Runtime)
set(CPACK_ARCHIVE_COMPONENT_INSTALL ON)
set(CPACK_COMPONENTS_GROUPING ALL_COMPONENTS_IN_ONE)
if(WIN32)
    set(CPACK_GENERATOR ZIP)
else()
    set(CPACK_GENERATOR TGZ)
endif()
include(CPack)
