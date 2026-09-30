#include "dr2hook/host.h"
#include "dr2hook/logger.h"

#include <atomic>
#include <cstring>
#include <mutex>
#include <string>

namespace dr2hook {
namespace {

HMODULE g_module = nullptr;
HMODULE g_core = nullptr;
Dr2CoreApi *g_api = nullptr;
std::recursive_mutex g_coreMutex;
std::atomic<bool> g_reloadRequested{false};
bool g_everLoaded = false;
unsigned int g_generation = 0;
wchar_t g_loadedPath[MAX_PATH] = {};

void FallbackLog(const char *message) {
  Logger::Error(message != nullptr ? message : "");
}

bool DirectoryOfModule(HMODULE module, wchar_t *dir, size_t cap) {
  wchar_t path[MAX_PATH] = {};
  const DWORD length = GetModuleFileNameW(module, path, MAX_PATH);
  if (length == 0 || length >= MAX_PATH) {
    return false;
  }

  wchar_t *slash = wcsrchr(path, L'\\');
  if (slash == nullptr) {
    slash = wcsrchr(path, L'/');
  }
  if (slash == nullptr) {
    return false;
  }
  *slash = L'\0';
  if (wcslen(path) + 1 > cap) {
    return false;
  }
  wcscpy(dir, path);
  return true;
}

bool JoinPath(wchar_t *out, size_t cap, const wchar_t *dir, const wchar_t *leaf) {
  if (out == nullptr || dir == nullptr || leaf == nullptr || cap == 0) {
    return false;
  }
  const size_t dirLen = wcslen(dir);
  const size_t leafLen = wcslen(leaf);
  const bool needSlash =
      dirLen > 0 && dir[dirLen - 1] != L'\\' && dir[dirLen - 1] != L'/';
  if (dirLen + (needSlash ? 1 : 0) + leafLen + 1 > cap) {
    return false;
  }
  wcscpy(out, dir);
  if (needSlash) {
    out[dirLen] = L'\\';
    out[dirLen + 1] = L'\0';
  }
  wcscat(out, leaf);
  return true;
}

void FormatCoreCopyName(wchar_t *leaf, size_t cap, unsigned int generation) {
  if (leaf == nullptr || cap < 32) {
    if (leaf != nullptr && cap > 0) {
      leaf[0] = L'\0';
    }
    return;
  }

  wchar_t digits[16] = {};
  wchar_t *cursor = digits + 15;
  *cursor = L'\0';
  unsigned int value = generation;
  do {
    *--cursor = static_cast<wchar_t>(L'0' + (value % 10u));
    value /= 10u;
  } while (value != 0 && cursor > digits);

  wcscpy(leaf, L"dr2hook_core.");
  wcscat(leaf, cursor);
  wcscat(leaf, L".dll");
}

void LogWide(const char *prefix, const wchar_t *path) {
  char narrow[MAX_PATH * 3] = {};
  if (path != nullptr) {
    WideCharToMultiByte(CP_UTF8, 0, path, -1, narrow,
                        static_cast<int>(sizeof(narrow)), nullptr, nullptr);
  }
  Logger::Error(std::string(prefix != nullptr ? prefix : "") + narrow);
}

void DeleteStaleCopies(const wchar_t *dir) {
  wchar_t pattern[MAX_PATH] = {};
  if (!JoinPath(pattern, MAX_PATH, dir, L"dr2hook_core.*.dll")) {
    return;
  }

  WIN32_FIND_DATAW data{};
  const HANDLE find = FindFirstFileW(pattern, &data);
  if (find == INVALID_HANDLE_VALUE) {
    return;
  }

  do {
    if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
      continue;
    }
    wchar_t full[MAX_PATH] = {};
    if (!JoinPath(full, MAX_PATH, dir, data.cFileName)) {
      continue;
    }
    DeleteFileW(full);
  } while (FindNextFileW(find, &data));

  FindClose(find);
}

void UnloadCoreUnlocked(bool freeLibrary) {
  Dr2CoreApi *api = g_api;
  g_api = nullptr;
  if (api != nullptr && api->Shutdown != nullptr) {
    api->Shutdown();
  }

  if (!freeLibrary || g_core == nullptr) {
    return;
  }

  FreeLibrary(g_core);
  g_core = nullptr;
  if (g_loadedPath[0] != L'\0') {
    DeleteFileW(g_loadedPath);
    g_loadedPath[0] = L'\0';
  }
}

bool LoadCoreUnlocked() {
  if (g_module == nullptr) {
    FallbackLog("Modulo dxgi.dll desconhecido; core nao carregado.");
    return false;
  }

  wchar_t dir[MAX_PATH] = {};
  if (!DirectoryOfModule(g_module, dir, MAX_PATH)) {
    FallbackLog("Nao foi possivel localizar a pasta da dxgi.dll.");
    return false;
  }

  if (g_core == nullptr) {
    DeleteStaleCopies(dir);
  }

  wchar_t source[MAX_PATH] = {};
  if (!JoinPath(source, MAX_PATH, dir, L"dr2hook_core.dll")) {
    return false;
  }
  if (GetFileAttributesW(source) == INVALID_FILE_ATTRIBUTES) {
    wchar_t exeDir[MAX_PATH] = {};
    if (DirectoryOfModule(nullptr, exeDir, MAX_PATH) &&
        JoinPath(source, MAX_PATH, exeDir, L"dr2hook_core.dll") &&
        GetFileAttributesW(source) != INVALID_FILE_ATTRIBUTES) {
      wcscpy(dir, exeDir);
    } else {
      LogWide("dr2hook_core.dll nao encontrada em: ", source);
      return false;
    }
  }

  const unsigned int generation = ++g_generation;
  wchar_t leaf[64] = {};
  FormatCoreCopyName(leaf, 64, generation);
  wchar_t dest[MAX_PATH] = {};
  if (leaf[0] == L'\0' || !JoinPath(dest, MAX_PATH, dir, leaf)) {
    return false;
  }
  if (!CopyFileW(source, dest, FALSE)) {
    FallbackLog("Falha ao copiar dr2hook_core.dll para a imagem recarregavel.");
    return false;
  }

  UnloadCoreUnlocked(true);

  HMODULE loaded = LoadLibraryW(dest);
  if (loaded == nullptr) {
    DeleteFileW(dest);
    FallbackLog("Falha ao carregar a copia de dr2hook_core.dll.");
    return false;
  }

  using GetApiFn = Dr2CoreApi *(*)();
  auto getApi = reinterpret_cast<GetApiFn>(GetProcAddress(loaded, "Dr2Core_GetApi"));
  if (getApi == nullptr) {
    FreeLibrary(loaded);
    DeleteFileW(dest);
    FallbackLog("dr2hook_core.dll nao exporta Dr2Core_GetApi.");
    return false;
  }

  Dr2CoreApi *api = getApi();
  if (api == nullptr || api->abiVersion != kCoreAbiVersion ||
      api->Initialize == nullptr || api->Shutdown == nullptr ||
      api->OnFrame == nullptr || api->OnWndProc == nullptr) {
    FreeLibrary(loaded);
    DeleteFileW(dest);
    FallbackLog("ABI de dr2hook_core.dll incompativel com esta dxgi.dll.");
    return false;
  }

  const int truncateLog = g_everLoaded ? 0 : 1;
  g_core = loaded;
  g_api = api;
  if (api->Initialize(truncateLog) == 0) {
    g_api = nullptr;
    g_core = nullptr;
    FreeLibrary(loaded);
    DeleteFileW(dest);
    FallbackLog("Initialize de dr2hook_core.dll falhou.");
    return false;
  }

  g_everLoaded = true;
  wcscpy(g_loadedPath, dest);
  Logger::Info(truncateLog != 0 ? "dr2hook_core.dll carregado."
                                : "dr2hook_core.dll recarregado.");
  return true;
}

} // namespace

void SetHostModule(HMODULE module) { g_module = module; }

bool LoadCore() {
  std::lock_guard<std::recursive_mutex> lock(g_coreMutex);
  if (g_core != nullptr) {
    return true;
  }
  return LoadCoreUnlocked();
}

void UnloadCore(bool freeLibrary) {
  std::lock_guard<std::recursive_mutex> lock(g_coreMutex);
  UnloadCoreUnlocked(freeLibrary);
}

void RequestCoreReload() { g_reloadRequested.store(true); }

void ReloadCoreIfRequested() {
  if (!g_reloadRequested.exchange(false)) {
    return;
  }

  std::lock_guard<std::recursive_mutex> lock(g_coreMutex);
  LoadCoreUnlocked();
}

void HostOnFrame(IDXGISwapChain *swapChain, HWND hwnd, double deltaTime) {
  std::lock_guard<std::recursive_mutex> lock(g_coreMutex);
  if (g_api != nullptr && g_api->OnFrame != nullptr) {
    g_api->OnFrame(swapChain, hwnd, deltaTime);
  }
}

int HostOnWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  std::lock_guard<std::recursive_mutex> lock(g_coreMutex);
  if (g_api == nullptr || g_api->OnWndProc == nullptr) {
    return 0;
  }
  return g_api->OnWndProc(hwnd, msg, wParam, lParam);
}

void HostLog(const char *message) {
  Logger::Info(message != nullptr ? message : "");
}

} // namespace dr2hook

void Dr2Host_RequestReload() { dr2hook::RequestCoreReload(); }

void Dr2Host_Log(int level, const char *message) {
  const std::string_view text = message != nullptr ? message : "";
  switch (level) {
  case 1:
    dr2hook::Logger::Warn(text);
    break;
  case 2:
    dr2hook::Logger::Error(text);
    break;
  case 3:
    dr2hook::Logger::Debug(text);
    break;
  default:
    dr2hook::Logger::Info(text);
    break;
  }
}
