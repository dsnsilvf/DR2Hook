#include "dr2hook/track_packages.h"
#include "dr2hook/boot_screen.h"
#include "dr2hook/logger.h"

#include <atomic>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>

#include <windows.h>

#include "MinHook.h"
#include "trackpkg/builder.h"

namespace dr2hook {
namespace {

// Resolução de caminho absoluto do VFS: (sistema de I/O, "/data/...", flags, saída da entrada,
// saída do caminho relativo dentro da string de entrada, saída de 32 bytes). Compara o prefixo
// de cada montagem e pergunta à camada (vtable+0x10). Chamada pelo open (0x1407ffc90), pelo
// stat (0x140800200) e por 0x140812cf0.
constexpr uintptr_t kResolveRva = 0x80e450;
constexpr uint8_t kResolveBytes[] = {0x48, 0x8b, 0xc4, 0x4c, 0x89, 0x48, 0x20, 0x44, 0x89, 0x40, 0x18, 0x53, 0x55, 0x41, 0x55};
// Pacote da location pronto? (gerenciador, location, pista ou nulo = "tiles")
constexpr uintptr_t kLocReadyRva = 0x5df3c0;
constexpr uint8_t kLocReadyBytes[] = {0x48, 0x8b, 0xc4, 0x56, 0x48, 0x81, 0xec, 0x50, 0x01, 0x00, 0x00};
// Nome e descritor do pacote da location: "%s__%s.nefs" com location e pista nas posições 5 e 6.
constexpr uintptr_t kLocNameRva = 0x5dcea0;
constexpr uint8_t kLocNameBytes[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57};

using ResolveFn = int (*)(void *, const char *, int, void *, const char **, void *);
using LocReadyFn = bool (*)(void *, const char *, const char *);
using LocNameFn = int (*)(void *, void *, void *, void *, const char *, const char *);
ResolveFn g_origResolve = nullptr;
LocReadyFn g_origLocReady = nullptr;
LocNameFn g_origLocName = nullptr;

uintptr_t g_base = 0;
const char *g_status = "nao instalado";
HANDLE g_built = nullptr; // sinalizado quando a montagem termina (com ou sem cache)
trackpkg::BuildResult g_result;
std::atomic<bool> g_ready{false};  // g_result preenchido
std::atomic<bool> g_mounted{false};
std::atomic<int> g_seen{0};

struct Redirect {
  std::string prefix;     // "/data/tracks/locations/<loc>/<id>/"
  std::string hostPrefix; // "/data/tracks/locations/<loc>/<hospedeira>/"
  const trackpkg::TrackRuntime *track = nullptr;
};
std::vector<Redirect> g_redirects;

std::mutex g_aliasMutex;
std::vector<std::pair<std::string, std::string>> g_aliases;

std::mutex g_internMutex;
std::unordered_set<std::string> g_interned; // caminhos reescritos: o VFS guarda ponteiros para dentro deles

const char *Intern(std::string s) {
  std::lock_guard<std::mutex> lock(g_internMutex);
  return g_interned.insert(std::move(s)).first->c_str();
}

std::string GameDirU8() {
  wchar_t exe[MAX_PATH] = {};
  GetModuleFileNameW(nullptr, exe, MAX_PATH);
  std::wstring dir(exe);
  dir.resize(dir.find_last_of(L"\\/"));
  const int n = WideCharToMultiByte(CP_UTF8, 0, dir.c_str(), static_cast<int>(dir.size()), nullptr, 0, nullptr, nullptr);
  std::string out(static_cast<size_t>(n), '\0');
  WideCharToMultiByte(CP_UTF8, 0, dir.c_str(), static_cast<int>(dir.size()), out.data(), n, nullptr, nullptr);
  return out;
}

bool StartsWithNoCase(const char *s, const std::string &prefix) {
  for (size_t i = 0; i < prefix.size(); ++i)
    if (s[i] == 0 || std::tolower(static_cast<unsigned char>(s[i])) != prefix[i]) return false;
  return true;
}

// Mesma sequência que 0x1403a3d70 usa para /data/video (ver MountOverlay em load_probe.cpp):
// camada de pasta, raiz "dispositivo:/caminho" (dispositivo 0 = pasta do jogo) e montagem no
// sistema de I/O global, que põe a entrada na frente da lista.
bool MountCache() {
  using GetMemFn = void *(*)();
  using TrackerFn = void *(*)(void *, const char *);
  using CtorFn = void *(*)(void *);
  using InitFn = int (*)(void *, void *, const char *);
  using MountFn = int (*)(void *, void *, const char *, int, void **, void *);
  struct Check {
    uintptr_t rva;
    std::vector<uint8_t> bytes;
  };
  const Check checks[] = {{0xb2730, {0x40, 0x53, 0x48, 0x83, 0xec, 0x20}},
                          {0x865180, {0x48, 0x89, 0x54, 0x24, 0x10, 0x48, 0x89, 0x4c, 0x24, 0x08}},
                          {0x7fcd40, {0x48, 0x8d, 0x05, 0x39, 0xd6, 0xac, 0x00}},
                          {0x813860, {0x48, 0x83, 0x79, 0x08, 0x00, 0x48, 0x8b, 0xc1}},
                          {0x815840, {0x4c, 0x89, 0x44, 0x24, 0x18, 0x41, 0x54, 0x41, 0x55}}};
  for (const Check &c : checks)
    if (std::memcmp(reinterpret_cast<const void *>(g_base + c.rva), c.bytes.data(), c.bytes.size()) != 0) {
      Logger::Warn("TrackPackages: cache não montado, bytes do exe diferentes do esperado.");
      return false;
    }
  void *iosys = *reinterpret_cast<void **>(g_base + 0x16925f8);
  if (iosys == nullptr) {
    Logger::Warn("TrackPackages: cache não montado, sistema de I/O ainda nulo.");
    return false;
  }
  void *layer = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, 0x128); // fica até o fim do processo
  reinterpret_cast<CtorFn>(g_base + 0x7fcd40)(layer);
  void *pool = reinterpret_cast<TrackerFn>(g_base + 0x865180)(reinterpret_cast<GetMemFn>(g_base + 0xb2730)(),
                                                               "INPUT_IO_LAYER");
  const int init = reinterpret_cast<InitFn>(g_base + 0x813860)(layer, pool, "00000000:/ctracks/_cache");
  void *entry = nullptr;
  const int mount =
      init == 0 ? reinterpret_cast<MountFn>(g_base + 0x815840)(iosys, layer, "/data", 1, &entry, nullptr) : -1;
  char msg[160];
  std::snprintf(msg, sizeof(msg), "TrackPackages: ctracks/_cache montado em /data: init=%d mount=%d entrada=%p", init,
                mount, entry);
  (init == 0 && mount == 0 ? Logger::Info : Logger::Warn)(msg);
  return init == 0 && mount == 0;
}

void OnFirstData(const char *path) {
  // Antes do boot_data.xml os pacotes do jogo ainda estão montando (a overlay da LoadProbe entra no
  // mesmo ponto, track_loading.md §11.4); o que vem depois fica na frente deles.
  if (std::strstr(path, "boot_data.xml") == nullptr && std::strncmp(path, "/data/catalogues/", 17) != 0) return;
  static std::mutex m;
  std::lock_guard<std::mutex> lock(m);
  if (g_mounted.load()) return;
  Logger::Info(std::string("TrackPackages: montando o cache no pedido de ") + path);
  if (WaitForSingleObject(g_built, 120000) != WAIT_OBJECT_0) {
    Logger::Warn("TrackPackages: montagem do cache demorou demais; seguindo sem as pistas.");
    g_mounted.store(true);
    return;
  }
  if (g_result.cacheReady) MountCache();
  g_mounted.store(true);
}

int DetourResolve(void *io, const char *path, int flags, void *entry, const char **rel, void *out) {
  if (path != nullptr && path[0] == '/') {
    const int n = g_seen.fetch_add(1);
    if (n < 24) Logger::Info(std::string("TrackPackages: VFS ") + path);
    if (!g_mounted.load(std::memory_order_acquire)) OnFirstData(path);
    if (g_ready.load(std::memory_order_acquire)) {
      for (const Redirect &r : g_redirects) {
        if (!StartsWithNoCase(path, r.prefix)) {
          // A própria pasta: sem arquivos no pacote, é a da hospedeira.
          if (r.track->files.empty() && std::strlen(path) == r.prefix.size() - 1 &&
              StartsWithNoCase(path, r.prefix.substr(0, r.prefix.size() - 1)))
            path = Intern(r.hostPrefix.substr(0, r.hostPrefix.size() - 1));
          continue;
        }
        std::string relPath(path + r.prefix.size());
        for (auto &c : relPath) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (!r.track->files.count(relPath)) path = Intern(r.hostPrefix + (path + r.prefix.size()));
        break;
      }
    }
  }
  return g_origResolve(io, path, flags, entry, rel, out);
}

const char *AliasTrack(const char *track) {
  if (track == nullptr || IsBadReadPtr(track, 1)) return track;
  std::lock_guard<std::mutex> lock(g_aliasMutex);
  for (const auto &a : g_aliases)
    if (_stricmp(track, a.first.c_str()) == 0) return a.second.c_str(); // nunca removidos: ponteiro estável
  return track;
}

bool DetourLocReady(void *mgr, const char *location, const char *track) {
  return g_origLocReady(mgr, location, AliasTrack(track));
}

int DetourLocName(void *mgr, void *name, void *desc, void *flag, const char *location, const char *track) {
  const char *alias = AliasTrack(track);
  if (alias != track)
    Logger::Info(std::string("TrackPackages: pacote da location ") + location + "__" + track + " -> " + alias);
  return g_origLocName(mgr, name, desc, flag, location, alias);
}

bool Hook(uintptr_t rva, const uint8_t *bytes, size_t n, void *detour, void **orig) {
  void *target = reinterpret_cast<void *>(g_base + rva);
  if (std::memcmp(target, bytes, n) != 0) return false;
  if (MH_CreateHook(target, detour, orig) != MH_OK) return false;
  if (MH_EnableHook(target) != MH_OK) {
    MH_RemoveHook(target);
    return false;
  }
  return true;
}

DWORD WINAPI BuildThread(LPVOID) {
  trackpkg::BuildOptions opt;
  opt.gameDir = GameDirU8();
  opt.info = [](const std::string &s) { Logger::Info("TrackPackages: " + s); };
  static std::atomic<int> warnings{0}, problems{0};
  opt.warn = [](const std::string &s) {
    warnings.fetch_add(1);
    Logger::Warn("TrackPackages: " + s);
  };
  opt.progress = [](trackpkg::BuildEvent e, const std::string &s, double fraction) {
    if (e == trackpkg::BuildEvent::Step) BootScreenStep(s, fraction);
    else BootScreenLine(e == trackpkg::BuildEvent::Done ? BootLine::Done : BootLine::Problem, s);
    if (e == trackpkg::BuildEvent::Problem) problems.fetch_add(1);
  };
  const DWORD t0 = GetTickCount();
  try {
    g_result = trackpkg::BuildTrackPackages(opt);
  } catch (const std::exception &e) {
    Logger::Error(std::string("TrackPackages: montagem falhou: ") + e.what());
    g_result = trackpkg::BuildResult{};
  }
  for (const auto &t : g_result.tracks) {
    Redirect r;
    r.prefix = "/data/tracks/locations/" + t.hostLocation + "/" + t.id + "/";
    r.hostPrefix = "/data/tracks/locations/" + t.hostLocation + "/" + t.hostTrack + "/";
    r.track = &t;
    g_redirects.push_back(std::move(r));
    AddTrackAlias(t.id, t.hostTrack);
    Logger::Info("TrackPackages: " + t.id + " (location " + std::to_string(t.location) + ", " +
                 std::to_string(t.files.size()) + " arquivos próprios, resto de " + t.hostTrack + ")");
  }
  g_ready.store(true, std::memory_order_release);
  const DWORD ms = GetTickCount() - t0;
  Logger::Info("TrackPackages: pronto em " + std::to_string(ms) + " ms.");
  // Avisos que não viraram linha própria (enxertos, arquivos) ficam numa só.
  if (warnings.load() > problems.load())
    BootScreenLine(BootLine::Problem, std::to_string(warnings.load() - problems.load()) +
                                          " track warning(s), see dr2hook.log");
  char secs[32];
  std::snprintf(secs, sizeof secs, "%.1f s", ms / 1000.0);
  const size_t n = g_result.tracks.size();
  BootScreenTracksDone(!g_result.cacheReady ? std::string("Custom tracks unavailable (see dr2hook.log)")
                       : n == 0             ? std::string("No custom tracks installed")
                                            : std::to_string(n) + " custom track(s) ready" +
                                                  (g_result.regenerated ? std::string(" (cache built in ") + secs + ")"
                                                                        : std::string(" (cache up to date)")));
  SetEvent(g_built);
  return 0;
}

} // namespace

bool InstallTrackPackages() {
  g_base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
  g_built = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  const MH_STATUS init = MH_Initialize();
  if (init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED) {
    g_status = "MH_Initialize falhou";
    return false;
  }
  if (!Hook(kResolveRva, kResolveBytes, sizeof(kResolveBytes), reinterpret_cast<void *>(&DetourResolve),
            reinterpret_cast<void **>(&g_origResolve))) {
    g_status = "resolucao de caminho do VFS diferente do esperado";
    return false;
  }
  const bool ready = Hook(kLocReadyRva, kLocReadyBytes, sizeof(kLocReadyBytes), reinterpret_cast<void *>(&DetourLocReady),
                          reinterpret_cast<void **>(&g_origLocReady));
  const bool name = Hook(kLocNameRva, kLocNameBytes, sizeof(kLocNameBytes), reinterpret_cast<void *>(&DetourLocName),
                         reinterpret_cast<void **>(&g_origLocName));
  g_status = ready && name ? "instalado" : "instalado sem o alias do pacote da location";
  return true;
}

void StartTrackPackages() {
  Logger::Info(std::string("TrackPackages: ") + g_status + ".");
  if (g_origResolve == nullptr) {
    if (g_built != nullptr) SetEvent(g_built);
    BootScreenTracksDone("Custom tracks disabled (see dr2hook.log)");
    return;
  }
  HANDLE t = CreateThread(nullptr, 0, BuildThread, nullptr, 0, nullptr);
  if (t != nullptr) {
    CloseHandle(t);
  } else {
    SetEvent(g_built);
    BootScreenTracksDone("Custom tracks disabled (see dr2hook.log)");
  }
}

const std::vector<std::pair<std::string, std::string>> &TrackPackageTexts() {
  static const std::vector<std::pair<std::string, std::string>> empty;
  if (g_built == nullptr || WaitForSingleObject(g_built, 30000) != WAIT_OBJECT_0 || !g_ready.load()) return empty;
  return g_result.texts;
}

void AddTrackAlias(const std::string &track, const std::string &host) {
  std::lock_guard<std::mutex> lock(g_aliasMutex);
  for (const auto &a : g_aliases)
    if (_stricmp(a.first.c_str(), track.c_str()) == 0) return;
  g_aliases.reserve(64); // os ponteiros de second são devolvidos ao jogo: sem realocar o vetor
  if (g_aliases.size() < 64) g_aliases.emplace_back(track, host);
}

} // namespace dr2hook
