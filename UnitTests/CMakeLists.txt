# UnitTests - GoogleTest-based verification target for DarkEngine6.
#
# Build:  cmake --build build --config Debug --target UnitTests
# Run:    ctest --test-dir build -C Debug --output-on-failure
#   or:   build\bin\Debug\UnitTests.exe

include(FetchContent)

# Keep gtest from overriding the parent project's CRT / compiler settings.
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.15.2
    GIT_SHALLOW    TRUE
)

FetchContent_MakeAvailable(googletest)

# Silence third-party warnings so /WX on our tests stays clean.
foreach(_gtest_target IN ITEMS gtest gtest_main)
    if (TARGET ${_gtest_target})
        if (MSVC)
            target_compile_options(${_gtest_target} PRIVATE /W0 /WX-)
        else()
            target_compile_options(${_gtest_target} PRIVATE -w)
        endif()
    endif()
endforeach()

file(GLOB_RECURSE DE_UNITTEST_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.hpp"
)

add_executable(UnitTests ${DE_UNITTEST_SOURCES})

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${DE_UNITTEST_SOURCES})

target_link_libraries(UnitTests
    PRIVATE
        DarkEngine
        GTest::gtest_main
)

target_include_directories(UnitTests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

if (COMMAND de_set_compiler_options)
    de_set_compiler_options(UnitTests)
endif()

set_target_properties(UnitTests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>"
)

include(GoogleTest)
# PRE_TEST works reliably with multi-config generators (Visual Studio).
gtest_discover_tests(UnitTests
    DISCOVERY_MODE PRE_TEST
    WORKING_DIRECTORY $<TARGET_FILE_DIR:UnitTests>
    PROPERTIES LABELS "unit"
)
