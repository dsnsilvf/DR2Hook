#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#define DR2HOOK_API extern "C" __declspec(dllexport)

namespace dr2hook {

inline constexpr const char *DR2HOOK_VERSION = "0.1.0";

} // namespace dr2hook

using dr2hook::DR2HOOK_VERSION;
