#pragma once

// Font Awesome 6 Free Solid subset. Load with content/fonts/fa-solid-900.ttf (see ImGuiHost).
// Codepoints from https://github.com/juliettef/IconFontCppHeaders (IconsFontAwesome6.h).

#define ICON_FA_XMARK "\xef\x80\x8d"              // U+f00d
#define ICON_FA_VOLUME_HIGH "\xef\x80\xa8"        // U+f028
#define ICON_FA_LIST "\xef\x80\xba"               // U+f03a
#define ICON_FA_PLAY "\xef\x81\x8b"               // U+f04b
#define ICON_FA_PAUSE "\xef\x81\x8c"              // U+f04c
#define ICON_FA_FORWARD_STEP "\xef\x81\x91"       // U+f051
#define ICON_FA_CIRCLE_PLUS "\xef\x81\x95"        // U+f055
#define ICON_FA_EYE "\xef\x81\xae"                // U+f06e
#define ICON_FA_FOLDER "\xef\x81\xbb"             // U+f07b
#define ICON_FA_FOLDER_OPEN "\xef\x81\xbc"        // U+f07c
#define ICON_FA_CLOUD "\xef\x83\x82"              // U+f0c2
#define ICON_FA_FLOPPY_DISK "\xef\x83\x87"        // U+f0c7
#define ICON_FA_BOLT "\xef\x83\xa7"               // U+f0e7
#define ICON_FA_LIGHTBULB "\xef\x83\xab"          // U+f0eb
#define ICON_FA_CIRCLE "\xef\x84\x91"             // U+f111
#define ICON_FA_FILE "\xef\x85\x9b"               // U+f15b
#define ICON_FA_BUG "\xef\x86\x88"                // U+f188
#define ICON_FA_CUBE "\xef\x86\xb2"               // U+f1b2
#define ICON_FA_PLUG "\xef\x87\xa6"               // U+f1e6
#define ICON_FA_TRASH "\xef\x87\xb8"              // U+f1f8
#define ICON_FA_SERVER "\xef\x88\xb3"             // U+f233
#define ICON_FA_RIGHT_FROM_BRACKET "\xef\x8b\xb5" // U+f2f5
#define ICON_FA_ROTATE_RIGHT "\xef\x8b\xb9"       // U+f2f9
#define ICON_FA_BROOM "\xef\x94\x9a"              // U+f51a
#define ICON_FA_MEMORY "\xef\x94\xb8"             // U+f538
#define ICON_FA_LAYER_GROUP "\xef\x97\xbd"        // U+f5fd
#define ICON_FA_GAUGE_HIGH "\xef\x98\xa5"         // U+f625
#define ICON_FA_NETWORK_WIRED "\xef\x9b\xbf"      // U+f6ff

// Every ICON_FA_* above. ImGuiHost decodes these into the atlas, so a new icon
// is one macro plus one line here.
#define DE_UI_ICON_STRINGS(X) \
    X(ICON_FA_XMARK)          \
    X(ICON_FA_VOLUME_HIGH)    \
    X(ICON_FA_LIST)           \
    X(ICON_FA_PLAY)           \
    X(ICON_FA_PAUSE)          \
    X(ICON_FA_FORWARD_STEP)   \
    X(ICON_FA_CIRCLE_PLUS)    \
    X(ICON_FA_EYE)            \
    X(ICON_FA_FOLDER)         \
    X(ICON_FA_FOLDER_OPEN)    \
    X(ICON_FA_CLOUD)          \
    X(ICON_FA_FLOPPY_DISK)    \
    X(ICON_FA_BOLT)           \
    X(ICON_FA_LIGHTBULB)      \
    X(ICON_FA_CIRCLE)         \
    X(ICON_FA_FILE)           \
    X(ICON_FA_BUG)            \
    X(ICON_FA_CUBE)           \
    X(ICON_FA_PLUG)           \
    X(ICON_FA_TRASH)          \
    X(ICON_FA_SERVER)         \
    X(ICON_FA_RIGHT_FROM_BRACKET) \
    X(ICON_FA_ROTATE_RIGHT)   \
    X(ICON_FA_BROOM)          \
    X(ICON_FA_MEMORY)         \
    X(ICON_FA_LAYER_GROUP)    \
    X(ICON_FA_GAUGE_HIGH)     \
    X(ICON_FA_NETWORK_WIRED)
