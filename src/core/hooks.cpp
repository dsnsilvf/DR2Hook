#include "dr2hook/hooks.h"
#include "dr2hook/common.h"
#include "dr2hook/logger.h"
#include "dr2hook/ui/overlay.h"

#include <MinHook.h>
#include <chrono>
#include <d3d11.h>
#include <mutex>
#include <vector>

namespace {

dr2hook::PFN_D3D11Present g_originalPresent = nullptr;
WNDPROC g_originalWndProc = nullptr;
HWND g_gameHwnd = nullptr;
bool g_wndProcHooked = false;

std::mutex g_callbackMutex;
std::vector<dr2hook::TickCallback> g_tickCallbacks;
std::vector<dr2hook::KeyCallback> g_keyCallbacks;

std::chrono::steady_clock::time_point g_lastTickTime;
bool g_firstFrame = true;

LRESULT CALLBACK DetourWndProc(HWND hWnd, UINT uMsg, WPARAM wParam,
                               LPARAM lParam) {
  if ((uMsg == WM_KEYDOWN || uMsg == WM_SYSKEYDOWN) &&
      (wParam == VK_INSERT || wParam == 0x2D)) {
    dr2hook::OverlayManager::ToggleMenu();
    return 0;
  }

  if (dr2hook::OverlayManager::HandleWndProc(hWnd, uMsg, wParam, lParam)) {
    return 0;
  }

  if (uMsg == WM_KEYDOWN || uMsg == WM_KEYUP || uMsg == WM_SYSKEYDOWN ||
      uMsg == WM_SYSKEYUP) {
    const UINT vkCode = static_cast<UINT>(wParam);
    const bool isDown = (uMsg == WM_KEYDOWN || uMsg == WM_SYSKEYDOWN);

    std::vector<dr2hook::KeyCallback> keyCallbacks;
    {
      std::lock_guard<std::mutex> lock(g_callbackMutex);
      keyCallbacks = g_keyCallbacks;
    }
    for (const auto &cb : keyCallbacks) {
      if (cb) {
        cb(vkCode, isDown);
      }
    }
  }

  if (g_originalWndProc != nullptr) {
    return CallWindowProcA(g_originalWndProc, hWnd, uMsg, wParam, lParam);
  }
  return DefWindowProcA(hWnd, uMsg, wParam, lParam);
}

HRESULT WINAPI DetourPresent(IDXGISwapChain *pSwapChain, UINT SyncInterval,
                             UINT Flags) {
  if (!g_wndProcHooked && pSwapChain != nullptr) {
    DXGI_SWAP_CHAIN_DESC desc{};
    if (SUCCEEDED(pSwapChain->GetDesc(&desc)) && desc.OutputWindow != nullptr) {
      g_gameHwnd = desc.OutputWindow;
      g_originalWndProc = reinterpret_cast<WNDPROC>(
          GetWindowLongPtrA(g_gameHwnd, GWLP_WNDPROC));
      LONG_PTR prev = SetWindowLongPtrA(
          g_gameHwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(DetourWndProc));
      if (prev != 0) {
        g_originalWndProc = reinterpret_cast<WNDPROC>(prev);
      }
      if (g_originalWndProc != nullptr) {
        g_wndProcHooked = true;
        dr2hook::Logger::Info("WndProc do jogo interceptado com sucesso.");
      } else {
        dr2hook::Logger::Warn(
            "Falha ao interceptar WndProc do jogo via SetWindowLongPtrA.");
      }
    }
  }

  if (!dr2hook::OverlayManager::IsInitialized() && pSwapChain != nullptr) {
    ID3D11Device *pDevice = nullptr;
    HRESULT hr = pSwapChain->GetDevice(__uuidof(ID3D11Device),
                                       reinterpret_cast<void **>(&pDevice));
    if (SUCCEEDED(hr) && pDevice != nullptr) {
      ID3D11DeviceContext *pContext = nullptr;
      pDevice->GetImmediateContext(&pContext);
      if (pContext != nullptr) {
        dr2hook::OverlayManager::Initialize(g_gameHwnd, pDevice, pContext);
        pContext->Release();
      }
      pDevice->Release();
    }
  }

  double deltaTime = 0.0;
  const auto now = std::chrono::steady_clock::now();
  if (g_firstFrame) {
    g_firstFrame = false;
    g_lastTickTime = now;
  } else {
    const std::chrono::duration<double> elapsed = now - g_lastTickTime;
    deltaTime = elapsed.count();
    g_lastTickTime = now;
  }

  std::vector<dr2hook::TickCallback> tickCallbacks;
  {
    std::lock_guard<std::mutex> lock(g_callbackMutex);
    tickCallbacks = g_tickCallbacks;
  }
  for (const auto &cb : tickCallbacks) {
    if (cb) {
      cb(deltaTime);
    }
  }

  if (dr2hook::OverlayManager::IsInitialized()) {
    dr2hook::OverlayManager::Render(pSwapChain);
  }

  return g_originalPresent(pSwapChain, SyncInterval, Flags);
}

} // namespace

namespace dr2hook {

bool InitializeHooks() {
  MH_STATUS mhStatus = MH_Initialize();
  if (mhStatus != MH_OK && mhStatus != MH_ERROR_ALREADY_INITIALIZED) {
    Logger::Error("Falha ao inicializar MinHook.");
    return false;
  }

  HMODULE hD3D11 = LoadLibraryA("d3d11.dll");
  if (!hD3D11) {
    Logger::Error("Falha ao carregar d3d11.dll dinamicamente.");
    return false;
  }

  auto pfnD3D11CreateDeviceAndSwapChain =
      reinterpret_cast<PFN_D3D11_CREATE_DEVICE_AND_SWAP_CHAIN>(
          GetProcAddress(hD3D11, "D3D11CreateDeviceAndSwapChain"));
  if (!pfnD3D11CreateDeviceAndSwapChain) {
    Logger::Error("Falha ao obter endereco de D3D11CreateDeviceAndSwapChain.");
    FreeLibrary(hD3D11);
    return false;
  }

  WNDCLASSA wc{};
  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = DefWindowProcA;
  wc.hInstance = GetModuleHandleA(nullptr);
  wc.lpszClassName = "DR2HookDummyClass";

  if (!RegisterClassA(&wc)) {
    Logger::Error("Falha ao registrar classe de janela dummy.");
    FreeLibrary(hD3D11);
    return false;
  }

  HWND hWndDummy = CreateWindowExA(0, wc.lpszClassName, "DR2HookDummyWindow",
                                   WS_OVERLAPPEDWINDOW, 0, 0, 100, 100, nullptr,
                                   nullptr, wc.hInstance, nullptr);
  if (!hWndDummy) {
    Logger::Error("Falha ao criar janela dummy.");
    UnregisterClassA(wc.lpszClassName, wc.hInstance);
    FreeLibrary(hD3D11);
    return false;
  }

  DXGI_SWAP_CHAIN_DESC scd{};
  scd.BufferCount = 1;
  scd.BufferDesc.Width = 100;
  scd.BufferDesc.Height = 100;
  scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  scd.BufferDesc.RefreshRate.Numerator = 60;
  scd.BufferDesc.RefreshRate.Denominator = 1;
  scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  scd.OutputWindow = hWndDummy;
  scd.SampleDesc.Count = 1;
  scd.SampleDesc.Quality = 0;
  scd.Windowed = TRUE;
  scd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

  const D3D_FEATURE_LEVEL featureLevels[] = {D3D_FEATURE_LEVEL_11_0};
  D3D_FEATURE_LEVEL featureLevel{};
  IDXGISwapChain *pDummySwapChain = nullptr;
  ID3D11Device *pDummyDevice = nullptr;
  ID3D11DeviceContext *pDummyContext = nullptr;

  HRESULT hr = pfnD3D11CreateDeviceAndSwapChain(
      nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, featureLevels, 1,
      D3D11_SDK_VERSION, &scd, &pDummySwapChain, &pDummyDevice, &featureLevel,
      &pDummyContext);

  if (FAILED(hr) || !pDummySwapChain) {
    Logger::Error("Falha ao criar Dummy Device e SwapChain com "
                  "D3D_DRIVER_TYPE_HARDWARE.");
    DestroyWindow(hWndDummy);
    UnregisterClassA(wc.lpszClassName, wc.hInstance);
    FreeLibrary(hD3D11);
    return false;
  }

  void **vTable = *reinterpret_cast<void ***>(pDummySwapChain);
  void *pPresentTarget = vTable[8];

  // Destruição segura e imediata dos recursos COM dummy e da janela
  pDummySwapChain->Release();
  pDummySwapChain = nullptr;
  if (pDummyContext != nullptr) {
    pDummyContext->Release();
    pDummyContext = nullptr;
  }
  if (pDummyDevice != nullptr) {
    pDummyDevice->Release();
    pDummyDevice = nullptr;
  }
  DestroyWindow(hWndDummy);
  UnregisterClassA(wc.lpszClassName, wc.hInstance);
  FreeLibrary(hD3D11);

  if (!pPresentTarget) {
    Logger::Error("Endereco de Present obtido da VTable e nulo.");
    return false;
  }

  mhStatus =
      MH_CreateHook(pPresentTarget, reinterpret_cast<void *>(&DetourPresent),
                    reinterpret_cast<void **>(&g_originalPresent));
  if (mhStatus != MH_OK) {
    Logger::Error("Falha ao criar hook de Present no MinHook.");
    return false;
  }

  mhStatus = MH_EnableHook(pPresentTarget);
  if (mhStatus != MH_OK) {
    Logger::Error("Falha ao habilitar hook de Present no MinHook.");
    return false;
  }

  Logger::Info("Hook de Present instalado e habilitado com sucesso.");
  return true;
}

void ShutdownHooks() {
  dr2hook::OverlayManager::Shutdown();

  if (g_gameHwnd != nullptr && g_originalWndProc != nullptr) {
    SetWindowLongPtrA(g_gameHwnd, GWLP_WNDPROC,
                      reinterpret_cast<LONG_PTR>(g_originalWndProc));
    g_originalWndProc = nullptr;
    g_gameHwnd = nullptr;
    g_wndProcHooked = false;
  }

  MH_DisableHook(MH_ALL_HOOKS);
  MH_Uninitialize();

  {
    std::lock_guard<std::mutex> lock(g_callbackMutex);
    g_tickCallbacks.clear();
    g_keyCallbacks.clear();
  }

  g_firstFrame = true;

  Logger::Info("Hooks do DR2Hook finalizados com sucesso.");
}

void RegisterTickCallback(TickCallback cb) {
  std::lock_guard<std::mutex> lock(g_callbackMutex);
  g_tickCallbacks.push_back(std::move(cb));
}

void RegisterKeyCallback(KeyCallback cb) {
  std::lock_guard<std::mutex> lock(g_callbackMutex);
  g_keyCallbacks.push_back(std::move(cb));
}

} // namespace dr2hook
