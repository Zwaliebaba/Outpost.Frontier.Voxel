// The one header that owns the Windows macro family (AGENTS.md §4).
//
// Include this before any Windows, D3D12, DXGI or WIC header. No project file and no other header
// defines any of these macros: /D spells a bare macro as `1` where a #define spells it as nothing,
// so a second owner is a guaranteed C4005, which /WX makes fatal.
#pragma once

#ifndef NOMINMAX
#   define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#   define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMCX
#   define NOMCX
#endif
#ifndef NOSERVICE
#   define NOSERVICE
#endif
#ifndef NOHELP
#   define NOHELP
#endif

#include <windows.h>
