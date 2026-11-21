# Per-exe platform setup (plan §3.15 "de_add_exe"): every exe that links DarkEngine calls
#
#   de_host_exe(<target> SHADERS RUNTIME)         # Editor, UnitTests: runtime DXC + cache in every config
#   de_host_exe(<target> SHADERS BAKED_RELEASE)   # game exes: runtime DXC in Debug, baked DXIL otherwise
#
# It adds Core/AgilityExports.cpp and the Agility runtime (cmake/AgilitySdk.cmake), defines the shader
# policy (Core/HostShaderPolicy.cpp -> Dark::kHostRuntimeShaderCompile), copies the DXC runtime to
# <exe dir>/dxc/ where the policy compiles at runtime, copies the baked shaders to <exe dir>/shaders/,
# and copies WinPixEventRuntime.dll.

set(DE_HOST_SHADER_POLICY_CPP "${CMAKE_SOURCE_DIR}/Core/HostShaderPolicy.cpp")

function(de_host_exe target)
    cmake_parse_arguments(PARSE_ARGV 1 _host "" "SHADERS" "")
    if(NOT _host_SHADERS MATCHES "^(RUNTIME|BAKED_RELEASE)$")
        message(FATAL_ERROR "de_host_exe(${target}): SHADERS must be RUNTIME or BAKED_RELEASE")
    endif()

    de_add_agility(${target})
    de_copy_pix_dll(${target})

    target_sources(${target} PRIVATE "${DE_HOST_SHADER_POLICY_CPP}")
    if(_host_SHADERS STREQUAL "RUNTIME")
        target_compile_definitions(${target} PRIVATE DE_HOST_RUNTIME_SHADERS=1)
        de_copy_dxc_runtime(${target} ALWAYS)
    else()
        target_compile_definitions(${target} PRIVATE "DE_HOST_RUNTIME_SHADERS=$<IF:$<CONFIG:Debug>,1,0>")
        de_copy_dxc_runtime(${target} DEBUG_ONLY)
        # Baked DXIL in every config, so DE_SHADER_BAKED=1 can exercise the Release path from Debug.
        add_dependencies(${target} de_shaders)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND}
                    -DSRC=${DE_BAKED_SHADER_DIR}
                    -DDST=$<TARGET_FILE_DIR:${target}>/shaders
                    -P ${CMAKE_SOURCE_DIR}/cmake/CopyContent.cmake
            COMMENT "Copying baked shaders next to ${target}"
        )
    endif()
endfunction()
