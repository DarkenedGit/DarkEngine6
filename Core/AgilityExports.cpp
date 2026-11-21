// Agility SDK exports (plan D4/D9, §3.15). Compiled into every exe by de_host_exe in CMake, never into a
// library: the D3D12 loader reads these from the process exe and then loads D3D12Core.dll from
// <exe dir>/D3D12/ instead of the OS runtime. RhiDevice's startup gate checks that it did.

#include <d3d12.h>

extern "C"
{
    __declspec(dllexport) extern const unsigned int D3D12SDKVersion = D3D12_SDK_VERSION;
    __declspec(dllexport) extern const char*        D3D12SDKPath    = ".\\D3D12\\";
}
