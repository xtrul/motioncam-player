#pragma once

// Material Design Icon codepoints for use with ImGui.
// These values should correspond to the glyphs in the MaterialIcons-Regular.ttf font file.
// The u8 prefix ensures the string literals are UTF-8.

// Defines the range of icon codepoints baked into the ImGui font atlas.
// Using a broad range can increase startup time and memory if many icons are unused.
// Consult the .codepoints file (e.g., from Google Fonts) for the specific font version.
#define ICON_MIN_MD 0xe000
#define ICON_MAX_16_MD 0xf23b // A common upper bound, adjust if needed.

// Playback & Volume
#define ICON_MD_PLAY_ARROW u8"\ue037"
#define ICON_MD_PAUSE u8"\ue034"
#define ICON_MD_SKIP_PREVIOUS u8"\ue045"
#define ICON_MD_SKIP_NEXT u8"\ue044"
#define ICON_MD_VOLUME_UP u8"\ue050"
#define ICON_MD_VOLUME_OFF u8"\ue04f"
#define ICON_MD_KEYBOARD_ARROW_LEFT u8"\uE314"
#define ICON_MD_KEYBOARD_ARROW_RIGHT u8"\uE315"

// UI & Navigation
#define ICON_MD_MENU u8"\ue5d2"         // Hamburger menu
#define ICON_MD_FULLSCREEN u8"\ue5d0"
#define ICON_MD_FULLSCREEN_EXIT u8"\ue5d1"
#define ICON_MD_CLOSE u8"\ue5cd"
#define ICON_MD_SETTINGS u8"\ue8b8"

// Information & Help
#define ICON_MD_INFO_OUTLINE u8"\ue88f" // General info
#define ICON_MD_INSIGHTS u8"\uf09c"     // Specific "insights" or metrics icon
#define ICON_MD_HELP_OUTLINE u8"\ue8fd"

// File Operations & Actions
#define ICON_MD_FOLDER_OPEN u8"\ue2c8"
#define ICON_MD_DELETE u8"\ue872"
#define ICON_MD_SAVE u8"\ue161"             // For "Save Current DNG"
#define ICON_MD_COLLECTIONS u8"\ue3b7"      // For "Export All DNGs" (or ICON_MD_PHOTO_LIBRARY u8"\ue413")

// Miscellaneous (Examples, may not be used)
#define ICON_MD_VISIBILITY u8"\ue8f4"
#define ICON_MD_VISIBILITY_OFF u8"\ue8f5"
#define ICON_MD_SEARCH u8"\ue8b6"
#define ICON_MD_REFRESH u8"\ue5d5"
#define ICON_MD_CHECK_BOX_OUTLINE_BLANK u8"\ue835"
#define ICON_MD_CHECK_BOX u8"\ue834"
#define ICON_MD_RADIO_BUTTON_UNCHECKED u8"\ue836"
#define ICON_MD_RADIO_BUTTON_CHECKED u8"\ue837"
#define ICON_MD_STAR u8"\ue838"
#define ICON_MD_STAR_BORDER u8"\ue83a"

// For more icons, refer to:
// Google Fonts: https://fonts.google.com/icons
// Material Design Icons GitHub (for .codepoints file): https://github.com/google/material-design-icons/
