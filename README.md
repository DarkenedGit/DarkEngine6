# DarkEngine6
6th version of the DarkEngine for games.

## Sourcetrail (code graph / coupling explorer)

Sourcetrail indexes C++ so you can browse class relationships, includes, and call edges.

1. **One-time / when sources change** — generate a Clang compilation database (Ninja; VS generators do not emit one):

   ```bat
   scripts\setup-sourcetrail.bat
   ```

   This configures `build-sourcetrail/`, writes `compile_commands.json` at the repo root, and optionally `compile_commands.engine.json` (engine TUs without gtest/imgui).

2. **Open the project** — double-click `DarkEngine6.srctrlprj` or *File → Open Project* in Sourcetrail.

3. **Index** — choose *Start*. First index takes a few minutes. Later use *Refresh* after re-running the setup script.

Sourcetrail is installed via `winget install CoatiSoftware.Sourcetrail` if missing. Database files (`*.srctrldb`) are gitignored.
