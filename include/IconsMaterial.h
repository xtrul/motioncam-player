// IconsMaterial.h
#pragma once

// Common Material Design Icon codepoints.
// Verify these against the .codepoints file for your specific MaterialIcons-Regular.ttf
// The u8 prefix ensures the string literal is UTF-8, which ImGui handles.

// Define the range of icons used from the Material Design font.
// This helps ImGui to only bake the necessary glyphs into the font atlas.
// Consult the .codepoints file or a character map for your MaterialIcons-Regular.ttf
// to find the min and max codepoints for the icons you are using.
// For a broad range (might increase startup time and memory if too many are unused):
#define ICON_MIN_MD 0xe000
#define ICON_MAX_16_MD 0xf23b // A common upper bound, check your font's .codepoints file

#define ICON_MD_PLAY_ARROW u8"\ue037"
#define ICON_MD_PAUSE u8"\ue034"
#define ICON_MD_SKIP_PREVIOUS u8"\ue045"
#define ICON_MD_SKIP_NEXT u8"\ue044"
#define ICON_MD_VOLUME_UP u8"\ue050"
#define ICON_MD_VOLUME_OFF u8"\ue04f"     // For mute state
#define ICON_MD_MENU u8"\ue5d2"           // Hamburger menu for playlist
#define ICON_MD_SETTINGS u8"\ue8b8"
#define ICON_MD_FULLSCREEN u8"\ue5d0"
#define ICON_MD_FULLSCREEN_EXIT u8"\ue5d1"
#define ICON_MD_INFO_OUTLINE u8"\ue88f"   // For metrics toggle (info icon) - Can be used for Insights
#define ICON_MD_INSIGHTS u8"\uf09c"       // Specific insights icon (if available and preferred)
#define ICON_MD_HELP_OUTLINE u8"\ue8fd"   // Help icon
#define ICON_MD_CLOSE u8"\ue5cd"
#define ICON_MD_FOLDER_OPEN u8"\ue2c8"
#define ICON_MD_DELETE u8"\ue872"
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
#define ICON_MD_KEYBOARD_ARROW_LEFT u8"\uE314" 
#define ICON_MD_KEYBOARD_ARROW_RIGHT u8"\uE315"

// <<< --- ADDED FOR DNG --- >>>
#define ICON_MD_SAVE u8"\ue161"             // For "Save Current DNG"
#define ICON_MD_COLLECTIONS u8"\ue3b7"      // For "Export All DNGs" (or e.g. ICON_MD_PHOTO_LIBRARY u8"\ue413")
// <<< --- END OF ADDITION --- >>>


// Add more icons as you need them from the Material Icons set.
// Find codepoints here: https://github.com/google/material-design-icons/blob/master/font/MaterialIcons-Regular.codepoints
// Or by browsing on https://fonts.google.com/icons (select an icon, look for "Codepoint")y