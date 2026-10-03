# WinPixEventRuntime 1.0.240308001 (MIT). Downloaded at configure time into the
# build tree so the repo does not store the DLL. GPU PIX events are what PIX,
# Nsight Graphics, and Nsight Systems show on the command list. The DLL also
# emits the CPU events PIX timing captures read. NVTX headers live in
# third_party/nvtx and need no binary.

set(DE_WINPIX_DLL "")
set(DE_WINPIX_LIB "")
set(DE_WINPIX_INCLUDE "")

if(DE_ENABLE_GPU_MARKERS)
set(DE_WINPIX_VERSION "1.0.240308001")
set(DE_WINPIX_ROOT "${CMAKE_BINARY_DIR}/_deps/winpixeventruntime-${DE_WINPIX_VERSION}")
set(DE_WINPIX_HEADER "${DE_WINPIX_ROOT}/Include/WinPixEventRuntime/pix3.h")
set(DE_WINPIX_LIB "${DE_WINPIX_ROOT}/bin/x64/WinPixEventRuntime.lib")
set(DE_WINPIX_DLL "${DE_WINPIX_ROOT}/bin/x64/WinPixEventRuntime.dll")

if(NOT EXISTS "${DE_WINPIX_HEADER}" OR NOT EXISTS "${DE_WINPIX_DLL}" OR NOT EXISTS "${DE_WINPIX_LIB}")
    file(MAKE_DIRECTORY "${DE_WINPIX_ROOT}")
    set(_de_winpix_nupkg "${DE_WINPIX_ROOT}/WinPixEventRuntime.${DE_WINPIX_VERSION}.nupkg")
    message(STATUS "Downloading WinPixEventRuntime ${DE_WINPIX_VERSION}")
    file(DOWNLOAD
        "https://www.nuget.org/api/v2/package/WinPixEventRuntime/${DE_WINPIX_VERSION}"
        "${_de_winpix_nupkg}"
        EXPECTED_HASH SHA256=726acc93d6968e2146261a1e415521747d50ad69894c2b42b5d0d4c29fd66ec4
        STATUS _de_winpix_status
        TLS_VERIFY ON
    )
    list(GET _de_winpix_status 0 _de_winpix_code)
    if(NOT _de_winpix_code EQUAL 0)
        list(GET _de_winpix_status 1 _de_winpix_msg)
        message(FATAL_ERROR "WinPixEventRuntime download failed (${_de_winpix_code}): ${_de_winpix_msg}")
    endif()
    file(ARCHIVE_EXTRACT INPUT "${_de_winpix_nupkg}" DESTINATION "${DE_WINPIX_ROOT}")
endif()

if(NOT EXISTS "${DE_WINPIX_HEADER}" OR NOT EXISTS "${DE_WINPIX_DLL}" OR NOT EXISTS "${DE_WINPIX_LIB}")
    message(FATAL_ERROR "WinPixEventRuntime ${DE_WINPIX_VERSION} is incomplete under ${DE_WINPIX_ROOT}")
endif()

set(DE_WINPIX_INCLUDE "${DE_WINPIX_ROOT}/Include/WinPixEventRuntime")
endif()

function(de_copy_pix_dll target)
    if(NOT DE_WINPIX_DLL)
        return()
    endif()
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${DE_WINPIX_DLL}"
            "$<TARGET_FILE_DIR:${target}>/WinPixEventRuntime.dll"
        COMMENT "Copying WinPixEventRuntime.dll next to ${target}"
    )
endfunction()
