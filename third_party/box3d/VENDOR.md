# Box3D vendor pin

- **Upstream:** https://github.com/erincatto/box3d
- **Tag:** v0.1.0
- **Commit:** 8441b4a06d6d09dcfb0b0f704df4d847d1437b92

This is a snapshot (no nested `.git`), same pattern as `third_party/box2d`.

## CMake patches (DarkEngine)

Applied in this tree's `CMakeLists.txt` so Box3D can be `add_subdirectory`'d:

1. Commented out unconditional `CMAKE_MSVC_RUNTIME_LIBRARY` `/MT`. DarkEngine uses `/MD`.
2. Gated `/ZI` + `/INCREMENTAL` on `PROJECT_IS_TOP_LEVEL`.
3. Removed `FETCHCONTENT_BASE_DIR` → `${CMAKE_SOURCE_DIR}/.fetchcontent-cache`.

Root `CMakeLists.txt` also `set_property(TARGET box3d PROPERTY MSVC_RUNTIME_LIBRARY ...DLL)` after `add_subdirectory`.
