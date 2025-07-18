//-----------------------------------------------------------------------------
// COMPILE-TIME OPTIONS FOR DEAR IMGUI
// Runtime options (clipboard callbacks, enabling various features, etc.) can be set via the ImGuiIO structure in ImGui::GetIO().
//-----------------------------------------------------------------------------
// --> Define IMGUI_USER_CONFIG to implement your own backend needs.
// --> Define IMGUI_DISABLE to disable certain features.
// --> Define IMGUI_ENABLE_FREETYPE to enable Freetype font builder.
// --> Define IMGUI_ENABLE_OBJC_BRIDGING to enable Objective C types.
// --> Define IMGUI_STB_TRUETYPE_FILENAME to use a custom path to stb_truetype.h
// --> Define IMGUI_STB_RECT_PACK_FILENAME to use a custom path to stb_rect_pack.h
// --> Define IMGUI_DISABLE_DEFAULT_ALLOCATORS to disable default allocators.
// --> Define IMGUI_DISABLE_SSE to disable SSE/AVX instructions.
//-----------------------------------------------------------------------------

#pragma once // Ensure this is at the top if not already

//---- Define assertion handler. Defaults to calling assert().
//#define IM_ASSERT(_EXPR)  MyAssert(_EXPR)
//#define IM_ASSERT(_EXPR)  ((void)(_EXPR))     // Disable asserts

//---- Define attributes of all API symbols declarations, e.g. for DLL under Windows
// Using Dear ImGui via a shared library is not recommended, because we don't guarantee API/ABI stability.
// DLL users: heaps and globals are not shared across DLL boundaries! You will need to call SetCurrentContext() + SetAllocatorFunctions()
// for each static/DLL boundary you are calling from. Read "Contexts" documentation.
//#define IMGUI_API __declspec( dllexport )
//#define IMGUI_API __declspec( dllimport )

//---- Don't define obsolete functions/enums names. Consider enabling from time to time after updating to avoid using soon-to-be obsolete function/names.
#define IMGUI_DISABLE_OBSOLETE_FUNCTIONS
#define IMGUI_DISABLE_OBSOLETE_KEYIO // Obsolete key input api. @PendingRemoval

//---- Disable all of Dear ImGui specific assert macros. Consider enabling if you want to integrate Dear ImGui into your own assertion library.
//#define IMGUI_DISABLE_IMASSERT

//---- Disable test engine api (BuildAssert() macros). Build time only.
//#define IMGUI_DISABLE_TEST_WINDOWS

//---- Disable MSVC security warnings: (suppress warnings about _CRT_SECURE_NO_WARNINGS since we are using it in examples)
// This is generally not recommended in production code.
#if !defined(_CRT_SECURE_NO_WARNINGS) && defined(_MSC_VER)
#define _CRT_SECURE_NO_WARNINGS
#endif

//---- Path to stb_truetype.h, stb_rect_pack.h (if they are not in same directory as imgui.cpp)
//#define IMGUI_STB_TRUETYPE_FILENAME   "my_folder/stb_truetype.h"
//#define IMGUI_STB_RECT_PACK_FILENAME  "my_folder/stb_rect_pack.h"

//---- Enable Freetype font builder (requires linking with Freetype library)
// (ImGuiFreeType::BuildFontAtlas() function is in 'misc/freetype/imgui_freetype.h')
//#define IMGUI_ENABLE_FREETYPE

//---- Enable SSE/AVX instructions to optimize some functions. Enabled by default when IMGUI_DISABLE_SSE is not defined.
//#define IMGUI_DISABLE_SSE

//---- Define constructor and implicit cast operators to convert back<>forth between your math types and ImVec2/ImVec4.
// This will be inlined as part of ImVec2 and ImVec4 class declarations.
/*
#define IM_VEC2_CLASS_EXTRA                                                 \
        ImVec2(const MyVec2& f) { x = f.x; y = f.y; }                       \
        operator MyVec2() const { return MyVec2(x,y); }

#define IM_VEC4_CLASS_EXTRA                                                 \
        ImVec4(const MyVec4& f) { x = f.x; y = f.y; z = f.z; w = f.w; }     \
        operator MyVec4() const { return MyVec4(x,y,z,w); }
*/

//---- Using 32-bit vertex indices (default is 16-bit) is necessary if you are using the vertex buffer offsetting
// (ImGuiBackendFlags_RendererHasVtxOffset) to draw meshes with more than 64K vertices.
// Your renderer backend will need to support it.
//#define ImDrawIdx unsigned int

//---- Support multiple viewports. Enable per-window viewports, which includes creation of OS windows
// and rendering into them. This is a complex feature! See "Viewports" documentation.
// (Requires IMGUI_HAS_DOCK enabling also)
// #define IMGUI_HAS_VIEWPORT // Deprecated, use ImGuiConfigFlags_ViewportsEnable in ImGuiIO
// #define IMGUI_VIEWPORT_BRANCH // For older viewport branch, not needed for docking branch

//---- Support for docking nodes. Enable programming docking nodes and tab bars.
// (Viewports are enabled by ImGuiConfigFlags_ViewportsEnable in ImGuiIO, if IMGUI_HAS_DOCK is set)
// #define IMGUI_HAS_DOCK // Deprecated, use ImGuiConfigFlags_DockingEnable in ImGuiIO

//---- Options: IMGUI_USER_CONFIG
// Defining IMGUI_USER_CONFIG to a specific filename will instruct Dear ImGui to load this file instead of the default imconfig.h.
// This is useful to prevent modifying the library code directly.
// #define IMGUI_USER_CONFIG "my_imconfig.h"


//---- Actual configuration options often go here, or are enabled via ImGuiIO in your code ----

// If you are using the docking branch, these are typically enabled via ImGuiIO flags,
// but defining them here can sometimes help if the ImGui build itself needs to know.
// However, the errors C2065 for ImGuiConfigFlags_DockingEnable suggest that the version
// of imgui.h being picked up doesn't have these flags, which means the docking code
// itself might not be compiled in.

// For the docking branch, these macros IMGUI_HAS_DOCK and IMGUI_HAS_VIEWPORT
// are usually *internally* defined when you include "imgui_internal.h"
// or when the correct ImGui source files from the docking branch are compiled.
// The public API uses io.ConfigFlags.

// The errors suggest that the `imgui.h` your `GuiOverlay.cpp` is seeing
// does not have these flags. This is strange if you are indeed using the docking branch.

// Let's try explicitly defining the macros that enable these features internally for ImGui,
// though this is usually done by ImGui's build system or by including imgui_internal.h.
// This is more of a "force enable" if the regular mechanism isn't working.
// Put these *before* any ImGui includes if you were to use a custom imconfig.h.
// Since we are editing the one in external/imgui, it will be picked up automatically.

// #define IMGUI_ENABLE_DOCKING // This is an older way, might not be used by current docking branch
// #define IMGUI_ENABLE_VIEWPORTS // Older way

// The modern docking branch usually makes these features available if you compile
// the correct source files (imgui_widgets.cpp etc. from the docking branch).
// The flags like ImGuiConfigFlags_DockingEnable should then be defined in imgui.h.

// Given the errors, it seems the imgui.h that your GuiOverlay.cpp is including
// is one that *doesn't* have these flags defined.
// This could happen if:
// 1. You are not actually on the 'docking' branch/tag in external/imgui.
// 2. The include paths are somehow picking up an older/different imgui.h.

// For now, let's assume you *are* on the docking branch.
// The flags should be available. The issue might be that the code using them
// in GuiOverlay.cpp is not guarded by checks for `io.ConfigFlags`.

// Let's ensure the code in GuiOverlay.cpp that uses these features is guarded.
// The errors are compile-time "undeclared identifier", which means the macros themselves
// are missing from the imgui.h that the compiler is seeing.

// The most direct way to enable these features if they are part of the ImGui source you have
// is to ensure your `imconfig.h` (in `external/imgui`) has:
// (These are usually enabled by io.ConfigFlags at runtime, but defining them here
// ensures the conditional compilation blocks in ImGui are active)
// #define IMGUI_HAS_DOCK // This is usually defined internally by ImGui if docking sources are compiled
// #define IMGUI_HAS_VIEWPORT // Same for viewports

// It's more likely that the issue is that your external/imgui is not actually the docking branch,
// or the docking branch you have is very old.
// The `ImGuiConfigFlags_DockingEnable` flag itself should be defined in `imgui.h` if you are using a
// version of ImGui that supports docking (like the docking branch).

// If you are certain you are on the docking branch, then the problem is very strange.
// One thing to try is to define IMGUI_API for static linking if it's not already.
// #define IMGUI_API // For static linking