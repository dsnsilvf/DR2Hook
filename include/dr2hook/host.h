#pragma once

#include "core_api.h"

namespace dr2hook {

void SetHostModule(HMODULE module);
bool LoadCore();
void UnloadCore(bool freeLibrary);
void RequestCoreReload();
void ReloadCoreIfRequested();
void HostOnFrame(IDXGISwapChain *swapChain, HWND hwnd, double deltaTime);
int HostOnWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
void HostLog(const char *message);

} // namespace dr2hook

DR2HOOK_API void Dr2Host_RequestReload();
DR2HOOK_API void Dr2Host_Log(int level, const char *message);
DR2HOOK_API int Dr2Host_LogRead(unsigned long long *seq, char *out, int cap);
