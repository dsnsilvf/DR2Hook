#include "dr2hook/load_probe.h"
#include "dr2hook/hooks.h"
#include "dr2hook/logger.h"

#include <MinHook.h>
#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cctype>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#include <d3d11.h>
#include <tlhelp32.h>

namespace dr2hook {
namespace {

struct Config {
  bool enabled = false;
  std::wstring out;            // pasta de saída (aceita Z:\... para cair fora do prefixo)
  int stack = 16;              // quadros de pilha por evento (0 desliga)
  bool gpu = true;             // hooks do ID3D11Device
  bool shaders = true;         // grava o bytecode dos sombreadores
  bool exe = true;             // hooks no carregador de pista do exe (eventos, PSSG, classes)
  int head = 48;               // bytes iniciais dos dados de buffer/textura no log
  double stopAfterStart = 20.0; // segundos depois do "racestart" para fechar o trace
  // Overlay experimental: monta <jogo>\dr2hook_overlay como camada de pasta do I/O do jogo e
  // redireciona esse prefixo para overlay_dir (fora da pasta do jogo). Vazio desliga.
  std::wstring overlayDir;
  std::string overlayMount = "/data"; // ponto de montagem virtual
  int overlayFlag = 1;                // flag da entrada de montagem (a pista montada como pasta usa 1)
  // Modo de PVS forçado em toda cena (campo cena+0x1734: 0 = "No PVS", 1 = "Per Node PVS",
  // 2 = "Per Item PVS", o padrão do construtor 0x140393ab0). -1 não mexe.
  int pvsMode = -1;
};

Config g_cfg;
std::atomic<bool> g_on{false};
FILE *g_out = nullptr;
std::mutex g_writeMutex;
std::string g_pending;
std::wstring g_stamp;
LARGE_INTEGER g_freq{}, g_t0{};
uintptr_t g_exeBase = 0, g_exeEnd = 0;
thread_local bool t_busy = false;

std::mutex g_handleMutex;
std::unordered_map<void *, uint32_t> g_handles; // handle de pacote -> id do arquivo
uint32_t g_nextFileId = 1;

std::mutex g_shaderMutex;
std::unordered_set<std::string> g_dumped;

// Linha do tempo por segundo (thread do Present).
double g_secAccum = 0.0, g_secMax = 0.0;
unsigned g_secFrames = 0;
double g_startAt = -1.0; // ms do "racestart"

struct Busy {
  bool prev;
  Busy() : prev(t_busy) { t_busy = true; }
  ~Busy() { t_busy = prev; }
};

double NowMs() {
  LARGE_INTEGER now;
  QueryPerformanceCounter(&now);
  return static_cast<double>(now.QuadPart - g_t0.QuadPart) * 1000.0 /
         static_cast<double>(g_freq.QuadPart);
}

void Flush() {
  std::lock_guard<std::mutex> lock(g_writeMutex);
  if (g_out == nullptr) return;
  if (!g_pending.empty()) fwrite(g_pending.data(), 1, g_pending.size(), g_out);
  g_pending.clear();
  fflush(g_out);
}

// Uma linha do trace: campos separados por tab, pilha no fim.
struct Line {
  char b[6144];
  size_t n = 0;
  Line(const char *kind) { add("%s\t%.3f\t%lu", kind, NowMs(), GetCurrentThreadId()); }
  void add(const char *fmt, ...) {
    if (n >= sizeof(b) - 2) return;
    va_list ap;
    va_start(ap, fmt);
    const int w = vsnprintf(b + n, sizeof(b) - 2 - n, fmt, ap);
    va_end(ap);
    if (w > 0) n = (std::min)(sizeof(b) - 2, n + static_cast<size_t>(w));
  }
  void hex(const void *data, size_t size) {
    add("\t");
    if (data == nullptr) { add("-"); return; }
    const auto *p = static_cast<const uint8_t *>(data);
    for (size_t i = 0; i < size; ++i) add("%02x", p[i]);
  }
  void stack() {
    add("\t");
    if (g_cfg.stack <= 0) return;
    void *frames[64];
    const USHORT count = RtlCaptureStackBackTrace(
        1, static_cast<DWORD>((std::min)(g_cfg.stack, 62)), frames, nullptr);
    for (USHORT i = 0; i < count; ++i) {
      const auto a = reinterpret_cast<uintptr_t>(frames[i]);
      if (a >= g_exeBase && a < g_exeEnd) {
        add("%se%llx", i ? "|" : "", static_cast<unsigned long long>(a - g_exeBase));
      } else {
        add("%s%llx", i ? "|" : "", static_cast<unsigned long long>(a));
      }
    }
  }
  void emit() {
    b[n++] = '\n';
    std::lock_guard<std::mutex> lock(g_writeMutex);
    if (g_out == nullptr) return;
    g_pending.append(b, n);
    if (g_pending.size() > (1u << 20)) {
      fwrite(g_pending.data(), 1, g_pending.size(), g_out);
      g_pending.clear();
    }
  }
};

std::string Narrow(const wchar_t *w) {
  char buf[1024];
  const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, buf, sizeof(buf), nullptr, nullptr);
  return n > 0 ? std::string(buf) : std::string();
}

std::string Trim(const std::string &s) {
  const size_t a = s.find_first_not_of(" \t\r\n");
  if (a == std::string::npos) return {};
  return s.substr(a, s.find_last_not_of(" \t\r\n") - a + 1);
}

std::wstring GameDir() {
  wchar_t exe[MAX_PATH] = {};
  GetModuleFileNameW(nullptr, exe, MAX_PATH);
  std::wstring dir(exe);
  const size_t slash = dir.find_last_of(L"\\/");
  return slash == std::wstring::npos ? std::wstring() : dir.substr(0, slash + 1);
}

bool ReadConfig() {
  std::ifstream in(std::filesystem::path(GameDir() + L"dr2hook_loadprobe.ini"));
  if (!in.is_open()) return false;
  std::string line;
  while (std::getline(in, line)) {
    const size_t c = line.find_first_of(";#");
    if (c != std::string::npos) line.resize(c);
    const size_t eq = line.find('=');
    if (eq == std::string::npos) continue;
    const std::string key = Trim(line.substr(0, eq)), val = Trim(line.substr(eq + 1));
    if (key == "enabled") g_cfg.enabled = val == "1";
    else if (key == "out") {
      wchar_t w[MAX_PATH] = {};
      MultiByteToWideChar(CP_UTF8, 0, val.c_str(), -1, w, MAX_PATH);
      g_cfg.out = w;
    } else if (key == "stack") g_cfg.stack = std::atoi(val.c_str());
    else if (key == "gpu") g_cfg.gpu = val == "1";
    else if (key == "shaders") g_cfg.shaders = val == "1";
    else if (key == "exe") g_cfg.exe = val == "1";
    else if (key == "head") g_cfg.head = (std::max)(0, (std::min)(512, std::atoi(val.c_str())));
    else if (key == "stop_after_start") g_cfg.stopAfterStart = std::atof(val.c_str());
    else if (key == "overlay_dir") {
      wchar_t w[MAX_PATH] = {};
      MultiByteToWideChar(CP_UTF8, 0, val.c_str(), -1, w, MAX_PATH);
      g_cfg.overlayDir = w;
    } else if (key == "overlay_mount") g_cfg.overlayMount = val;
    else if (key == "overlay_flag") g_cfg.overlayFlag = std::atoi(val.c_str());
    else if (key == "pvs_mode") g_cfg.pvsMode = std::atoi(val.c_str());
  }
  if (!g_cfg.overlayDir.empty() && g_cfg.overlayDir.back() != L'\\' && g_cfg.overlayDir.back() != L'/')
    g_cfg.overlayDir += L'\\';
  if (g_cfg.out.empty()) g_cfg.out = GameDir() + L"dr2hook_loadprobe";
  if (g_cfg.out.back() != L'\\' && g_cfg.out.back() != L'/') g_cfg.out += L'\\';
  return g_cfg.enabled;
}

void MakeDirs(const std::wstring &path) {
  for (size_t i = 3; i < path.size(); ++i) {
    if (path[i] == L'\\' || path[i] == L'/') CreateDirectoryW(path.substr(0, i).c_str(), nullptr);
  }
}

// Base e fim de cada módulo carregado, para traduzir os endereços da pilha fora do exe.
void WriteModules(const char *when) {
  Busy busy;
  const std::wstring path = g_cfg.out + L"modules_" + g_stamp + L".txt";
  FILE *f = _wfopen(path.c_str(), L"a");
  if (f == nullptr) return;
  fprintf(f, "# %s %.3f\n", when, NowMs());
  HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, 0);
  if (snap != INVALID_HANDLE_VALUE) {
    MODULEENTRY32W me{};
    me.dwSize = sizeof(me);
    for (BOOL ok = Module32FirstW(snap, &me); ok; ok = Module32NextW(snap, &me)) {
      fprintf(f, "%llx\t%lx\t%s\n", static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(me.modBaseAddr)),
              me.modBaseSize, Narrow(me.szModule).c_str());
    }
    CloseHandle(snap);
  }
  fclose(f);
}

bool IsPackage(const wchar_t *path) {
  const size_t n = std::wcslen(path);
  auto ends = [&](const wchar_t *ext) {
    const size_t e = std::wcslen(ext);
    if (n < e) return false;
    for (size_t i = 0; i < e; ++i)
      if (std::towlower(path[n - e + i]) != ext[i]) return false;
    return true;
  };
  return ends(L".nefs") || ends(L".dat");
}

// ---- Hooks de arquivo próprios da sonda (sondagens de existência) ----

using GetAttrWFn = DWORD(WINAPI *)(LPCWSTR);
using GetAttrExWFn = BOOL(WINAPI *)(LPCWSTR, GET_FILEEX_INFO_LEVELS, LPVOID);
GetAttrWFn g_origGetAttrW = nullptr;
GetAttrExWFn g_origGetAttrExW = nullptr;

void LogAttr(LPCWSTR name, bool found) {
  if (!g_on.load(std::memory_order_relaxed) || t_busy || name == nullptr) return;
  Busy busy;
  Line l("attr");
  l.add("\t%d\t%s", found ? 1 : 0, Narrow(name).c_str());
  l.stack();
  l.emit();
}

DWORD WINAPI DetourGetAttrW(LPCWSTR name) {
  std::wstring real;
  const DWORD r = g_origGetAttrW(LoadProbeRewritePath(name, &real) ? real.c_str() : name);
  LogAttr(name, r != INVALID_FILE_ATTRIBUTES);
  return r;
}

BOOL WINAPI DetourGetAttrExW(LPCWSTR name, GET_FILEEX_INFO_LEVELS level, LPVOID info) {
  std::wstring real;
  const BOOL r = g_origGetAttrExW(LoadProbeRewritePath(name, &real) ? real.c_str() : name, level, info);
  LogAttr(name, r != FALSE);
  return r;
}

// ---- Hooks do ID3D11Device ----

using CreateBufferFn = HRESULT(STDMETHODCALLTYPE *)(ID3D11Device *, const D3D11_BUFFER_DESC *,
                                                    const D3D11_SUBRESOURCE_DATA *, ID3D11Buffer **);
using CreateTex2DFn = HRESULT(STDMETHODCALLTYPE *)(ID3D11Device *, const D3D11_TEXTURE2D_DESC *,
                                                   const D3D11_SUBRESOURCE_DATA *, ID3D11Texture2D **);
using CreateLayoutFn = HRESULT(STDMETHODCALLTYPE *)(ID3D11Device *, const D3D11_INPUT_ELEMENT_DESC *, UINT,
                                                    const void *, SIZE_T, ID3D11InputLayout **);
using CreateVSFn = HRESULT(STDMETHODCALLTYPE *)(ID3D11Device *, const void *, SIZE_T, ID3D11ClassLinkage *,
                                                ID3D11VertexShader **);
using CreatePSFn = HRESULT(STDMETHODCALLTYPE *)(ID3D11Device *, const void *, SIZE_T, ID3D11ClassLinkage *,
                                                ID3D11PixelShader **);

CreateBufferFn g_origCreateBuffer = nullptr;
CreateTex2DFn g_origCreateTex2D = nullptr;
CreateLayoutFn g_origCreateLayout = nullptr;
CreateVSFn g_origCreateVS = nullptr;
CreatePSFn g_origCreatePS = nullptr;
void *g_gpuTargets[5] = {};
void *g_attrTargets[2] = {};

// Identificador do bytecode: os 8 primeiros bytes do checksum do cabeçalho DXBC.
std::string ShaderId(const void *code, SIZE_T size) {
  char id[24] = "-";
  if (code != nullptr && size >= 20 && std::memcmp(code, "DXBC", 4) == 0) {
    const auto *p = static_cast<const uint8_t *>(code) + 4;
    std::snprintf(id, sizeof(id), "%02x%02x%02x%02x%02x%02x%02x%02x", p[0], p[1], p[2], p[3], p[4], p[5],
                  p[6], p[7]);
  }
  return id;
}

void DumpShader(const char *kind, const std::string &id, const void *code, SIZE_T size) {
  if (!g_cfg.shaders || code == nullptr || id == "-") return;
  {
    std::lock_guard<std::mutex> lock(g_shaderMutex);
    if (!g_dumped.insert(id).second) return;
  }
  wchar_t name[64];
  std::swprintf(name, 64, L"%hs_%hs.dxbc", kind, id.c_str());
  const std::wstring path = g_cfg.out + L"shaders\\" + name;
  if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) return;
  FILE *f = _wfopen(path.c_str(), L"wb");
  if (f == nullptr) return;
  fwrite(code, 1, size, f);
  fclose(f);
}

HRESULT STDMETHODCALLTYPE DetourCreateBuffer(ID3D11Device *dev, const D3D11_BUFFER_DESC *desc,
                                             const D3D11_SUBRESOURCE_DATA *init, ID3D11Buffer **out) {
  const HRESULT hr = g_origCreateBuffer(dev, desc, init, out);
  if (!g_on.load(std::memory_order_relaxed) || t_busy || desc == nullptr) return hr;
  Busy busy;
  Line l("buf");
  l.add("\t%p\t%u\t%u\t%x\t%x\t%x\t%u\t%d", (SUCCEEDED(hr) && out) ? static_cast<void *>(*out) : nullptr,
        desc->ByteWidth, desc->Usage, desc->BindFlags, desc->CPUAccessFlags, desc->MiscFlags,
        desc->StructureByteStride, init != nullptr && init->pSysMem != nullptr);
  l.hex(init ? init->pSysMem : nullptr, init ? (std::min)(static_cast<size_t>(g_cfg.head), static_cast<size_t>(desc->ByteWidth)) : 0);
  l.stack();
  l.emit();
  return hr;
}

HRESULT STDMETHODCALLTYPE DetourCreateTex2D(ID3D11Device *dev, const D3D11_TEXTURE2D_DESC *desc,
                                            const D3D11_SUBRESOURCE_DATA *init, ID3D11Texture2D **out) {
  const HRESULT hr = g_origCreateTex2D(dev, desc, init, out);
  if (!g_on.load(std::memory_order_relaxed) || t_busy || desc == nullptr) return hr;
  Busy busy;
  Line l("tex2d");
  l.add("\t%p\t%u\t%u\t%u\t%u\t%u\t%u\t%x\t%x\t%d", (SUCCEEDED(hr) && out) ? static_cast<void *>(*out) : nullptr,
        desc->Width, desc->Height, desc->MipLevels, desc->ArraySize, desc->Format, desc->Usage, desc->BindFlags,
        desc->MiscFlags, init != nullptr && init->pSysMem != nullptr);
  l.hex(init ? init->pSysMem : nullptr, init ? (std::min)(static_cast<size_t>(g_cfg.head), static_cast<size_t>(16)) : 0);
  l.stack();
  l.emit();
  return hr;
}

HRESULT STDMETHODCALLTYPE DetourCreateLayout(ID3D11Device *dev, const D3D11_INPUT_ELEMENT_DESC *elems, UINT count,
                                             const void *code, SIZE_T size, ID3D11InputLayout **out) {
  const HRESULT hr = g_origCreateLayout(dev, elems, count, code, size, out);
  if (!g_on.load(std::memory_order_relaxed) || t_busy) return hr;
  Busy busy;
  Line l("layout");
  l.add("\t%p\t%s\t%u\t", (SUCCEEDED(hr) && out) ? static_cast<void *>(*out) : nullptr,
        ShaderId(code, size).c_str(), count);
  for (UINT i = 0; elems != nullptr && i < count; ++i) {
    const auto &e = elems[i];
    l.add("%s%s%u:f%u:s%u:o%u:c%u", i ? " " : "", e.SemanticName ? e.SemanticName : "?", e.SemanticIndex,
          e.Format, e.InputSlot, e.AlignedByteOffset, e.InputSlotClass);
  }
  l.stack();
  l.emit();
  return hr;
}

template <typename Out>
void LogShader(const char *kind, HRESULT hr, const void *code, SIZE_T size, Out **out) {
  if (!g_on.load(std::memory_order_relaxed) || t_busy) return;
  Busy busy;
  const std::string id = ShaderId(code, size);
  DumpShader(kind, id, code, size);
  Line l(kind);
  l.add("\t%p\t%s\t%llu", (SUCCEEDED(hr) && out) ? static_cast<void *>(*out) : nullptr, id.c_str(),
        static_cast<unsigned long long>(size));
  l.stack();
  l.emit();
}

HRESULT STDMETHODCALLTYPE DetourCreateVS(ID3D11Device *dev, const void *code, SIZE_T size,
                                         ID3D11ClassLinkage *link, ID3D11VertexShader **out) {
  const HRESULT hr = g_origCreateVS(dev, code, size, link, out);
  LogShader("vs", hr, code, size, out);
  return hr;
}

HRESULT STDMETHODCALLTYPE DetourCreatePS(ID3D11Device *dev, const void *code, SIZE_T size,
                                         ID3D11ClassLinkage *link, ID3D11PixelShader **out) {
  const HRESULT hr = g_origCreatePS(dev, code, size, link, out);
  LogShader("ps", hr, code, size, out);
  return hr;
}

bool HookAt(void *target, void *detour, void **original) {
  if (target == nullptr) return false;
  if (MH_CreateHook(target, detour, original) != MH_OK) return false;
  if (MH_EnableHook(target) != MH_OK) {
    MH_RemoveHook(target);
    return false;
  }
  return true;
}

// A vtable do ID3D11Device é a mesma para todo dispositivo da d3d11.dll (DXVK no Proton):
// um dispositivo descartável basta para achar as funções.
int InstallGpuHooks() {
  HMODULE d3d = GetModuleHandleW(L"d3d11.dll");
  if (d3d == nullptr) d3d = LoadLibraryW(L"d3d11.dll");
  auto create = d3d ? reinterpret_cast<PFN_D3D11_CREATE_DEVICE>(GetProcAddress(d3d, "D3D11CreateDevice")) : nullptr;
  if (create == nullptr) return 0;
  ID3D11Device *dev = nullptr;
  if (FAILED(create(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &dev, nullptr,
                    nullptr)) ||
      dev == nullptr) {
    return 0;
  }
  void **vt = *reinterpret_cast<void ***>(dev);
  g_gpuTargets[0] = vt[3];  // CreateBuffer
  g_gpuTargets[1] = vt[5];  // CreateTexture2D
  g_gpuTargets[2] = vt[11]; // CreateInputLayout
  g_gpuTargets[3] = vt[12]; // CreateVertexShader
  g_gpuTargets[4] = vt[15]; // CreatePixelShader
  dev->Release();
  void *detours[5] = {reinterpret_cast<void *>(&DetourCreateBuffer), reinterpret_cast<void *>(&DetourCreateTex2D),
                      reinterpret_cast<void *>(&DetourCreateLayout), reinterpret_cast<void *>(&DetourCreateVS),
                      reinterpret_cast<void *>(&DetourCreatePS)};
  void **originals[5] = {reinterpret_cast<void **>(&g_origCreateBuffer), reinterpret_cast<void **>(&g_origCreateTex2D),
                         reinterpret_cast<void **>(&g_origCreateLayout), reinterpret_cast<void **>(&g_origCreateVS),
                         reinterpret_cast<void **>(&g_origCreatePS)};
  int ok = 0;
  for (int i = 0; i < 5; ++i) {
    if (HookAt(g_gpuTargets[i], detours[i], originals[i])) ++ok;
    else g_gpuTargets[i] = nullptr;
  }
  return ok;
}

// ---- Hooks no exe (endereços de docs/reverse_engineering/track_loading.md) ----

// Leitura de memória que não derruba o jogo com ponteiro ruim.
bool Readable(const void *p, size_t n) {
  if (p == nullptr) return false;
  MEMORY_BASIC_INFORMATION mbi{};
  if (VirtualQuery(p, &mbi, sizeof(mbi)) == 0 || mbi.State != MEM_COMMIT) return false;
  if ((mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) != 0) return false;
  const auto end = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
  return reinterpret_cast<uintptr_t>(p) + n <= end;
}

template <typename T> T ReadOr(const void *p, T fallback) {
  return Readable(p, sizeof(T)) ? *static_cast<const T *>(p) : fallback;
}

std::string SafeStr(const char *p, size_t max = 200) {
  if (!Readable(p, 1)) return "-";
  std::string s;
  for (size_t i = 0; i < max && Readable(p + i, 1); ++i) {
    const char c = p[i];
    if (c == 0) break;
    if (c < 32 || c > 126) return s.empty() ? "<bin>" : s + "<bin>";
    s += c;
  }
  return s;
}

unsigned long long Rva(const void *p) {
  const auto a = reinterpret_cast<uintptr_t>(p);
  return (a >= g_exeBase && a < g_exeEnd) ? a - g_exeBase : a;
}

// Descritor de classe do PSSG (registrado em 0x1408c5c30): +8 nome do nó.
const char *ClassName(const void *desc) {
  const auto a = reinterpret_cast<uintptr_t>(desc);
  if (a < g_exeBase || a >= g_exeEnd) return nullptr;
  const char *name = *reinterpret_cast<const char *const *>(a + 8);
  const auto n = reinterpret_cast<uintptr_t>(name);
  return (n >= g_exeBase && n < g_exeEnd) ? name : nullptr;
}

// Banco PSSG que a thread está carregando (para contar objetos por arquivo).
thread_local int t_db = -1;
std::mutex g_classMutex;
std::vector<std::string> g_dbNames;
std::unordered_map<unsigned long long, unsigned long long> g_classCount; // (db << 40 | rva do descritor)

using TrackEventFn = uint64_t (*)(void *, void *);
using ProvideFn = uint64_t (*)(void *, void *, int, void *, const char *);
using PssgLoadFn = uint64_t (*)(void *, void *);
using CreateObjFn = uint64_t (*)(void *, void *, void **);
using SubmitFn = uint64_t (*)(void *, const void *, int, const char *, int, void *, void *);
SubmitFn g_origSubmit = nullptr;
using SubmitFileFn = uint64_t (*)(void *, const char *, int, void *, void *, uint8_t);
using SceneFrameFn = uint64_t (*)(void *, void *, void *, void *);
SubmitFileFn g_origSubmitFile = nullptr;
std::atomic<int> g_manifests{0};
TrackEventFn g_origTrackEvent = nullptr;
ProvideFn g_origProvide = nullptr;
PssgLoadFn g_origPssgLoad = nullptr;
CreateObjFn g_origCreateObj = nullptr;
SceneFrameFn g_origSceneFrame = nullptr;
std::atomic<int> g_pvsLogged{0};
void *g_exeTargets[7] = {};

// Preparo da cena por quadro (0x1403c7aa0, rcx = cena): copia o modo de PVS de cena+0x1734 para
// a vista. Gravar o campo antes força o modo em todas as cenas.
uint64_t DetourSceneFrame(void *scene, void *a2, void *a3, void *a4) {
  if (g_cfg.pvsMode >= 0 && scene != nullptr) {
    auto *mode = reinterpret_cast<int32_t *>(static_cast<char *>(scene) + 0x1734);
    const int32_t before = *mode;
    *mode = g_cfg.pvsMode;
    if (before != g_cfg.pvsMode && g_pvsLogged.fetch_add(1) < 8) {
      char buf[128];
      std::snprintf(buf, sizeof(buf), "LoadProbe: PVS da cena %p: %d -> %d", scene, before, g_cfg.pvsMode);
      Logger::Info(buf);
    }
  }
  return g_origSceneFrame(scene, a2, a3, a4);
}

// TrackLoader::OnEvent(this, evento): o nome do evento está em evento+0x10 -> +0x10.
uint64_t DetourTrackEvent(void *self, void *ev) {
  if (g_on.load(std::memory_order_relaxed) && !t_busy) {
    Busy busy;
    const void *inner = ReadOr<const void *>(static_cast<const char *>(ev) + 0x10, nullptr);
    const char *name = inner ? ReadOr<const char *>(static_cast<const char *>(inner) + 0x10, nullptr) : nullptr;
    Line l("tevent");
    l.add("\t%p\t%llx\t%s", self, Rva(ReadOr<const void *>(ev, nullptr)), SafeStr(name).c_str());
    l.stack();
    l.emit();
  }
  return g_origTrackEvent(self, ev);
}

// Registro de dados por nome (0x140b39020): entrega (dados, tamanho) ao ouvinte do slot `name`.
// O original inverte os bytes do bloco antes de entregar; logamos início e fim nos dois momentos.
uint64_t DetourProvide(void *mgr, void *data, int size, void *listener, const char *name) {
  const bool on = g_on.load(std::memory_order_relaxed) && !t_busy;
  uint8_t before[32] = {};
  const size_t n = (data != nullptr && size > 0) ? (std::min)(static_cast<size_t>(size), static_cast<size_t>(16)) : 0;
  if (on && n) {
    std::memcpy(before, data, n);
    std::memcpy(before + 16, static_cast<const uint8_t *>(data) + size - n, n);
  }
  const double t0 = NowMs();
  const uint64_t r = g_origProvide(mgr, data, size, listener, name);
  if (on) {
    Busy busy;
    Line l("provide");
    l.add("\t%s\t%d\t%p\t%llx\t%.3f", SafeStr(name).c_str(), size, data,
          Rva(ReadOr<const void *>(listener, nullptr)), NowMs() - t0);
    l.hex(n ? before : nullptr, n);
    l.hex(n ? before + 16 : nullptr, n);
    l.stack();
    l.emit();
  }
  return r;
}

// Carga de um banco PSSG (0x1408e7360): desc+0x10 nome, desc+0x18 ?, desc -> stream.
uint64_t DetourPssgLoad(void *out, void *desc) {
  if (!g_on.load(std::memory_order_relaxed) || t_busy) return g_origPssgLoad(out, desc);
  const char *name = ReadOr<const char *>(static_cast<const char *>(desc) + 0x10, nullptr);
  const std::string label = SafeStr(name);
  int db;
  {
    std::lock_guard<std::mutex> lock(g_classMutex);
    db = static_cast<int>(g_dbNames.size());
    g_dbNames.push_back(label);
  }
  {
    Busy busy;
    Line l("pssg+");
    l.add("\t%d\t%s\t%llx\t%llx", db, label.c_str(),
          static_cast<unsigned long long>(ReadOr<uintptr_t>(static_cast<const char *>(desc) + 0x18, 0)),
          Rva(ReadOr<const void *>(desc, nullptr)));
    l.stack();
    l.emit();
  }
  const int prev = t_db;
  t_db = db;
  const double t0 = NowMs();
  const uint64_t r = g_origPssgLoad(out, desc);
  t_db = prev;
  Busy busy;
  Line l("pssg-");
  l.add("\t%d\t%s\t%llu\t%.3f", db, label.c_str(), static_cast<unsigned long long>(r & 0xffffffff), NowMs() - t0);
  l.emit();
  return r;
}

// Criação de cada objeto do PSSG (0x1408f3890): elemento+0x28 = descritor da classe.
uint64_t DetourCreateObj(void *ctx, void *elem, void **out) {
  if (g_on.load(std::memory_order_relaxed) && elem != nullptr) {
    const void *desc = *reinterpret_cast<void *const *>(static_cast<const char *>(elem) + 0x28);
    const unsigned long long key = (static_cast<unsigned long long>(t_db + 1) << 40) | (Rva(desc) & 0xffffffffffull);
    std::lock_guard<std::mutex> lock(g_classMutex);
    ++g_classCount[key];
  }
  return g_origCreateObj(ctx, elem, out);
}

// Pedido de carga (0x140c5fda0): manifesto XML `<dataset><binary processor= map= pool= filename=
// userdata=/>...` montado pelo chamador, entregue com tamanho e um rótulo. Cada manifesto vai
// para <out>/manifests/.
uint64_t DetourSubmit(void *mgr, const void *data, int size, const char *name, int flags, void *a6, void *a7) {
  if (g_on.load(std::memory_order_relaxed) && !t_busy) {
    Busy busy;
    const int index = g_manifests.fetch_add(1);
    const std::string label = SafeStr(name, 80);
    if (data != nullptr && size > 0 && Readable(data, static_cast<size_t>(size))) {
      std::string file = label;
      for (char &c : file)
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '.' && c != '_') c = '_';
      wchar_t wname[160];
      std::swprintf(wname, 160, L"manifests\\%ls_%03d_%hs.bin", g_stamp.c_str(), index, file.c_str());
      if (FILE *f = _wfopen((g_cfg.out + wname).c_str(), L"wb")) {
        fwrite(data, 1, static_cast<size_t>(size), f);
        fclose(f);
      }
    }
    Line l("submit");
    l.add("\t%d\t%s\t%d\t%d\t%p\t%p", index, label.c_str(), size, flags, a6, a7);
    l.hex(data, data != nullptr && size > 0 ? (std::min)(static_cast<size_t>(size), static_cast<size_t>(32)) : 0);
    l.stack();
    l.emit();
  }
  return g_origSubmit(mgr, data, size, name, flags, a6, a7);
}

// ---- Overlay experimental (camada de pasta do próprio I/O do jogo) ----

std::wstring g_overlayVirt; // "<jogo>\dr2hook_overlay\" em minúsculas; vazio = overlay desligado
std::atomic<bool> g_overlayMounted{false};

bool BytesAt(uintptr_t rva, const char *hex) {
  const auto *fn = reinterpret_cast<const uint8_t *>(g_exeBase + rva);
  for (size_t i = 0; hex[i * 3] != 0; ++i) {
    if (fn[i] != std::strtoul(std::string(hex + i * 3, 2).c_str(), nullptr, 16)) return false;
    if (hex[i * 3 + 2] == 0) break;
  }
  return true;
}

// Mesma sequência que 0x1403a3d70 usa para /data/video: camada de pasta de 0x128 bytes
// (construtor 0x1407fcd40), raiz "dispositivo:/caminho" (0x140813860, dispositivo 0 = pasta do jogo)
// e montagem no sistema de I/O global (0x140815840), que põe a entrada na frente da lista.
void MountOverlay() {
  using GetMemFn = void *(*)();
  using TrackerFn = void *(*)(void *, const char *);
  using CtorFn = void *(*)(void *);
  using InitFn = int (*)(void *, void *, const char *);
  using MountFn = int (*)(void *, void *, const char *, int, void **, void *);
  if (!BytesAt(0xb2730, "40 53 48 83 ec 20") || !BytesAt(0x865180, "48 89 54 24 10 48 89 4c 24 08") ||
      !BytesAt(0x7fcd40, "48 8d 05 39 d6 ac 00") || !BytesAt(0x813860, "48 83 79 08 00 48 8b c1") ||
      !BytesAt(0x815840, "4c 89 44 24 18 41 54 41 55")) {
    Logger::Warn("LoadProbe: overlay ignorado, bytes do exe diferentes do esperado.");
    return;
  }
  void *iosys = ReadOr<void *>(reinterpret_cast<const void *>(g_exeBase + 0x16925f8), nullptr);
  if (iosys == nullptr) {
    Logger::Warn("LoadProbe: overlay ignorado, sistema de I/O ainda nulo.");
    return;
  }
  void *layer = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, 0x128); // nunca liberada: a montagem fica até o fim
  reinterpret_cast<CtorFn>(g_exeBase + 0x7fcd40)(layer);
  void *pool = reinterpret_cast<TrackerFn>(g_exeBase + 0x865180)(reinterpret_cast<GetMemFn>(g_exeBase + 0xb2730)(),
                                                                  "INPUT_IO_LAYER");
  const int init = reinterpret_cast<InitFn>(g_exeBase + 0x813860)(layer, pool, "00000000:/dr2hook_overlay");
  void *entry = nullptr;
  const int mount = init == 0 ? reinterpret_cast<MountFn>(g_exeBase + 0x815840)(
                                    iosys, layer, g_cfg.overlayMount.c_str(), g_cfg.overlayFlag, &entry, nullptr)
                              : -1;
  char msg[200];
  std::snprintf(msg, sizeof(msg), "LoadProbe: overlay em %s (flag %d): init=%d mount=%d entrada=%p",
                g_cfg.overlayMount.c_str(), g_cfg.overlayFlag, init, mount, entry);
  Logger::Info(msg);
  Busy busy;
  Line l("overlay");
  l.add("\t%s\t%d\t%d\t%d\t%p", g_cfg.overlayMount.c_str(), g_cfg.overlayFlag, init, mount, entry);
  l.emit();
}

// Pedido de carga por arquivo (0x140c5ff80): o manifesto vem de um arquivo do sistema virtual
// (ex.: `tracks/track_loader.xml`, que mora no raceload.jpk do game_1.dat) e r9 é a tabela de
// tokens (%location%, %track%, %route%...) montada em 0x140487480.
uint64_t DetourSubmitFile(void *mgr, const char *file, int flags, void *tokens, void *a5, uint8_t a6) {
  // O .nefs da pista já está montado quando o track_loader é pedido; montando depois, a pasta
  // fica na frente dele na lista.
  if (!g_overlayVirt.empty() && file != nullptr && std::strcmp(file, "tracks/track_loader.xml") == 0 &&
      !g_overlayMounted.exchange(true))
    MountOverlay();
  if (g_on.load(std::memory_order_relaxed) && !t_busy) {
    Busy busy;
    Line l("subfile");
    l.add("\t%s\t%d\t%p\t%p\t%u", SafeStr(file, 160).c_str(), flags, tokens, a5, a6);
    l.stack();
    l.emit();
    // Tabela de tokens (hash map de 0x1401d5de0): lista com sentinela em +0x20, próximo nó em
    // nó+8, chave (char*) em nó+0x20 e, pelo layout do par montado em 0x140489d61, valor em nó+0x58.
    if (tokens != nullptr) {
      const char *sentinel = static_cast<const char *>(tokens) + 0x20;
      const char *node = ReadOr<const char *>(sentinel + 8, nullptr);
      for (int i = 0; i < 256 && node != nullptr && node != sentinel; ++i) {
        Line t("token");
        t.add("\t%s\t%s\t%s", SafeStr(file, 60).c_str(), SafeStr(ReadOr<const char *>(node + 0x20, nullptr), 64).c_str(),
              SafeStr(ReadOr<const char *>(node + 0x58, nullptr), 160).c_str());
        t.emit();
        node = ReadOr<const char *>(node + 8, nullptr);
      }
    }
  }
  return g_origSubmitFile(mgr, file, flags, tokens, a5, a6);
}

bool HookExe(uintptr_t rva, const char *prologue, void *detour, void **original, void **target) {
  auto *fn = reinterpret_cast<uint8_t *>(g_exeBase + rva);
  for (size_t i = 0; prologue[i * 3] != 0; ++i) {
    const unsigned expect = std::strtoul(std::string(prologue + i * 3, 2).c_str(), nullptr, 16);
    if (fn[i] != expect) {
      Logger::Warn("LoadProbe: prologo diferente em exe+" + std::to_string(rva) + "; hook ignorado.");
      return false;
    }
    if (prologue[i * 3 + 2] == 0) break;
  }
  if (!HookAt(fn, detour, original)) return false;
  *target = fn;
  return true;
}

int InstallExeHooks() {
  int ok = 0;
  ok += HookExe(0x499650, "48 89 6c 24 10 48 89 74 24 18 57", reinterpret_cast<void *>(&DetourTrackEvent),
                reinterpret_cast<void **>(&g_origTrackEvent), &g_exeTargets[0]);
  ok += HookExe(0xb39020, "40 53 55 56 41 54 41 55 48 83 ec 20", reinterpret_cast<void *>(&DetourProvide),
                reinterpret_cast<void **>(&g_origProvide), &g_exeTargets[1]);
  ok += HookExe(0x8e7360, "48 89 6c 24 18 48 89 74 24 20 57", reinterpret_cast<void *>(&DetourPssgLoad),
                reinterpret_cast<void **>(&g_origPssgLoad), &g_exeTargets[2]);
  ok += HookExe(0x8f3890, "48 89 5c 24 08 48 89 6c 24 10", reinterpret_cast<void *>(&DetourCreateObj),
                reinterpret_cast<void **>(&g_origCreateObj), &g_exeTargets[3]);
  ok += HookExe(0xc5fda0, "48 89 5c 24 10 48 89 6c 24 18 48 89 74 24 20 57", reinterpret_cast<void *>(&DetourSubmit),
                reinterpret_cast<void **>(&g_origSubmit), &g_exeTargets[4]);
  ok += HookExe(0xc5ff80, "48 89 5c 24 10 48 89 6c 24 18 48 89 74 24 20 57", reinterpret_cast<void *>(&DetourSubmitFile),
                reinterpret_cast<void **>(&g_origSubmitFile), &g_exeTargets[5]);
  if (g_cfg.pvsMode >= 0)
    ok += HookExe(0x3c7aa0, "48 89 5c 24 08 57 48 81 ec 30 01 00 00 48 8b f9", reinterpret_cast<void *>(&DetourSceneFrame),
                  reinterpret_cast<void **>(&g_origSceneFrame), &g_exeTargets[6]);
  return ok;
}

void WriteClassCounts() {
  std::lock_guard<std::mutex> lock(g_classMutex);
  for (const auto &kv : g_classCount) {
    const int db = static_cast<int>(kv.first >> 40) - 1;
    const auto rva = kv.first & 0xffffffffffull;
    const char *name = ClassName(reinterpret_cast<const void *>(g_exeBase + rva));
    Line l("class");
    l.add("\t%d\t%s\t%llx\t%s\t%llu", db, db >= 0 && db < static_cast<int>(g_dbNames.size()) ? g_dbNames[db].c_str() : "-",
          rva, name ? name : "?", kv.second);
    l.emit();
  }
}

void StopTrace(const char *why) {
  if (!g_on.load()) return;
  WriteClassCounts();
  if (!g_on.exchange(false)) return;
  WriteModules(why);
  Flush();
  {
    std::lock_guard<std::mutex> lock(g_writeMutex);
    if (g_out != nullptr) fclose(g_out);
    g_out = nullptr;
  }
  Logger::Info(std::string("LoadProbe: trace fechado (") + why + ").");
}

void OnTick(double dt) {
  if (!g_on.load(std::memory_order_relaxed)) return;
  ++g_secFrames;
  g_secMax = (std::max)(g_secMax, dt);
  g_secAccum += dt;
  if (g_secAccum < 1.0) return;
  {
    Busy busy;
    Line l("sec");
    l.add("\t%u\t%.1f", g_secFrames, g_secMax * 1000.0);
    l.emit();
  }
  g_secAccum = g_secMax = 0.0;
  g_secFrames = 0;
  Flush();
  if (g_startAt >= 0.0 && g_cfg.stopAfterStart > 0.0 && NowMs() - g_startAt > g_cfg.stopAfterStart * 1000.0) {
    StopTrace("tempo depois da largada");
  }
}

} // namespace

bool LoadProbeRewritePath(const wchar_t *path, std::wstring *out) {
  if (g_overlayVirt.empty() || path == nullptr) return false;
  // Aceita também a própria pasta sem a barra final (a camada consulta a raiz).
  const size_t n = g_overlayVirt.size();
  size_t i = 0;
  for (; i < n; ++i) {
    wchar_t c = static_cast<wchar_t>(std::towlower(path[i]));
    if (c == L'/') c = L'\\';
    if (c != g_overlayVirt[i]) break;
  }
  if (i < n && !(i == n - 1 && path[i] == 0)) return false;
  std::wstring rest = i == n ? std::wstring(path + n) : std::wstring();
  for (auto &c : rest)
    if (c == L'/') c = L'\\';
  *out = g_cfg.overlayDir + rest;
  if (rest.empty()) out->pop_back();
  return true;
}

void LoadProbeOnOpen(void *handle, const wchar_t *path, unsigned long access) {
  if (!g_on.load(std::memory_order_relaxed) || t_busy || path == nullptr) return;
  Busy busy;
  uint32_t fid = 0;
  const bool failed = handle == INVALID_HANDLE_VALUE;
  if (!failed) {
    std::lock_guard<std::mutex> lock(g_handleMutex);
    if ((access & GENERIC_READ) != 0 && IsPackage(path)) {
      fid = g_nextFileId++;
      g_handles[handle] = fid;
    } else {
      g_handles.erase(handle); // handle reaproveitado por um arquivo que não interessa
    }
  }
  Line l("open");
  l.add("\t%d\t%p\t%lx\t%s", failed ? -1 : static_cast<int>(fid), handle, access, Narrow(path).c_str());
  l.stack();
  l.emit();
}

void LoadProbeOnRead(void *handle, unsigned long size, void *overlapped) {
  if (!g_on.load(std::memory_order_relaxed) || t_busy) return;
  uint32_t fid = 0;
  {
    std::lock_guard<std::mutex> lock(g_handleMutex);
    const auto it = g_handles.find(handle);
    if (it == g_handles.end()) return;
    fid = it->second;
  }
  Busy busy;
  unsigned long long offset = 0;
  if (overlapped != nullptr) {
    const auto *ov = static_cast<const OVERLAPPED *>(overlapped);
    offset = (static_cast<unsigned long long>(ov->OffsetHigh) << 32) | ov->Offset;
  } else {
    LARGE_INTEGER zero{}, pos{};
    if (SetFilePointerEx(handle, zero, &pos, FILE_CURRENT)) offset = static_cast<unsigned long long>(pos.QuadPart);
  }
  Line l("read");
  l.add("\t%u\t%llu\t%lu\t%d", fid, offset, size, overlapped != nullptr);
  l.stack();
  l.emit();
}

void LoadProbeMark(const char *what) {
  if (!g_on.load(std::memory_order_relaxed) || what == nullptr) return;
  Busy busy;
  if (std::strcmp(what, "racestart") == 0 && g_startAt < 0.0) g_startAt = NowMs();
  Line l("mark");
  l.add("\t%s", what);
  l.stack();
  l.emit();
  if (std::strncmp(what, "stage ", 6) == 0) WriteModules(what);
}

bool InstallLoadProbe() {
  if (!ReadConfig()) return false;
  QueryPerformanceFrequency(&g_freq);
  QueryPerformanceCounter(&g_t0);
  g_exeBase = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
  const auto *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(g_exeBase);
  const auto *nt = reinterpret_cast<const IMAGE_NT_HEADERS *>(g_exeBase + dos->e_lfanew);
  g_exeEnd = g_exeBase + nt->OptionalHeader.SizeOfImage;
  if (!g_cfg.overlayDir.empty() && g_cfg.exe) {
    g_overlayVirt = GameDir() + L"dr2hook_overlay\\";
    for (auto &c : g_overlayVirt) c = c == L'/' ? L'\\' : static_cast<wchar_t>(std::towlower(c));
    Logger::Info("LoadProbe: overlay " + Narrow(g_overlayVirt.c_str()) + " -> " + Narrow(g_cfg.overlayDir.c_str()));
  }

  SYSTEMTIME st;
  GetLocalTime(&st);
  wchar_t stamp[32];
  std::swprintf(stamp, 32, L"%04u%02u%02u_%02u%02u%02u", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute,
                st.wSecond);
  g_stamp = stamp;
  MakeDirs(g_cfg.out + L"shaders\\");
  MakeDirs(g_cfg.out + L"manifests\\");
  const std::wstring trace = g_cfg.out + L"trace_" + g_stamp + L".tsv";
  g_out = _wfopen(trace.c_str(), L"wb");
  if (g_out == nullptr) {
    Logger::Warn("LoadProbe: nao consegui criar " + Narrow(trace.c_str()));
    return false;
  }
  fprintf(g_out, "# DR2Hook LoadProbe; exe base %llx; colunas: tipo, ms, thread, campos..., pilha (e<rva> = exe)\n",
          static_cast<unsigned long long>(g_exeBase));
  g_on.store(true);
  WriteModules("inicio");

  const int gpu = g_cfg.gpu ? InstallGpuHooks() : 0;
  const int exe = g_cfg.exe ? InstallExeHooks() : 0;
  int attr = 0;
  if (HMODULE k32 = GetModuleHandleW(L"kernel32.dll")) {
    g_attrTargets[0] = reinterpret_cast<void *>(GetProcAddress(k32, "GetFileAttributesW"));
    g_attrTargets[1] = reinterpret_cast<void *>(GetProcAddress(k32, "GetFileAttributesExW"));
    if (HookAt(g_attrTargets[0], reinterpret_cast<void *>(&DetourGetAttrW), reinterpret_cast<void **>(&g_origGetAttrW))) ++attr;
    else g_attrTargets[0] = nullptr;
    if (HookAt(g_attrTargets[1], reinterpret_cast<void *>(&DetourGetAttrExW), reinterpret_cast<void **>(&g_origGetAttrExW))) ++attr;
    else g_attrTargets[1] = nullptr;
  }
  RegisterTickCallback(&OnTick);
  Logger::Info("LoadProbe: ativo em " + Narrow(trace.c_str()) + " (D3D11 " + std::to_string(gpu) + "/5, exe " + std::to_string(exe) + "/" + std::to_string(std::size(g_exeTargets)) + ", atributos " +
               std::to_string(attr) + "/2, pilha " + std::to_string(g_cfg.stack) + ").");
  return true;
}

void UninstallLoadProbe() {
  StopTrace("descarregando");
  for (void *&t : g_gpuTargets) {
    if (t != nullptr) {
      MH_DisableHook(t);
      MH_RemoveHook(t);
      t = nullptr;
    }
  }
  for (void *&t : g_exeTargets) {
    if (t != nullptr) {
      MH_DisableHook(t);
      MH_RemoveHook(t);
      t = nullptr;
    }
  }
  for (void *&t : g_attrTargets) {
    if (t != nullptr) {
      MH_DisableHook(t);
      MH_RemoveHook(t);
      t = nullptr;
    }
  }
}

} // namespace dr2hook

#else

namespace dr2hook {
bool InstallLoadProbe() { return false; }
void UninstallLoadProbe() {}
void LoadProbeOnOpen(void *, const wchar_t *, unsigned long) {}
void LoadProbeOnRead(void *, unsigned long, void *) {}
void LoadProbeMark(const char *) {}
bool LoadProbeRewritePath(const wchar_t *, std::wstring *) { return false; }
} // namespace dr2hook

#endif
