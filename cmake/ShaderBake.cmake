# Release shader bake (plan D10, §3.15.1): custom target de_shaders compiles every permutation in
# content/shaders/shaders.manifest with dxc into ${CMAKE_BINARY_DIR}/shaders/<name>.dxil, signed by
# the build machine's dxil.dll. PDBs go to ${CMAKE_BINARY_DIR}/shaders_pdb/ and stay on the build
# machine. Exes with baked shaders copy shaders/ next to themselves (de_host_exe).
#
# Inputs: DE_DXC_EXE (cmake/Dxc.cmake). The shader model is read from kShaderModelDxc in
# Render/ShaderKey.h so the bake and the runtime compiler always agree.
# Names follow bakedShaderFileName() in Render/ShaderKey.cpp; ShaderManifestTests checks the manifest.
#
# Each permutation is one custom command with two dxc runs: the compile, then `-M -MF` for a depfile
# so a change to an included .hlsli rebuilds only the permutations that include it.

set(DE_SHADER_MANIFEST "${CMAKE_SOURCE_DIR}/content/shaders/shaders.manifest")
set(DE_SHADER_SOURCE_DIR "${CMAKE_SOURCE_DIR}/content/shaders")
set(DE_BAKED_SHADER_DIR "${CMAKE_BINARY_DIR}/shaders")
set(DE_BAKED_SHADER_PDB_DIR "${CMAKE_BINARY_DIR}/shaders_pdb")
set(DE_BAKED_SHADER_DEP_DIR "${CMAKE_BINARY_DIR}/shaders_deps")

file(STRINGS "${CMAKE_SOURCE_DIR}/Render/ShaderKey.h" _de_sm_line REGEX "kShaderModelDxc[ \t]*=")
string(REGEX MATCH "\"([0-9]+_[0-9]+)\"" _de_sm_match "${_de_sm_line}")
set(DE_SHADER_MODEL "${CMAKE_MATCH_1}")
if(NOT DE_SHADER_MODEL)
    message(FATAL_ERROR "ShaderBake: could not read kShaderModelDxc from Render/ShaderKey.h")
endif()
unset(_de_sm_line)
unset(_de_sm_match)

# Same rule as bakedShaderFileName(): "<stem>.<entry>.<stage>[.<defines>].dxil" with '=' -> '-',
# ',' and '/' -> '_', anything else outside [A-Za-z0-9_.-] -> '_'.
function(de_baked_shader_name out_var file entry stage defines)
    string(REGEX REPLACE "\\.hlsl$" "" _stem "${file}")
    set(_name "${_stem}.${entry}.${stage}")
    if(NOT defines STREQUAL "")
        string(APPEND _name ".${defines}")
    endif()
    string(REPLACE "=" "-" _name "${_name}")
    string(REPLACE "," "_" _name "${_name}")
    string(REPLACE "/" "_" _name "${_name}")
    string(REGEX REPLACE "[^A-Za-z0-9_.-]" "_" _name "${_name}")
    set(${out_var} "${_name}.dxil" PARENT_SCOPE)
endfunction()

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${DE_SHADER_MANIFEST}" "${CMAKE_SOURCE_DIR}/Render/ShaderKey.h")
file(STRINGS "${DE_SHADER_MANIFEST}" _de_manifest_lines)
file(MAKE_DIRECTORY "${DE_BAKED_SHADER_DIR}" "${DE_BAKED_SHADER_PDB_DIR}" "${DE_BAKED_SHADER_DEP_DIR}")

set(DE_BAKED_SHADER_OUTPUTS "")
set(_de_line_no 0)
foreach(_line IN LISTS _de_manifest_lines)
    math(EXPR _de_line_no "${_de_line_no} + 1")
    string(STRIP "${_line}" _line)
    if(_line STREQUAL "" OR _line MATCHES "^#")
        continue()
    endif()
    string(REGEX REPLACE "[ \t]+" ";" _fields "${_line}")
    list(LENGTH _fields _count)
    if(_count LESS 3 OR _count GREATER 4)
        message(FATAL_ERROR "shaders.manifest line ${_de_line_no}: expected '<file> <entry> <stage> [defines]': ${_line}")
    endif()
    list(GET _fields 0 _file)
    list(GET _fields 1 _entry)
    list(GET _fields 2 _stage)
    set(_defines "")
    if(_count EQUAL 4)
        list(GET _fields 3 _defines)
    endif()
    if(NOT _stage MATCHES "^(vs|ps|cs)$")
        message(FATAL_ERROR "shaders.manifest line ${_de_line_no}: stage '${_stage}' is not vs, ps or cs")
    endif()
    set(_src "${DE_SHADER_SOURCE_DIR}/${_file}")
    if(NOT EXISTS "${_src}")
        message(FATAL_ERROR "shaders.manifest line ${_de_line_no}: ${_file} does not exist under content/shaders")
    endif()

    de_baked_shader_name(_name "${_file}" "${_entry}" "${_stage}" "${_defines}")
    set(_out "${DE_BAKED_SHADER_DIR}/${_name}")
    string(REGEX REPLACE "\\.dxil$" "" _base "${_name}")
    set(_pdb "${DE_BAKED_SHADER_PDB_DIR}/${_base}.pdb")
    set(_dep "${DE_BAKED_SHADER_DEP_DIR}/${_base}.d")

    set(_define_args "")
    if(NOT _defines STREQUAL "")
        string(REPLACE "," ";" _pairs "${_defines}")
        foreach(_pair IN LISTS _pairs)
            list(APPEND _define_args -D "${_pair}")
        endforeach()
    endif()

    set(_common -nologo -T "${_stage}_${DE_SHADER_MODEL}" -E "${_entry}" -HV 2021 -Ges ${_define_args} -I "${DE_SHADER_SOURCE_DIR}")
    add_custom_command(
        OUTPUT "${_out}"
        COMMAND "${DE_DXC_EXE}" ${_common} -O3 -Zi -Qstrip_debug -Fd "${_pdb}" -Fo "${_out}" "${_src}"
        COMMAND "${DE_DXC_EXE}" ${_common} -M -MF "${_dep}" -Fo "${_out}" "${_src}"
        DEPENDS "${_src}" "${DE_SHADER_MANIFEST}"
        DEPFILE "${_dep}"
        COMMENT "DXC ${_stage}_${DE_SHADER_MODEL} ${_name}"
        VERBATIM
    )
    list(APPEND DE_BAKED_SHADER_OUTPUTS "${_out}")
endforeach()
unset(_de_manifest_lines)
unset(_de_line_no)

list(LENGTH DE_BAKED_SHADER_OUTPUTS _de_baked_count)
message(STATUS "Shader bake: ${_de_baked_count} permutations at SM ${DE_SHADER_MODEL}")
unset(_de_baked_count)

add_custom_target(de_shaders DEPENDS ${DE_BAKED_SHADER_OUTPUTS} SOURCES "${DE_SHADER_MANIFEST}")
set_target_properties(de_shaders PROPERTIES FOLDER "Engine")

# Generated byte-array header for the LoadingScreen splash (no files, no compiler at runtime).
set(DE_LOADING_SCREEN_SHADERS_H "${DE_GENERATED_DIR}/Render/LoadingScreenShaders.h")
add_custom_command(
    OUTPUT "${DE_LOADING_SCREEN_SHADERS_H}"
    COMMAND ${CMAKE_COMMAND}
        "-DDXC=${DE_DXC_EXE}"
        "-DSRC=${DE_SHADER_SOURCE_DIR}/LoadingScreen.hlsl"
        "-DOUT=${DE_LOADING_SCREEN_SHADERS_H}"
        "-DSM=${DE_SHADER_MODEL}"
        -DNAMESPACE=LoadingScreenShaders
        "-DENTRIES=VSMain:vs$<SEMICOLON>PSMain:ps"
        -P "${CMAKE_SOURCE_DIR}/cmake/EmbedShaders.cmake"
    DEPENDS "${DE_SHADER_SOURCE_DIR}/LoadingScreen.hlsl" "${CMAKE_SOURCE_DIR}/cmake/EmbedShaders.cmake" "${CMAKE_SOURCE_DIR}/Render/ShaderKey.h"
    COMMENT "Embedding LoadingScreen shaders"
    VERBATIM
)
