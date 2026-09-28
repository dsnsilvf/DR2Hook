#include "dr2hook/common.h"
#include "dr2hook/hooks.h"
#include "dr2hook/logger.h"
#include "dr2hook/proxy.h"
#include "dr2hook/script/mod_manager.h"

DWORD WINAPI DR2Hook_InitThread(LPVOID lpParam) {
  Logger::Init("dr2hook.log");
  Logger::Info("DR2Hook Core v0.1.0 inicializando...");

  dr2hook::EnsureProxyInitialized();
  if (dr2hook::GetOriginalProc("CreateDXGIFactory") != nullptr) {
    Logger::Info("DR2Hook Core inicializado com sucesso.");
    if (dr2hook::InitializeHooks()) {
      Logger::Info("Hooks principais inicializados com sucesso.");

      dr2hook::RegisterTickCallback(
          [](double dt) { dr2hook::ModManager::DispatchTick(dt); });

      dr2hook::RegisterKeyCallback([](UINT vkCode, bool isDown) {
        if (isDown) {
          dr2hook::ModManager::DispatchKeyDown(vkCode);
        }
      });

      if (dr2hook::ModManager::Initialize()) {
        Logger::Info("ModManager inicializado com sucesso.");
      } else {
        Logger::Error("Falha ao inicializar ModManager.");
      }
    } else {
      Logger::Error("Falha ao inicializar hooks principais.");
    }
  } else {
    Logger::Error("DR2Hook Core falhou ao inicializar o proxy DXGI.");
  }

  return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call,
                      LPVOID lpReserved) {
  switch (ul_reason_for_call) {
  case DLL_PROCESS_ATTACH: {
    DisableThreadLibraryCalls(hModule);
    HANDLE hThread =
        CreateThread(nullptr, 0, DR2Hook_InitThread, hModule, 0, nullptr);
    if (hThread != nullptr) {
      CloseHandle(hThread);
    }
    return TRUE;
  }

  case DLL_PROCESS_DETACH:
    dr2hook::ModManager::Shutdown();
    dr2hook::ShutdownHooks();
    ShutdownProxy();
    Logger::Shutdown();
    return TRUE;

  case DLL_THREAD_ATTACH:
  case DLL_THREAD_DETACH:
    break;
  }
  return TRUE;
}
