#include "dr2hook/common.h"
#include "dr2hook/hooks.h"
#include "dr2hook/host.h"
#include "dr2hook/logger.h"
#include "dr2hook/pause_menu.h"
#include "dr2hook/proxy.h"
#include "dr2hook/ui_data.h"

DWORD WINAPI DR2Hook_InitThread(LPVOID lpParam) {
  dr2hook::SetHostModule(static_cast<HMODULE>(lpParam));
  dr2hook::Logger::Init("dr2hook.log");
  dr2hook::StartUiDataLog();
  dr2hook::EnsureProxyInitialized();

  if (dr2hook::GetOriginalProc("CreateDXGIFactory") != nullptr) {
    if (!dr2hook::LoadCore()) {
      dr2hook::HostLog("DR2Hook Core falhou ao carregar dr2hook_core.dll.");
    }
    if (dr2hook::InitializeHooks()) {
      dr2hook::HostLog("Hooks principais inicializados com sucesso.");
      dr2hook::InstallPauseMenuHooks();
    } else {
      dr2hook::HostLog("Falha ao inicializar hooks principais.");
    }
  } else {
    dr2hook::HostLog("DR2Hook falhou ao inicializar o proxy DXGI.");
  }

  return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call,
                      LPVOID lpReserved) {
  switch (ul_reason_for_call) {
  case DLL_PROCESS_ATTACH: {
    DisableThreadLibraryCalls(hModule);
    dr2hook::InstallUiDataHook(hModule);
    HANDLE hThread =
        CreateThread(nullptr, 0, DR2Hook_InitThread, hModule, 0, nullptr);
    if (hThread != nullptr) {
      CloseHandle(hThread);
    }
    return TRUE;
  }

  case DLL_PROCESS_DETACH:
    dr2hook::UnloadCore(lpReserved == nullptr);
    dr2hook::ShutdownHooks();
    ShutdownProxy();
    dr2hook::Logger::Shutdown();
    return TRUE;

  case DLL_THREAD_ATTACH:
  case DLL_THREAD_DETACH:
    break;
  }
  return TRUE;
}
