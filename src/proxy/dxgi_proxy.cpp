#include "dr2hook/logger.h"
#include "dr2hook/proxy.h"

#include <mutex>
#include <string>

namespace {
HMODULE g_originalDxgiModule = nullptr;
static std::once_flag g_proxyInitOnce;

PFN_CreateDXGIFactory g_pfnCreateDXGIFactory = nullptr;
PFN_CreateDXGIFactory1 g_pfnCreateDXGIFactory1 = nullptr;
PFN_CreateDXGIFactory2 g_pfnCreateDXGIFactory2 = nullptr;
PFN_DXGIGetDebugInterface1 g_pfnDXGIGetDebugInterface1 = nullptr;
PFN_DXGIDeclareAdapterRemovalSupport g_pfnDXGIDeclareAdapterRemovalSupport =
    nullptr;
} // namespace

namespace dr2hook {

FARPROC GetOriginalProc(const char *procName) {
  if (!g_originalDxgiModule) {
    return nullptr;
  }
  return GetProcAddress(g_originalDxgiModule, procName);
}

void EnsureProxyInitialized() {
  std::call_once(g_proxyInitOnce, []() { InitializeProxy(); });
}

bool InitializeProxy() {
  char sysDir[MAX_PATH];
  const UINT len = GetSystemDirectoryA(sysDir, MAX_PATH);
  if (len == 0 || len >= MAX_PATH) {
    Logger::Error("Failed to retrieve system directory path.");
    return false;
  }

  HMODULE ourModule = nullptr;
  GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                         GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                     reinterpret_cast<LPCSTR>(&InitializeProxy), &ourModule);

  const std::string sysPath = std::string(sysDir) + "\\dxgi.dll";
  g_originalDxgiModule =
      LoadLibraryExA(sysPath.c_str(), NULL, LOAD_WITH_ALTERED_SEARCH_PATH);

  if (!g_originalDxgiModule) {
    Logger::Error("Failed to load original dxgi.dll from: " + sysPath);
    return false;
  }

  if (g_originalDxgiModule == ourModule) {
    Logger::Error(
        "Original dxgi.dll matches our module (self-reference)! Aborting.");
    g_originalDxgiModule = nullptr;
    return false;
  }

  Logger::Info("Original dxgi.dll loaded successfully from: " + sysPath);

  g_pfnCreateDXGIFactory = reinterpret_cast<PFN_CreateDXGIFactory>(
      GetOriginalProc("CreateDXGIFactory"));
  g_pfnCreateDXGIFactory1 = reinterpret_cast<PFN_CreateDXGIFactory1>(
      GetOriginalProc("CreateDXGIFactory1"));
  g_pfnCreateDXGIFactory2 = reinterpret_cast<PFN_CreateDXGIFactory2>(
      GetOriginalProc("CreateDXGIFactory2"));
  g_pfnDXGIGetDebugInterface1 = reinterpret_cast<PFN_DXGIGetDebugInterface1>(
      GetOriginalProc("DXGIGetDebugInterface1"));
  g_pfnDXGIDeclareAdapterRemovalSupport =
      reinterpret_cast<PFN_DXGIDeclareAdapterRemovalSupport>(
          GetOriginalProc("DXGIDeclareAdapterRemovalSupport"));

  if (!g_pfnCreateDXGIFactory) {
    Logger::Warn("Failed to resolve CreateDXGIFactory from original dxgi.dll");
  }
  if (!g_pfnCreateDXGIFactory1) {
    Logger::Warn("Failed to resolve CreateDXGIFactory1 from original dxgi.dll");
  }
  if (!g_pfnCreateDXGIFactory2) {
    Logger::Warn("Failed to resolve CreateDXGIFactory2 from original dxgi.dll");
  }
  if (!g_pfnDXGIGetDebugInterface1) {
    Logger::Warn(
        "Failed to resolve DXGIGetDebugInterface1 from original dxgi.dll");
  }
  if (!g_pfnDXGIDeclareAdapterRemovalSupport) {
    Logger::Warn("Failed to resolve DXGIDeclareAdapterRemovalSupport from "
                 "original dxgi.dll");
  }

  Logger::Info("DXGI proxy function pointers resolved.");
  return true;
}

void ShutdownProxy() {
  if (g_originalDxgiModule) {
    FreeLibrary(g_originalDxgiModule);
    g_originalDxgiModule = nullptr;
  }
  g_pfnCreateDXGIFactory = nullptr;
  g_pfnCreateDXGIFactory1 = nullptr;
  g_pfnCreateDXGIFactory2 = nullptr;
  g_pfnDXGIGetDebugInterface1 = nullptr;
  g_pfnDXGIDeclareAdapterRemovalSupport = nullptr;
  Logger::Info("DXGI proxy shutdown complete.");
}

} // namespace dr2hook

DR2HOOK_API HRESULT WINAPI CreateDXGIFactory(REFIID riid, void **ppFactory) {
  dr2hook::EnsureProxyInitialized();
  if (!g_pfnCreateDXGIFactory) {
    dr2hook::Logger::Error(
        "CreateDXGIFactory called but original function pointer is null!");
    return DXGI_ERROR_UNSUPPORTED;
  }
  dr2hook::Logger::Debug(
      "CreateDXGIFactory forwarding call to original dxgi.dll");
  return g_pfnCreateDXGIFactory(riid, ppFactory);
}

DR2HOOK_API HRESULT WINAPI CreateDXGIFactory1(REFIID riid, void **ppFactory) {
  dr2hook::EnsureProxyInitialized();
  if (!g_pfnCreateDXGIFactory1) {
    dr2hook::Logger::Error(
        "CreateDXGIFactory1 called but original function pointer is null!");
    return DXGI_ERROR_UNSUPPORTED;
  }
  dr2hook::Logger::Debug(
      "CreateDXGIFactory1 forwarding call to original dxgi.dll");
  return g_pfnCreateDXGIFactory1(riid, ppFactory);
}

DR2HOOK_API HRESULT WINAPI CreateDXGIFactory2(UINT Flags, REFIID riid,
                                              void **ppFactory) {
  dr2hook::EnsureProxyInitialized();
  if (!g_pfnCreateDXGIFactory2) {
    dr2hook::Logger::Error(
        "CreateDXGIFactory2 called but original function pointer is null!");
    return DXGI_ERROR_UNSUPPORTED;
  }
  dr2hook::Logger::Debug(
      "CreateDXGIFactory2 forwarding call to original dxgi.dll");
  return g_pfnCreateDXGIFactory2(Flags, riid, ppFactory);
}

DR2HOOK_API HRESULT WINAPI DXGIGetDebugInterface1(UINT Flags, REFIID riid,
                                                  void **pDebug) {
  dr2hook::EnsureProxyInitialized();
  if (!g_pfnDXGIGetDebugInterface1) {
    dr2hook::Logger::Error("DXGIGetDebugInterface1 called but original "
                           "function pointer is null!");
    return DXGI_ERROR_UNSUPPORTED;
  }
  dr2hook::Logger::Debug(
      "DXGIGetDebugInterface1 forwarding call to original dxgi.dll");
  return g_pfnDXGIGetDebugInterface1(Flags, riid, pDebug);
}

DR2HOOK_API HRESULT WINAPI DXGIDeclareAdapterRemovalSupport() {
  dr2hook::EnsureProxyInitialized();
  if (!g_pfnDXGIDeclareAdapterRemovalSupport) {
    dr2hook::Logger::Error("DXGIDeclareAdapterRemovalSupport called but "
                           "original function pointer is null!");
    return DXGI_ERROR_UNSUPPORTED;
  }
  dr2hook::Logger::Debug("DXGIDeclareAdapterRemovalSupport forwarding call to "
                         "original dxgi.dll");
  return g_pfnDXGIDeclareAdapterRemovalSupport();
}
