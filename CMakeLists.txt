cmake_minimum_required(VERSION 3.21)
project(DarkEngine6 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Emit compile_commands.json for clang-based tools (Sourcetrail, clangd, etc.).
# Note: Visual Studio generators do not write this file; use the Ninja configure
# in scripts/setup-sourcetrail.ps1 (build-sourcetrail/) for a working database.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(DE_ENABLE_ASSERTS "Enable DE_ASSERT" ON)
option(DE_BUILD_TESTS "Build UnitTests executable and CTest targets" ON)

# --- Collect sources via wildcards -------------------------------------------------
# CONFIGURE_DEPENDS re-scans when files are added/removed (VS / Ninja).

function(de_glob_folder out_var folder)
    file(GLOB_RECURSE _files CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/${folder}/*.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/${folder}/*.hpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/${folder}/*.hh"
        "${CMAKE_CURRENT_SOURCE_DIR}/${folder}/*.inl"
        "${CMAKE_CURRENT_SOURCE_DIR}/${folder}/*.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/${folder}/*.cc"
        "${CMAKE_CURRENT_SOURCE_DIR}/${folder}/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/${folder}/*.cxx"
        "${CMAKE_CURRENT_SOURCE_DIR}/${folder}/*.hlsl"
        "${CMAKE_CURRENT_SOURCE_DIR}/${folder}/*.hlsli"
        "${CMAKE_CURRENT_SOURCE_DIR}/${folder}/*.natvis"
    )
    set(${out_var} "${_files}" PARENT_SCOPE)
endfunction()

set(DE_ENGINE_FOLDERS
    AI
    Assets
    Character
    Collision
    Core
    ECS
    Geometry
    Input
    Math
    Network
    Particles
    Render
    Scene
    Shaders
    Sky
    Sprite
    Terrain
    Water
)

set(DE_ENGINE_SOURCES "")
foreach(folder IN LISTS DE_ENGINE_FOLDERS)
    de_glob_folder(_folder_sources "${folder}")
    list(APPEND DE_ENGINE_SOURCES ${_folder_sources})
endforeach()

de_glob_folder(DE_SANDBOX_SOURCES Sandbox)
de_glob_folder(DE_SANDBOX2D_SOURCES Sandbox2D)
de_glob_folder(DE_EDITOR_SOURCES Editor)

# Runtime HLSL lives under content/shaders (not the empty Shaders/ source folder).
# Add them to DarkEngine so they appear in the VS solution; do not compile them.
file(GLOB_RECURSE DE_SHADER_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/content/shaders/*.hlsl"
    "${CMAKE_CURRENT_SOURCE_DIR}/content/shaders/*.hlsli"
)
list(APPEND DE_ENGINE_SOURCES ${DE_SHADER_SOURCES})

# Non-C/C++ files are IDE-visible only (not passed to the compiler).
foreach(src IN LISTS DE_ENGINE_SOURCES DE_SANDBOX_SOURCES DE_SANDBOX2D_SOURCES DE_EDITOR_SOURCES)
    get_filename_component(_ext "${src}" EXT)
    string(TOLOWER "${_ext}" _ext)
    if(NOT _ext MATCHES "\\.(c|cc|cpp|cxx)$")
        set_source_files_properties("${src}" PROPERTIES HEADER_FILE_ONLY TRUE)
    endif()
    if(_ext MATCHES "\\.(hlsl|hlsli)$")
        set_source_files_properties("${src}" PROPERTIES VS_TOOL_OVERRIDE "None")
    endif()
endforeach()

# --- Engine library ----------------------------------------------------------------
add_library(DarkEngine STATIC ${DE_ENGINE_SOURCES})

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${DE_ENGINE_SOURCES})

target_include_directories(DarkEngine
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(DarkEngine
    PUBLIC
        d3d12
        dxgi
        dxguid
        d3dcompiler
        windowscodecs
        xinput
)

target_compile_definitions(DarkEngine
    PUBLIC
        $<$<BOOL:${DE_ENABLE_ASSERTS}>:DE_ENABLE_ASSERTS=1>
        NOMINMAX
        WIN32_LEAN_AND_MEAN
)

if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CompilerOptions.cmake")
    include(cmake/CompilerOptions.cmake)
    if (COMMAND de_set_compiler_options)
        de_set_compiler_options(DarkEngine)
    endif()
endif()

# --- Sandbox executable ------------------------------------------------------------
add_executable(Sandbox WIN32 ${DE_SANDBOX_SOURCES})

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${DE_SANDBOX_SOURCES})

target_link_libraries(Sandbox PRIVATE DarkEngine)

if (COMMAND de_set_compiler_options)
    de_set_compiler_options(Sandbox)
endif()

set_target_properties(Sandbox PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# Keep runtime content next to Sandbox.exe (CWD is not reliable under VS).
# Best-effort: does not fail the build if Sandbox is still running and locks files.
add_custom_command(TARGET Sandbox POST_BUILD
    COMMAND ${CMAKE_COMMAND}
            -DSRC=${CMAKE_SOURCE_DIR}/content
            -DDST=$<TARGET_FILE_DIR:Sandbox>/content
            -P ${CMAKE_SOURCE_DIR}/cmake/CopyContent.cmake
    COMMENT "Copying content/ next to Sandbox.exe"
)

# --- Sandbox2D executable (side-scrolling 2D template) -----------------------------
add_executable(Sandbox2D WIN32 ${DE_SANDBOX2D_SOURCES})

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${DE_SANDBOX2D_SOURCES})

target_link_libraries(Sandbox2D PRIVATE DarkEngine)

if (COMMAND de_set_compiler_options)
    de_set_compiler_options(Sandbox2D)
endif()

set_target_properties(Sandbox2D PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>"
)

add_custom_command(TARGET Sandbox2D POST_BUILD
    COMMAND ${CMAKE_COMMAND}
            -DSRC=${CMAKE_SOURCE_DIR}/content
            -DDST=$<TARGET_FILE_DIR:Sandbox2D>/content
            -P ${CMAKE_SOURCE_DIR}/cmake/CopyContent.cmake
    COMMENT "Copying content/ next to Sandbox2D.exe"
)

# --- Dear ImGui (Editor UI) --------------------------------------------------------
include(FetchContent)
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.91.8
    GIT_SHALLOW    TRUE
)
FetchContent_GetProperties(imgui)
if(NOT imgui_POPULATED)
    FetchContent_Populate(imgui)
endif()

set(IMGUI_SOURCES
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_dx12.cpp
)

add_library(imgui_editor STATIC ${IMGUI_SOURCES})
target_include_directories(imgui_editor PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
target_compile_definitions(imgui_editor PUBLIC IMGUI_DEFINE_MATH_OPERATORS)
target_link_libraries(imgui_editor PUBLIC d3d12 dxgi)

# --- Editor executable -------------------------------------------------------------
add_executable(Editor WIN32 ${DE_EDITOR_SOURCES})

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${DE_EDITOR_SOURCES})

target_link_libraries(Editor PRIVATE DarkEngine imgui_editor)

target_include_directories(Editor PRIVATE
    "${CMAKE_SOURCE_DIR}/third_party"
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)

if (COMMAND de_set_compiler_options)
    de_set_compiler_options(Editor)
endif()

set_target_properties(Editor PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_custom_command(TARGET Editor POST_BUILD
    COMMAND ${CMAKE_COMMAND}
            -DSRC=${CMAKE_SOURCE_DIR}/content
            -DDST=$<TARGET_FILE_DIR:Editor>/content
            -P ${CMAKE_SOURCE_DIR}/cmake/CopyContent.cmake
    COMMENT "Copying content/ next to Editor.exe"
)

# --- Unit tests --------------------------------------------------------------------
if (DE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(UnitTests)
endif()
