#include "dr2hook/free_camera.h"
#include "dr2hook/free_camera_math.h"
#include "dr2hook/logger.h"
#include "dr2hook/ui/overlay.h"

#include <MinHook.h>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>

#if defined(_WIN32)

namespace dr2hook {
namespace {

constexpr uintptr_t kImageBase = 0x140000000;
constexpr uintptr_t kOwnerRva = 0x14168caf0 - kImageBase;
constexpr uintptr_t kOwnerVtableRva = 0x14127a030 - kImageBase;
constexpr uintptr_t kTickRva = 0x140a51ea0 - kImageBase;
constexpr uintptr_t kFlowCoordinatorRva = 0x1416951e0 - kImageBase;
constexpr uintptr_t kRunnerVtableRva = 0x141249898 - kImageBase;
constexpr uintptr_t kPauseScreenVtableRva = 0x141250b50 - kImageBase;
constexpr uint8_t kTickPrologue[] = {
    0x48, 0x89, 0x5c, 0x24, 0x08,             // mov [rsp+8], rbx
    0x48, 0x89, 0x74, 0x24, 0x10,             // mov [rsp+10h], rsi
    0x57,                                     // push rdi
    0x48, 0x81, 0xec, 0x80, 0x00, 0x00, 0x00, // sub rsp, 80h
};

constexpr uintptr_t kWorld = 0x20;
constexpr uintptr_t kCameraSlot = 0x1cf8;
constexpr uintptr_t kRow0 = 0x210;
constexpr uintptr_t kEye = 0x240;

constexpr float kSpeedSteps[] = {1.25f, 2.5f, 5.f,  10.f, 20.f,
                                 40.f,  80.f, 160.f, 320.f};
constexpr int kSpeedStepCount =
    static_cast<int>(sizeof(kSpeedSteps) / sizeof(kSpeedSteps[0]));
constexpr int kSpeedStepDefault = 4;
constexpr float kFastScale = 4.f;
constexpr float kLookRadiansPerPixel = 0.0025f;
constexpr float kMaxDt = 0.05f;

constexpr uint32_t kBitForward = 1u << 0;
constexpr uint32_t kBitBack = 1u << 1;
constexpr uint32_t kBitLeft = 1u << 2;
constexpr uint32_t kBitRight = 1u << 3;
constexpr uint32_t kBitUp = 1u << 4;
constexpr uint32_t kBitDown = 1u << 5;
constexpr uint32_t kBitFast = 1u << 6;
constexpr uint32_t kBitHold = 1u << 7;

using TickFn = void (*)(void *camera);

TickFn g_originalTick = nullptr;
void *g_tickTarget = nullptr;
uintptr_t g_gameBase = 0;

std::atomic<int> g_inFlight{0};
std::atomic<bool> g_enabled{false};
std::atomic<bool> g_hasPose{false};
std::atomic<bool> g_capture{false};
std::atomic<bool> g_seedFailed{false};
std::atomic<uint32_t> g_keys{0};
std::atomic<int> g_speedStep{kSpeedStepDefault};
std::atomic<int> g_mouseX{0};
std::atomic<int> g_mouseY{0};
std::atomic<bool> g_armMouse{false};

struct PendingLook {
  bool set = false;
  float eye[3]{};
  float target[3]{};
};
std::mutex g_pendingMutex;
PendingLook g_pending;

bool g_cursorHidden = false;
bool g_ignoreWarp = false;
FreeCamPose g_pose{};
void *g_boundCamera = nullptr;
bool g_haveDt = false;
LARGE_INTEGER g_qpcFreq{};
LARGE_INTEGER g_lastQpc{};

struct InFlight {
  InFlight() { g_inFlight.fetch_add(1, std::memory_order_relaxed); }
  ~InFlight() { g_inFlight.fetch_sub(1, std::memory_order_relaxed); }
};

bool PlausiblePointer(uintptr_t address) {
  return address > 0x10000ull && address < 0x00007fffffffffffull &&
         (address & 7ull) == 0;
}

bool ModulePointer(uintptr_t address) {
  return g_gameBase != 0 && address > g_gameBase &&
         address < g_gameBase + 0x3000000ull;
}

struct Simd4 {
  float x, y, z, w;
};

Simd4 Load(const void *camera, uintptr_t offset) {
  Simd4 value{};
  std::memcpy(&value, static_cast<const char *>(camera) + offset, sizeof(value));
  return value;
}

void Store(void *camera, uintptr_t offset, FreeCamVec3 v, float w) {
  const Simd4 value{v.x, v.y, v.z, w};
  std::memcpy(static_cast<char *>(camera) + offset, &value, sizeof(value));
}

void *StageCamera() {
  if (!ModulePointer(g_gameBase + kOwnerRva)) {
    return nullptr;
  }
  const uintptr_t owner =
      *reinterpret_cast<const uintptr_t *>(g_gameBase + kOwnerRva);
  if (!PlausiblePointer(owner)) {
    return nullptr;
  }
  const uintptr_t vtable = *reinterpret_cast<const uintptr_t *>(owner);
  if (vtable != g_gameBase + kOwnerVtableRva) {
    return nullptr;
  }
  const uintptr_t world =
      *reinterpret_cast<const uintptr_t *>(owner + kWorld);
  if (!PlausiblePointer(world)) {
    return nullptr;
  }
  const uintptr_t camera =
      *reinterpret_cast<const uintptr_t *>(world + kCameraSlot);
  if (!PlausiblePointer(camera) || !ModulePointer(
          *reinterpret_cast<const uintptr_t *>(camera))) {
    return nullptr;
  }
  return reinterpret_cast<void *>(camera);
}

float SecondsSinceLast() {
  LARGE_INTEGER now{};
  QueryPerformanceCounter(&now);
  if (!g_haveDt) {
    g_haveDt = true;
    g_lastQpc = now;
    return 0.f;
  }
  const float dt = static_cast<float>(
      static_cast<double>(now.QuadPart - g_lastQpc.QuadPart) /
      static_cast<double>(g_qpcFreq.QuadPart));
  g_lastQpc = now;
  if (!(dt > 0.f) || dt > kMaxDt) {
    return dt > kMaxDt ? kMaxDt : 0.f;
  }
  return dt;
}

void DropPose() {
  g_hasPose.store(false, std::memory_order_relaxed);
  g_capture.store(false, std::memory_order_relaxed);
  g_boundCamera = nullptr;
  g_haveDt = false;
}

bool Seed(void *camera) {
  const Simd4 rows[3] = {Load(camera, kRow0), Load(camera, kRow0 + 0x10),
                         Load(camera, kRow0 + 0x20)};
  const Simd4 eye = Load(camera, kEye);
  const FreeCamVec3 raw[3] = {{rows[0].x, rows[0].y, rows[0].z},
                              {rows[1].x, rows[1].y, rows[1].z},
                              {rows[2].x, rows[2].y, rows[2].z}};
  FreeCamPose pose{};
  if (!FreeCamPoseFromRows(raw, {eye.x, eye.y, eye.z}, eye.w, pose)) {
    return false;
  }
  g_pose = pose;
  g_hasPose.store(true, std::memory_order_relaxed);
  g_capture.store(true, std::memory_order_relaxed);
  g_haveDt = false;
  Logger::Info("FreeCamera: pose copiada da especial.");
  return true;
}

bool GamePaused();

void Apply(void *camera) {
  if (!g_enabled.load(std::memory_order_relaxed)) {
    if (g_hasPose.load(std::memory_order_relaxed)) {
      DropPose();
    }
    return;
  }
  if (g_boundCamera != camera) {
    g_hasPose.store(false, std::memory_order_relaxed);
    g_boundCamera = camera;
    g_haveDt = false;
  }
  if (!g_hasPose.load(std::memory_order_relaxed)) {
    if (!Seed(camera)) {
      g_enabled.store(false, std::memory_order_relaxed);
      g_boundCamera = nullptr;
      g_seedFailed.store(true, std::memory_order_relaxed);
      Logger::Warn("FreeCamera: a câmera da especial não tem base usável.");
      return;
    }
  }

  {
    PendingLook look;
    {
      std::lock_guard<std::mutex> lock(g_pendingMutex);
      look = g_pending;
      g_pending.set = false;
    }
    if (look.set) {
      const FreeCamVec3 eye{look.eye[0], look.eye[1], look.eye[2]};
      const FreeCamVec3 forward = FreeCamNormalize(
          {look.target[0] - eye.x, look.target[1] - eye.y, look.target[2] - eye.z});
      const FreeCamVec3 right = FreeCamNormalize(FreeCamCross({0.f, 1.f, 0.f}, forward));
      if (FreeCamLength(forward) > 0.5f && FreeCamLength(right) > 0.5f) {
        g_pose.eye = eye;
        g_pose.forward = forward;
        g_pose.right = right;
        g_pose.up = FreeCamNormalize(FreeCamCross(forward, right));
      }
    }
  }

  const bool menu = OverlayManager::IsMenuVisible();
  const bool paused = GamePaused();
  const float dt = SecondsSinceLast();
  const int dx = g_mouseX.exchange(0, std::memory_order_relaxed);
  const int dy = g_mouseY.exchange(0, std::memory_order_relaxed);
  if (menu || paused) {
    g_mouseX.store(0, std::memory_order_relaxed);
    g_mouseY.store(0, std::memory_order_relaxed);
  }
  if (!menu && !paused && (dx != 0 || dy != 0)) {
    // dx positivo é o mouse para a direita. O yaw positivo da base gira
    // para o lado oposto do que a imagem mostra.
    FreeCamLook(g_pose, -static_cast<float>(dx) * kLookRadiansPerPixel,
                static_cast<float>(dy) * kLookRadiansPerPixel);
  }
  const uint32_t keys = g_keys.load(std::memory_order_relaxed);
  const bool hold = (keys & kBitHold) != 0;
  const float axisRight =
      ((keys & kBitRight) ? 1.f : 0.f) - ((keys & kBitLeft) ? 1.f : 0.f);
  const float axisForward =
      ((keys & kBitForward) ? 1.f : 0.f) - ((keys & kBitBack) ? 1.f : 0.f);
  const float axisUp =
      ((keys & kBitUp) ? 1.f : 0.f) - ((keys & kBitDown) ? 1.f : 0.f);
  int step = g_speedStep.load(std::memory_order_relaxed);
  if (step < 0) {
    step = 0;
  } else if (step >= kSpeedStepCount) {
    step = kSpeedStepCount - 1;
  }
  const float speed =
      kSpeedSteps[step] * ((keys & kBitFast) ? kFastScale : 1.f);
  if (!menu && !paused && !hold && dt > 0.f &&
      (axisRight != 0.f || axisForward != 0.f || axisUp != 0.f)) {
    FreeCamMove(g_pose, -axisRight, axisForward, axisUp, speed * dt);
  }

  FreeCamVec3 rows[3]{};
  FreeCamWriteRows(g_pose, rows);
  const uintptr_t slots[3] = {kRow0, kRow0 + 0x10, kRow0 + 0x20};
  for (int i = 0; i < 3; ++i) {
    Store(camera, slots[i], rows[i], 0.f);
  }
  Store(camera, kEye, g_pose.eye, g_pose.eyeW);
}

// Última pose da câmera da especial (livre ou do jogo), para o LiveLink.
std::mutex g_viewMutex;
FreeCamPose g_view{};
ULONGLONG g_viewTick = 0;

void KeepView(void *camera) {
  const Simd4 rows[3] = {Load(camera, kRow0), Load(camera, kRow0 + 0x10),
                         Load(camera, kRow0 + 0x20)};
  const Simd4 eye = Load(camera, kEye);
  const FreeCamVec3 raw[3] = {{rows[0].x, rows[0].y, rows[0].z},
                              {rows[1].x, rows[1].y, rows[1].z},
                              {rows[2].x, rows[2].y, rows[2].z}};
  FreeCamPose pose{};
  if (!FreeCamPoseFromRows(raw, {eye.x, eye.y, eye.z}, eye.w, pose)) {
    return;
  }
  std::lock_guard<std::mutex> lock(g_viewMutex);
  g_view = pose;
  g_viewTick = GetTickCount64();
}

void AfterTick(void *camera) {
  void *stage = StageCamera();
  if (stage == nullptr || camera != stage) {
    return;
  }
  Apply(camera);
  KeepView(camera);
}

void DetourTick(void *camera) {
  InFlight guard;
  g_originalTick(camera);
  try {
    AfterTick(camera);
  } catch (...) {
    Logger::Error("FreeCamera: excecao ao gravar a pose.");
  }
}

// O runner do fluxo está em [coordinator+0x28]. A pilha é o vetor em
// [runner+0x38]: base em +0x10 e contagem em +0x20. O id do nó é o dword
// em +0xc, e o mapa ordenado em [registry+0x10] devolve o estado (registros
// de 16 bytes, chave em +0, ponteiro em +8). StatePauseScreen fica na pilha
// enquanto o menu de pausa, e as telas abertas a partir dele, estão ativos.
bool GamePaused() {
  if (g_gameBase == 0 || !ModulePointer(g_gameBase + kFlowCoordinatorRva)) {
    return false;
  }
  const uintptr_t coordinator =
      *reinterpret_cast<const uintptr_t *>(g_gameBase + kFlowCoordinatorRva);
  if (!PlausiblePointer(coordinator)) {
    return false;
  }
  const uintptr_t runner =
      *reinterpret_cast<const uintptr_t *>(coordinator + 0x28);
  if (!PlausiblePointer(runner) ||
      *reinterpret_cast<const uintptr_t *>(runner) !=
          g_gameBase + kRunnerVtableRva) {
    return false;
  }
  const uintptr_t registry =
      *reinterpret_cast<const uintptr_t *>(runner + 0x18);
  const uintptr_t stack =
      *reinterpret_cast<const uintptr_t *>(runner + 0x38);
  if (!PlausiblePointer(registry) || !PlausiblePointer(stack)) {
    return false;
  }
  const uintptr_t map = *reinterpret_cast<const uintptr_t *>(registry + 0x10);
  if (!PlausiblePointer(map)) {
    return false;
  }
  const uintptr_t begin = *reinterpret_cast<const uintptr_t *>(map + 0x8);
  const uintptr_t end = *reinterpret_cast<const uintptr_t *>(map + 0x10);
  if (!PlausiblePointer(begin) || end < begin || (end - begin) % 16 != 0 ||
      (end - begin) / 16 > 4096) {
    return false;
  }
  const uintptr_t nodes = *reinterpret_cast<const uintptr_t *>(stack + 0x10);
  const uint64_t count = *reinterpret_cast<const uint64_t *>(stack + 0x20);
  if (count == 0 || count > 32 || !PlausiblePointer(nodes)) {
    return false;
  }
  const uintptr_t pauseVtable = g_gameBase + kPauseScreenVtableRva;
  for (uint64_t i = 0; i < count; ++i) {
    const uintptr_t node =
        *reinterpret_cast<const uintptr_t *>(nodes + i * sizeof(uintptr_t));
    if (!PlausiblePointer(node)) {
      continue;
    }
    const uint32_t key = *reinterpret_cast<const uint32_t *>(node + 0xc);
    uintptr_t lo = begin;
    uintptr_t hi = end;
    while (lo < hi) {
      const uintptr_t mid = lo + ((hi - lo) / 32) * 16;
      const uint32_t midKey = *reinterpret_cast<const uint32_t *>(mid);
      if (midKey < key) {
        lo = mid + 16;
      } else {
        hi = mid;
      }
    }
    if (lo >= end || *reinterpret_cast<const uint32_t *>(lo) != key) {
      continue;
    }
    const uintptr_t state = *reinterpret_cast<const uintptr_t *>(lo + 8);
    if (PlausiblePointer(state) &&
        *reinterpret_cast<const uintptr_t *>(state) == pauseVtable) {
      return true;
    }
  }
  return false;
}

bool WantCapture() {
  return g_capture.load(std::memory_order_relaxed) &&
         !OverlayManager::IsMenuVisible() && !GamePaused();
}

uint32_t BitForKey(UINT vk) {
  switch (vk) {
  case 0x57:
    return kBitForward; // W
  case 0x53:
    return kBitBack; // S
  case 0x41:
    return kBitLeft; // A
  case 0x44:
    return kBitRight; // D
  case VK_SPACE:
    return kBitUp;
  case 0x51:
    return kBitDown; // Q
  case VK_CONTROL:
    return kBitHold;
  case VK_SHIFT:
    return kBitFast;
  default:
    return 0;
  }
}

int SpeedDelta(UINT vk) {
  if (vk == VK_OEM_PLUS || vk == VK_ADD) {
    return 1;
  }
  if (vk == VK_OEM_MINUS || vk == VK_SUBTRACT) {
    return -1;
  }
  return 0;
}

void AdjustKeyboardSpeed(int delta) {
  int step = g_speedStep.load(std::memory_order_relaxed);
  for (;;) {
    int next = step + delta;
    if (next < 0) {
      next = 0;
    } else if (next >= kSpeedStepCount) {
      next = kSpeedStepCount - 1;
    }
    if (next == step) {
      return;
    }
    if (g_speedStep.compare_exchange_weak(step, next,
                                          std::memory_order_relaxed)) {
      char text[64];
      std::snprintf(text, sizeof(text), "Keyboard speed: %.3g m/s",
                    static_cast<double>(kSpeedSteps[next]));
      if (OverlayManager::IsInitialized()) {
        OverlayManager::AddNotification(text, 1.5f, ToastType::Info);
      }
      Logger::Info(std::string("FreeCamera: ") + text);
      return;
    }
  }
}

void SetKey(uint32_t bit, bool down) {
  uint32_t keys = g_keys.load(std::memory_order_relaxed);
  for (;;) {
    const uint32_t next = down ? (keys | bit) : (keys & ~bit);
    if (g_keys.compare_exchange_weak(keys, next, std::memory_order_relaxed)) {
      return;
    }
  }
}

} // namespace

bool FreeCamera::Install(uintptr_t gameBase) {
  if (gameBase == 0) {
    return false;
  }
  g_gameBase = gameBase;
  QueryPerformanceFrequency(&g_qpcFreq);
  const MH_STATUS init = MH_Initialize();
  if (init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED) {
    Logger::Error("FreeCamera: MH_Initialize falhou.");
    return false;
  }
  void *fn = reinterpret_cast<void *>(gameBase + kTickRva);
  if (std::memcmp(fn, kTickPrologue, sizeof(kTickPrologue)) != 0) {
    Logger::Warn("FreeCamera: prologo do tick diferente; hook abortado.");
    return false;
  }
  if (MH_CreateHook(fn, reinterpret_cast<void *>(&DetourTick),
                    reinterpret_cast<void **>(&g_originalTick)) != MH_OK ||
      MH_EnableHook(fn) != MH_OK) {
    MH_RemoveHook(fn);
    g_originalTick = nullptr;
    Logger::Error("FreeCamera: falha ao instalar o hook do tick.");
    return false;
  }
  g_tickTarget = fn;
  Logger::Info("FreeCamera: hook do tick ativo. F9 liga a câmera livre.");
  return true;
}

void FreeCamera::Shutdown() {
  if (g_tickTarget != nullptr) {
    MH_DisableHook(g_tickTarget);
  }
  for (int waited = 0; g_inFlight.load() > 0 && waited < 2000; ++waited) {
    Sleep(1);
  }
  if (g_tickTarget != nullptr) {
    MH_RemoveHook(g_tickTarget);
    g_tickTarget = nullptr;
  }
  g_originalTick = nullptr;
  g_enabled.store(false, std::memory_order_relaxed);
  DropPose();
  g_keys.store(0, std::memory_order_relaxed);
  if (g_cursorHidden) {
    ShowCursor(TRUE);
    g_cursorHidden = false;
  }
}

std::string FreeCamera::RemoteLookAt(float ex, float ey, float ez, float tx, float ty, float tz) {
  if (!g_enabled.load(std::memory_order_relaxed)) {
    return "camera livre desligada (key f9)";
  }
  std::lock_guard<std::mutex> lock(g_pendingMutex);
  g_pending.set = true;
  g_pending.eye[0] = ex;
  g_pending.eye[1] = ey;
  g_pending.eye[2] = ez;
  g_pending.target[0] = tx;
  g_pending.target[1] = ty;
  g_pending.target[2] = tz;
  return "ok";
}

std::string FreeCamera::RemotePose() {
  if (!g_enabled.load(std::memory_order_relaxed) || !g_hasPose.load(std::memory_order_relaxed)) {
    return "camera livre desligada (key f9)";
  }
  char text[160];
  std::snprintf(text, sizeof(text), "olho %.2f %.2f %.2f  frente %.3f %.3f %.3f", static_cast<double>(g_pose.eye.x),
                static_cast<double>(g_pose.eye.y), static_cast<double>(g_pose.eye.z), static_cast<double>(g_pose.forward.x),
                static_cast<double>(g_pose.forward.y), static_cast<double>(g_pose.forward.z));
  return text;
}

bool FreeCamera::StageView(float eye[3], float forward[3], float up[3]) {
  std::lock_guard<std::mutex> lock(g_viewMutex);
  if (g_viewTick == 0 || GetTickCount64() - g_viewTick > 500) {
    return false;
  }
  const FreeCamVec3 src[3] = {g_view.eye, g_view.forward, g_view.up};
  float *dst[3] = {eye, forward, up};
  for (int i = 0; i < 3; ++i) {
    dst[i][0] = src[i].x;
    dst[i][1] = src[i].y;
    dst[i][2] = src[i].z;
  }
  return true;
}

void FreeCamera::OnFrame(HWND hwnd) {
  if (g_seedFailed.exchange(false, std::memory_order_relaxed) &&
      OverlayManager::IsInitialized()) {
    OverlayManager::AddNotification(
        "Free camera needs the stage camera. Enter a stage, then press F9.",
        4.0f, ToastType::Warning);
  }
  const bool hide = WantCapture();
  if (hide != g_cursorHidden) {
    ShowCursor(hide ? FALSE : TRUE);
    g_cursorHidden = hide;
    if (hide) {
      g_armMouse.store(true, std::memory_order_relaxed);
    } else {
      g_ignoreWarp = false;
    }
  }
  (void)hwnd;
}

bool FreeCamera::Enabled() { return g_enabled.load(std::memory_order_relaxed); }

int FreeCamera::OnWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  if ((msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) && wParam == VK_F9 &&
      (lParam & (1 << 30)) == 0) {
    const bool enable = !g_enabled.load(std::memory_order_relaxed);
    g_keys.store(0, std::memory_order_relaxed);
    g_mouseX.store(0, std::memory_order_relaxed);
    g_mouseY.store(0, std::memory_order_relaxed);
    if (!enable) {
      g_capture.store(false, std::memory_order_relaxed);
    }
    g_enabled.store(enable, std::memory_order_relaxed);
    if (OverlayManager::IsInitialized()) {
      OverlayManager::AddNotification(
          enable ? "Free camera on. Mouse looks. WASD moves, Space/Q up/down. "
                   "Hold Ctrl to freeze the keyboard. + and - set the "
                   "keyboard speed. F9 toggles."
                 : "Free camera off.",
          4.0f, ToastType::Info);
    }
    Logger::Info(enable ? "FreeCamera: ligada." : "FreeCamera: desligada.");
    return 1;
  }

  if (!WantCapture()) {
    if (msg == WM_KEYUP || msg == WM_SYSKEYUP) {
      if (const uint32_t bit = BitForKey(static_cast<UINT>(wParam))) {
        SetKey(bit, false);
      }
    }
    return 0;
  }

  if (msg == WM_MOUSEMOVE) {
    if (g_ignoreWarp) {
      g_ignoreWarp = false;
      return 1;
    }
    const bool arm = g_armMouse.exchange(false, std::memory_order_relaxed);
    RECT rect{};
    if (!GetClientRect(hwnd, &rect)) {
      return 1;
    }
    const int cx = (rect.left + rect.right) / 2;
    const int cy = (rect.top + rect.bottom) / 2;
    const int x = static_cast<short>(LOWORD(lParam));
    const int y = static_cast<short>(HIWORD(lParam));
    if (!arm) {
      g_mouseX.fetch_add(x - cx, std::memory_order_relaxed);
      g_mouseY.fetch_add(y - cy, std::memory_order_relaxed);
    }
    if (x != cx || y != cy) {
      POINT screen{cx, cy};
      if (ClientToScreen(hwnd, &screen)) {
        g_ignoreWarp = true;
        SetCursorPos(screen.x, screen.y);
      }
    }
    return 1;
  }

  if (msg == WM_KEYDOWN || msg == WM_KEYUP || msg == WM_SYSKEYDOWN ||
      msg == WM_SYSKEYUP) {
    const UINT vk = static_cast<UINT>(wParam);
    const bool down = msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN;
    if (down) {
      if (const int delta = SpeedDelta(vk)) {
        AdjustKeyboardSpeed(delta);
        return 1;
      }
    } else if (SpeedDelta(vk) != 0) {
      return 1;
    }
    if (const uint32_t bit = BitForKey(vk)) {
      SetKey(bit, down);
      return 1;
    }
  }
  return 0;
}

} // namespace dr2hook

#else

namespace dr2hook {

bool FreeCamera::Install(uintptr_t) { return false; }
void FreeCamera::Shutdown() {}
void FreeCamera::OnFrame(HWND) {}
int FreeCamera::OnWndProc(HWND, UINT, WPARAM, LPARAM) { return 0; }
bool FreeCamera::Enabled() { return false; }

} // namespace dr2hook

#endif
