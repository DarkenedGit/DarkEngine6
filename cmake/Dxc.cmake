# DirectX Shader Compiler (NuGet Microsoft.Direct3D.DXC), plan D4/D10 and §3.15.
#
# Version 1.9.2609.5 (GitHub release v1.9.2609, Sep 29 2026). Licenses in the package:
# LICENSE-LLVM.txt (University of Illinois/NCSA, LLVM), LICENCE-MIT.txt, and LICENSE-MS.txt
# (Microsoft license for dxil.dll, the signing library; redistributable with an application).
#
#   DE_DXC_EXE       dxc.exe for the build-time bake (cmake/ShaderBake.cmake). Cache variable, so a
#                    different compiler (e.g. a Linux dxc for CI checks) can be passed with -DDE_DXC_EXE=.
#   DE_DXC_INCLUDE   dxcapi.h for Render/ShaderDxc.cpp.
#   DE_DXC_BIN       dxcompiler.dll + dxil.dll, copied to <exe dir>/dxc/ for hosts that compile at runtime.
#
# Runtime DXC is loaded with LoadLibrary from <exe dir>/dxc/ (no import library), so a Release game
# exe has no DXC dependency and its bin/ has no DXC DLLs (D10).

include("${CMAKE_CURRENT_LIST_DIR}/NuGetPackage.cmake")

set(DE_DXC_VERSION "1.9.2609.5")
de_fetch_nuget_package(Microsoft.Direct3D.DXC "${DE_DXC_VERSION}"
    73cfa082245918c2cc1ec6740250eb7377e83a757b3b3a3e16e0d71535bfbfc3
    DE_DXC_ROOT
    build/native/include/dxcapi.h
    build/native/bin/x64/dxc.exe
    build/native/bin/x64/dxcompiler.dll
    build/native/bin/x64/dxil.dll
)
set(DE_DXC_INCLUDE "${DE_DXC_ROOT}/build/native/include")
set(DE_DXC_BIN "${DE_DXC_ROOT}/build/native/bin/x64")
set(DE_DXC_EXE "${DE_DXC_BIN}/dxc.exe" CACHE FILEPATH "dxc used by the build-time shader bake (de_shaders)")
message(STATUS "DXC ${DE_DXC_VERSION}: ${DE_DXC_EXE}")

# Copies dxcompiler.dll + dxil.dll to <exe dir>/dxc/.
#   ALWAYS     every config (Editor, UnitTests)
#   DEBUG_ONLY Debug only (game exes; Release loads baked DXIL)
function(de_copy_dxc_runtime target mode)
    if(mode STREQUAL "ALWAYS")
        set(_mkdir make_directory)
        set(_op copy_if_different)
    elseif(mode STREQUAL "DEBUG_ONLY")
        # `cmake -E true` (CMake 3.16+) is a no-op, so non-Debug configs get no dxc/ folder at all.
        set(_mkdir "$<IF:$<CONFIG:Debug>,make_directory,true>")
        set(_op "$<IF:$<CONFIG:Debug>,copy_if_different,true>")
    else()
        message(FATAL_ERROR "de_copy_dxc_runtime: mode must be ALWAYS or DEBUG_ONLY")
    endif()
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E ${_mkdir} "$<TARGET_FILE_DIR:${target}>/dxc"
        COMMAND ${CMAKE_COMMAND} -E ${_op} "${DE_DXC_BIN}/dxcompiler.dll" "${DE_DXC_BIN}/dxil.dll" "$<TARGET_FILE_DIR:${target}>/dxc/"
        COMMENT "DXC runtime for ${target} (${mode})"
        VERBATIM
    )
endfunction()
