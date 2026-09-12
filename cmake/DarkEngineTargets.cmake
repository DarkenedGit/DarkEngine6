# cmake/DarkEngineTargets.cmake
# Layered STATIC libraries + source globs for DarkEngine6.
# Included from the root CMakeLists.txt after Version.h generation setup.
# CMAKE_CURRENT_SOURCE_DIR is the repo root (include does not change it).

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

function(de_glob_folders out_var)
    set(_all "")
    foreach(folder IN LISTS ARGN)
        de_glob_folder(_folder_sources "${folder}")
        list(APPEND _all ${_folder_sources})
    endforeach()
    set(${out_var} "${_all}" PARENT_SCOPE)
endfunction()

# Mark non-C/C++ sources as IDE-only (not passed to the compiler).
function(de_mark_ide_only)
    foreach(src IN LISTS ARGN)
        get_filename_component(_ext "${src}" EXT)
        string(TOLOWER "${_ext}" _ext)
        if(NOT _ext MATCHES "\\.(c|cc|cpp|cxx)$")
            set_source_files_properties("${src}" PROPERTIES HEADER_FILE_ONLY TRUE)
        endif()
        if(_ext MATCHES "\\.(hlsl|hlsli)$")
            set_source_files_properties("${src}" PROPERTIES VS_TOOL_OVERRIDE "None")
        endif()
    endforeach()
endfunction()

# Shared includes / defs / warnings for every engine layer (and the umbrella).
function(de_engine_target_common target)
    target_include_directories(${target}
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${DE_GENERATED_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/third_party
    )
    target_compile_definitions(${target}
        PUBLIC
            $<$<BOOL:${DE_ENABLE_ASSERTS}>:DE_ENABLE_ASSERTS=1>
            NOMINMAX
            WIN32_LEAN_AND_MEAN
    )
    if (COMMAND de_set_compiler_options)
        de_set_compiler_options(${target})
    endif()
endfunction()

# --- Layer folder maps -------------------------------------------------------------
# Geometry/ does not exist in-tree (only UnitTests/Geometry); omitted.
# Shaders/ source folder is empty; runtime HLSL is under content/shaders/.

set(DE_FOUNDATION_FOLDERS
    Math
    Collision
    ECS
)

# Core split: foundation-safe TUs stay out of Renderer/Network/Debug coupling.
# Application.cpp / Window.cpp / MemoryTracker.cpp live in the DarkEngine umbrella.
set(DE_FOUNDATION_CORE_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/Core/Log.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Core/Log.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Core/Paths.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Core/Paths.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Core/ContentRoots.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Core/ContentRoots.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Core/UUID.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Core/UUID.h"
)

set(DE_ENGINE_CORE_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/Core/Application.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Core/Application.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Core/Window.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Core/Window.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Core/MemoryTracker.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Core/MemoryTracker.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Core/UiPalette.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Core/Version.h.in"
)

set(DE_RENDER_FOLDERS Render)
set(DE_ASSETS_FOLDERS Assets)
set(DE_NET_FOLDERS Network)

# Remaining engine folders compiled by the DarkEngine umbrella only.
set(DE_ENGINE_REST_FOLDERS
    AI
    Animation
    Audio
    Character
    Debug
    Input
    Particles
    Scene
    Sky
    Sprite
    Terrain
    Water
    Weapons
)

de_glob_folders(DE_FOUNDATION_SOURCES ${DE_FOUNDATION_FOLDERS})
list(APPEND DE_FOUNDATION_SOURCES ${DE_FOUNDATION_CORE_SOURCES})

de_glob_folders(DE_RENDER_SOURCES ${DE_RENDER_FOLDERS})
de_glob_folders(DE_ASSETS_SOURCES ${DE_ASSETS_FOLDERS})
de_glob_folders(DE_NET_SOURCES ${DE_NET_FOLDERS})
de_glob_folders(DE_ENGINE_REST_SOURCES ${DE_ENGINE_REST_FOLDERS})
list(APPEND DE_ENGINE_REST_SOURCES ${DE_ENGINE_CORE_SOURCES})

# Runtime HLSL lives under content/shaders (not a Shaders/ source folder).
# Add them to DarkEngine so they appear in the VS solution; do not compile them.
file(GLOB_RECURSE DE_SHADER_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/content/shaders/*.hlsl"
    "${CMAKE_CURRENT_SOURCE_DIR}/content/shaders/*.hlsli"
)
list(APPEND DE_ENGINE_REST_SOURCES ${DE_SHADER_SOURCES})

de_glob_folder(DE_SANDBOX_SOURCES Sandbox)
de_glob_folder(DE_SANDBOX2D_SOURCES Sandbox2D)
de_glob_folder(DE_EDITOR_SOURCES Editor)
de_glob_folder(DE_UI_SOURCES Ui)
de_glob_folder(DE_VISUALDEBUGGER_SOURCES VisualDebugger)
list(APPEND DE_EDITOR_SOURCES ${DE_UI_SOURCES})
list(APPEND DE_VISUALDEBUGGER_SOURCES ${DE_UI_SOURCES})

de_mark_ide_only(
    ${DE_FOUNDATION_SOURCES}
    ${DE_RENDER_SOURCES}
    ${DE_ASSETS_SOURCES}
    ${DE_NET_SOURCES}
    ${DE_ENGINE_REST_SOURCES}
    ${DE_SANDBOX_SOURCES}
    ${DE_SANDBOX2D_SOURCES}
    ${DE_EDITOR_SOURCES}
    ${DE_VISUALDEBUGGER_SOURCES}
)

# Compiler options helper (used by de_engine_target_common and host exes).
if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CompilerOptions.cmake")
    include(cmake/CompilerOptions.cmake)
endif()

# --- Layered engine libraries ------------------------------------------------------
# Target graph (STATIC; each .cpp compiled exactly once):
#
#   DarkFoundation  (Math, Collision, ECS, foundation Core)
#        ^
#        |-- DarkRender   (Render/* + D3D12 stack)
#        |        ^
#        |        `-- DarkAssets  (Assets/*; links Render — Model/TextureCache need Renderer)
#        |
#        `-- DarkNet      (Network/*; PRIVATE ws2_32)
#
#   DarkEngine umbrella PUBLIC-links all layers and compiles the rest
#   (AI Animation Audio Character Debug Input Particles Scene Sky Sprite
#    Terrain Water Weapons + Application/Window/MemoryTracker + content shaders).
#
# Note: Render/*.cpp also includes Assets headers (Material/ModelDraw). DarkRender
# does NOT link DarkAssets (would cycle); unresolved refs resolve at final link
# via the DarkEngine umbrella. Assets also includes Animation headers while
# Animation/*.cpp stay in DarkEngine — same final-link pattern.

add_library(DarkFoundation STATIC ${DE_FOUNDATION_SOURCES})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${DE_FOUNDATION_SOURCES})
de_engine_target_common(DarkFoundation)
set_target_properties(DarkFoundation PROPERTIES FOLDER "Engine")

add_library(DarkRender STATIC ${DE_RENDER_SOURCES})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${DE_RENDER_SOURCES})
de_engine_target_common(DarkRender)
target_link_libraries(DarkRender
    PUBLIC
        DarkFoundation
        d3d12
        dxgi
        dxguid
        d3dcompiler
        windowscodecs
)
set_target_properties(DarkRender PROPERTIES FOLDER "Engine")

add_library(DarkAssets STATIC ${DE_ASSETS_SOURCES})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${DE_ASSETS_SOURCES})
de_engine_target_common(DarkAssets)
target_link_libraries(DarkAssets
    PUBLIC
        DarkFoundation
        DarkRender
)
set_target_properties(DarkAssets PROPERTIES FOLDER "Engine")

add_library(DarkNet STATIC ${DE_NET_SOURCES})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${DE_NET_SOURCES})
de_engine_target_common(DarkNet)
target_link_libraries(DarkNet
    PUBLIC
        DarkFoundation
    PRIVATE
        # UDP/TCP sockets. PRIVATE on this STATIC lib still propagates to
        # dependents at final link (CMake 3.13+).
        ws2_32
)
set_target_properties(DarkNet PROPERTIES FOLDER "Engine")

add_library(DarkEngine STATIC ${DE_ENGINE_REST_SOURCES})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${DE_ENGINE_REST_SOURCES})
de_engine_target_common(DarkEngine)

target_sources(DarkEngine PRIVATE "${DE_GENERATED_DIR}/Core/Version.h")
set_source_files_properties("${DE_GENERATED_DIR}/Core/Version.h" PROPERTIES HEADER_FILE_ONLY TRUE)
source_group("generated" FILES "${DE_GENERATED_DIR}/Core/Version.h")

target_link_libraries(DarkEngine
    PUBLIC
        DarkFoundation
        DarkRender
        DarkAssets
        DarkNet
        xinput
        xaudio2
        ole32
    PRIVATE
        psapi
)
set_target_properties(DarkEngine PROPERTIES FOLDER "Engine")
