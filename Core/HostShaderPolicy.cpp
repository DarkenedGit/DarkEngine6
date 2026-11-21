// Shader policy for one exe (plan D10). Compiled into every exe by de_host_exe in CMake, never into a
// library. DE_HOST_RUNTIME_SHADERS is 1 for hosts that compile HLSL at runtime with DXC (Debug, Editor,
// UnitTests) and 0 for Release game exes, which load baked DXIL only. Declared in Render/ShaderCompile.h.

#ifndef DE_HOST_RUNTIME_SHADERS
#error "DE_HOST_RUNTIME_SHADERS is set by de_host_exe (cmake/HostExe.cmake)"
#endif

namespace Dark
{
    extern const bool kHostRuntimeShaderCompile;
    const bool        kHostRuntimeShaderCompile = DE_HOST_RUNTIME_SHADERS != 0;
} // namespace Dark
