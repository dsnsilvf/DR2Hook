#pragma once

#include "common.h"
#include <dxgi.h>
#include <functional>

namespace dr2hook {

using PFN_D3D11Present = HRESULT(WINAPI *)(IDXGISwapChain *pSwapChain,
                                           UINT SyncInterval, UINT Flags);
using TickCallback = std::function<void(double deltaTime)>;
using KeyCallback = std::function<void(UINT vkCode, bool isDown)>;

bool InitializeHooks();
void ShutdownHooks();
void RegisterTickCallback(TickCallback cb);
void RegisterKeyCallback(KeyCallback cb);

} // namespace dr2hook

using dr2hook::InitializeHooks;
using dr2hook::KeyCallback;
using dr2hook::PFN_D3D11Present;
using dr2hook::RegisterKeyCallback;
using dr2hook::RegisterTickCallback;
using dr2hook::ShutdownHooks;
using dr2hook::TickCallback;
