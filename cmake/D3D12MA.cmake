# D3D12 Memory Allocator (GPUOpen D3D12MA), render-graph plan §3.15 and Phase 5.
#
# Version 3.3.0 (tag v3.3.0, commit 600949266be004553f74c9ef809384380a9d44cf, released 2026-11-09).
# License: MIT, "Copyright (c) 2019-2026 Advanced Micro Devices, Inc." (LICENSE.txt in the archive,
# kept next to the sources under _deps). D3D12MA neither throws nor catches C++ exceptions.
#
# Downloaded at configure time with a pinned SHA-256, like the Agility SDK and DXC packages
# (cmake/AgilitySdk.cmake, cmake/Dxc.cmake), so the repo stores no third-party sources for it.
# Built as its own static library (D3D12MA) against the Agility headers. The engine's /W4 /WX
# flags are not applied to it (third-party code); Render/GpuAllocator.cpp is the only user.

set(DE_D3D12MA_VERSION "3.3.0")
set(DE_D3D12MA_COMMIT "600949266be004553f74c9ef809384380a9d44cf")
set(DE_D3D12MA_SHA256 "3d120fcf1b8d481b956eeae8377486fa1c0b5f726a78a2db1b566cf45ece6061")

set(_de_d3d12ma_root "${CMAKE_BINARY_DIR}/_deps/d3d12ma-${DE_D3D12MA_VERSION}")
set(_de_d3d12ma_src "${_de_d3d12ma_root}/D3D12MemoryAllocator-${DE_D3D12MA_COMMIT}")
set(_de_d3d12ma_files include/D3D12MemAlloc.h src/D3D12MemAlloc.cpp LICENSE.txt)

set(_de_d3d12ma_missing FALSE)
foreach(_rel IN LISTS _de_d3d12ma_files)
    if(NOT EXISTS "${_de_d3d12ma_src}/${_rel}")
        set(_de_d3d12ma_missing TRUE)
    endif()
endforeach()

if(_de_d3d12ma_missing)
    file(MAKE_DIRECTORY "${_de_d3d12ma_root}")
    set(_de_d3d12ma_archive "${_de_d3d12ma_root}/D3D12MemoryAllocator-${DE_D3D12MA_VERSION}.tar.gz")
    message(STATUS "Downloading D3D12MemoryAllocator ${DE_D3D12MA_VERSION}")
    file(DOWNLOAD
        "https://github.com/GPUOpen-LibrariesAndSDKs/D3D12MemoryAllocator/archive/${DE_D3D12MA_COMMIT}.tar.gz"
        "${_de_d3d12ma_archive}"
        EXPECTED_HASH SHA256=${DE_D3D12MA_SHA256}
        STATUS _de_d3d12ma_status
        TLS_VERIFY ON
    )
    list(GET _de_d3d12ma_status 0 _de_d3d12ma_code)
    if(NOT _de_d3d12ma_code EQUAL 0)
        list(GET _de_d3d12ma_status 1 _de_d3d12ma_msg)
        message(FATAL_ERROR "D3D12MemoryAllocator ${DE_D3D12MA_VERSION} download failed (${_de_d3d12ma_code}): ${_de_d3d12ma_msg}")
    endif()
    file(ARCHIVE_EXTRACT INPUT "${_de_d3d12ma_archive}" DESTINATION "${_de_d3d12ma_root}")
    file(REMOVE "${_de_d3d12ma_archive}")
endif()

foreach(_rel IN LISTS _de_d3d12ma_files)
    if(NOT EXISTS "${_de_d3d12ma_src}/${_rel}")
        message(FATAL_ERROR "D3D12MemoryAllocator ${DE_D3D12MA_VERSION} is incomplete: ${_de_d3d12ma_src}/${_rel} is missing")
    endif()
endforeach()
message(STATUS "D3D12MemoryAllocator ${DE_D3D12MA_VERSION} (${DE_D3D12MA_COMMIT})")

set(DE_D3D12MA_INCLUDE "${_de_d3d12ma_src}/include")

add_library(D3D12MA STATIC "${_de_d3d12ma_src}/src/D3D12MemAlloc.cpp" "${_de_d3d12ma_src}/include/D3D12MemAlloc.h")
# Agility headers first, as for every engine target, so D3D12MA sees the same d3d12.h as DarkRender.
target_include_directories(D3D12MA BEFORE PUBLIC "${DE_AGILITY_INCLUDE}")
target_include_directories(D3D12MA PUBLIC "${DE_D3D12MA_INCLUDE}")
target_compile_definitions(D3D12MA PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(D3D12MA PUBLIC d3d12 dxgi dxguid)
set_target_properties(D3D12MA PROPERTIES FOLDER "ThirdParty")

unset(_de_d3d12ma_root)
unset(_de_d3d12ma_src)
unset(_de_d3d12ma_files)
unset(_de_d3d12ma_missing)
