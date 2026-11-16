#pragma once

namespace Dark
{

    // Reads masterVolume from %LOCALAPPDATA%/DarkEngine6/settings.json.
    // A missing file or a bad value returns fallback.
    float loadMasterVolume(float fallback);

    // Writes that same file. False when the folder or the file cannot be written.
    bool saveMasterVolume(float volume);

} // namespace Dark
