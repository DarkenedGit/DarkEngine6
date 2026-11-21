# DirectX 12 Agility SDK (NuGet Microsoft.Direct3D.D3D12), plan D4/D9 and §3.15.
#
# Version 1.620.2 (retail), D3D12_SDK_VERSION 620. License: Microsoft DirectX SDK license
# (LICENSE.txt in the package). D3D12Core.dll and d3d12SDKLayers.dll are on the package's
# "distributable files.txt" list; the headers and d3dx12 helpers are MIT (LICENSE-CODE.txt).
#
# Every exe exports D3D12SDKVersion/D3D12SDKPath (Core/AgilityExports.cpp, added by de_host_exe)
# and gets D3D12Core.dll (+ d3d12SDKLayers.dll in Debug) in <exe dir>/D3D12/.
# The package headers come first on every engine and exe include path, so d3d12.h is the
# Agility version (enhanced barriers, D3D12_FEATURE_D3D12_OPTIONS12, SM 6.6+ enums).

include("${CMAKE_CURRENT_LIST_DIR}/NuGetPackage.cmake")

set(DE_AGILITY_VERSION "1.620.2")
de_fetch_nuget_package(Microsoft.Direct3D.D3D12 "${DE_AGILITY_VERSION}"
    70489669c2bbeaf318b62d72b0ac3e753d521387bfc848c9763791c4e920bfcc
    DE_AGILITY_ROOT
    build/native/include/d3d12.h
    build/native/bin/x64/D3D12Core.dll
    build/native/bin/x64/d3d12SDKLayers.dll
)
set(DE_AGILITY_INCLUDE "${DE_AGILITY_ROOT}/build/native/include")
set(DE_AGILITY_BIN "${DE_AGILITY_ROOT}/build/native/bin/x64")

# The exported D3D12SDKVersion must match the package's D3D12_SDK_VERSION; read it so a version
# bump cannot leave the two apart.
file(STRINGS "${DE_AGILITY_INCLUDE}/d3d12.h" _de_agility_sdk_line REGEX "^#define[ \t]+D3D12_SDK_VERSION[ \t]")
string(REGEX MATCH "D3D12_SDK_VERSION[ \t]+\\(?[ \t]*([0-9]+)" _de_agility_sdk_match "${_de_agility_sdk_line}")
set(DE_AGILITY_SDK_VERSION "${CMAKE_MATCH_1}")
if(NOT DE_AGILITY_SDK_VERSION)
    message(FATAL_ERROR "Could not read D3D12_SDK_VERSION from ${DE_AGILITY_INCLUDE}/d3d12.h")
endif()
message(STATUS "Agility SDK ${DE_AGILITY_VERSION} (D3D12_SDK_VERSION ${DE_AGILITY_SDK_VERSION})")
unset(_de_agility_sdk_line)
unset(_de_agility_sdk_match)

set(DE_AGILITY_EXPORTS_CPP "${CMAKE_SOURCE_DIR}/Core/AgilityExports.cpp")

# Exports + runtime DLLs for one exe.
function(de_add_agility target)
    target_sources(${target} PRIVATE "${DE_AGILITY_EXPORTS_CPP}")
    set_source_files_properties("${DE_AGILITY_EXPORTS_CPP}" PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${target}>/D3D12"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${DE_AGILITY_BIN}/D3D12Core.dll" "$<TARGET_FILE_DIR:${target}>/D3D12/"
        # d3d12SDKLayers.dll only in Debug (the debug layer is Debug-only, see RendererDiagnostics.cpp).
        # Other configs copy D3D12Core.dll again, which is a no-op.
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<IF:$<CONFIG:Debug>,${DE_AGILITY_BIN}/d3d12SDKLayers.dll,${DE_AGILITY_BIN}/D3D12Core.dll>"
            "$<TARGET_FILE_DIR:${target}>/D3D12/"
        COMMENT "Copying Agility SDK ${DE_AGILITY_VERSION} runtime to ${target}/D3D12"
        VERBATIM
    )
endfunction()
