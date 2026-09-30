#include "dr2hook/core_api.h"
#include "dr2hook/logger.h"
#include "dr2hook/memory.h"
#include "dr2hook/player.h"
#include "dr2hook/safety.h"
#include "dr2hook/savestate.h"
#include "dr2hook/script/mod_manager.h"
#include "dr2hook/ui/overlay.h"

#include <d3d11.h>

namespace {

dr2hook::DirectMemoryAccessor g_directAccessor;
dr2hook::MemoryScanner g_memoryScanner(&g_directAccessor);
bool g_notifyReload = false;

int Core_Initialize(int truncateLog) {
  try {
    dr2hook::Logger::Init("dr2hook.log", truncateLog != 0);
    dr2hook::Logger::Info(truncateLog != 0
                              ? "DR2Hook Core v0.1.0 inicializando..."
                              : "DR2Hook Core recarregado em runtime.");

    dr2hook::SavestateManager::Initialize();
    dr2hook::SafetyGuard::Configure(&g_memoryScanner, 0);
    dr2hook::SafetyGuard::SetPermissiveMode(true);
    dr2hook::Player::Configure(&g_memoryScanner, 0);

    if (dr2hook::ModManager::Initialize()) {
      dr2hook::Logger::Info("ModManager inicializado com sucesso.");
    } else {
      dr2hook::Logger::Error("Falha ao inicializar ModManager.");
    }

    g_notifyReload = truncateLog == 0;
    return 1;
  } catch (...) {
    return 0;
  }
}

void Core_Shutdown() {
  try {
    dr2hook::OverlayManager::Shutdown();
    dr2hook::SavestateManager::Shutdown();
    dr2hook::ModManager::Shutdown();
    dr2hook::Logger::Info("DR2Hook Core descarregado.");
    dr2hook::Logger::Shutdown();
  } catch (...) {
    dr2hook::Logger::Shutdown();
  }
}

void Core_OnFrame(IDXGISwapChain *swapChain, HWND hwnd, double deltaTime) {
  try {
    if (!dr2hook::OverlayManager::IsInitialized() && swapChain != nullptr) {
      ID3D11Device *device = nullptr;
      const HRESULT hr = swapChain->GetDevice(
          __uuidof(ID3D11Device), reinterpret_cast<void **>(&device));
      if (SUCCEEDED(hr) && device != nullptr) {
        ID3D11DeviceContext *context = nullptr;
        device->GetImmediateContext(&context);
        if (context != nullptr) {
          dr2hook::OverlayManager::Initialize(hwnd, device, context);
          context->Release();
        }
        device->Release();
      }
    }

    if (g_notifyReload && dr2hook::OverlayManager::IsInitialized()) {
      dr2hook::OverlayManager::AddNotification(
          "Native core reloaded. In-memory checkpoint was cleared.", 4.0f,
          dr2hook::ToastType::Info);
      g_notifyReload = false;
    }

    if (dr2hook::Player::GetVehicleAddress() == 0) {
      dr2hook::Player::ResolveVehicleAddress(
          reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr)));
    }
    dr2hook::ModManager::DispatchTick(deltaTime);

    if (dr2hook::OverlayManager::IsInitialized()) {
      dr2hook::OverlayManager::Render(swapChain);
    }
  } catch (...) {
    dr2hook::Logger::Error("Excecao em Dr2Core::OnFrame.");
  }
}

int Core_OnWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  try {
    if ((msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) &&
        (wParam == VK_INSERT || wParam == 0x2D)) {
      dr2hook::OverlayManager::ToggleMenu();
      return 1;
    }

    if (dr2hook::OverlayManager::HandleWndProc(hwnd, msg, wParam, lParam)) {
      return 1;
    }

    if (msg == WM_KEYDOWN || msg == WM_KEYUP || msg == WM_SYSKEYDOWN ||
        msg == WM_SYSKEYUP) {
      const UINT vkCode = static_cast<UINT>(wParam);
      const bool isDown = (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN);
      dr2hook::SavestateManager::OnKeyAction(vkCode, isDown);
      if (isDown) {
        dr2hook::ModManager::DispatchKeyDown(vkCode);
      }
    }
    return 0;
  } catch (...) {
    dr2hook::Logger::Error("Excecao em Dr2Core::OnWndProc.");
    return 0;
  }
}

dr2hook::Dr2CoreApi g_api = {
    dr2hook::kCoreAbiVersion, Core_Initialize, Core_Shutdown, Core_OnFrame,
    Core_OnWndProc,
};

} // namespace

DR2HOOK_API dr2hook::Dr2CoreApi *Dr2Core_GetApi() { return &g_api; }

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) {
    DisableThreadLibraryCalls(hModule);
  }
  return TRUE;
}
