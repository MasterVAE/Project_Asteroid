if(NOT ASTEROID_FETCH_DEPENDENCIES)
    find_package(SFML 3 CONFIG REQUIRED COMPONENTS Graphics Window System)
    find_package(Boost 1.87 CONFIG REQUIRED COMPONENTS json filesystem process)
    return()
endif()
include(FetchContent)

# Scope dependency settings so they do not change the application's options.
function(asteroid_fetch_dependencies)
    set(BUILD_SHARED_LIBS OFF)
    set(BUILD_TESTING OFF)
    set(BOOST_INCLUDE_LIBRARIES json filesystem process)
    set(BOOST_SKIP_INSTALL_RULES ON)
    if(CMAKE_VERSION VERSION_GREATER_EQUAL 4.0)
        set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
    endif()
    FetchContent_Declare(boost
        URL https://github.com/boostorg/boost/releases/download/boost-1.87.0/boost-1.87.0-cmake.tar.xz
        URL_HASH SHA256=7da75f171837577a52bbf217e17f8ea576c7c246e4594d617bfde7fafd408be5
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(boost)

    set(SFML_BUILD_AUDIO OFF)
    set(SFML_BUILD_NETWORK OFF)
    set(SFML_BUILD_EXAMPLES OFF)
    set(SFML_BUILD_TEST_SUITE OFF)
    set(SFML_BUILD_DOC OFF)
    set(SFML_CONFIGURE_EXTRAS OFF)
    set(SFML_INSTALL_XCODE_TEMPLATES OFF)
    FetchContent_Declare(sfml
        URL https://codeload.github.com/SFML/SFML/tar.gz/refs/tags/3.0.2
        URL_HASH SHA256=0034e05f95509e5d3fb81b1625713e06da7b068f210288ce3fd67106f8f46995
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(sfml)

    set(boost_SOURCE_DIR "${boost_SOURCE_DIR}" PARENT_SCOPE)
    set(sfml_SOURCE_DIR "${sfml_SOURCE_DIR}" PARENT_SCOPE)
    FetchContent_GetProperties(freetype)
    if(freetype_POPULATED)
        set(freetype_SOURCE_DIR "${freetype_SOURCE_DIR}" PARENT_SCOPE)
    endif()
endfunction()
asteroid_fetch_dependencies()
