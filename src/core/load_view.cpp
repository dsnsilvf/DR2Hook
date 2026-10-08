#include "dr2hook/load_view.h"
#include "dr2hook/free_camera_math.h"
#include "dr2hook/logger.h"

#include "MinHook.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <map>
#include <set>
#include <mutex>
#include <string>
#include <vector>

namespace dr2hook {
// ghost_lab.cpp: watchpoint de hardware de escrita em todas as threads (VEH do GhostLab loga).
void GhostLabArmWriteWatch(const std::vector<uintptr_t> &addrs, const std::vector<int> &lens);
void GhostLabDumpThreads();
namespace {

// Índices na vtable do ID3D11DeviceContext (d3d11.h).
constexpr int kDrawIndexed = 12;
constexpr int kDraw = 13;
constexpr int kDrawIndexedInstanced = 20;
constexpr int kDrawInstanced = 21;
constexpr int kOMSetRenderTargets = 33;
constexpr int kOMSetRenderTargetsAndUAVs = 34;
constexpr int kDrawIndexedInstancedIndirect = 39;
constexpr int kDrawInstancedIndirect = 40;
constexpr int kDispatch = 41;
constexpr int kDispatchIndirect = 42;
constexpr int kClearDepthStencilView = 53;
constexpr int kExecuteCommandList = 58;

constexpr ULONGLONG kTailMs = 3000;
// A cena só vai para o fundo se for um alvo grande, com draws de mundo, por
// alguns quadros seguidos (a interface da carga usa alvos de 480x270 com
// poucos draws, que piscavam esticados na tela).
constexpr uint32_t kSceneMinDraws = 50;
constexpr int kSceneStreak = 3;

// Um alvo em que o jogo desenhou neste quadro (com referência até o fim do
// quadro, para o ponteiro não morrer na mão).
struct Target {
  ID3D11Resource *res = nullptr;
  UINT width = 0;
  UINT height = 0;
  DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
  UINT bind = 0;
  UINT samples = 1;
  bool depth = false; // só profundidade (sombras, pré-passo)
  std::atomic<uint32_t> draws{0};
};

// Soma do segundo, por alvo.
struct Tally {
  ID3D11Resource *res;
  UINT width, height;
  DXGI_FORMAT format;
  bool depth;
  bool backbuffer;
  uint64_t draws;
  UINT samples;
};

ID3D11DeviceContext *g_context = nullptr;
std::vector<void *> g_targets; // funções enganchadas
std::atomic<int> g_inFlight{0};
std::atomic<bool> g_measure{false};
std::atomic<bool> g_own{false};
bool g_active = false;
ULONGLONG g_tailUntil = 0;

std::mutex g_mutex;
std::array<Target, 48> g_frame;
int g_frameCount = 0;
std::atomic<int> g_current{-1};
std::atomic<uint32_t> g_unbound{0};  // draws sem alvo conhecido
std::atomic<uint32_t> g_deferred{0}; // draws de contextos adiados
std::atomic<uint32_t> g_lists{0};    // ExecuteCommandList

// Segundo corrente.
std::vector<Tally> g_tally;
uint32_t g_secFrames = 0;
uint64_t g_secDraws = 0;
uint64_t g_secDeferred = 0;
uint64_t g_secLists = 0;
ULONGLONG g_secStart = 0;
std::string g_status;

// Cena para o fundo do terminal.
ID3D11Resource *g_scene = nullptr; // com ref
float g_sceneAspect = 1.0f;
// Com MSAA (o padrão do jogo é 4x) o alvo da cena não pode ser lido direto:
// cada quadro ele é resolvido numa textura nossa de 1 amostra.
struct View {
  ID3D11Resource *res; // com ref
  ID3D11ShaderResourceView *srv;
  ID3D11Texture2D *resolved = nullptr;
  DXGI_FORMAT resolveFormat = DXGI_FORMAT_UNKNOWN;
};
std::vector<View> g_views;
ID3D11ShaderResourceView *g_sceneView = nullptr;
// Exposição do fundo HDR (r11g11b10f/rgba16f: luz linear, sem a curva que o
// jogo aplica no pós-processo). A cada ~0,25 s a cena resolvida vai para uma
// textura de leitura e a média log da luminância dos pixels acesos (o vazio em
// volta da pista é preto) dá a exposição, como o "key" do Reinhard.
ID3D11Texture2D *g_hdrStaging = nullptr;
ID3D11Resource *g_hdrSource = nullptr; // só o ponteiro, para comparar
bool g_hdrPending = false;
int g_hdrTick = 0;
float g_exposure = 0.0f; // 0 = sem curva (alvo LDR ou ainda sem medida)
std::string g_hdrNote;
// "despejar_hdr=<prefixo>": cada medida do HDR vira <prefixo>_NNN_{carga,corrida}.ppm (480x270, tonemap).
std::string g_hdrDump;
int g_hdrDumpN = 0;
ID3D11Resource *g_lastCandidate = nullptr; // só o ponteiro, para comparar
int g_streak = 0;
bool g_sceneLogged = false;

// Pilhas dos draws: quem, no exe, desenha em cada alvo (na carga e na
// corrida). De tanto em tanto quadro, o 1º draw de cada alvo e um a cada 64
// guardam a pilha; o resumo sai no log quando a medição acaba
// ("LoadView[pilha]"), no formato da LoadProbe (`exe_re.py stack`).
constexpr int kStackFrames = 24;
constexpr uint32_t kSampleEvery = 15; // quadros
constexpr std::size_t kMaxStacks = 400;
struct StackSeen {
  std::array<uint32_t, kStackFrames> rva{};
  int count = 0;
  char target[48] = {};
  bool load = false;
  DWORD thread = 0;
  uint32_t hits = 0;
};
std::mutex g_stackMutex;
std::vector<StackSeen> g_stacks;
std::atomic<bool> g_sampleFrame{false};
uint32_t g_frameNo = 0;
uintptr_t g_exeBase = 0;
uintptr_t g_exeEnd = 0;
void SampleStack(const Target *t, const char *label);

// Quadro da corrida (0x1404b1990, rcx = renderer; [renderer+0x2050] = cena
// "main game"). Ele escolhe o caminho pelo modo em +0x22a8: 0 desenha o mundo
// (sondas IBL, câmera, mapas de ambiente e a cena); 1 e 2 só chamam a cena
// (vt+0xa8), que na carga sai só com o pós-processo. Também grava
// [[renderer+0x38]+0x16308] = (modo == 0). Aqui só se loga cada mudança.
constexpr uintptr_t kRaceFrameRva = 0x4b1990;
constexpr uintptr_t kSkipCheckRva = 0x1d2b30; // bool(obj): pula a cena
constexpr uint8_t kRaceFramePrologue[] = {0x48, 0x89, 0x5c, 0x24, 0x20, 0x55, 0x56, 0x57, 0x41, 0x54};
using RaceFrameFn = void(__fastcall *)(void *, void *);
using SkipCheckFn = bool(__fastcall *)(void *);
RaceFrameFn g_raceFrame = nullptr;
// Experimento (RE): com dr2hook_loadview.ini contendo "forcar_mundo_ms=N", na
// tela preta o quadro em modo 2 (carga) roda como modo 0 (mundo) depois de N ms
// em modo 2; o modo volta a 2 logo depois do quadro.
int g_forceAfterMs = -1;
bool g_forcePos = false;
bool g_forceLook = false;
// "fov=<graus>": projeção forçada junto com "olhar=" (a da corrida é 55).
float g_lookFov = 0.f;
float g_lookEye[3] = {}, g_lookTarget[3] = {};
// "olhar=auto": pose da largada por rota, em dr2hook_olhar.ini ("<pista>_route_N=olho,alvo,fov").
// A rota vem da proxy, da tela de carregamento ("<pista>_route_N", NoteRoute); a pose é a
// câmera da especial no começo da corrida, gravada em toda carga com a tela preta.
bool g_lookAuto = false;
std::mutex g_routeMutex;
std::string g_routeKey;
bool g_routeSaved = false;
float g_forcePosXYZ[3] = {};
ULONGLONG g_mode2Since = 0;
uint32_t g_forcedFrames = 0;
bool g_forceVis = false;
uint32_t g_forcedVis = 0;
// "manter_cena=1": o modo da cena ([cena+0x1e98]) fica em 0 entre os quadros
// forçados; o cull dos objetos (0x14039ccb0) roda fora do quadro e sai cedo
// com o modo 1/2 da carga.
bool g_keepSceneMode = false;

// Pose do "olhar=" no formato de fonte de câmera do jogo: linhas cima, direita, frente, olho.
void LookRows(float (&rows)[16]) {
  const FreeCamVec3 eye{g_lookEye[0], g_lookEye[1], g_lookEye[2]};
  const FreeCamVec3 fwd = FreeCamNormalize({g_lookTarget[0] - eye.x, g_lookTarget[1] - eye.y, g_lookTarget[2] - eye.z});
  const FreeCamVec3 right = FreeCamNormalize(FreeCamCross({0.f, 1.f, 0.f}, fwd));
  const FreeCamVec3 up = FreeCamNormalize(FreeCamCross(fwd, right));
  const float r[16] = {up.x,  up.y,  up.z,  0.f, right.x, right.y, right.z, 0.f,
                       fwd.x, fwd.y, fwd.z, 0.f, eye.x,   eye.y,   eye.z,   1.f};
  std::memcpy(rows, r, sizeof rows);
}

// Câmera da especial ([[exe+0x168caf0]+0x20]+0x1cf8, a da FreeCamera): na
// carga fica na origem; com "olhar=" recebe a pose do ini até RestoreLook.
uint8_t *StageCam() {
  const auto owner = *reinterpret_cast<const uintptr_t *>(g_exeBase + 0x168caf0);
  if (owner > 0x10000 && *reinterpret_cast<const uintptr_t *>(owner) == g_exeBase + 0x127a030) {
    const auto world = *reinterpret_cast<const uintptr_t *>(owner + 0x20);
    if (world > 0x10000) {
      return *reinterpret_cast<uint8_t **>(world + 0x1cf8);
    }
  }
  return nullptr;
}
uint8_t *ApplyLook(uint8_t (&saved)[0x40]) {
  if (!g_forceLook) {
    return nullptr;
  }
  uint8_t *stage = StageCam();
  if (stage != nullptr) {
    float rows[16];
    LookRows(rows);
    std::memcpy(saved, stage + 0x210, sizeof saved);
    std::memcpy(stage + 0x210, rows, sizeof rows);
  }
  return stage;
}
// Cena do último quadro da corrida/carga ([renderer+0x2050]).
std::atomic<uint8_t *> g_lastScene{nullptr};

// dr2hook_olhar.ini: uma linha por rota, "chave=ox,oy,oz,ax,ay,az,fov".
constexpr const char *kLookFile = "dr2hook_olhar.ini";

void LoadSavedLook(const std::string &key) {
  std::ifstream in(kLookFile);
  for (std::string l; std::getline(in, l);) {
    float v[7] = {};
    if (l.size() > key.size() && l.compare(0, key.size(), key) == 0 && l[key.size()] == '=' &&
        std::sscanf(l.c_str() + key.size() + 1, "%f,%f,%f,%f,%f,%f,%f", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5],
                    &v[6]) >= 6) {
      std::memcpy(g_lookEye, v, sizeof g_lookEye);
      std::memcpy(g_lookTarget, v + 3, sizeof g_lookTarget);
      if (v[6] > 0.f) {
        g_lookFov = v[6];
      }
      g_forceLook = true;
      Logger::Info("LoadView: olhar automatico de " + key + ": " + l.substr(key.size() + 1) + ".");
      return;
    }
  }
  Logger::Info("LoadView: olhar automatico sem pose salva para " + key + " (a largada desta carga grava).");
}

// No começo da corrida: a câmera da especial (linhas cima, direita, frente, olho) e o fov da câmera da
// cena vão para o dr2hook_olhar.ini, na chave da rota desta carga.
void SaveStartLook() {
  std::string key;
  {
    std::lock_guard<std::mutex> lock(g_routeMutex);
    if (g_routeSaved || g_routeKey.empty()) {
      return;
    }
    key = g_routeKey;
  }
  const uint8_t *stage = StageCam();
  if (stage == nullptr) {
    return;
  }
  float r[16];
  std::memcpy(r, stage + 0x210, sizeof r);
  const float *up = r, *fwd = r + 8, *eye = r + 12;
  const float fl = std::sqrt(fwd[0] * fwd[0] + fwd[1] * fwd[1] + fwd[2] * fwd[2]);
  if (std::fabs(eye[0]) + std::fabs(eye[1]) + std::fabs(eye[2]) < 1.f || up[1] < 0.5f || fl < 0.9f || fl > 1.1f) {
    return; // ainda na origem (o carro nasce ~1 s antes da largada)
  }
  float fov = 0.f;
  if (uint8_t *scene = g_lastScene.load(std::memory_order_relaxed)) {
    const auto *cam = *reinterpret_cast<uint8_t *const *>(scene + 0x17c0);
    if (cam != nullptr && *reinterpret_cast<const uintptr_t *>(cam) == g_exeBase + 0x138cd38) {
      std::memcpy(&fov, cam + 0x124, 4);
      if (!(fov > 10.f && fov < 150.f)) {
        fov = 0.f;
      }
    }
  }
  char value[200];
  std::snprintf(value, sizeof value, "%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.1f", eye[0], eye[1], eye[2],
                eye[0] + fwd[0] * 10.f, eye[1] + fwd[1] * 10.f, eye[2] + fwd[2] * 10.f, fov);
  std::vector<std::string> lines;
  {
    std::ifstream in(kLookFile);
    for (std::string l; std::getline(in, l);) {
      if (!l.empty() && l.back() == '\r') {
        l.pop_back();
      }
      if (!(l.size() > key.size() && l.compare(0, key.size(), key) == 0 && l[key.size()] == '=')) {
        lines.push_back(l);
      }
    }
  }
  if (lines.empty()) {
    lines.push_back("; DR2 Hook: pose da largada por rota (olho x,y,z, alvo x,y,z, fov), gravada pela LoadView.");
  }
  lines.push_back(key + "=" + value);
  const std::string tmp = std::string(kLookFile) + ".tmp";
  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    for (const auto &l : lines) {
      out << l << "\r\n";
    }
  }
  if (!MoveFileExA(tmp.c_str(), kLookFile, MOVEFILE_REPLACE_EXISTING)) {
    Logger::Warn("LoadView: nao consegui gravar o dr2hook_olhar.ini.");
  } else {
    Logger::Info("LoadView: pose da largada de " + key + " salva: " + value + ".");
  }
  std::lock_guard<std::mutex> lock(g_routeMutex);
  g_routeSaved = true;
}

// Câmera da cena ([cena+0x17c0], vtable exe+0x138cd38): é dela que o quadro
// copia a pose para a câmera de render ([cena+0x38], via 6a2ad0). Na corrida o
// diretor de câmeras (9d9b80, dentro do preparo) a escreve a partir da câmera
// do carro; na carga não há carro e ela fica parada. Layout: +0x60 e +0xa0 =
// mundo (direita, cima, trás, olho), +0x150 = projeção, +0x190 = vista,
// +0x1d0 = vista x projeção. Com "olhar=" recebe a pose do ini.
bool g_sceneCamLogged = false;
// Cena do quadro forçado em curso (preparo ou quadro da corrida), para o
// hook do diretor de câmera; nullptr fora deles.
std::atomic<uint8_t *> g_forcedScene{nullptr};
void ApplySceneCam(uint8_t *scene) {
  if (!g_forceLook || scene == nullptr) {
    return;
  }
  auto *cam = *reinterpret_cast<uint8_t **>(scene + 0x17c0);
  const bool ok = cam != nullptr && *reinterpret_cast<const uintptr_t *>(cam) == g_exeBase + 0x138cd38;
  if (!g_sceneCamLogged) {
    g_sceneCamLogged = true;
    char line[256];
    if (ok) {
      const auto *f = reinterpret_cast<const float *>(cam);
      std::snprintf(line, sizeof line,
                    "LoadView[camcena]: %p olho [%.1f %.1f %.1f] proj [%.3f %.3f %.3f %.3f] fov=%.1f perto=%.2f "
                    "longe=%.0f aspecto=%.3f",
                    static_cast<void *>(cam), f[0xd0 / 4], f[0xd4 / 4], f[0xd8 / 4], f[0x150 / 4], f[0x164 / 4],
                    f[0x178 / 4], f[0x188 / 4], f[0x124 / 4], f[0x128 / 4], f[0x12c / 4], f[0x130 / 4]);
    } else {
      std::snprintf(line, sizeof line, "LoadView[camcena]: sem camera da cena (%p).", static_cast<void *>(cam));
    }
    Logger::Info(line);
  }
  if (!ok) {
    return;
  }
  const FreeCamVec3 eye{g_lookEye[0], g_lookEye[1], g_lookEye[2]};
  const FreeCamVec3 back =
      FreeCamNormalize({eye.x - g_lookTarget[0], eye.y - g_lookTarget[1], eye.z - g_lookTarget[2]});
  const FreeCamVec3 right = FreeCamNormalize(FreeCamCross({0.f, 1.f, 0.f}, back));
  const FreeCamVec3 up = FreeCamCross(back, right);
  const float world[16] = {right.x, right.y, right.z, 0.f, up.x,  up.y,  up.z,  0.f,
                           back.x,  back.y,  back.z,  0.f, eye.x, eye.y, eye.z, 1.f};
  std::memcpy(cam + 0x60, world, sizeof world);
  std::memcpy(cam + 0xa0, world, sizeof world);
  const auto dot = [](const FreeCamVec3 &a, const FreeCamVec3 &b) { return a.x * b.x + a.y * b.y + a.z * b.z; };
  float view[4][4] = {{right.x, up.x, back.x, 0.f},
                      {right.y, up.y, back.y, 0.f},
                      {right.z, up.z, back.z, 0.f},
                      {-dot(eye, right), -dot(eye, up), -dot(eye, back), 1.f}};
  std::memcpy(cam + 0x190, view, sizeof view);
  float proj[4][4];
  std::memcpy(proj, cam + 0x150, sizeof proj);
  if (g_lookFov > 0.f) {
    // +0x124 = fov em graus (+0x128 perto, +0x12c longe, +0x130 aspecto). O diretor refaz a projeção a
    // partir dele antes de enviar o CB 3 do pré-passe; uma projeção montada aqui à mão não bateria com a
    // dele (perto/longe) e o teste de profundidade "igual" do passe de cor falharia.
    std::memcpy(cam + 0x124, &g_lookFov, 4);
  }
  if (proj[0][0] == 0.f || proj[1][1] == 0.f) {
    // Projeção do jogo na corrida: fov 55, perto 0,2, 16:9.
    const float p11 = 1.f / std::tan((g_lookFov > 0.f ? g_lookFov : 55.f) * 0.5f * 3.14159265f / 180.f);
    const float rows[4][4] = {
        {p11 / 1.7778f, 0.f, 0.f, 0.f}, {0.f, p11, 0.f, 0.f}, {0.f, 0.f, -1.f, -1.f}, {0.f, 0.f, -0.4f, 0.f}};
    std::memcpy(proj, rows, sizeof proj);
    std::memcpy(cam + 0x150, proj, sizeof proj);
  }
  float vp[4][4];
  for (int i = 0; i < 4; ++i) {
    for (int j = 0; j < 4; ++j) {
      vp[i][j] = view[i][0] * proj[0][j] + view[i][1] * proj[1][j] + view[i][2] * proj[2][j] +
                 view[i][3] * proj[3][j];
    }
  }
  std::memcpy(cam + 0x1d0, vp, sizeof vp);
  // Fator de LOD do culler ([cena+0x14e0]+0xf0, copiado por 3c7aa0): o fov da
  // cena em radianos. Começa em pi/2 (393c3c); na corrida vale o fov (55°).
  *reinterpret_cast<float *>(scene + 0x1830) = (g_lookFov > 0.f ? g_lookFov : 55.f) * 3.14159265f / 180.f;
}
void *g_raceFrameTarget = nullptr;
struct RenderState {
  uint32_t mode = ~0u;
  uint32_t s2310 = 0, s2314 = 0, s23b0 = 0;
  int skip = -1;
  int world = -1;
  bool operator!=(const RenderState &o) const {
    return mode != o.mode || s2310 != o.s2310 || s2314 != o.s2314 || s23b0 != o.s23b0 || skip != o.skip ||
           world != o.world;
  }
};
RenderState g_lastState;

using DrawIndexedFn = void(STDMETHODCALLTYPE *)(ID3D11DeviceContext *, UINT, UINT, INT);
using DrawFn = void(STDMETHODCALLTYPE *)(ID3D11DeviceContext *, UINT, UINT);
using DrawIndexedInstancedFn = void(STDMETHODCALLTYPE *)(ID3D11DeviceContext *, UINT, UINT, UINT, INT, UINT);
using DrawInstancedFn = void(STDMETHODCALLTYPE *)(ID3D11DeviceContext *, UINT, UINT, UINT, UINT);
using DrawIndirectFn = void(STDMETHODCALLTYPE *)(ID3D11DeviceContext *, ID3D11Buffer *, UINT);
using OMSetRTFn = void(STDMETHODCALLTYPE *)(ID3D11DeviceContext *, UINT, ID3D11RenderTargetView *const *,
                                            ID3D11DepthStencilView *);
using OMSetRTUAVFn = void(STDMETHODCALLTYPE *)(ID3D11DeviceContext *, UINT, ID3D11RenderTargetView *const *,
                                               ID3D11DepthStencilView *, UINT, UINT,
                                               ID3D11UnorderedAccessView *const *, const UINT *);
using ExecuteFn = void(STDMETHODCALLTYPE *)(ID3D11DeviceContext *, ID3D11CommandList *, BOOL);

DrawIndexedFn g_drawIndexed = nullptr;
DrawFn g_draw = nullptr;
DrawIndexedInstancedFn g_drawIndexedInstanced = nullptr;
DrawInstancedFn g_drawInstanced = nullptr;
DrawIndirectFn g_drawIndexedIndirect = nullptr;
DrawIndirectFn g_drawIndirect = nullptr;
OMSetRTFn g_setRT = nullptr;
OMSetRTUAVFn g_setRTUAV = nullptr;
ExecuteFn g_execute = nullptr;
using DispatchFn = void(STDMETHODCALLTYPE *)(ID3D11DeviceContext *, UINT, UINT, UINT);
using DispatchIndirectFn = void(STDMETHODCALLTYPE *)(ID3D11DeviceContext *, ID3D11Buffer *, UINT);
using ClearDsvFn = void(STDMETHODCALLTYPE *)(ID3D11DeviceContext *, ID3D11DepthStencilView *, UINT, FLOAT, UINT8);
DispatchFn g_dispatch = nullptr;
DispatchIndirectFn g_dispatchIndirect = nullptr;
ClearDsvFn g_clearDsv = nullptr;

struct InFlight {
  InFlight() { g_inFlight.fetch_add(1, std::memory_order_relaxed); }
  ~InFlight() { g_inFlight.fetch_sub(1, std::memory_order_relaxed); }
};

// Experimento "sonda_cb=<pasta>": a cada ~0,5 s copia os constant buffers do
// vertex shader de dois draws do alvo grande da cena (o 11º e o 151º do
// quadro), junto com a câmera da cena ([cena+0x17c0]) e a de render
// ([cena+0x38]), e grava tudo em <pasta>\cb_NNNN.bin. Serve para ver se a
// pose que pomos na carga chega à GPU e comparar com a da corrida.
std::string g_cbDir;
std::atomic<bool> g_cbWant{false};
// "rastrear_geo=1" (ver GeoDraw).
bool g_traceGeo = false;
// "sonda_ps=1" (com sonda_cb): os tiros passam a ser os CBs do pixel shader do 1º draw de objeto de stride 24
// (tiro 0) e 64 (tiro 1) do quadro.
bool g_probePs = false;
constexpr int kCbSlots = 8;
constexpr int kCbShots = 2;
constexpr UINT kCbBytes = 4096;
struct CbShot {
  ID3D11Buffer *staging[kCbSlots] = {};
  UINT bytes[kCbSlots] = {};
  void *shader = nullptr;
  uint32_t drawNo = 0;
  uint32_t verts = 0; // com rastrear_geo: vértices do draw (o tiro 1 é o 1º com >= 20 mil)
  bool taken = false;
};
CbShot g_cbShots[kCbShots];
uint8_t g_cbCams[0x400];
uint32_t g_cbFile = 0;

void CbCapture(ID3D11DeviceContext *ctx, int shot, uint32_t drawNo, bool ps = false) {
  CbShot &c = g_cbShots[shot];
  ID3D11Buffer *bufs[kCbSlots] = {};
  if (ps) {
    ctx->PSGetConstantBuffers(0, kCbSlots, bufs);
    ID3D11PixelShader *sh = nullptr;
    ctx->PSGetShader(&sh, nullptr, nullptr);
    c.shader = sh;
    if (sh != nullptr) {
      sh->Release();
    }
  } else {
    ID3D11VertexShader *vs = nullptr;
    ctx->VSGetShader(&vs, nullptr, nullptr);
    c.shader = vs;
    if (vs != nullptr) {
      vs->Release();
    }
  }
  ID3D11Device *dev = nullptr;
  ctx->GetDevice(&dev);
  for (int i = 0; i < kCbSlots; ++i) {
    c.bytes[i] = 0;
    if (bufs[i] == nullptr) {
      continue;
    }
    D3D11_BUFFER_DESC d{};
    bufs[i]->GetDesc(&d);
    if (c.staging[i] == nullptr && dev != nullptr) {
      D3D11_BUFFER_DESC sd{};
      sd.ByteWidth = kCbBytes;
      sd.Usage = D3D11_USAGE_STAGING;
      sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
      dev->CreateBuffer(&sd, nullptr, &c.staging[i]);
    }
    if (c.staging[i] != nullptr) {
      const D3D11_BOX box{0, 0, 0, std::min(d.ByteWidth, kCbBytes), 1, 1};
      ctx->CopySubresourceRegion(c.staging[i], 0, 0, 0, 0, bufs[i], 0, &box);
      c.bytes[i] = box.right;
    }
    bufs[i]->Release();
  }
  if (dev != nullptr) {
    dev->Release();
  }
  c.drawNo = drawNo;
  c.taken = true;
}

// Grava as cópias do quadro anterior (Map bloqueante: só no experimento).
void CbFlush(bool loading) {
  if (!g_cbShots[0].taken && !g_cbShots[1].taken) {
    return;
  }
  char name[64];
  std::snprintf(name, sizeof name, "\\cb_%04u.bin", g_cbFile++);
  std::ofstream out(g_cbDir + name, std::ios::binary);
  const uint32_t head[4] = {0x31626364u, loading ? 1u : 0u, static_cast<uint32_t>(GetTickCount64()), kCbShots};
  out.write(reinterpret_cast<const char *>(head), sizeof head);
  out.write(reinterpret_cast<const char *>(g_cbCams), sizeof g_cbCams);
  for (CbShot &c : g_cbShots) {
    const uint64_t sh = reinterpret_cast<uintptr_t>(c.shader);
    const uint32_t meta[2] = {c.taken ? c.drawNo : ~0u, c.verts};
    out.write(reinterpret_cast<const char *>(meta), sizeof meta);
    out.write(reinterpret_cast<const char *>(&sh), sizeof sh);
    for (int i = 0; i < kCbSlots; ++i) {
      uint8_t data[kCbBytes] = {};
      uint32_t n = c.taken ? c.bytes[i] : 0;
      D3D11_MAPPED_SUBRESOURCE m{};
      if (n > 0 && SUCCEEDED(g_context->Map(c.staging[i], 0, D3D11_MAP_READ, 0, &m))) {
        std::memcpy(data, m.pData, n);
        g_context->Unmap(c.staging[i], 0);
      } else {
        n = 0;
      }
      out.write(reinterpret_cast<const char *>(&n), 4);
      out.write(reinterpret_cast<const char *>(data), kCbBytes);
    }
    c.taken = false;
  }
}

void CbRelease() {
  for (CbShot &c : g_cbShots) {
    for (auto *&b : c.staging) {
      if (b != nullptr) {
        b->Release();
        b = nullptr;
      }
    }
    c.taken = false;
  }
}

// Draws desta thread (qualquer contexto), para atribuir draws a quem chamou.
thread_local uint64_t t_draws = 0;
void CountDraw(ID3D11DeviceContext *ctx) {
  ++t_draws;
  if (!g_measure.load(std::memory_order_relaxed) || g_own.load(std::memory_order_relaxed)) {
    return;
  }
  if (ctx != g_context) {
    g_deferred.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  const int cur = g_current.load(std::memory_order_relaxed);
  if (cur >= 0 && cur < static_cast<int>(g_frame.size())) {
    const uint32_t before = g_frame[cur].draws.fetch_add(1, std::memory_order_relaxed);
    if (g_cbWant.load(std::memory_order_relaxed) && !g_probePs && (before == 10 || (before == 150 && !g_traceGeo)) && !g_frame[cur].depth &&
        g_frame[cur].width >= 1280) {
      CbCapture(ctx, before == 10 ? 0 : 1, before);
    }
    if ((before & 63) == 0 && g_sampleFrame.load(std::memory_order_relaxed)) {
      SampleStack(&g_frame[cur], nullptr);
    }
  } else {
    g_unbound.fetch_add(1, std::memory_order_relaxed);
  }
}

// "rastrear_geo=1": estado do pipeline nos draws do alvo grande com MSAA (a
// cena), para comparar a carga forçada com a corrida (linha LoadView[geo]).
struct GeoStats {
  std::atomic<uint32_t> draws{0}, big{0}, indirect{0}, noDsv{0}, depthOff{0}, noWrite{0}, predicated{0},
      cullNone{0}, stencil{0}, clears{0}, dispatches{0};
  std::atomic<uint64_t> verts{0};
  std::atomic<uint32_t> func[9]{};
  std::atomic<uint32_t> vpW{0}, vpH{0}, vpMinZ{0}, vpMaxZ{0}, clearDepth{0}, clearFlags{0};
};
GeoStats g_geo;

bool GeoSceneTarget(ID3D11DeviceContext *ctx) {
  if (!g_traceGeo || ctx != g_context || !g_measure.load(std::memory_order_relaxed) ||
      g_own.load(std::memory_order_relaxed)) {
    return false;
  }
  const int cur = g_current.load(std::memory_order_relaxed);
  if (cur < 0 || cur >= static_cast<int>(g_frame.size())) {
    return false;
  }
  const Target &t = g_frame[cur];
  return !t.depth && t.samples > 1 && t.width >= 1280;
}

// Input layouts criados depois do Install (CreateInputLayout do device): elementos de cada um.
using CreateLayoutFn = HRESULT(STDMETHODCALLTYPE *)(ID3D11Device *, const D3D11_INPUT_ELEMENT_DESC *, UINT,
                                                    const void *, SIZE_T, ID3D11InputLayout **);
CreateLayoutFn g_createLayout = nullptr;
std::mutex g_layoutMutex;
std::map<void *, std::string> g_layouts;
HRESULT STDMETHODCALLTYPE HookCreateLayout(ID3D11Device *d, const D3D11_INPUT_ELEMENT_DESC *e, UINT n,
                                           const void *bc, SIZE_T len, ID3D11InputLayout **out) {
  const HRESULT hr = g_createLayout(d, e, n, bc, len, out);
  if (SUCCEEDED(hr) && out != nullptr && *out != nullptr) {
    std::string desc;
    for (UINT i = 0; i < n; ++i) {
      char one[96];
      std::snprintf(one, sizeof one, "%s%s%u s%u f%u o%u%s", i ? " " : "", e[i].SemanticName, e[i].SemanticIndex,
                    e[i].InputSlot, static_cast<unsigned>(e[i].Format), e[i].AlignedByteOffset,
                    e[i].InputSlotClass == D3D11_INPUT_PER_INSTANCE_DATA ? " inst" : "");
      desc += one;
    }
    std::lock_guard<std::mutex> lock(g_layoutMutex);
    g_layouts[*out] = desc;
  }
  return hr;
}
// Argumentos do draw em curso (os hooks preenchem antes do GeoDraw).
struct DrawArgs {
  char kind = '?';
  UINT a = 0, b = 0, c = 0, d = 0, e = 0;
};
thread_local DrawArgs t_args;
// "vigiar_cb0=1" (com sonda_stride): Map/Unmap do CB 0 do VS do draw de stride N. Guarda a última
// escrita (quem chamou no exe e os registros c0, c1 e c20) e conta as escritas por chamador a cada segundo.
using MapFn = HRESULT(STDMETHODCALLTYPE *)(ID3D11DeviceContext *, ID3D11Resource *, UINT, D3D11_MAP, UINT,
                                           D3D11_MAPPED_SUBRESOURCE *);
using UnmapFn = void(STDMETHODCALLTYPE *)(ID3D11DeviceContext *, ID3D11Resource *, UINT);
MapFn g_map = nullptr;
UnmapFn g_unmap = nullptr;
bool g_watchCb0 = false;
std::atomic<void *> g_cb0Buf{nullptr};
struct Cb0Write {
  uint32_t seq = 0;
  uint32_t rva[6] = {};
  float c0[4] = {}, c1[4] = {}, c20[4] = {};
};
std::mutex g_cb0Mutex;
Cb0Write g_cb0Last;
uint32_t g_cb0Seq = 0;
uint32_t g_cb0SeqAtDraw = 0;
std::map<std::string, uint32_t> g_cb0Callers;
thread_local void *t_bindItem = nullptr;   // último item de 0x14090aa20 nesta thread
thread_local void *t_bindParams = nullptr; // e o [item+0x58] dele
std::map<void *, uint64_t> g_groundItems;   // itens dos draws grandes com o stride da sonda -> índices (sob g_cb0Mutex)
std::set<std::tuple<bool, void *, uint32_t>> g_groundFillSeen; // (carga, item, estágio) já logados
std::atomic<int> g_groundFillLeft{0};      // linhas [fillg] por segundo
thread_local int64_t t_bindIdx = -2;        // índice de thread do motor ([TLS 0x2049310]+0x48)
thread_local void *t_cb0Data = nullptr;
thread_local int t_cb0Type = 0;
std::atomic<int> g_cb0LogLeft{0}; // escritas completas ainda a logar neste segundo (linha LoadView[cb0w])
thread_local Cb0Write t_cb0Pending;
// Com "vigiar_cb0=1": o CB 3 (CameraParams) do draw da sonda. Cada escrita é contada por fase, olho (c8) e
// pilha no exe (linha LoadView[cb3] por segundo), para achar quem escreve a câmera de cada passe.
std::atomic<void *> g_cb3Buf{nullptr};
thread_local void *t_cb3Data = nullptr;
thread_local std::string t_cb3Stack;
std::map<std::string, uint32_t> g_cb3Writers; // sob g_cb0Mutex
// VSSetConstantBuffers (vtbl 7). Com "vigiar_cb0=1": quem liga um CB de 3264 bytes no slot 4 do VS (o chão
// da corrida tem esse CB; o da carga não), por fase e pilha. Linha LoadView[cb4] por segundo.
using VsSetCbFn = void(STDMETHODCALLTYPE *)(ID3D11DeviceContext *, UINT, UINT, ID3D11Buffer *const *);
VsSetCbFn g_vsSetCb = nullptr;
std::mutex g_cb4Mutex;
std::map<std::string, uint32_t> g_cb4Callers; // "fase pilha" -> vezes
std::atomic<ID3D11Buffer *> g_cb4Buf{nullptr};
bool g_bindCb4 = false; // "ligar_cb4=1": na carga, liga o último CB 4 visto nos draws do chão sem CB 4
std::atomic<uint32_t> g_cb4Forced{0};
// "z_sempre=1": na carga, os draws do chão (sonda_stride, >= 1000 índices) passam o teste de profundidade sempre.
bool g_zAlways = false;
thread_local bool t_groundLoad = false;
// "foto_chao=1" (com despejar_hdr): alvo 0 resolvido antes e depois do 1º draw grande de sonda_stride por
// segundo logado (<prefixo>_rt_NNN_{carga,corrida}_{antes,depois}.ppm, 480x270, sem tonemap).
bool g_snapGround = false;
int g_snapN = 0;
thread_local bool t_snap = false;
std::map<ID3D11DepthStencilState *, ID3D11DepthStencilState *> g_zAlwaysStates;
std::atomic<uint32_t> g_zAlwaysDraws{0};
// CB 5 do VS do chão (928 B; zerado na carga): quem faz Map nele e quem o liga, por fase e pilha.
std::atomic<ID3D11Buffer *> g_cb5Buf{nullptr};
std::map<std::string, uint32_t> g_cb5Callers; // sob g_cb4Mutex
std::string StackKey(const char *what, int skip) {
  void *frames[32];
  const USHORT n = RtlCaptureStackBackTrace(skip, 30, frames, nullptr);
  std::string key = std::string(g_active ? "carga " : "corrida ") + what;
  int k = 0;
  for (USHORT i = 0; i < n && k < 8; ++i) {
    const auto a = reinterpret_cast<uintptr_t>(frames[i]);
    if (a >= g_exeBase && a < g_exeEnd) {
      char one[16];
      std::snprintf(one, sizeof one, "%s%x", k == 0 ? " " : "|", static_cast<uint32_t>(a - g_exeBase));
      key += one;
      ++k;
    }
  }
  return key;
}
void STDMETHODCALLTYPE HookVsSetCb(ID3D11DeviceContext *c, UINT start, UINT num, ID3D11Buffer *const *bufs) {
  if (g_watchCb0 && bufs != nullptr && start <= 5 && start + num > 5 && bufs[5 - start] != nullptr &&
      bufs[5 - start] == g_cb5Buf.load(std::memory_order_relaxed)) {
    const std::string key = StackKey("liga", 1) + " (início " + std::to_string(start) + ", n " + std::to_string(num) + ")";
    std::lock_guard<std::mutex> lock(g_cb4Mutex);
    ++g_cb5Callers[key];
  }
  if (g_watchCb0 && bufs != nullptr && start <= 4 && start + num > 4) {
    ID3D11Buffer *b = bufs[4 - start];
    uint32_t width = 0;
    if (b != nullptr) {
      D3D11_BUFFER_DESC d{};
      b->GetDesc(&d);
      width = d.ByteWidth;
    }
    if (width == 3264) {
      g_cb4Buf.store(b, std::memory_order_relaxed);
      void *frames[32];
      const USHORT n = RtlCaptureStackBackTrace(1, 30, frames, nullptr);
      std::string key = g_active ? "carga" : "corrida";
      int k = 0;
      for (USHORT i = 0; i < n && k < 8; ++i) {
        const auto a = reinterpret_cast<uintptr_t>(frames[i]);
        if (a >= g_exeBase && a < g_exeEnd) {
          char one[16];
          std::snprintf(one, sizeof one, "%s%x", k == 0 ? " " : "|", static_cast<uint32_t>(a - g_exeBase));
          key += one;
          ++k;
        }
      }
      char sz[48];
      std::snprintf(sz, sizeof sz, " (início %u, n %u)", start, num);
      key += sz;
      std::lock_guard<std::mutex> lock(g_cb4Mutex);
      ++g_cb4Callers[key];
    }
  }
  g_vsSetCb(c, start, num, bufs);
}

std::string Cb0Stack(const Cb0Write &w) {
  std::string s;
  for (uint32_t r : w.rva) {
    if (r == 0) {
      break;
    }
    char one[16];
    std::snprintf(one, sizeof one, "%s%x", s.empty() ? "" : "|", r);
    s += one;
  }
  return s;
}
HRESULT STDMETHODCALLTYPE HookMap(ID3D11DeviceContext *c, ID3D11Resource *r, UINT sub, D3D11_MAP type, UINT flags,
                                  D3D11_MAPPED_SUBRESOURCE *out) {
  const HRESULT hr = g_map(c, r, sub, type, flags, out);
  if (g_watchCb0 && r != nullptr && r == g_cb5Buf.load(std::memory_order_relaxed)) {
    const std::string key = StackKey("map", 1) + " tipo " + std::to_string(static_cast<int>(type));
    std::lock_guard<std::mutex> lock(g_cb4Mutex);
    ++g_cb5Callers[key];
  }
  if (g_watchCb0 && r != nullptr && r == g_cb3Buf.load(std::memory_order_relaxed) && SUCCEEDED(hr) && out != nullptr) {
    t_cb3Data = out->pData;
    t_cb3Stack = StackKey("", 1);
  }
  if (g_watchCb0 && r != nullptr && r == g_cb0Buf.load(std::memory_order_relaxed) && SUCCEEDED(hr) && out != nullptr) {
    t_cb0Data = out->pData;
    t_cb0Type = static_cast<int>(type);
    t_cb0Pending = Cb0Write{};
    void *frames[32];
    const USHORT n = RtlCaptureStackBackTrace(1, 30, frames, nullptr);
    int k = 0;
    for (USHORT i = 0; i < n && k < 6; ++i) {
      const auto a = reinterpret_cast<uintptr_t>(frames[i]);
      if (a >= g_exeBase && a < g_exeEnd) {
        t_cb0Pending.rva[k++] = static_cast<uint32_t>(a - g_exeBase);
      }
    }
  }
  return hr;
}
void STDMETHODCALLTYPE HookUnmap(ID3D11DeviceContext *c, ID3D11Resource *r, UINT sub) {
  if (t_cb3Data != nullptr && r == g_cb3Buf.load(std::memory_order_relaxed)) {
    const auto *f = static_cast<const float *>(t_cb3Data);
    char one[96];
    std::snprintf(one, sizeof one, "%s olho %.1f,%.1f,%.1f p00 %.3f", g_active ? "carga" : "corrida", f[32], f[33],
                  f[34], f[0]);
    std::lock_guard<std::mutex> lock(g_cb0Mutex);
    ++g_cb3Writers[std::string(one) + " " + t_cb3Stack];
    t_cb3Data = nullptr;
  }
  if (t_cb0Data != nullptr && r == g_cb0Buf.load(std::memory_order_relaxed)) {
    const auto *f = static_cast<const float *>(t_cb0Data);
    std::memcpy(t_cb0Pending.c0, f, 16);
    std::memcpy(t_cb0Pending.c1, f + 4, 16);
    std::memcpy(t_cb0Pending.c20, f + 80, 16);
    std::lock_guard<std::mutex> lock(g_cb0Mutex);
    t_cb0Pending.seq = ++g_cb0Seq;
    g_cb0Last = t_cb0Pending;
    ++g_cb0Callers[std::string(g_active ? "carga " : "corrida ") + Cb0Stack(t_cb0Pending)];
    if (g_cb0LogLeft.load(std::memory_order_relaxed) > 0) {
      g_cb0LogLeft.fetch_sub(1, std::memory_order_relaxed);
      std::string line = "LoadView[cb0w]: " + std::string(g_active ? "carga" : "corrida") + " #" +
                         std::to_string(t_cb0Pending.seq) + " tipo " + std::to_string(t_cb0Type) + ":";
      for (int i = 0; i < 28; ++i) {
        char one[80];
        std::snprintf(one, sizeof one, " c%d=[%.4g %.4g %.4g %.4g]", i, f[i * 4], f[i * 4 + 1], f[i * 4 + 2],
                      f[i * 4 + 3]);
        line += one;
      }
      Logger::Info(line);
    }
    t_cb0Data = nullptr;
  }
  g_unmap(c, r, sub);
}
// Com sonda_stride: quantos draws desse stride ainda logar em detalhe neste segundo.
std::atomic<int> g_drawLogLeft{0};
// Conteúdo de um buffer da GPU (cópia para staging): hash, palavras zeradas e os primeiros floats.
// maxFloats/whole: quantos floats imprimir e se começam do 0 (constant buffers) em vez do primeiro não nulo.
std::string BufferDigest(ID3D11DeviceContext *ctx, ID3D11Buffer *buf, const D3D11_BUFFER_DESC &bd, uint32_t maxFloats = 48,
                         bool whole = false) {
  ID3D11Device *dev = nullptr;
  ctx->GetDevice(&dev);
  if (dev == nullptr) {
    return " [sem device]";
  }
  D3D11_BUFFER_DESC sd{};
  sd.ByteWidth = bd.ByteWidth;
  sd.Usage = D3D11_USAGE_STAGING;
  sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  ID3D11Buffer *st = nullptr;
  dev->CreateBuffer(&sd, nullptr, &st);
  dev->Release();
  if (st == nullptr) {
    return " [sem staging]";
  }
  ctx->CopyResource(st, buf);
  D3D11_MAPPED_SUBRESOURCE m{};
  std::string out = " [não mapeou]";
  if (SUCCEEDED(g_map(ctx, st, 0, D3D11_MAP_READ, 0, &m))) {
    const auto *w = static_cast<const uint32_t *>(m.pData);
    const uint32_t nw = bd.ByteWidth / 4;
    uint64_t h = 1469598103934665603ull;
    uint32_t zeros = 0, firstNz = nw, lastNz = 0;
    for (uint32_t i = 0; i < nw; ++i) {
      h = (h ^ w[i]) * 1099511628211ull;
      if (w[i] == 0) {
        ++zeros;
      } else {
        firstNz = std::min(firstNz, i);
        lastNz = i;
      }
    }
    char head[128];
    std::snprintf(head, sizeof head, " [hash %016llx zeros %u/%u nz %u-%u:", static_cast<unsigned long long>(h), zeros,
                  nw, firstNz, lastNz);
    out = head;
    const auto *f = static_cast<const float *>(m.pData);
    const uint32_t from = whole || nw <= 64 ? 0 : (firstNz < nw ? firstNz : 0);
    for (uint32_t i = from; i < nw && i < from + maxFloats; ++i) {
      char one[24];
      std::snprintf(one, sizeof one, " %g", f[i]);
      out += one;
    }
    out += "]";
    g_unmap(ctx, st, 0);
  }
  st->Release();
  return out;
}
float HalfFloat(uint16_t h);
float SmallFloat(uint32_t bits, int mant);
void SnapTarget(ID3D11DeviceContext *ctx, const char *when) {
  ID3D11RenderTargetView *rtv = nullptr;
  ctx->OMGetRenderTargets(1, &rtv, nullptr);
  if (rtv == nullptr) {
    return;
  }
  ID3D11Resource *res = nullptr;
  rtv->GetResource(&res);
  rtv->Release();
  D3D11_RESOURCE_DIMENSION dim{};
  if (res != nullptr) {
    res->GetType(&dim);
  }
  if (res == nullptr || dim != D3D11_RESOURCE_DIMENSION_TEXTURE2D) {
    if (res != nullptr) {
      res->Release();
    }
    return;
  }
  auto *tex = static_cast<ID3D11Texture2D *>(res);
  D3D11_TEXTURE2D_DESC td{};
  tex->GetDesc(&td);
  ID3D11Device *dev = nullptr;
  ctx->GetDevice(&dev);
  ID3D11Texture2D *one = nullptr, *st = nullptr;
  D3D11_TEXTURE2D_DESC d = td;
  d.MipLevels = 1;
  d.ArraySize = 1;
  d.SampleDesc = {1, 0};
  d.MiscFlags = 0;
  d.CPUAccessFlags = 0;
  d.Usage = D3D11_USAGE_DEFAULT;
  d.BindFlags = 0;
  if (dev != nullptr && td.SampleDesc.Count > 1) {
    dev->CreateTexture2D(&d, nullptr, &one);
  }
  d.Usage = D3D11_USAGE_STAGING;
  d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  if (dev != nullptr) {
    dev->CreateTexture2D(&d, nullptr, &st);
    dev->Release();
  }
  if (st != nullptr && (one != nullptr || td.SampleDesc.Count == 1)) {
    if (one != nullptr) {
      ctx->ResolveSubresource(one, 0, tex, 0, td.Format);
      ctx->CopyResource(st, one);
    } else {
      ctx->CopySubresourceRegion(st, 0, 0, 0, 0, tex, 0, nullptr);
    }
    D3D11_MAPPED_SUBRESOURCE m{};
    if (SUCCEEDED(g_map(ctx, st, 0, D3D11_MAP_READ, 0, &m))) {
      constexpr UINT kStep = 4;
      const UINT w = td.Width / kStep, h = td.Height / kStep;
      std::string px(static_cast<size_t>(w) * h * 3, '\0');
      const bool bpp8 = td.Format == DXGI_FORMAT_R16G16B16A16_FLOAT;
      for (UINT y = 0; y < h; ++y) {
        const auto *row = static_cast<const uint8_t *>(m.pData) + static_cast<size_t>(y * kStep) * m.RowPitch;
        for (UINT x = 0; x < w; ++x) {
          for (int c = 0; c < 3; ++c) {
            float v;
            if (td.Format == DXGI_FORMAT_R11G11B10_FLOAT) {
              uint32_t pv;
              std::memcpy(&pv, row + x * kStep * 4, 4);
              v = c == 0 ? SmallFloat(pv & 0x7ff, 6) : c == 1 ? SmallFloat((pv >> 11) & 0x7ff, 6) : SmallFloat(pv >> 22, 5);
              v = std::isfinite(v) ? std::pow(v / (1.f + v), 1.f / 2.2f) * 255.f : 0.f;
            } else if (bpp8) {
              uint16_t hv;
              std::memcpy(&hv, row + x * kStep * 8 + c * 2, 2);
              v = HalfFloat(hv);
              v = std::isfinite(v) ? std::pow(v / (1.f + v), 1.f / 2.2f) : 0.f;
              v *= 255.f;
            } else {
              v = row[x * kStep * 4 + c]; // 4 bytes por pixel (RGBA8 e afins)
            }
            px[(static_cast<size_t>(y) * w + x) * 3 + c] = static_cast<char>(std::clamp(static_cast<int>(v), 0, 255));
          }
        }
      }
      ctx->Unmap(st, 0);
      char name[96];
      std::snprintf(name, sizeof name, "_rt_%03d_%s_%s.ppm", g_snapN, g_active ? "carga" : "corrida", when);
      std::ofstream out(g_hdrDump + name, std::ios::binary);
      out << "P6\n" << w << " " << h << "\n255\n";
      out.write(px.data(), static_cast<std::streamsize>(px.size()));
      char line[160];
      std::snprintf(line, sizeof line, "LoadView[foto]: %s %s alvo %p fmt %d %ux%u s%u", name, when,
                    static_cast<void *>(tex), static_cast<int>(td.Format), td.Width, td.Height, td.SampleDesc.Count);
      Logger::Info(line);
    }
  }
  if (one != nullptr) {
    one->Release();
  }
  if (st != nullptr) {
    st->Release();
  }
  res->Release();
}

void LogDrawDetail(ID3D11DeviceContext *ctx, void *vs) {
  ID3D11InputLayout *il = nullptr;
  ctx->IAGetInputLayout(&il);
  std::string layout = "?";
  if (il != nullptr) {
    std::lock_guard<std::mutex> lock(g_layoutMutex);
    if (auto it = g_layouts.find(il); it != g_layouts.end()) {
      layout = it->second;
    }
  }
  ID3D11Buffer *vbs[4] = {};
  UINT strides[4] = {}, offs[4] = {};
  ctx->IAGetVertexBuffers(0, 4, vbs, strides, offs);
  std::string vb;
  for (int i = 0; i < 4; ++i) {
    if (vbs[i] == nullptr) {
      continue;
    }
    D3D11_BUFFER_DESC bd{};
    vbs[i]->GetDesc(&bd);
    char one[128];
    std::snprintf(one, sizeof one, " vb%d=%p(%u B, passo %u, off %u, u%d)", i, static_cast<void *>(vbs[i]), bd.ByteWidth,
                  strides[i], offs[i], static_cast<int>(bd.Usage));
    vb += one;
    if (i == 0) {
      vb += BufferDigest(ctx, vbs[i], bd);
    }
    vbs[i]->Release();
  }
  ID3D11Buffer *ib = nullptr;
  DXGI_FORMAT ibf = DXGI_FORMAT_UNKNOWN;
  UINT ibo = 0;
  ctx->IAGetIndexBuffer(&ib, &ibf, &ibo);
  UINT ibBytes = 0;
  std::string ibInfo;
  if (ib != nullptr) {
    D3D11_BUFFER_DESC bd{};
    ib->GetDesc(&bd);
    ibBytes = bd.ByteWidth;
    char one[64];
    std::snprintf(one, sizeof one, " ib=%p u%d", static_cast<void *>(ib), static_cast<int>(bd.Usage));
    ibInfo = one + BufferDigest(ctx, ib, bd);
    ib->Release();
  }
  {
    ID3D11GeometryShader *gs = nullptr;
    ID3D11HullShader *hs = nullptr;
    ID3D11DomainShader *ds = nullptr;
    ctx->GSGetShader(&gs, nullptr, nullptr);
    ctx->HSGetShader(&hs, nullptr, nullptr);
    ctx->DSGetShader(&ds, nullptr, nullptr);
    char one[96];
    std::snprintf(one, sizeof one, " | gs %p hs %p ds %p", static_cast<void *>(gs), static_cast<void *>(hs),
                  static_cast<void *>(ds));
    ibInfo += one;
    for (IUnknown *u : {static_cast<IUnknown *>(gs), static_cast<IUnknown *>(hs), static_cast<IUnknown *>(ds)}) {
      if (u != nullptr) {
        u->Release();
      }
    }
  }
  D3D11_PRIMITIVE_TOPOLOGY topo{};
  ctx->IAGetPrimitiveTopology(&topo);
  char head[256];
  std::snprintf(head, sizeof head,
                "LoadView[draw]: %s vs %p %c(%u,%u,%u,%u,%u) topo %d ib %u B fmt %d off %u il %p [", g_active ? "carga" : "corrida",
                vs, t_args.kind, t_args.a, t_args.b, t_args.c, t_args.d, t_args.e, static_cast<int>(topo), ibBytes,
                static_cast<int>(ibf), ibo, static_cast<void *>(il));
  if (il != nullptr) {
    il->Release();
  }
  std::string cbs = " | vscb";
  std::string cbData = " | cbdados";
  ID3D11Buffer *cb[8] = {};
  ctx->VSGetConstantBuffers(0, 8, cb);
  for (int i = 0; i < 8; ++i) {
    if (cb[i] == nullptr) {
      continue;
    }
    D3D11_BUFFER_DESC bd{};
    cb[i]->GetDesc(&bd);
    char one[64];
    std::snprintf(one, sizeof one, " %d=%p/%u/u%d", i, static_cast<void *>(cb[i]), bd.ByteWidth, static_cast<int>(bd.Usage));
    cbs += one;
    // Conteúdo que a GPU usa neste draw (o CB 0 é compartilhado; o log [cb0] só mostra a última escrita).
    if (i == 0 || i == 1 || i == 3 || i == 7) {
      cbData += " s" + std::to_string(i) + BufferDigest(ctx, cb[i], bd, 112, true);
    }
    cb[i]->Release();
  }
  // Texturas do VS (mapa de altura?) e estado de rasterização/profundidade.
  cbs += " | vssrv";
  ID3D11ShaderResourceView *srv[16] = {};
  ctx->VSGetShaderResources(0, 16, srv);
  for (int i = 0; i < 16; ++i) {
    if (srv[i] == nullptr) {
      continue;
    }
    D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
    srv[i]->GetDesc(&sd);
    ID3D11Resource *res = nullptr;
    srv[i]->GetResource(&res);
    char one[160];
    int n = std::snprintf(one, sizeof one, " %d=%p res %p dim %d fmt %d", i, static_cast<void *>(srv[i]),
                          static_cast<void *>(res), static_cast<int>(sd.ViewDimension), static_cast<int>(sd.Format));
    if (res != nullptr) {
      D3D11_RESOURCE_DIMENSION rd{};
      res->GetType(&rd);
      if (rd == D3D11_RESOURCE_DIMENSION_TEXTURE2D) {
        D3D11_TEXTURE2D_DESC td{};
        static_cast<ID3D11Texture2D *>(res)->GetDesc(&td);
        std::snprintf(one + n, sizeof one - n, " %ux%u m%u a%u", td.Width, td.Height, td.MipLevels, td.ArraySize);
      } else if (rd == D3D11_RESOURCE_DIMENSION_BUFFER) {
        D3D11_BUFFER_DESC bd{};
        static_cast<ID3D11Buffer *>(res)->GetDesc(&bd);
        std::snprintf(one + n, sizeof one - n, " buf %u B u%d bind %x passo %u", bd.ByteWidth, static_cast<int>(bd.Usage),
                      bd.BindFlags, bd.StructureByteStride);
        cbs += one;
        one[0] = '\0';
        cbs += BufferDigest(ctx, static_cast<ID3D11Buffer *>(res), bd);
      }
      res->Release();
    }
    cbs += one;
    srv[i]->Release();
  }
  {
    ID3D11ShaderResourceView *ps[32] = {};
    ctx->PSGetShaderResources(0, 32, ps);
    int nps = 0;
    std::string psrv;
    for (int i = 0; i < 32; ++i) {
      ID3D11ShaderResourceView *p = ps[i];
      if (p == nullptr) {
        continue;
      }
      ++nps;
      // Cada textura do PS: recurso, formato e tamanho (texturas faltando na carga?).
      ID3D11Resource *res = nullptr;
      p->GetResource(&res);
      D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
      p->GetDesc(&sd);
      char one[96];
      int n = std::snprintf(one, sizeof one, " %d=%p f%d d%d", i, static_cast<void *>(res), static_cast<int>(sd.Format),
                            static_cast<int>(sd.ViewDimension));
      if (res != nullptr) {
        D3D11_RESOURCE_DIMENSION rd{};
        res->GetType(&rd);
        if (rd == D3D11_RESOURCE_DIMENSION_TEXTURE2D) {
          D3D11_TEXTURE2D_DESC td{};
          static_cast<ID3D11Texture2D *>(res)->GetDesc(&td);
          std::snprintf(one + n, sizeof one - n, " %ux%u m%u a%u s%u", td.Width, td.Height, td.MipLevels, td.ArraySize,
                        td.SampleDesc.Count);
        } else if (rd == D3D11_RESOURCE_DIMENSION_BUFFER) {
          D3D11_BUFFER_DESC bd{};
          static_cast<ID3D11Buffer *>(res)->GetDesc(&bd);
          std::snprintf(one + n, sizeof one - n, " buf %u", bd.ByteWidth);
        }
        res->Release();
      }
      psrv += one;
      p->Release();
    }
    ID3D11RasterizerState *rs = nullptr;
    ctx->RSGetState(&rs);
    D3D11_RASTERIZER_DESC rd{};
    if (rs != nullptr) {
      rs->GetDesc(&rd);
      rs->Release();
    }
    ID3D11DepthStencilState *ds = nullptr;
    UINT sref = 0;
    ctx->OMGetDepthStencilState(&ds, &sref);
    D3D11_DEPTH_STENCIL_DESC dd{};
    if (ds != nullptr) {
      ds->GetDesc(&dd);
      ds->Release();
    }
    UINT nvp = 1;
    D3D11_VIEWPORT vp{};
    ctx->RSGetViewports(&nvp, &vp);
    char one[256];
    std::snprintf(one, sizeof one,
                  " | pssrv %d | rs %s fill %d cull %d ccw %d bias %d/%.3f clip %d | ds %s z %d/%d st %d ref %u | vp "
                  "%.0f,%.0f %.0fx%.0f z %.2f-%.2f",
                  nps, rs ? "ok" : "nulo", static_cast<int>(rd.FillMode), static_cast<int>(rd.CullMode),
                  rd.FrontCounterClockwise, rd.DepthBias, rd.SlopeScaledDepthBias, rd.DepthClipEnable, ds ? "ok" : "nulo",
                  dd.DepthEnable, static_cast<int>(dd.DepthFunc), dd.StencilEnable, sref, vp.TopLeftX, vp.TopLeftY,
                  vp.Width, vp.Height, vp.MinDepth, vp.MaxDepth);
    cbs += one;
    cbs += " | pssrvs" + psrv;
  }
  {
    // Lado do pixel: shader, alvos, blend e CBs do PS (a geometria do VS é igual na carga e na corrida).
    ID3D11PixelShader *ps = nullptr;
    ctx->PSGetShader(&ps, nullptr, nullptr);
    char one[200];
    std::snprintf(one, sizeof one, " | ps %p", static_cast<void *>(ps));
    cbs += one;
    if (ps != nullptr) {
      ps->Release();
    }
    ID3D11RenderTargetView *rtv[8] = {};
    ID3D11DepthStencilView *dsv = nullptr;
    ctx->OMGetRenderTargets(8, rtv, &dsv);
    cbs += " | rt";
    for (int i = 0; i < 8; ++i) {
      if (rtv[i] == nullptr) {
        continue;
      }
      D3D11_RENDER_TARGET_VIEW_DESC rd{};
      rtv[i]->GetDesc(&rd);
      ID3D11Resource *res = nullptr;
      rtv[i]->GetResource(&res);
      D3D11_TEXTURE2D_DESC td{};
      D3D11_RESOURCE_DIMENSION dim{};
      if (res != nullptr) {
        res->GetType(&dim);
        if (dim == D3D11_RESOURCE_DIMENSION_TEXTURE2D) {
          static_cast<ID3D11Texture2D *>(res)->GetDesc(&td);
        }
      }
      std::snprintf(one, sizeof one, " %d=%p fmt %d %ux%u s%u", i, static_cast<void *>(res), static_cast<int>(rd.Format),
                    td.Width, td.Height, td.SampleDesc.Count);
      cbs += one;
      if (res != nullptr) {
        res->Release();
      }
      rtv[i]->Release();
    }
    if (dsv != nullptr) {
      ID3D11Resource *res = nullptr;
      dsv->GetResource(&res);
      std::snprintf(one, sizeof one, " dsv %p", static_cast<void *>(res));
      cbs += one;
      if (res != nullptr) {
        res->Release();
      }
      dsv->Release();
    }
    ID3D11BlendState *bs = nullptr;
    float factor[4] = {};
    UINT mask = 0;
    ctx->OMGetBlendState(&bs, factor, &mask);
    D3D11_BLEND_DESC bd{};
    if (bs != nullptr) {
      bs->GetDesc(&bd);
      bs->Release();
    }
    std::snprintf(one, sizeof one, " | blend %s a2c %d ind %d", bs ? "ok" : "nulo", bd.AlphaToCoverageEnable,
                  bd.IndependentBlendEnable);
    cbs += one;
    for (int i = 0; i < (bd.IndependentBlendEnable ? 4 : 1); ++i) {
      const auto &r = bd.RenderTarget[i];
      std::snprintf(one, sizeof one, " [%d en %d %d/%d op %d m %x]", i, r.BlendEnable, static_cast<int>(r.SrcBlend),
                    static_cast<int>(r.DestBlend), static_cast<int>(r.BlendOp), r.RenderTargetWriteMask);
      cbs += one;
    }
    std::snprintf(one, sizeof one, " mask %x", mask);
    cbs += one;
    ID3D11Buffer *pcb[8] = {};
    ctx->PSGetConstantBuffers(0, 8, pcb);
    cbs += " | pscb";
    for (int i = 0; i < 8; ++i) {
      if (pcb[i] == nullptr) {
        continue;
      }
      D3D11_BUFFER_DESC d{};
      pcb[i]->GetDesc(&d);
      std::snprintf(one, sizeof one, " %d=%p/%u", i, static_cast<void *>(pcb[i]), d.ByteWidth);
      cbs += one;
      // Hash e começo de cada CB do PS (o 0 é o $Globals do material).
      cbs += BufferDigest(ctx, pcb[i], d, i == 0 ? 112 : 24, true);
      pcb[i]->Release();
    }
  }
  if (g_watchCb0) {
    void *frames[48];
    const USHORT fn = RtlCaptureStackBackTrace(0, 46, frames, nullptr);
    std::string st = " | pilha";
    for (USHORT i = 0, k = 0; i < fn && k < 12; ++i) {
      const auto a = reinterpret_cast<uintptr_t>(frames[i]);
      if (a >= g_exeBase && a < g_exeEnd) {
        char one[16];
        std::snprintf(one, sizeof one, "%s%x", k ? "|" : " ", static_cast<unsigned>(a - g_exeBase));
        st += one;
        ++k;
      }
    }
    cbs += st;
    char it[160];
    std::snprintf(it, sizeof it, " | item %p params %p thread %lu idx %lld", t_bindItem, t_bindParams,
                  GetCurrentThreadId(), static_cast<long long>(t_bindIdx));
    cbs += it;
    std::lock_guard<std::mutex> lock(g_cb0Mutex);
    const Cb0Write &w = g_cb0Last;
    char one[400];
    std::snprintf(one, sizeof one,
                  " | cb0 #%u (+%u desde o draw anterior) de %s c0=[%.3f %.3f %.3f %.3f] c1=[%.3f %.3f %.3f %.3f] "
                  "c20=[%.3f %.3f %.3f %.3f]",
                  w.seq, w.seq - g_cb0SeqAtDraw, Cb0Stack(w).c_str(), w.c0[0], w.c0[1], w.c0[2], w.c0[3], w.c1[0],
                  w.c1[1], w.c1[2], w.c1[3], w.c20[0], w.c20[1], w.c20[2], w.c20[3]);
    cbs += one;
  }
  Logger::Info(head + layout + "]" + vb + ibInfo + cbs + cbData);
  if (g_snapGround && !g_hdrDump.empty() && t_args.a >= 2000 && g_snapN < 40) {
    t_snap = true;
    SnapTarget(ctx, "antes");
  }
}

// "zerar_cb7=1": na carga, zera os 2 primeiros registros do CB 7 (LightsConstantBuffer: contagens e
// índices das sondas, NaN na carga) do VS e do PS antes de cada draw da cena. Cópia de GPU a partir de um buffer de zeros.
bool g_fixCb7 = false;
ID3D11Buffer *g_zeros = nullptr;
ID3D11Buffer *g_cb7Ours = nullptr;
ID3D11Buffer *g_cb7Game = nullptr; // buffer do jogo que a cópia substitui (sem ref)
void FixCb7(ID3D11DeviceContext *ctx, bool active) {
  ID3D11Buffer *cb = nullptr;
  ctx->VSGetConstantBuffers(7, 1, &cb);
  if (cb == nullptr) {
    return;
  }
  if (!active) {
    if (cb == g_cb7Ours && g_cb7Game != nullptr) {
      ctx->VSSetConstantBuffers(7, 1, &g_cb7Game);
      ctx->PSSetConstantBuffers(7, 1, &g_cb7Game);
    }
    cb->Release();
    return;
  }
  ID3D11Buffer *src = cb == g_cb7Ours ? g_cb7Game : cb;
  if (src != nullptr && src != g_cb7Ours) {
    g_cb7Game = src;
    D3D11_BUFFER_DESC d{};
    src->GetDesc(&d);
    ID3D11Device *dev = nullptr;
    ctx->GetDevice(&dev);
    if (dev != nullptr) {
      if (g_zeros == nullptr) {
        const uint8_t zero[32] = {};
        D3D11_BUFFER_DESC zd{};
        zd.ByteWidth = 32;
        zd.Usage = D3D11_USAGE_DEFAULT;
        zd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        const D3D11_SUBRESOURCE_DATA init{zero, 0, 0};
        dev->CreateBuffer(&zd, &init, &g_zeros);
      }
      if (g_cb7Ours == nullptr) {
        D3D11_BUFFER_DESC od{};
        od.ByteWidth = d.ByteWidth;
        od.Usage = D3D11_USAGE_DEFAULT;
        od.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        dev->CreateBuffer(&od, nullptr, &g_cb7Ours);
        char line[160];
        std::snprintf(line, sizeof line, "LoadView[cb7]: cópia %p de %u B no lugar de %p (usage %d)",
                      static_cast<void *>(g_cb7Ours), d.ByteWidth, static_cast<void *>(src), static_cast<int>(d.Usage));
        Logger::Info(line);
      }
      dev->Release();
    }
    if (g_cb7Ours != nullptr && g_zeros != nullptr) {
      D3D11_BUFFER_DESC od{};
      g_cb7Ours->GetDesc(&od);
      const D3D11_BOX all{0, 0, 0, std::min(od.ByteWidth, d.ByteWidth), 1, 1};
      ctx->CopySubresourceRegion(g_cb7Ours, 0, 0, 0, 0, src, 0, &all);
      const D3D11_BOX box{0, 0, 0, 32, 1, 1};
      ctx->CopySubresourceRegion(g_cb7Ours, 0, 0, 0, 0, g_zeros, 0, &box);
      ctx->VSSetConstantBuffers(7, 1, &g_cb7Ours);
      // O PS é quem lê as luzes (m_numLights, índices das sondas); na carga os índices vêm NaN.
      ctx->PSSetConstantBuffers(7, 1, &g_cb7Ours);
    }
  }
  cb->Release();
}

// Draws e vértices da cena por VS (linha LoadView[vs]).
struct VsTally {
  uint32_t draws = 0;
  uint64_t verts = 0;
  uint32_t stride0 = 0, nvb = 0, eq = 0;
  uint32_t rtFmt = 0, nrt = 0; // formato do alvo 0 e quantos alvos (passe de cor, de movimento...)
};
std::map<void *, VsTally> g_vsTally;
// "pular_vs=1": na corrida, os draws da cena com o VS do 1º draw grande da carga não desenham.
bool g_skipBigVs = false;
void *g_bigVs = nullptr;
// "pular_stride=N": na corrida, os draws da cena com o vertex buffer 0 de stride N não desenham.
uint32_t g_skipStride = 0;
// "pular_stride_carga=N": na carga, os draws grandes (>= 1000 índices) de stride N não desenham.
uint32_t g_skipStrideLoad = 0;
// "sonda_stride=N": o tiro 1 da sonda_cb pega o 1º draw grande com o vertex buffer 0 de stride N.
uint32_t g_cbStride = 0;
// "sonda_stride2=N": outro stride logado em detalhe (cota própria), p.ex. o pré-passe junto do passe de cor.
uint32_t g_cbStride2 = 0;
std::atomic<int> g_drawLogLeft2{0};
// true = o draw não deve ir para a GPU.
// "vigiar_tex=1" (com rastrear_geo): texturas minúsculas (<= 8x8) no PS dos draws da cena, por fase e stride
// (linha LoadView[tex]). Prédios pretos na carga = streaming ainda sem a textura?
bool g_watchTex = false;
// "zerar_slots=<máscara>": na corrida, solta esses slots do PS nos draws de objeto (stride 24/52/60/64).
uint32_t g_nullSlots = 0;
bool g_nullInLoad = false; // "zerar_na_carga=1": zera na carga em vez da corrida
UINT g_nullStride = 0; // "zerar_stride=N": só esse stride (0 = os de objeto)
// "trocar_cb=<máscara>": na carga, guarda cópias dos CBs do VS e do PS (slots da máscara) dos draws de objeto;
// na corrida, liga essas cópias nos draws de objeto. Mostra qual CB da carga apaga a luz.
uint32_t g_swapCb = 0;
ID3D11Buffer *g_swapVs[8] = {};
ID3D11Buffer *g_swapPs[8] = {};
std::atomic<uint32_t> g_swapDone{0};
void SwapCbs(ID3D11DeviceContext *ctx) {
  for (int stage = 0; stage < 2; ++stage) {
    ID3D11Buffer **ours = stage == 0 ? g_swapVs : g_swapPs;
    ID3D11Buffer *cur[8] = {};
    if (stage == 0) {
      ctx->VSGetConstantBuffers(0, 8, cur);
    } else {
      ctx->PSGetConstantBuffers(0, 8, cur);
    }
    for (UINT i = 0; i < 8; ++i) {
      if (((g_swapCb >> i) & 1u) != 0) {
        if (g_active && cur[i] != nullptr) {
          D3D11_BUFFER_DESC d{};
          cur[i]->GetDesc(&d);
          if (ours[i] != nullptr) {
            D3D11_BUFFER_DESC od{};
            ours[i]->GetDesc(&od);
            if (od.ByteWidth != d.ByteWidth) {
              ours[i]->Release();
              ours[i] = nullptr;
            }
          }
          if (ours[i] == nullptr) {
            ID3D11Device *dev = nullptr;
            ctx->GetDevice(&dev);
            if (dev != nullptr) {
              D3D11_BUFFER_DESC od{};
              od.ByteWidth = d.ByteWidth;
              od.Usage = D3D11_USAGE_DEFAULT;
              od.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
              dev->CreateBuffer(&od, nullptr, &ours[i]);
              dev->Release();
            }
          }
          if (ours[i] != nullptr) {
            ctx->CopyResource(ours[i], cur[i]);
          }
        } else if (!g_active && ours[i] != nullptr) {
          if (stage == 0) {
            ctx->VSSetConstantBuffers(i, 1, &ours[i]);
          } else {
            ctx->PSSetConstantBuffers(i, 1, &ours[i]);
          }
          g_swapDone.fetch_add(1, std::memory_order_relaxed);
        }
      }
      if (cur[i] != nullptr) {
        cur[i]->Release();
      }
    }
  }
}
std::mutex g_texMutex;
std::map<std::string, std::pair<uint32_t, std::string>> g_texSeen;
void TexCensus(ID3D11DeviceContext *ctx, UINT stride) {
  ID3D11ShaderResourceView *ps[16] = {};
  ctx->PSGetShaderResources(0, 16, ps);
  uint32_t mask = 0;
  std::string desc;
  for (int i = 0; i < 16; ++i) {
    if (ps[i] == nullptr) {
      continue;
    }
    mask |= 1u << i;
    ID3D11Resource *res = nullptr;
    ps[i]->GetResource(&res);
    char one[64] = {};
    if (res != nullptr) {
      D3D11_RESOURCE_DIMENSION rd{};
      res->GetType(&rd);
      if (rd == D3D11_RESOURCE_DIMENSION_TEXTURE2D) {
        D3D11_TEXTURE2D_DESC td{};
        static_cast<ID3D11Texture2D *>(res)->GetDesc(&td);
        std::snprintf(one, sizeof one, " %d:%ux%u%s f%u", i, td.Width, td.Height, td.ArraySize > 1 ? "a" : "",
                      static_cast<unsigned>(td.Format));
      } else {
        std::snprintf(one, sizeof one, " %d:d%d", i, static_cast<int>(rd));
      }
      res->Release();
    }
    desc += one;
    ps[i]->Release();
  }
  char head[64];
  std::snprintf(head, sizeof head, "%s s%u m%04x", g_active ? "carga" : "corrida", stride, mask);
  std::string hk = head;
  if (mask == 0xffff && (stride == 24 || stride == 60 || stride == 64)) {
    hk += StackKey("", 3).substr(g_active ? 6 : 8);
  }
  std::lock_guard<std::mutex> lock(g_texMutex);
  auto &e = g_texSeen[hk];
  if (e.first == 0) {
    e.second = desc;
  }
  ++e.first;
}

// Com vigiar_tex: quem liga o slot 13 do PS (na corrida, array 128x128 R11G11B10 dos objetos), por fase e pilha.
using PsSetSrvFn = void(STDMETHODCALLTYPE *)(ID3D11DeviceContext *, UINT, UINT, ID3D11ShaderResourceView *const *);
PsSetSrvFn g_psSetSrv = nullptr;
std::map<std::string, uint32_t> g_srvCallers; // sob g_texMutex
void STDMETHODCALLTYPE HookPsSetSrv(ID3D11DeviceContext *c, UINT start, UINT num,
                                    ID3D11ShaderResourceView *const *v) {
  if (g_watchTex && v != nullptr && start <= 13 && start + num > 11) {
    std::string d;
    for (UINT i = start; i < start + num; ++i) {
      if (i < 11 || i > 14) {
        continue;
      }
      char one[48] = {};
      ID3D11Resource *res = nullptr;
      if (v[i - start] != nullptr) {
        v[i - start]->GetResource(&res);
      }
      if (res != nullptr) {
        D3D11_RESOURCE_DIMENSION rd{};
        res->GetType(&rd);
        if (rd == D3D11_RESOURCE_DIMENSION_TEXTURE2D) {
          D3D11_TEXTURE2D_DESC td{};
          static_cast<ID3D11Texture2D *>(res)->GetDesc(&td);
          std::snprintf(one, sizeof one, " %u:%ux%u%s f%u", i, td.Width, td.Height, td.ArraySize > 1 ? "a" : "",
                        static_cast<unsigned>(td.Format));
        }
        res->Release();
      } else {
        std::snprintf(one, sizeof one, " %u:-", i);
      }
      d += one;
    }
    const std::string key = StackKey("ps", 1) + " (início " + std::to_string(start) + ", n " + std::to_string(num) +
                            ")" + d;
    std::lock_guard<std::mutex> lock(g_texMutex);
    ++g_srvCallers[key];
  }
  g_psSetSrv(c, start, num, v);
}

// "vigiar_vb=<stride>" (com rastrear_geo): nos draws da cena com esse stride no slot 0, conta por fase os
// vertex buffers (stride, bytes, uso) e o layout de entrada (linha LoadView[vb]). Malhas estáticas (stride 12)
// escuras na carga = algum fluxo de vértice provisório?
UINT g_watchVb = 0;
std::map<std::string, uint32_t> g_vbSeen; // sob g_texMutex
void VbCensus(ID3D11DeviceContext *ctx) {
  ID3D11Buffer *vbs[8] = {};
  UINT strides[8] = {}, offs[8] = {};
  ctx->IAGetVertexBuffers(0, 8, vbs, strides, offs);
  std::string k = g_active ? "carga" : "corrida";
  for (int i = 0; i < 8; ++i) {
    if (vbs[i] == nullptr) {
      continue;
    }
    D3D11_BUFFER_DESC d{};
    vbs[i]->GetDesc(&d);
    char one[64];
    std::snprintf(one, sizeof one, " vb%d:s%u u%u%s", i, strides[i], static_cast<unsigned>(d.Usage),
                  d.ByteWidth < 4096 ? " pequeno" : "");
    k += one;
    vbs[i]->Release();
  }
  ID3D11InputLayout *il = nullptr;
  ctx->IAGetInputLayout(&il);
  if (il != nullptr) {
    std::lock_guard<std::mutex> lock(g_layoutMutex);
    auto it = g_layouts.find(il);
    k += " | " + (it != g_layouts.end() ? it->second : std::string("layout ?"));
    il->Release();
  }
  std::lock_guard<std::mutex> lock(g_texMutex);
  ++g_vbSeen[k];
}

bool GeoDraw(ID3D11DeviceContext *ctx, uint64_t verts, bool indirect) {
  if (!GeoSceneTarget(ctx)) {
    return false;
  }
  {
    ID3D11VertexShader *vs = nullptr;
    ctx->VSGetShader(&vs, nullptr, nullptr);
    if (vs != nullptr) {
      vs->Release();
    }
    if (g_skipBigVs && !g_active && g_bigVs != nullptr && vs == g_bigVs) {
      return true;
    }
    if (g_fixCb7) {
      FixCb7(ctx, g_active);
    }
    ID3D11Buffer *vbs[8] = {};
    UINT strides[8] = {}, offs[8] = {};
    ctx->IAGetVertexBuffers(0, 8, vbs, strides, offs);
    uint32_t nvb = 0;
    for (int i = 0; i < 8; ++i) {
      if (vbs[i] != nullptr) {
        ++nvb;
        vbs[i]->Release();
      }
    }
    if (g_watchTex) {
      TexCensus(ctx, strides[0]);
    }
    if (g_watchVb != 0 && strides[0] == g_watchVb) {
      VbCensus(ctx);
    }
    if (g_swapCb != 0 && (strides[0] == 24 || strides[0] == 52 || strides[0] == 60 || strides[0] == 64)) {
      SwapCbs(ctx);
    }
    if (g_nullSlots != 0 && g_active == g_nullInLoad && g_psSetSrv != nullptr &&
        (g_nullStride != 0 ? strides[0] == g_nullStride
                           : (strides[0] == 24 || strides[0] == 52 || strides[0] == 60 || strides[0] == 64))) {
      ID3D11ShaderResourceView *none[1] = {};
      for (UINT i = 0; i < 32; ++i) {
        if ((g_nullSlots >> i) & 1u) {
          g_psSetSrv(ctx, i, 1, none);
        }
      }
    }
    if (g_skipStrideLoad != 0 && g_active && strides[0] == g_skipStrideLoad && verts >= 1000) {
      return true;
    }
    if (g_skipStride != 0 && !g_active && strides[0] == g_skipStride) {
      return true;
    }
    if (g_cbStride != 0 && strides[0] == g_cbStride && g_watchCb0 && g_cb0Buf.load(std::memory_order_relaxed) == nullptr) {
      ID3D11Buffer *cb0 = nullptr;
      ctx->VSGetConstantBuffers(0, 1, &cb0);
      if (cb0 != nullptr) {
        g_cb0Buf.store(cb0, std::memory_order_relaxed);
        cb0->Release();
        char line[96];
        std::snprintf(line, sizeof line, "LoadView[cb0]: vigiando %p", static_cast<void *>(cb0));
        Logger::Info(line);
      }
      ID3D11Buffer *cb3 = nullptr;
      ctx->VSGetConstantBuffers(3, 1, &cb3);
      if (cb3 != nullptr) {
        g_cb3Buf.store(cb3, std::memory_order_relaxed);
        cb3->Release();
      }
    }
    if (g_cbStride != 0 && strides[0] == g_cbStride && g_drawLogLeft.load(std::memory_order_relaxed) > 0) {
      g_drawLogLeft.fetch_sub(1, std::memory_order_relaxed);
      LogDrawDetail(ctx, vs);
    }
    if (g_cbStride2 != 0 && strides[0] == g_cbStride2 && verts >= 1000 &&
        g_drawLogLeft2.load(std::memory_order_relaxed) > 0) {
      g_drawLogLeft2.fetch_sub(1, std::memory_order_relaxed);
      LogDrawDetail(ctx, vs);
    }
    if (g_watchCb0 && g_cbStride != 0 && strides[0] == g_cbStride && verts >= 1000 &&
        g_cb5Buf.load(std::memory_order_relaxed) == nullptr) {
      ID3D11Buffer *cb5 = nullptr;
      ctx->VSGetConstantBuffers(5, 1, &cb5);
      if (cb5 != nullptr) {
        g_cb5Buf.store(cb5, std::memory_order_relaxed);
        cb5->Release();
        char line[96];
        std::snprintf(line, sizeof line, "LoadView[cb5]: vigiando %p", static_cast<void *>(cb5));
        Logger::Info(line);
      }
    }
    t_groundLoad = g_zAlways && g_active && g_cbStride != 0 && strides[0] == g_cbStride && verts >= 1000;
    if (g_bindCb4 && g_active && g_cbStride != 0 && strides[0] == g_cbStride && verts >= 1000) {
      ID3D11Buffer *cur = nullptr;
      ctx->VSGetConstantBuffers(4, 1, &cur);
      if (cur != nullptr) {
        cur->Release();
      } else if (ID3D11Buffer *b = g_cb4Buf.load(std::memory_order_relaxed); b != nullptr) {
        g_vsSetCb(ctx, 4, 1, &b);
        g_cb4Forced.fetch_add(1, std::memory_order_relaxed);
      }
    }
    if (g_watchCb0 && g_cbStride != 0 && strides[0] == g_cbStride) {
      std::lock_guard<std::mutex> lock(g_cb0Mutex);
      g_cb0SeqAtDraw = g_cb0Seq;
      if (t_bindItem != nullptr && verts >= 1000 && g_groundItems.size() < 4096) {
        g_groundItems[t_bindItem] = verts;
      }
    }
    VsTally &t = g_vsTally[vs];
    ++t.draws;
    t.verts += verts;
    t.nvb = nvb;
    t.stride0 = strides[0];
    {
      ID3D11RenderTargetView *rtv[8] = {};
      ctx->OMGetRenderTargets(8, rtv, nullptr);
      t.nrt = 0;
      for (int i = 0; i < 8; ++i) {
        if (rtv[i] != nullptr) {
          if (t.nrt++ == 0) {
            D3D11_RENDER_TARGET_VIEW_DESC rd{};
            rtv[i]->GetDesc(&rd);
            t.rtFmt = static_cast<uint32_t>(rd.Format);
          }
          rtv[i]->Release();
        }
      }
    }
    ID3D11DepthStencilState *ds = nullptr;
    UINT ref = 0;
    ctx->OMGetDepthStencilState(&ds, &ref);
    if (ds != nullptr) {
      D3D11_DEPTH_STENCIL_DESC dd{};
      ds->GetDesc(&dd);
      t.eq += dd.DepthFunc == D3D11_COMPARISON_EQUAL ? 1 : 0;
      ds->Release();
    }
  }
  g_geo.draws.fetch_add(1, std::memory_order_relaxed);
  g_geo.verts.fetch_add(verts, std::memory_order_relaxed);
  // Tiro 1: o 1º draw grande da carga fixa o VS; depois (e na corrida) só draws com esse VS.
  if (g_cbWant.load(std::memory_order_relaxed) && g_probePs) {
    ID3D11Buffer *vb0 = nullptr;
    UINT st0 = 0, off0 = 0;
    ctx->IAGetVertexBuffers(0, 1, &vb0, &st0, &off0);
    if (vb0 != nullptr) {
      vb0->Release();
    }
    const int shot = st0 == 24 ? 0 : st0 == 64 ? 1 : -1;
    if (shot >= 0 && !g_cbShots[shot].taken) {
      CbCapture(ctx, shot, g_frame[g_current.load(std::memory_order_relaxed)].draws.load(std::memory_order_relaxed), true);
      g_cbShots[shot].verts = static_cast<uint32_t>(verts);
    }
  }
  if (g_cbWant.load(std::memory_order_relaxed) && !g_probePs && (verts >= 20000 || g_cbStride != 0) &&
      !g_cbShots[1].taken) {
    ID3D11VertexShader *vs = nullptr;
    ctx->VSGetShader(&vs, nullptr, nullptr);
    if (vs != nullptr) {
      vs->Release();
    }
    if (g_cbStride != 0) {
      ID3D11Buffer *vb0 = nullptr;
      UINT st0 = 0, off0 = 0;
      ctx->IAGetVertexBuffers(0, 1, &vb0, &st0, &off0);
      if (vb0 != nullptr) {
        vb0->Release();
      }
      if (st0 == g_cbStride) {
        g_bigVs = vs;
      }
    } else if (g_bigVs == nullptr && g_active) {
      g_bigVs = vs;
    }
    if (vs != nullptr && vs == g_bigVs) {
      CbCapture(ctx, 1, g_frame[g_current.load(std::memory_order_relaxed)].draws.load(std::memory_order_relaxed));
      g_cbShots[1].verts = static_cast<uint32_t>(verts);
      // Texturas e buffers do VS e da geometria nesse draw.
      ID3D11ShaderResourceView *srvs[16] = {};
      ctx->VSGetShaderResources(0, 16, srvs);
      std::string desc;
      for (int i = 0; i < 16; ++i) {
        if (srvs[i] == nullptr) {
          continue;
        }
        D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
        srvs[i]->GetDesc(&sd);
        char one[48];
        std::snprintf(one, sizeof one, " t%d(dim %d fmt %d)", i, sd.ViewDimension, sd.Format);
        desc += one;
        srvs[i]->Release();
      }
      ID3D11Buffer *vbs[4] = {};
      UINT strides[4] = {}, offs[4] = {};
      ctx->IAGetVertexBuffers(0, 4, vbs, strides, offs);
      for (int i = 0; i < 4; ++i) {
        if (vbs[i] != nullptr) {
          D3D11_BUFFER_DESC bd{};
          vbs[i]->GetDesc(&bd);
          char one[64];
          std::snprintf(one, sizeof one, " vb%d(%u B, passo %u, bind 0x%x)", i, bd.ByteWidth, strides[i], bd.BindFlags);
          desc += one;
          vbs[i]->Release();
        }
      }
      static ULONGLONG srvAt = 0;
      if (GetTickCount64() >= srvAt) {
        srvAt = GetTickCount64() + 1000;
        char head[96];
        std::snprintf(head, sizeof head, "LoadView[vsrv]: %s vs %p, %llu vértices:", g_active ? "carga" : "corrida",
                      static_cast<void *>(vs), static_cast<unsigned long long>(verts));
        Logger::Info(head + desc);
      }
    }
  }
  if (indirect) {
    g_geo.indirect.fetch_add(1, std::memory_order_relaxed);
  } else if (verts >= 3000) {
    g_geo.big.fetch_add(1, std::memory_order_relaxed);
  }
  ID3D11RenderTargetView *rtv = nullptr;
  ID3D11DepthStencilView *dsv = nullptr;
  ctx->OMGetRenderTargets(1, &rtv, &dsv);
  if (rtv != nullptr) {
    rtv->Release();
  }
  if (dsv == nullptr) {
    g_geo.noDsv.fetch_add(1, std::memory_order_relaxed);
  } else {
    dsv->Release();
  }
  ID3D11DepthStencilState *ds = nullptr;
  UINT ref = 0;
  ctx->OMGetDepthStencilState(&ds, &ref);
  D3D11_DEPTH_STENCIL_DESC dd{TRUE, D3D11_DEPTH_WRITE_MASK_ALL, D3D11_COMPARISON_LESS, FALSE};
  if (ds != nullptr) {
    ds->GetDesc(&dd);
    ds->Release();
  }
  if (!dd.DepthEnable) {
    g_geo.depthOff.fetch_add(1, std::memory_order_relaxed);
  } else {
    g_geo.func[dd.DepthFunc < 9 ? dd.DepthFunc : 0].fetch_add(1, std::memory_order_relaxed);
    if (dd.DepthWriteMask == D3D11_DEPTH_WRITE_MASK_ZERO) {
      g_geo.noWrite.fetch_add(1, std::memory_order_relaxed);
    }
  }
  if (dd.StencilEnable) {
    g_geo.stencil.fetch_add(1, std::memory_order_relaxed);
  }
  ID3D11Predicate *pred = nullptr;
  BOOL value = FALSE;
  ctx->GetPredication(&pred, &value);
  if (pred != nullptr) {
    g_geo.predicated.fetch_add(1, std::memory_order_relaxed);
    pred->Release();
  }
  ID3D11RasterizerState *rs = nullptr;
  ctx->RSGetState(&rs);
  if (rs != nullptr) {
    D3D11_RASTERIZER_DESC rd{};
    rs->GetDesc(&rd);
    if (rd.CullMode == D3D11_CULL_NONE) {
      g_geo.cullNone.fetch_add(1, std::memory_order_relaxed);
    }
    rs->Release();
  }
  UINT n = 1;
  D3D11_VIEWPORT vp{};
  ctx->RSGetViewports(&n, &vp);
  g_geo.vpW.store(static_cast<uint32_t>(vp.Width), std::memory_order_relaxed);
  g_geo.vpH.store(static_cast<uint32_t>(vp.Height), std::memory_order_relaxed);
  g_geo.vpMinZ.store(static_cast<uint32_t>(vp.MinDepth * 1000.f), std::memory_order_relaxed);
  g_geo.vpMaxZ.store(static_cast<uint32_t>(vp.MaxDepth * 1000.f), std::memory_order_relaxed);
  return false;
}

// Dispatch por chamador e pelos buffers UAV que escreve (linha LoadView[cs]).
std::mutex g_csMutex;
std::map<std::string, uint32_t> g_csSeen;
void GeoDispatch(ID3D11DeviceContext *c, uintptr_t ret, UINT x, bool indirect) {
  g_geo.dispatches.fetch_add(1, std::memory_order_relaxed);
  ID3D11UnorderedAccessView *uavs[8] = {};
  c->CSGetUnorderedAccessViews(0, 8, uavs);
  char key[256];
  int n = std::snprintf(key, sizeof key, "+%llx%s%s",
                        static_cast<unsigned long long>(ret >= g_exeBase && ret < g_exeEnd ? ret - g_exeBase : ret),
                        ret >= g_exeBase && ret < g_exeEnd ? "" : "(fora)", indirect ? " ind" : "");
  for (int i = 0; i < 8; ++i) {
    if (uavs[i] == nullptr) {
      continue;
    }
    ID3D11Resource *res = nullptr;
    uavs[i]->GetResource(&res);
    ID3D11Buffer *buf = nullptr;
    if (res != nullptr &&
        SUCCEEDED(res->QueryInterface(__uuidof(ID3D11Buffer), reinterpret_cast<void **>(&buf))) && buf != nullptr) {
      D3D11_BUFFER_DESC bd{};
      buf->GetDesc(&bd);
      if (n > 0 && n < static_cast<int>(sizeof key)) {
        n += std::snprintf(key + n, sizeof key - n, " u%d=%uB/%u", i, bd.ByteWidth, bd.StructureByteStride);
      }
      buf->Release();
    } else if (n > 0 && n < static_cast<int>(sizeof key)) {
      n += std::snprintf(key + n, sizeof key - n, " u%d=tex", i);
    }
    if (res != nullptr) {
      res->Release();
    }
    uavs[i]->Release();
  }
  (void)x;
  std::lock_guard<std::mutex> lock(g_csMutex);
  ++g_csSeen[key];
  // Dispatch que a carga não fez: pilha uma vez.
  static std::set<std::string> known;
  if (g_active) {
    known.insert(key);
  } else if (known.insert(key).second) {
    void *frames[24];
    const USHORT fn = RtlCaptureStackBackTrace(0, 24, frames, nullptr);
    std::string stack = std::string("LoadView[csnovo]: ") + key + " pilha";
    for (USHORT i = 0; i < fn; ++i) {
      const auto a = reinterpret_cast<uintptr_t>(frames[i]);
      if (a >= g_exeBase && a < g_exeEnd) {
        char one[24];
        std::snprintf(one, sizeof one, " +%llx", static_cast<unsigned long long>(a - g_exeBase));
        stack += one;
      }
    }
    Logger::Info(stack);
  }
}
void STDMETHODCALLTYPE HookDispatch(ID3D11DeviceContext *c, UINT x, UINT y, UINT z) {
  InFlight f;
  if (g_traceGeo && c == g_context && g_measure.load(std::memory_order_relaxed)) {
    GeoDispatch(c, reinterpret_cast<uintptr_t>(__builtin_return_address(0)), x, false);
  }
  g_dispatch(c, x, y, z);
}
void STDMETHODCALLTYPE HookDispatchIndirect(ID3D11DeviceContext *c, ID3D11Buffer *b, UINT o) {
  InFlight f;
  if (g_traceGeo && c == g_context && g_measure.load(std::memory_order_relaxed)) {
    GeoDispatch(c, reinterpret_cast<uintptr_t>(__builtin_return_address(0)), 0, true);
  }
  g_dispatchIndirect(c, b, o);
}
// Limpeza da profundidade de um alvo grande (a da cena).
void STDMETHODCALLTYPE HookClearDsv(ID3D11DeviceContext *c, ID3D11DepthStencilView *dsv, UINT flags, FLOAT depth,
                                    UINT8 stencil) {
  InFlight f;
  if (g_traceGeo && c == g_context && g_measure.load(std::memory_order_relaxed) && dsv != nullptr) {
    ID3D11Resource *res = nullptr;
    dsv->GetResource(&res);
    ID3D11Texture2D *tex = nullptr;
    if (res != nullptr &&
        SUCCEEDED(res->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void **>(&tex))) &&
        tex != nullptr) {
      D3D11_TEXTURE2D_DESC desc{};
      tex->GetDesc(&desc);
      if (desc.Width >= 1280 && desc.SampleDesc.Count > 1) {
        g_geo.clears.fetch_add(1, std::memory_order_relaxed);
        g_geo.clearDepth.store(static_cast<uint32_t>(depth * 1000.f), std::memory_order_relaxed);
        g_geo.clearFlags.store(flags, std::memory_order_relaxed);
      }
      tex->Release();
    }
    if (res != nullptr) {
      res->Release();
    }
  }
  g_clearDsv(c, dsv, flags, depth, stencil);
}

void DescribeTarget(Target &t) {
  ID3D11Texture2D *tex = nullptr;
  if (SUCCEEDED(t.res->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void **>(&tex))) &&
      tex != nullptr) {
    D3D11_TEXTURE2D_DESC desc{};
    tex->GetDesc(&desc);
    t.width = desc.Width;
    t.height = desc.Height;
    t.format = desc.Format;
    t.bind = desc.BindFlags;
    t.samples = desc.SampleDesc.Count;
    tex->Release();
  }
}

// O alvo que o jogo acabou de ligar: o 1º de cor, ou a profundidade.
void Bind(ID3D11DeviceContext *ctx, UINT count, ID3D11RenderTargetView *const *rtvs, ID3D11DepthStencilView *dsv) {
  if (!g_measure.load(std::memory_order_relaxed) || g_own.load(std::memory_order_relaxed) || ctx != g_context) {
    return;
  }
  ID3D11View *view = nullptr;
  bool depth = false;
  if (count > 0 && rtvs != nullptr && rtvs[0] != nullptr) {
    view = rtvs[0];
  } else if (dsv != nullptr) {
    view = dsv;
    depth = true;
  }
  if (view == nullptr) {
    g_current.store(-1, std::memory_order_relaxed);
    return;
  }
  ID3D11Resource *res = nullptr;
  view->GetResource(&res);
  if (res == nullptr) {
    g_current.store(-1, std::memory_order_relaxed);
    return;
  }
  std::lock_guard<std::mutex> lock(g_mutex);
  for (int i = 0; i < g_frameCount; ++i) {
    if (g_frame[i].res == res) {
      res->Release();
      g_current.store(i, std::memory_order_relaxed);
      return;
    }
  }
  if (g_frameCount >= static_cast<int>(g_frame.size())) {
    res->Release();
    g_current.store(-1, std::memory_order_relaxed);
    return;
  }
  Target &t = g_frame[g_frameCount];
  t.res = res; // fica com a ref do GetResource até o fim do quadro
  t.depth = depth;
  t.width = t.height = 0;
  t.format = DXGI_FORMAT_UNKNOWN;
  t.bind = 0;
  t.samples = 1;
  t.draws.store(0, std::memory_order_relaxed);
  DescribeTarget(t);
  g_current.store(g_frameCount, std::memory_order_relaxed);
  ++g_frameCount;
}

// "vigiar_z=1" (com rastrear_geo): conta os draws do alvo da cena por fase, função de profundidade e stride
// (vão na linha LoadView[vista]). "z_igual=<func>": na carga, os draws com teste "igual" (3) passam a usar
// <func> (8 = sempre, 4 = menor ou igual) sem gravar profundidade. Objetos escuros = cor reprovada no "igual"?
std::mutex g_viewMutex;
std::map<std::string, uint32_t> g_viewGates;
bool g_zWatch = false;
UINT g_zEqualFunc = 0;
std::map<ID3D11DepthStencilState *, ID3D11DepthStencilState *> g_zEqualStates; // só na thread de render
std::atomic<uint32_t> g_zEqualDraws{0};
struct ZGuard {
  ID3D11DeviceContext *c = nullptr;
  ID3D11DepthStencilState *orig = nullptr;
  UINT ref = 0;
  bool swapped = false;
  explicit ZGuard(ID3D11DeviceContext *ctx) {
    if ((!g_zWatch && (g_zEqualFunc == 0 || !g_active)) || !GeoSceneTarget(ctx)) {
      return;
    }
    c = ctx;
    c->OMGetDepthStencilState(&orig, &ref);
    D3D11_DEPTH_STENCIL_DESC dd{};
    if (orig != nullptr) {
      orig->GetDesc(&dd);
    }
    if (g_zWatch) {
      ID3D11Buffer *vb = nullptr;
      UINT stride = 0, off = 0;
      c->IAGetVertexBuffers(0, 1, &vb, &stride, &off);
      if (vb != nullptr) {
        vb->Release();
      }
      char k[96];
      std::snprintf(k, sizeof k, "z %s f%d w%d s%u", g_active ? "carga" : "corrida",
                    orig != nullptr && dd.DepthEnable ? static_cast<int>(dd.DepthFunc) : 0,
                    static_cast<int>(dd.DepthWriteMask), stride);
      std::lock_guard<std::mutex> lock(g_viewMutex);
      ++g_viewGates[k];
    }
    if (g_zEqualFunc != 0 && g_active && orig != nullptr && dd.DepthEnable &&
        dd.DepthFunc == D3D11_COMPARISON_EQUAL) {
      ID3D11DepthStencilState *&mine = g_zEqualStates[orig];
      if (mine == nullptr) {
        dd.DepthFunc = static_cast<D3D11_COMPARISON_FUNC>(g_zEqualFunc);
        dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
        ID3D11Device *dev = nullptr;
        c->GetDevice(&dev);
        if (dev != nullptr) {
          dev->CreateDepthStencilState(&dd, &mine);
          dev->Release();
        }
      }
      if (mine != nullptr) {
        c->OMSetDepthStencilState(mine, ref);
        swapped = true;
        g_zEqualDraws.fetch_add(1, std::memory_order_relaxed);
      }
    }
  }
  ~ZGuard() {
    if (c == nullptr) {
      return;
    }
    if (swapped) {
      c->OMSetDepthStencilState(orig, ref);
    }
    if (orig != nullptr) {
      orig->Release();
    }
  }
};

void STDMETHODCALLTYPE HookDrawIndexed(ID3D11DeviceContext *c, UINT a, UINT b, INT d) {
  InFlight f;
  t_args = {'I', a, 1, b, static_cast<UINT>(d), 0};
  CountDraw(c);
  t_groundLoad = false;
  if (GeoDraw(c, a, false)) {
    return;
  }
  ID3D11DepthStencilState *orig = nullptr;
  UINT ref = 0;
  if (t_groundLoad) {
    c->OMGetDepthStencilState(&orig, &ref);
    ID3D11DepthStencilState *&mine = g_zAlwaysStates[orig];
    if (mine == nullptr) {
      D3D11_DEPTH_STENCIL_DESC dd{};
      if (orig != nullptr) {
        orig->GetDesc(&dd);
      } else {
        dd.DepthEnable = TRUE;
        dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
      }
      dd.DepthFunc = D3D11_COMPARISON_ALWAYS;
      ID3D11Device *dev = nullptr;
      c->GetDevice(&dev);
      if (dev != nullptr) {
        dev->CreateDepthStencilState(&dd, &mine);
        dev->Release();
      }
    }
    if (mine != nullptr) {
      c->OMSetDepthStencilState(mine, ref);
      g_zAlwaysDraws.fetch_add(1, std::memory_order_relaxed);
    }
  }
  ZGuard zg(c);
  g_drawIndexed(c, a, b, d);
  if (t_snap) {
    t_snap = false;
    SnapTarget(c, "depois");
    ++g_snapN;
  }
  if (t_groundLoad) {
    c->OMSetDepthStencilState(orig, ref);
    if (orig != nullptr) {
      orig->Release();
    }
  }
}
void STDMETHODCALLTYPE HookDraw(ID3D11DeviceContext *c, UINT a, UINT b) {
  InFlight f;
  t_args = {'D', a, 1, b, 0, 0};
  CountDraw(c);
  if (GeoDraw(c, a, false)) {
    return;
  }
  ZGuard zg(c);
  g_draw(c, a, b);
}
void STDMETHODCALLTYPE HookDrawIndexedInstanced(ID3D11DeviceContext *c, UINT a, UINT b, UINT d, INT e, UINT g) {
  InFlight f;
  t_args = {'J', a, b, d, static_cast<UINT>(e), g};
  CountDraw(c);
  if (GeoDraw(c, static_cast<uint64_t>(a) * b, false)) {
    return;
  }
  ZGuard zg(c);
  g_drawIndexedInstanced(c, a, b, d, e, g);
}
void STDMETHODCALLTYPE HookDrawInstanced(ID3D11DeviceContext *c, UINT a, UINT b, UINT d, UINT e) {
  InFlight f;
  t_args = {'N', a, b, d, 0, e};
  CountDraw(c);
  if (GeoDraw(c, static_cast<uint64_t>(a) * b, false)) {
    return;
  }
  ZGuard zg(c);
  g_drawInstanced(c, a, b, d, e);
}
void STDMETHODCALLTYPE HookDrawIndexedIndirect(ID3D11DeviceContext *c, ID3D11Buffer *b, UINT o) {
  InFlight f;
  t_args = {'X', 0, 0, o, 0, 0};
  CountDraw(c);
  if (GeoDraw(c, 0, true)) {
    return;
  }
  g_drawIndexedIndirect(c, b, o);
}
void STDMETHODCALLTYPE HookDrawIndirect(ID3D11DeviceContext *c, ID3D11Buffer *b, UINT o) {
  InFlight f;
  t_args = {'Y', 0, 0, o, 0, 0};
  CountDraw(c);
  if (GeoDraw(c, 0, true)) {
    return;
  }
  g_drawIndirect(c, b, o);
}
void STDMETHODCALLTYPE HookSetRT(ID3D11DeviceContext *c, UINT n, ID3D11RenderTargetView *const *rtvs,
                                 ID3D11DepthStencilView *dsv) {
  InFlight f;
  Bind(c, n, rtvs, dsv);
  g_setRT(c, n, rtvs, dsv);
}
void STDMETHODCALLTYPE HookSetRTUAV(ID3D11DeviceContext *c, UINT n, ID3D11RenderTargetView *const *rtvs,
                                    ID3D11DepthStencilView *dsv, UINT us, UINT un,
                                    ID3D11UnorderedAccessView *const *uavs, const UINT *counts) {
  InFlight f;
  if (n != D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL) {
    Bind(c, n, rtvs, dsv);
  }
  g_setRTUAV(c, n, rtvs, dsv, us, un, uavs, counts);
}
void STDMETHODCALLTYPE HookExecute(ID3D11DeviceContext *c, ID3D11CommandList *list, BOOL restore) {
  InFlight f;
  if (g_measure.load(std::memory_order_relaxed) && c == g_context) {
    g_lists.fetch_add(1, std::memory_order_relaxed);
  }
  g_execute(c, list, restore);
}

void __fastcall HookRaceFrame(void *renderer, void *dt) {
  InFlight f;
  if (g_measure.load(std::memory_order_relaxed) && renderer != nullptr) {
    const auto *b = static_cast<const uint8_t *>(renderer);
    g_lastScene.store(*reinterpret_cast<uint8_t *const *>(b + 0x2050), std::memory_order_relaxed);
    RenderState st;
    std::memcpy(&st.mode, b + 0x22a8, 4);
    std::memcpy(&st.s2310, b + 0x2310, 4);
    std::memcpy(&st.s2314, b + 0x2314, 4);
    std::memcpy(&st.s23b0, b + 0x23b0, 4);
    void *checked = *reinterpret_cast<void *const *>(b + 0x2298);
    if (st.mode <= 2 && checked != nullptr) {
      st.skip = reinterpret_cast<SkipCheckFn>(g_exeBase + kSkipCheckRva)(checked) ? 1 : 0;
    }
    const auto *manager = *reinterpret_cast<const uint8_t *const *>(b + 0x38);
    if (manager != nullptr) {
      st.world = manager[0x16308];
    }
    if (!g_active) {
      SaveStartLook();
    }
    static uint32_t camTick = 0;
    auto *cam = *reinterpret_cast<const uint8_t *const *>(b + 0x2060);
    if (cam != nullptr && camTick++ % 30 == 0) {
      float m[16];
      std::memcpy(m, cam + 0x1110, sizeof m);
      char line[320];
      std::snprintf(line, sizeof line,
                    "LoadView[camera]: modo=%u cam=%p [%.2f %.2f %.2f %.2f] [%.2f %.2f %.2f %.2f] [%.2f %.2f %.2f "
                    "%.2f] [%.1f %.1f %.1f %.2f]",
                    st.mode, cam, m[0], m[1], m[2], m[3], m[4], m[5], m[6], m[7], m[8], m[9], m[10], m[11], m[12],
                    m[13], m[14], m[15]);
      Logger::Info(line);
      // Câmera da especial (a mesma da FreeCamera): [[exe+0x168caf0]+0x20]+0x1cf8.
      const auto owner = *reinterpret_cast<const uintptr_t *>(g_exeBase + 0x168caf0);
      uintptr_t stage = 0;
      if (owner > 0x10000 && *reinterpret_cast<const uintptr_t *>(owner) == g_exeBase + 0x127a030) {
        const auto world = *reinterpret_cast<const uintptr_t *>(owner + 0x20);
        if (world > 0x10000) {
          stage = *reinterpret_cast<const uintptr_t *>(world + 0x1cf8);
        }
      }
      if (stage > 0x10000) {
        float r[16];
        std::memcpy(r, reinterpret_cast<const void *>(stage + 0x210), sizeof r);
        std::snprintf(line, sizeof line,
                      "LoadView[especial]: cam=%p frente? [%.2f %.2f %.2f] [%.2f %.2f %.2f] [%.2f %.2f %.2f] olho "
                      "[%.1f %.1f %.1f]",
                      reinterpret_cast<void *>(stage), r[0], r[1], r[2], r[4], r[5], r[6], r[8], r[9], r[10], r[12],
                      r[13], r[14]);
      } else {
        std::snprintf(line, sizeof line, "LoadView[especial]: sem camera (dono %p)", reinterpret_cast<void *>(owner));
      }
      Logger::Info(line);
    }
    if (st != g_lastState) {
      g_lastState = st;
      char line[200];
      std::snprintf(line, sizeof line,
                    "LoadView[estado]: modo=%u pula_cena=%d mundo=%d +2310=%u +2314=%u +23b0=%u (renderer %p)%s",
                    st.mode, st.skip, st.world, st.s2310, st.s2314, st.s23b0, renderer,
                    g_active ? "" : " (corrida)");
      Logger::Info(line);
    }
  }
  if (g_active && g_forceAfterMs >= 0 && renderer != nullptr) {
    auto *mode = reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(renderer) + 0x22a8);
    if (*mode != 2) {
      g_mode2Since = 0;
    } else {
      const ULONGLONG now = GetTickCount64();
      if (g_mode2Since == 0) {
        g_mode2Since = now;
      }
      if (now - g_mode2Since >= static_cast<ULONGLONG>(g_forceAfterMs)) {
        if (g_forcedFrames++ % 60 == 0) {
          char line[96];
          std::snprintf(line, sizeof line, "LoadView[forca]: quadro %u da carga como modo 0.", g_forcedFrames);
          Logger::Info(line);
        }
        // +0x88: subsistema ainda não montado na carga (abd040 cai num
        // ponteiro de função lixo); o modo 0 já o pula quando é nulo.
        auto **sub88 = reinterpret_cast<void **>(static_cast<uint8_t *>(renderer) + 0x88);
        void *saved88 = *sub88;
        *sub88 = nullptr;
        // A cena guarda uma cópia do modo em +0x1e98 (SetMode -> 3c4a80); com
        // 1 ou 2 o passe do mundo (3c9170) sai sem desenhar nada.
        auto *scene = *reinterpret_cast<uint8_t **>(static_cast<uint8_t *>(renderer) + 0x2050);
        uint32_t sceneMode = 0;
        if (scene != nullptr) {
          std::memcpy(&sceneMode, scene + 0x1e98, 4);
          std::memset(scene + 0x1e98, 0, 4);
        }
        // [renderer+0x2060]+0x1140: posição de referência (na corrida segue a
        // câmera; na carga o SetMode deixa (0,1,0)).
        auto *cam = *reinterpret_cast<uint8_t **>(static_cast<uint8_t *>(renderer) + 0x2060);
        float savedPos[3] = {};
        if (g_forcePos && cam != nullptr) {
          std::memcpy(savedPos, cam + 0x1140, sizeof savedPos);
          std::memcpy(cam + 0x1140, g_forcePosXYZ, sizeof g_forcePosXYZ);
        }
        uint8_t savedStage[0x40];
        uint8_t *stage = ApplyLook(savedStage);
        ApplySceneCam(scene);
        *mode = 0;
        g_forcedScene.store(scene, std::memory_order_relaxed);
        g_raceFrame(renderer, dt);
        g_forcedScene.store(nullptr, std::memory_order_relaxed);
        if (g_forceLook && scene != nullptr && g_forcedFrames % 60 == 1) {
          // Câmera de render ([cena+0x38]): olho em +0x40 do quadro [+0x168].
          auto *rc = *reinterpret_cast<uint8_t **>(scene + 0x38);
          auto *src = *reinterpret_cast<uint8_t **>(scene + 0x17c0);
          if (rc != nullptr && src != nullptr) {
            uint32_t idx = 0;
            std::memcpy(&idx, rc + 0x168, 4);
            const auto *e = reinterpret_cast<const float *>(rc + idx * 64 + 0x40);
            const auto *z = reinterpret_cast<const float *>(rc + idx * 64 + 0x30);
            const auto *o = reinterpret_cast<const float *>(src + 0xd0);
            char line[200];
            std::snprintf(line, sizeof line,
                          "LoadView[camrender]: %p quadro %u olho [%.1f %.1f %.1f] eixo z [%.2f %.2f %.2f]; fonte "
                          "[%.1f %.1f %.1f]",
                          static_cast<void *>(rc), idx, e[0], e[1], e[2], z[0], z[1], z[2], o[0], o[1], o[2]);
            Logger::Info(line);
          }
        }
        if (stage != nullptr) {
          std::memcpy(stage + 0x210, savedStage, sizeof savedStage);
        }
        if (g_forcePos && cam != nullptr) {
          std::memcpy(cam + 0x1140, savedPos, sizeof savedPos);
        }
        if (*mode == 0) {
          *mode = 2;
        }
        if (scene != nullptr && !g_keepSceneMode) {
          uint32_t now0 = 1;
          std::memcpy(&now0, scene + 0x1e98, 4);
          if (now0 == 0) {
            std::memcpy(scene + 0x1e98, &sceneMode, 4);
          }
        }
        if (*sub88 == nullptr) {
          *sub88 = saved88;
        }
        return;
      }
    }
  }
  g_raceFrame(renderer, dt);
}

// 0x140bbf4a0: desenha uma lista ligada de itens de render (nó -> [nó], item
// em [nó+0x18]); o passe do mundo (3c9170, via bbdfb0) passa a lista visível
// da vista. Conta itens por segundo para ver se a lista existe na carga.
constexpr uintptr_t kItemListRva = 0xbbf4a0;
constexpr uint8_t kItemListPrologue[] = {0x48, 0x89, 0x5c, 0x24, 0x10, 0x48, 0x89, 0x6c, 0x24, 0x18};
using ItemListFn = int(__fastcall *)(void *, void *, void *, void *, void *, int);
ItemListFn g_itemList = nullptr;
std::atomic<uint64_t> g_secItems{0}, g_secItemCalls{0};
// Por chamador (cada passe chama de um ponto diferente): chamadas e itens no
// segundo, para comparar a carga forçada com a corrida.
struct ItemCaller {
  uintptr_t caller;
  uint64_t calls, items, draws;
  uint32_t width, height; // alvo ligado na última chamada (0 = contexto adiado)
};
std::mutex g_itemCallerMutex;
std::array<ItemCaller, 48> g_itemCallers{};
int __fastcall HookItemList(void *head, void *a, void *b, void *c, void *d, int e) {
  InFlight f;
  if (g_measure.load(std::memory_order_relaxed)) {
    uint64_t n = 0;
    for (auto *node = static_cast<void *const *>(head); node != nullptr && n < 1000000; ++n) {
      node = static_cast<void *const *>(*node);
    }
    g_secItems.fetch_add(n, std::memory_order_relaxed);
    g_secItemCalls.fetch_add(1, std::memory_order_relaxed);
    const uintptr_t caller = reinterpret_cast<uintptr_t>(__builtin_return_address(0)) - g_exeBase;
    const uint64_t before = t_draws;
    const int ret = g_itemList(head, a, b, c, d, e);
    const uint64_t drawn = t_draws - before;
    uint32_t w = 0, h = 0;
    const int cur = g_current.load(std::memory_order_relaxed);
    if (drawn > 0 && cur >= 0 && cur < static_cast<int>(g_frame.size())) {
      w = g_frame[cur].width;
      h = g_frame[cur].height;
    }
    std::lock_guard<std::mutex> lock(g_itemCallerMutex);
    for (auto &slot : g_itemCallers) {
      if (slot.caller == caller || slot.caller == 0) {
        slot.caller = caller;
        ++slot.calls;
        slot.items += n;
        slot.draws += drawn;
        if (drawn > 0) {
          slot.width = w;
          slot.height = h;
        }
        break;
      }
    }
    return ret;
  }
  return g_itemList(head, a, b, c, d, e);
}

// 0x1406c2de0: atualização do mundo (método virtual, xmm1 = dt). Avança o
// estado do terreno [self+0xf8]+0xd0: 4 -> 6a57c0 e acc960 (4 -> 5); 5 ->
// [self+0x108]->vt[0x20] e acc960 (5 -> 6). Loga quem chama e se roda na carga.
constexpr uintptr_t kWorldUpdateRva = 0x6c2de0;
constexpr uint8_t kWorldUpdatePrologue[] = {0x40, 0x53, 0x48, 0x83, 0xec, 0x30, 0x48, 0x8b, 0x81, 0xf8};
using WorldUpdateFn = void(__fastcall *)(void *, float);
WorldUpdateFn g_worldUpdate = nullptr;
std::atomic<uint64_t> g_secWorldUpd{0};
int g_worldUpdState = -2;
void __fastcall HookWorldUpdate(void *self, float dt) {
  InFlight fl;
  if (g_measure.load(std::memory_order_relaxed) && self != nullptr) {
    g_secWorldUpd.fetch_add(1, std::memory_order_relaxed);
    const auto *terrain = *reinterpret_cast<uint8_t *const *>(static_cast<uint8_t *>(self) + 0xf8);
    const int state = terrain != nullptr ? *reinterpret_cast<const int *>(terrain + 0xd0) : -1;
    if (state != g_worldUpdState) {
      const auto caller = reinterpret_cast<uintptr_t>(__builtin_return_address(0)) - g_exeBase;
      char line[200];
      std::snprintf(line, sizeof line, "LoadView[mundo]: self %p terreno %p estado %d -> %d, dt %.4f, chamador +%llx, %s",
                    self, terrain, g_worldUpdState, state, dt, static_cast<unsigned long long>(caller),
                    g_active ? "carga" : "corrida");
      Logger::Info(line);
      g_worldUpdState = state;
    }
  }
  g_worldUpdate(self, dt);
}

// 0x140497fe0(renderer): fim do quadro, chamado por 4b26f0. Só com o modo 0
// ([renderer+0x22a8]) zera os buffers de itens de desenho dos dois Ms
// ([renderer+0x70] por 0x140ab16f0, contador [M+0x1b28]; [renderer+0x78] por
// 0x140ab1870, contador +0x14d8). O 0x140a78150 pega blocos de 64 itens com
// lock xadd no contador sem checar a capacidade (0x3ac0 itens em [M+0x1b20]):
// nos quadros forçados da carga o contador nunca zerava, passava do fim e
// escrevia por cima do array de buffers de instância que vem logo depois no
// heap (crash no upload 0x140ab20e0 e no próprio reset depois da largada).
// Nos quadros forçados roda como modo 0.
constexpr uintptr_t kFrameResetRva = 0x497fe0;
constexpr uint8_t kFrameResetPrologue[] = {0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x83, 0xb9, 0xa8, 0x22, 0x00, 0x00, 0x00};
using FrameResetFn = void(__fastcall *)(void *);
FrameResetFn g_frameReset = nullptr;
std::atomic<uint32_t> g_secFrameReset{0};
void __fastcall HookFrameReset(void *renderer) {
  InFlight fl;
  auto *r = static_cast<uint8_t *>(renderer);
  if (g_active && g_forceVis && g_forceAfterMs >= 0 && r != nullptr && g_mode2Since != 0 &&
      GetTickCount64() - g_mode2Since >= static_cast<ULONGLONG>(g_forceAfterMs)) {
    auto *mode = reinterpret_cast<uint32_t *>(r + 0x22a8);
    if (*mode != 0) {
      const uint32_t saved = *mode;
      *mode = 0;
      g_frameReset(renderer);
      if (*mode == 0) {
        *mode = saved;
      }
      g_secFrameReset.fetch_add(1, std::memory_order_relaxed);
      return;
    }
  }
  g_frameReset(renderer);
}

// 0x1404aabd0: preparo do quadro (renderer, rdx), chamado por 4b26f0 antes
// do quadro da corrida. Com o modo 0 atualiza a cena (3c4190/3c6910/3cdf20)
// e chama 3c7aa0, que com scene+0x1e98 == 0 dispara a visibilidade do mundo
// (jobs a7f700 que enchem as listas da vista); com 1/2 pula tudo isso.
// Experimento "forcar_vis=1": nos quadros forçados roda como modo 0.
constexpr uintptr_t kFramePrepRva = 0x4aabd0;
constexpr uint8_t kFramePrepPrologue[] = {0x48, 0x89, 0x7c, 0x24, 0x20, 0x41, 0x56, 0x48, 0x81, 0xec};
using FramePrepFn = void(__fastcall *)(void *, void *);
FramePrepFn g_framePrep = nullptr;
uint64_t g_prepWhy = ~0ull;
std::atomic<uint64_t> g_secPrep{0};
void __fastcall HookFramePrep(void *renderer, void *arg) {
  InFlight fl;
  g_secPrep.fetch_add(1, std::memory_order_relaxed);
  auto *r = static_cast<uint8_t *>(renderer);
  if (g_active && g_forceVis && g_forceAfterMs >= 0 && r != nullptr && g_mode2Since != 0 &&
      GetTickCount64() - g_mode2Since >= static_cast<ULONGLONG>(g_forceAfterMs)) {
    auto *mode = reinterpret_cast<uint32_t *>(r + 0x22a8);
    auto *prev = reinterpret_cast<uint32_t *>(r + 0x22ac);
    auto *scene = *reinterpret_cast<uint8_t **>(r + 0x2050);
    const uint64_t why = (uint64_t(*mode) << 40) | (uint64_t(*prev) << 8) | (scene != nullptr ? 1u : 0u);
    if (why != g_prepWhy) {
      g_prepWhy = why;
      char line[128];
      std::snprintf(line, sizeof line, "LoadView[forca]: preparo com modo=%u prev=%u cena=%p.", *mode, *prev,
                    static_cast<void *>(scene));
      Logger::Info(line);
    }
    if (*mode == 2 && (*prev == 2 || *prev == 0) && scene != nullptr) {
      const uint32_t wanted = *prev;
      if (g_forcedVis++ % 60 == 0) {
        char line[96];
        std::snprintf(line, sizeof line, "LoadView[forca]: preparo %u da carga como modo 0.", g_forcedVis);
        Logger::Info(line);
      }
      uint32_t sceneMode = 0;
      std::memcpy(&sceneMode, scene + 0x1e98, 4);
      std::memset(scene + 0x1e98, 0, 4);
      uint8_t savedStage[0x40];
      uint8_t *stage = ApplyLook(savedStage);
      ApplySceneCam(scene);
      *mode = 0;
      *prev = 0;
      g_forcedScene.store(scene, std::memory_order_relaxed);
      g_framePrep(renderer, arg);
      g_forcedScene.store(nullptr, std::memory_order_relaxed);
      ApplySceneCam(scene);
      if (*mode == 0) {
        *mode = 2;
      }
      if (*prev == 0) {
        *prev = wanted;
      }
      if (stage != nullptr) {
        std::memcpy(stage + 0x210, savedStage, sizeof savedStage);
      }
      uint32_t now0 = 1;
      std::memcpy(&now0, scene + 0x1e98, 4);
      if (now0 == 0) {
        std::memcpy(scene + 0x1e98, &sceneMode, 4);
      }
      return;
    }
  }
  g_framePrep(renderer, arg);
}

// 0x1409d9b80: diretor de câmera (ctx, câmera, r8, xmm3 float, int na pilha).
// Escreve a pose em [câmera+0x60/+0xa0] a partir da câmera do carro; roda no
// preparo e de novo dentro do render da cena (9d6cc0), logo antes da cópia
// para a câmera de render. Na carga não há carro e ele zera a câmera da cena;
// nos quadros forçados com "olhar=" a pose do ini é reaplicada depois dele.
constexpr uintptr_t kDirectorRva = 0x9d9b80;
constexpr uint8_t kDirectorPrologue[] = {0x48, 0x8b, 0xc4, 0x48, 0x89, 0x58, 0x10, 0x48, 0x89, 0x70, 0x18};
using DirectorFn = uint64_t(__fastcall *)(void *, void *, void *, float, uint32_t);
DirectorFn g_director = nullptr;
uint64_t __fastcall HookDirector(void *ctx, void *cam, void *r8, float x3, uint32_t flags) {
  InFlight fl;
  if (g_watchCb0 && g_active && r8 != nullptr) {
    static std::atomic<uint32_t> n{0};
    if (n.fetch_add(1, std::memory_order_relaxed) % 97 == 0) {
      const auto *m = reinterpret_cast<const float *>(static_cast<uint8_t *>(r8) + 0x10);
      uint8_t *stage = nullptr;
      const auto owner = *reinterpret_cast<const uintptr_t *>(g_exeBase + 0x168caf0);
      if (owner > 0x10000 && *reinterpret_cast<const uintptr_t *>(owner) == g_exeBase + 0x127a030) {
        const auto world = *reinterpret_cast<const uintptr_t *>(owner + 0x20);
        if (world > 0x10000) {
          stage = *reinterpret_cast<uint8_t **>(world + 0x1cf8);
        }
      }
      uint8_t *scene = g_forcedScene.load(std::memory_order_relaxed);
      char line[320];
      std::snprintf(line, sizeof line,
                    "LoadView[diretor]: ctx %p cam %p fonte %p (palco+0x200 %p) flags %x forçado %d camcena %d tid %lu "
                    "olho [%.1f %.1f %.1f] frente [%.2f %.2f %.2f] %s",
                    ctx, cam, r8, stage != nullptr ? stage + 0x200 : nullptr, flags, scene != nullptr,
                    scene != nullptr && cam == *reinterpret_cast<void **>(scene + 0x17c0), GetCurrentThreadId(), m[12],
                    m[13], m[14], m[8], m[9], m[10], StackKey("", 1).c_str());
      Logger::Info(line);
    }
  }
  uint8_t *forced = g_forcedScene.load(std::memory_order_relaxed);
  if (g_forceLook && forced != nullptr && r8 != nullptr && cam != nullptr &&
      cam == *reinterpret_cast<void **>(forced + 0x17c0)) {
    // A fonte (r8+0x10: cima, direita, frente, olho) é a cópia da vista, parada na origem na carga. Com o
    // bit 0 de flags o diretor já envia o CB 3 (CameraParams) do pré-passe de profundidade com ela; o
    // passe de cor (teste de profundidade "igual") usa a pose reaplicada depois. Sem a pose aqui também,
    // as profundidades não batem e o chão some.
    float rows[16];
    LookRows(rows);
    std::memcpy(static_cast<uint8_t *>(r8) + 0x10, rows, sizeof rows);
  }
  const uint64_t ret = g_director(ctx, cam, r8, x3, flags);
  uint8_t *scene = g_forcedScene.load(std::memory_order_relaxed);
  if (scene != nullptr && cam != nullptr && cam == *reinterpret_cast<void **>(scene + 0x17c0)) {
    ApplySceneCam(scene);
  }
  return ret;
}

// 0x1406a1a70: push numa lista de itens da cena ({pool, cabeça}; pool =
// {capacidade, nós, contador}); devolve 0 quando o pool está cheio. Os jobs
// de cull (0x140953220) empurram aqui; a vista principal copia as listas de
// [cena+0x1908] (0x1409cc0f0), p.ex. vista+0x30 = cena+0x19f8. Conta os
// pushes por offset da lista dentro da cena.
constexpr uintptr_t kPushRva = 0x6a1a70;
constexpr uint8_t kPushPrologue[] = {0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0x19};
using PushFn = uint8_t(__fastcall *)(void *, void *);
PushFn g_push = nullptr;
constexpr std::size_t kPushBuckets = 0x4000 / 8;
std::array<std::atomic<uint32_t>, kPushBuckets> g_pushOk{};
std::array<std::atomic<uint32_t>, kPushBuckets> g_pushFail{};
std::atomic<uint32_t> g_pushOther{0};
thread_local uint64_t t_pushes = 0;
uint8_t __fastcall HookPush(void *list, void *item) {
  ++t_pushes;
  const uint8_t ok = g_push(list, item);
  if (g_measure.load(std::memory_order_relaxed)) {
    const uint8_t *scene = g_lastScene.load(std::memory_order_relaxed);
    const auto off = static_cast<uintptr_t>(static_cast<uint8_t *>(list) - scene);
    if (scene != nullptr && off < 0x4000) {
      (ok ? g_pushOk : g_pushFail)[off / 8].fetch_add(1, std::memory_order_relaxed);
    } else {
      g_pushOther.fetch_add(1, std::memory_order_relaxed);
    }
  }
  return ok;
}

// 0x14039ccb0: job de cull dos objetos da cena (rcx = cena). Sai cedo se
// [cena+0x1e98] != 0 ou o byte global exe+0x169af48 != 0; senão chama o
// cull 0x140953220 com os objetos [cena+0x1330] (quantos em [cena+0x1328]),
// que empurra os visíveis nas listas de [cena+0x1900].
// 0x1406ee9e0(carro_visual, dt): atualização visual do carro. Com "manter_cena=1"
// ela roda na carga e lê [carro+0xa28] ainda lixo (crash em 0x1406eed75); nos
// quadros forçados da carga é pulada.
constexpr uintptr_t kCarVisRva = 0x6ee9e0;
constexpr uint8_t kCarVisPrologue[] = {0xf3, 0x0f, 0x11, 0x4c, 0x24, 0x10, 0x57, 0x48, 0x83, 0xec, 0x30};
using CarVisFn = void(__fastcall *)(void *, float);
CarVisFn g_carVis = nullptr;
std::atomic<uint32_t> g_secCarVisSkip{0};
bool Readable(const void *p, std::size_t n);
long long SinceMode2();
// "vigiar_carro=1": loga quando muda o objeto em [[carVis+8]+0xa28] (o ponteiro que crasha na carga),
// com a vtable dele, a do sim [carVis+8] e a posição [sim+0x3170], para achar quando o carro fica pronto.
bool g_watchCar = false;
// "carro_na_carga=1": não pula o visual do carro nos quadros forçados (teste; o crash antigo era em +0x6eed75).
bool g_carInLoad = false;
void WatchCar(const uint8_t *car, bool skipping) {
  static uintptr_t lastSim = 1, lastA28 = 1, lastVt = 1;
  static int lines = 0;
  if (lines >= 40 || car == nullptr) {
    return;
  }
  const bool carOk = Readable(car, 0x70);
  const auto *sim = carOk ? *reinterpret_cast<const uint8_t *const *>(car + 8) : nullptr;
  const bool simOk = sim != nullptr && Readable(sim, 0xa30) && Readable(sim + 0x3170, 0x10);
  const auto a28 = simOk ? *reinterpret_cast<const uintptr_t *>(sim + 0xa28) : 0;
  const uintptr_t vt = a28 > 0x10000 && Readable(reinterpret_cast<const void *>(a28), 0x1f0)
                           ? *reinterpret_cast<const uintptr_t *>(a28) : 0;
  if (reinterpret_cast<uintptr_t>(sim) == lastSim && a28 == lastA28 && vt == lastVt) {
    return;
  }
  lastSim = reinterpret_cast<uintptr_t>(sim);
  lastA28 = a28;
  lastVt = vt;
  ++lines;
  const float *pos = simOk ? reinterpret_cast<const float *>(sim + 0x3170) : nullptr;
  const auto rel = [](uintptr_t v) { return v >= g_exeBase && v < g_exeBase + 0x2000000 ? v - g_exeBase : v; };
  const uintptr_t simVt = sim != nullptr && Readable(sim, 8) ? *reinterpret_cast<const uintptr_t *>(sim) : 0;
  char line[320];
  std::snprintf(line, sizeof line,
                "LoadView[carro]: %s t%+lld ms vis %p b69 %d sim %p vt +%llx a28 %p vt +%llx pos %.2f %.2f %.2f",
                skipping ? "carga" : "livre", SinceMode2(), car, carOk ? car[0x69] : -1, sim,
                static_cast<unsigned long long>(rel(simVt)), reinterpret_cast<void *>(a28),
                static_cast<unsigned long long>(rel(vt)), pos ? pos[0] : 0.f, pos ? pos[1] : 0.f, pos ? pos[2] : 0.f);
  Logger::Info(line);
}

void __fastcall HookCarVis(void *car, float dt) {
  InFlight fl;
  const bool skipping = g_active && g_keepSceneMode && g_forceAfterMs >= 0 && g_mode2Since != 0 && !g_carInLoad;
  if (g_watchCar) {
    WatchCar(static_cast<const uint8_t *>(car), skipping);
  }
  if (skipping) {
    g_secCarVisSkip.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  g_carVis(car, dt);
}

// 0x140ab20e0(conjunto, índice): envia um buffer do anel de instâncias à GPU
// (Map/copia/Unmap pelo device em [exe+0x1f45258]). Na carga, com "manter_cena=1",
// o conjunto [[[cena+0x1018]+0x1d30]+0x20] tem itens pendentes num objeto de
// buffer já liberado (salto para lixo em 0x1408f497c); nos quadros forçados
// pula a entrada cujo objeto não tem vtable no exe.
constexpr uintptr_t kRingUploadRva = 0xab20e0;
constexpr uint8_t kRingUploadPrologue[] = {0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x80, 0x39, 0x00};
using RingUploadFn = void(__fastcall *)(void *, int32_t);
RingUploadFn g_ringUpload = nullptr;
std::atomic<uint32_t> g_secRingSkip{0};
void __fastcall HookRingUpload(void *set, int32_t index) {
  InFlight fl;
  if (g_active && g_keepSceneMode && g_forceAfterMs >= 0 && g_mode2Since != 0 && set != nullptr) {
    const auto *st = static_cast<const uint8_t *>(set);
    const int32_t count = *reinterpret_cast<const int32_t *>(st + 0x28);
    const auto *entries = *reinterpret_cast<const uint8_t *const *>(st + 0x20);
    if (st[0] != 0 && index >= 0 && index < count && entries != nullptr) {
      const auto *e = entries + static_cast<std::size_t>(index) * 24;
      const auto obj = *reinterpret_cast<const uintptr_t *>(e);
      const uintptr_t vt = obj > 0x10000 ? *reinterpret_cast<const uintptr_t *>(obj) : 0;
      if (*reinterpret_cast<const int32_t *>(e + 0x14) > 0 && (vt < g_exeBase || vt >= g_exeBase + 0x2000000)) {
        g_secRingSkip.fetch_add(1, std::memory_order_relaxed);
        return;
      }
    }
  }
  g_ringUpload(set, index);
}

// Rastreio do conjunto de buffers de instância ("rastrear_buffers=1"). O objeto
// de 0x3d0 em [M+0x1d30] (M = [cena+0x1018]) nasce no init do M (0x140a6c9c0); o
// preparo de GPU 0x140a95c40(M) (chamado pelo método virtual 0x140b920c0) zera os
// tamanhos e cria o conjunto em [obj+0x20] por 0x140a8d9f0 → 0x140a9eff0 (array
// de 32 entradas de 24 bytes pelo alocador [obj+8]); 0x140a92a60 o destrói. Na
// carga forçada o array aparece liberado (lista livre de nós de 0x18) com o
// conjunto ainda apontando para ele: loga quem cria, quem destrói e quem libera.
bool g_traceBuffers = false;
std::atomic<int> g_traceLines{0};
void TraceLog(const char *s) {
  if (g_traceLines.fetch_add(1, std::memory_order_relaxed) < 400) {
    Logger::Info(s);
  }
}
uintptr_t Rel(uintptr_t a) { return a >= g_exeBase && a < g_exeBase + 0x2000000 ? a - g_exeBase : a; }
bool Readable(const void *p, std::size_t n) {
  MEMORY_BASIC_INFORMATION mbi{};
  if (VirtualQuery(p, &mbi, sizeof mbi) == 0 || mbi.State != MEM_COMMIT ||
      (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) != 0) {
    return false;
  }
  return reinterpret_cast<uintptr_t>(p) + n <= reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
}
long long SinceMode2() { return g_mode2Since != 0 ? static_cast<long long>(GetTickCount64() - g_mode2Since) : -1; }

std::atomic<uintptr_t> g_trackSet{0}, g_trackEntries{0}, g_trackAlloc{0};
std::array<uintptr_t, 32> g_trackObjs{};
int g_trackCount = 0;

using FreeFn = uintptr_t(__fastcall *)(void *, void *, uintptr_t, uintptr_t);
FreeFn g_allocFree = nullptr;
void *g_allocFreeTarget = nullptr;
uintptr_t __fastcall HookAllocFree(void *alloc, void *ptr, uintptr_t a, uintptr_t b) {
  InFlight fl;
  const auto p = reinterpret_cast<uintptr_t>(ptr);
  const uintptr_t entries = g_trackEntries.load(std::memory_order_relaxed);
  if (p != 0 && (p == entries || p == g_trackSet.load(std::memory_order_relaxed))) {
    char line[200];
    std::snprintf(line, sizeof line, "LoadView: buffers: free(%p) do %s pelo alocador %p, chamado de +%llx (%lld ms apos modo 2)",
                  ptr, p == entries ? "array" : "conjunto", alloc,
                  static_cast<unsigned long long>(Rel(reinterpret_cast<uintptr_t>(__builtin_return_address(0)))), SinceMode2());
    TraceLog(line);
  }
  return g_allocFree(alloc, ptr, a, b);
}

constexpr uintptr_t kGpuPrepRva = 0xb920c0;
constexpr uint8_t kGpuPrepPrologue[] = {0x40, 0x57, 0x48, 0x83, 0xec, 0x40, 0x48, 0x8b, 0xf9};
using GpuPrepFn = uint8_t(__fastcall *)(void *);
GpuPrepFn g_gpuPrep = nullptr;
uint8_t __fastcall HookGpuPrep(void *self) {
  InFlight fl;
  if (!g_traceBuffers) {
    return g_gpuPrep(self);
  }
  char line[200];
  std::snprintf(line, sizeof line, "LoadView: buffers: preparo de GPU 0xb920c0(%p) chamado de +%llx (%lld ms apos modo 2, quadros forcados %s)",
                self, static_cast<unsigned long long>(Rel(reinterpret_cast<uintptr_t>(__builtin_return_address(0)))),
                SinceMode2(), g_active && g_forceAfterMs >= 0 ? "sim" : "nao");
  TraceLog(line);
  return g_gpuPrep(self);
}

constexpr uintptr_t kSetBuildRva = 0xa9eff0;
constexpr uint8_t kSetBuildPrologue[] = {0x40, 0x53, 0x55, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x48, 0x81, 0xec};
using SetBuildFn = uint8_t(__fastcall *)(void *, void *);
SetBuildFn g_setBuild = nullptr;
uint8_t __fastcall HookSetBuild(void *set, void *desc) {
  InFlight fl;
  const auto caller = Rel(reinterpret_cast<uintptr_t>(__builtin_return_address(0)));
  const uint8_t ok = g_setBuild(set, desc);
  if (!g_traceBuffers || caller != 0xa8db1c) { // só o conjunto de instâncias do M (de 0x140a8d9f0)
    return ok;
  }
  const auto *st = static_cast<const uint8_t *>(set);
  const auto entries = *reinterpret_cast<const uintptr_t *>(st + 0x20);
  const int32_t n = *reinterpret_cast<const int32_t *>(st + 0x28);
  // O alocador é o [obj+8] de quem chamou; o conjunto guarda o mesmo em [set+8].
  void *alloc = *reinterpret_cast<void *const *>(st + 8);
  g_trackCount = std::min<int32_t>(std::max<int32_t>(n, 0), 32);
  int live = 0;
  for (int i = 0; i < g_trackCount; ++i) {
    g_trackObjs[i] = entries != 0 ? *reinterpret_cast<const uintptr_t *>(entries + i * 24) : 0;
    live += g_trackObjs[i] != 0;
  }
  g_trackSet = reinterpret_cast<uintptr_t>(set);
  g_trackEntries = entries;
  g_trackAlloc = reinterpret_cast<uintptr_t>(alloc);
  char line[240];
  std::snprintf(line, sizeof line, "LoadView: buffers: conjunto %p montado (ok %u): array %llx, %d entradas, %d com buffer, alocador %p vt +%llx (%lld ms apos modo 2)",
                set, ok, static_cast<unsigned long long>(entries), n, live, alloc,
                static_cast<unsigned long long>(alloc != nullptr ? Rel(**reinterpret_cast<uintptr_t *const *>(alloc)) : 0),
                SinceMode2());
  TraceLog(line);
  if (entries != 0) {
    // Quem escreve no 1º objeto do array (a entrada 0 fica sem buffer) e no ponteiro do array.
    GhostLabArmWriteWatch({entries, reinterpret_cast<uintptr_t>(set) + 0x20}, {8, 8});
  }
  if (g_allocFree == nullptr && alloc != nullptr) {
    void *target = reinterpret_cast<void *>((*reinterpret_cast<uintptr_t *const *>(alloc))[5]);
    if (MH_CreateHook(target, reinterpret_cast<void *>(&HookAllocFree), reinterpret_cast<void **>(&g_allocFree)) ==
            MH_OK &&
        MH_EnableHook(target) == MH_OK) {
      g_allocFreeTarget = target;
      std::snprintf(line, sizeof line, "LoadView: buffers: hook no free do alocador (+%llx).",
                    static_cast<unsigned long long>(Rel(reinterpret_cast<uintptr_t>(target))));
      TraceLog(line);
    }
  }
  return ok;
}

constexpr uintptr_t kSetFreeRva = 0xa92a60;
constexpr uint8_t kSetFreePrologue[] = {0x48, 0x89, 0x5c, 0x24, 0x18, 0x55, 0x48, 0x83, 0xec, 0x20};
using SetFreeFn = void(__fastcall *)(void *);
SetFreeFn g_setFree = nullptr;
void __fastcall HookSetFree(void *set) {
  InFlight fl;
  if (g_traceBuffers && reinterpret_cast<uintptr_t>(set) == g_trackSet.load(std::memory_order_relaxed)) {
    char line[200];
    std::snprintf(line, sizeof line, "LoadView: buffers: conjunto %p destruido (0xa92a60) por +%llx (%lld ms apos modo 2)", set,
                  static_cast<unsigned long long>(Rel(reinterpret_cast<uintptr_t>(__builtin_return_address(0)))),
                  SinceMode2());
    TraceLog(line);
  }
  g_setFree(set);
}

// Por quadro: o conjunto ainda aponta para o mesmo array, e o array tem os mesmos buffers?
void WatchBufferSet() {
  const uintptr_t set = g_trackSet.load(std::memory_order_relaxed);
  if (!g_traceBuffers || set == 0) {
    return;
  }
  static uintptr_t lastEntries = 0, lastObj0 = 0;
  static int lastBad = -1;
  if (!Readable(reinterpret_cast<const void *>(set), 0x30)) {
    return;
  }
  const auto entries = *reinterpret_cast<const uintptr_t *>(set + 0x20);
  const int32_t n = *reinterpret_cast<const int32_t *>(set + 0x28);
  int bad = 0;
  uintptr_t obj0 = 0;
  if (entries != 0 && entries == g_trackEntries.load() && Readable(reinterpret_cast<const void *>(entries), 32 * 24)) {
    for (int i = 0; i < g_trackCount; ++i) {
      const auto o = *reinterpret_cast<const uintptr_t *>(entries + i * 24);
      if (i == 0) obj0 = o;
      bad += o != g_trackObjs[i];
    }
  }
  if (entries != lastEntries || bad != lastBad || obj0 != lastObj0) {
    char line[220];
    std::snprintf(line, sizeof line, "LoadView: buffers: quadro: conjunto %llx array %llx n %d, %d entradas mudaram, entrada0 %llx -> [%llx] (%lld ms apos modo 2)",
                  static_cast<unsigned long long>(set), static_cast<unsigned long long>(entries), n, bad,
                  static_cast<unsigned long long>(obj0),
                  static_cast<unsigned long long>(obj0 > 0x10000 && Readable(reinterpret_cast<const void *>(obj0), 8)
                                                      ? Rel(*reinterpret_cast<const uintptr_t *>(obj0)) : 0),
                  SinceMode2());
    TraceLog(line);
    lastEntries = entries;
    lastBad = bad;
    lastObj0 = obj0;
  }
}

// Ciclo de vida dos sistemas do jogo ("rastrear_sistemas=1"). Cada sistema tem
// o estado em [sys+0xd8] (2 -> 3 por 0x140ba8890 com vt[0x28] verdadeiro, 3 ->
// 4 por 0x140bbed60 com vt[0x30] verdadeiro; os dois "todos" 0x140ba88d0 e
// 0x140bbeda0 percorrem a lista [mgr+0x118..+0x120] de entradas de 0x18). O
// 0x140bc6560 só atualiza quem está em 4, então o mundo (0x14159d770, que leva
// o terreno [+0xf8]+0xd0 de 4 a 6) só anda depois de ativado. Loga cada troca e
// a hora, para achar o que segura a ativação até o fim da carga.
bool g_traceSystems = false;
constexpr uint8_t kSysPrepAllPrologue[] = {0x48, 0x8b, 0xc4, 0x55, 0x57, 0x48, 0x83, 0xec, 0x58, 0x48, 0x8b, 0xb9};
constexpr uint8_t kSysActAllPrologue[] = {0x40, 0x55, 0x57, 0x48, 0x83, 0xec, 0x58, 0x48, 0x8b, 0xe9};
constexpr uint8_t kSysPrepOnePrologue[] = {0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x83, 0xba, 0xd8, 0x00, 0x00, 0x00, 0x02};
constexpr uint8_t kSysActOnePrologue[] = {0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x83, 0xba, 0xd8, 0x00, 0x00, 0x00, 0x03};
constexpr uint8_t kSysMsgPrologue[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x57, 0x48, 0x83, 0xec, 0x20, 0x48, 0x83};
constexpr uint8_t kLoadStepPrologue[] = {0x40, 0x53, 0x55, 0x56, 0x57, 0x41, 0x56, 0x48, 0x83, 0xec, 0x70, 0x4c};
long long g_sysEpoch = 0;
long long SysMs() { return static_cast<long long>(GetTickCount64()) - g_sysEpoch; }
void SysLog(const char *what, void *mgr, void *sys, int before, int after, uintptr_t caller) {
  char line[220];
  const auto vt = sys != nullptr && Readable(sys, 8) ? *static_cast<uintptr_t *>(sys) : 0;
  std::snprintf(line, sizeof line, "LoadView[sist]: %s mgr +%llx sys +%llx (vt +%llx) %d -> %d, chamador +%llx, t %lld ms",
                what, static_cast<unsigned long long>(Rel(reinterpret_cast<uintptr_t>(mgr))),
                static_cast<unsigned long long>(Rel(reinterpret_cast<uintptr_t>(sys))),
                static_cast<unsigned long long>(Rel(vt)), before, after, static_cast<unsigned long long>(Rel(caller)),
                SysMs());
  TraceLog(line);
}
int SysState(void *sys) { return sys != nullptr ? *reinterpret_cast<int *>(static_cast<uint8_t *>(sys) + 0xd8) : -1; }

// Lista de sistemas com estado: "sys:estado" de cada entrada.
void SysListLog(const char *what, void *mgr, uintptr_t caller) {
  auto *m = static_cast<uint8_t *>(mgr);
  auto *it = *reinterpret_cast<uint8_t **>(m + 0x118);
  auto *end = *reinterpret_cast<uint8_t **>(m + 0x120);
  std::string line = std::string("LoadView[sist]: ") + what + " mgr +";
  char buf[64];
  std::snprintf(buf, sizeof buf, "%llx chamador +%llx thread %lu t %lld ms:",
                static_cast<unsigned long long>(Rel(reinterpret_cast<uintptr_t>(mgr))),
                static_cast<unsigned long long>(Rel(caller)), GetCurrentThreadId(), SysMs());
  line += buf;
  for (int n = 0; it != end && n < 48; it += 0x18, ++n) {
    void *sys = *reinterpret_cast<void **>(it);
    std::snprintf(buf, sizeof buf, " %llx:%d", static_cast<unsigned long long>(Rel(reinterpret_cast<uintptr_t>(sys))),
                  sys != nullptr && Readable(sys, 0xe0) ? SysState(sys) : -1);
    line += buf;
  }
  TraceLog(line.c_str());
}

using SysAllFn = void(__fastcall *)(void *);
SysAllFn g_sysPrepAll = nullptr, g_sysActAll = nullptr;
// Para o "adiantar_tudo": o gerente da lista e quantos "preparar todos" já
// rodaram; a trava impede a nossa thread de preparar junto com o jogo.
std::atomic<void *> g_sysMgr{nullptr};
std::atomic<int> g_sysPrepAllCalls{0};
std::mutex g_earlyMutex;
void __fastcall HookSysPrepAll(void *mgr) {
  InFlight fl;
  const auto caller = reinterpret_cast<uintptr_t>(__builtin_return_address(0));
  {
    std::lock_guard<std::mutex> lock(g_earlyMutex);
    g_sysMgr.store(mgr);
    g_sysPrepAllCalls.fetch_add(1);
    g_sysPrepAll(mgr);
  }
  if (g_traceSystems && mgr != nullptr) {
    SysListLog("preparar todos", mgr, caller);
  }
}
void __fastcall HookSysActAll(void *mgr) {
  InFlight fl;
  const auto caller = reinterpret_cast<uintptr_t>(__builtin_return_address(0));
  g_sysActAll(mgr);
  if (g_traceSystems && mgr != nullptr) {
    SysListLog("ativar todos", mgr, caller);
  }
}
using SysOneFn = uint8_t(__fastcall *)(void *, void *);
SysOneFn g_sysPrepOne = nullptr, g_sysActOne = nullptr;
std::mutex g_sysMutex;
std::vector<std::pair<void *, int>> g_sysSeen; // (sys, último resultado*16 + estado)
// Loga só a primeira chamada por sistema e as que mudam estado ou resultado.
void SysOneLog(const char *what, void *mgr, void *sys, int before, uint8_t ok, uintptr_t caller) {
  const int after = SysState(sys);
  const int key = (ok ? 16 : 0) + before + (what[0] == 'a' ? 256 : 0);
  {
    std::lock_guard<std::mutex> lock(g_sysMutex);
    auto it = std::find_if(g_sysSeen.begin(), g_sysSeen.end(), [&](const auto &p) { return p.first == sys && (p.second & 256) == (key & 256); });
    if (it != g_sysSeen.end()) {
      if (it->second == key) {
        return;
      }
      it->second = key;
    } else {
      g_sysSeen.emplace_back(sys, key);
    }
  }
  char what2[48];
  std::snprintf(what2, sizeof what2, "%s (%s)", what, ok ? "ok" : "nao");
  SysLog(what2, mgr, sys, before, after, caller);
}
uint8_t __fastcall HookSysPrepOne(void *mgr, void *sys) {
  InFlight fl;
  if (!g_traceSystems || sys == nullptr) {
    return g_sysPrepOne(mgr, sys);
  }
  const int before = SysState(sys);
  const uint8_t ok = g_sysPrepOne(mgr, sys);
  SysOneLog("preparar", mgr, sys, before, ok, reinterpret_cast<uintptr_t>(__builtin_return_address(0)));
  return ok;
}
uint8_t __fastcall HookSysActOne(void *mgr, void *sys) {
  InFlight fl;
  if (!g_traceSystems || sys == nullptr) {
    return g_sysActOne(mgr, sys);
  }
  const int before = SysState(sys);
  const uint8_t ok = g_sysActOne(mgr, sys);
  SysOneLog("ativar", mgr, sys, before, ok, reinterpret_cast<uintptr_t>(__builtin_return_address(0)));
  return ok;
}
using SysMsgFn = uintptr_t(__fastcall *)(void *, void *);
SysMsgFn g_sysMsg = nullptr;
uintptr_t __fastcall HookSysMsg(void *self, void *msg) {
  InFlight fl;
  if (g_traceSystems) {
    char line[160];
    std::snprintf(line, sizeof line, "LoadView[sist]: mensagem 0x140479860 (prepara, ativa e pede modo 0), chamador +%llx, t %lld ms",
                  static_cast<unsigned long long>(Rel(reinterpret_cast<uintptr_t>(__builtin_return_address(0)))), SysMs());
    TraceLog(line);
  }
  return g_sysMsg(self, msg);
}
using LoadStepFn = void(__fastcall *)(void *);
LoadStepFn g_loadStep = nullptr;
std::atomic<int> g_loadStepCalls{0};
void __fastcall HookLoadStep(void *self) {
  InFlight fl;
  if (g_traceSystems && g_loadStepCalls.fetch_add(1, std::memory_order_relaxed) < 3) {
    char line[160];
    std::snprintf(line, sizeof line, "LoadView[sist]: 0x140477b00(%p) chamado de +%llx, t %lld ms", self,
                  static_cast<unsigned long long>(Rel(reinterpret_cast<uintptr_t>(__builtin_return_address(0)))), SysMs());
    TraceLog(line);
  }
  g_loadStep(self);
}

// A cada quadro: estado do mundo ([0x14159d770+0xd8]) e do terreno.
void WatchWorldSystem() {
  if (!g_traceSystems) {
    return;
  }
  static int lastWorld = -100, lastTerrain = -100;
  auto *world = reinterpret_cast<uint8_t *>(g_exeBase + 0x159d770);
  const int ws = *reinterpret_cast<int *>(world + 0xd8);
  const auto *terrain = *reinterpret_cast<uint8_t *const *>(world + 0xf8);
  const int ts = terrain != nullptr && Readable(terrain, 0xd4) ? *reinterpret_cast<const int *>(terrain + 0xd0) : -1;
  if (ws != lastWorld || ts != lastTerrain) {
    char line[160];
    std::snprintf(line, sizeof line, "LoadView[sist]: quadro: mundo %d -> %d, terreno %p %d -> %d, t %lld ms", lastWorld, ws,
                  static_cast<const void *>(terrain), lastTerrain, ts, SysMs());
    TraceLog(line);
    lastWorld = ws;
    lastTerrain = ts;
  }
}

// Experimento "adiantar_mundo=1": o mundo chega ao estado 2 (carregado,
// [+0x100]=1) uns 2,6 s antes do "preparar todos" do fim da carga, que só roda
// depois do resto (patchup_ot, entity...). Uma thread nossa espera isso (o
// [terreno+0x38938]->vt[0xb8]() que 0x140a96150 testa é o tipo do tracksplit,
// sempre 0 aqui: só escolhe o caminho da montagem) e faz o que o job 0x140477a70 faz: pega um contexto de GPU da
// thread ([[0x141f45258]] vt+0x248(1)), prepara (0x140ba8890, monta o terreno)
// e ativa (0x140bbed60) só o mundo, e devolve o contexto (vt+0x250(1)). O
// "preparar/ativar todos" do fim da carga pula quem já está em 4.
bool g_earlyWorld = false;
// "adiantar_tudo=1": depois do mundo, prepara e ativa cada sistema da lista
// assim que ele chega ao estado 2, até o "preparar todos" do fim da carga.
bool g_earlyAll = false;
// "adiantar_vis=1": junto com o adiantar_tudo, monta também as células da
// visibilidade cedo (EarlyVisCells). Sem ele o sistema 0x1416ba550 fica para o
// preparo do jogo: a multidão dele crashou em todas as tentativas (cam47–cam52).
bool g_earlyVis = false;
// "vigiar_vis=1": watchpoint de escrita em [vis+0x4b0] (dados do track.vis),
// para achar quem grava e quando.
bool g_watchVis = false;
bool g_watchVisArmed = false;
std::atomic<bool> g_earlyWorldRunning{false};
// Prepara e ativa um sistema no contexto de GPU da thread; devolve o estado final.
// Quando o EarlyPrepare começou (0 = parado): a vigia no HookObjCull mostra as
// pilhas se ele passar de 3 s.
std::atomic<ULONGLONG> g_earlyPrepSince{0};
// O quadro da visibilidade (0x140c1e350, thread principal) pede o recurso de
// células que o c12660 segura, enquanto o c12660 espera um lock da thread
// principal: deadlock. Enquanto a montagem cedo roda, o quadro é pulado.
SRWLOCK g_visFrameLock = SRWLOCK_INIT;
constexpr uint8_t kVisFramePrologue[] = {0x40, 0x57, 0x48, 0x83, 0xec, 0x50, 0x48, 0x83, 0xb9, 0x98, 0x01, 0x00, 0x00, 0x00};
using VisFrameFn = void(__fastcall *)(void *);
VisFrameFn g_visFrame = nullptr;
void __fastcall HookVisFrame(void *vis) {
  if (!TryAcquireSRWLockShared(&g_visFrameLock)) {
    return;
  }
  g_visFrame(vis);
  ReleaseSRWLockShared(&g_visFrameLock);
}
int EarlyPrepare(void *mgr, uint8_t *sys) {
  g_earlyPrepSince = GetTickCount64();
  void *gfx = *reinterpret_cast<void **>(g_exeBase + 0x1f45258);
  using SlotFn = void(__fastcall *)(void *, bool);
  auto *gvt = *static_cast<uintptr_t **>(gfx);
  reinterpret_cast<SlotFn>(gvt[0x248 / 8])(gfx, true);
  const auto prep = g_sysPrepOne != nullptr ? g_sysPrepOne : reinterpret_cast<SysOneFn>(g_exeBase + 0xba8890);
  const auto act = g_sysActOne != nullptr ? g_sysActOne : reinterpret_cast<SysOneFn>(g_exeBase + 0xbbed60);
  if (prep(mgr, sys) != 0 && SysState(sys) == 3) {
    act(mgr, sys);
  }
  reinterpret_cast<SlotFn>(gvt[0x250 / 8])(gfx, true);
  g_earlyPrepSince = 0;
  return SysState(sys);
}
// Sistema de visibilidade/multidão (0x1416ba550): o preparo
// (0x1404c49c0) monta o objeto de visibilidade [sys+0xf0] (0x140c15020 liga o
// +0x5ae0 que 0x140c2de70 exige para pedir a lista de células). Antes do
// "preparar todos", 0x140497e20 chama 0x1404d3570, que só copia dois ponteiros
// para o objeto: [vis+0x5498] = [[jogo+0x1b00]+0x67818] e [vis+0x54a0] =
// [[jogo+0x58f0]+0x18], com jogo = [0x14168caf8]. Fazemos o mesmo, quando o
// recurso do track.vis já está na lista: o próprio preparo (0x140c12660, com
// rcx = vis+0xb0) acha o recurso pelo id [vis+0x194] (0x140916bd0, a mesma
// busca usada aqui) e grava os dados em [vis+0x4b0].
int g_visWhy = -1;
bool EarlyVisSetup(uint8_t *sys) {
  auto *vis = *reinterpret_cast<uint8_t **>(sys + 0xf0);
  const auto *game = *reinterpret_cast<const uint8_t *const *>(g_exeBase + 0x168caf8);
  int why = 0;
  if (vis == nullptr || game == nullptr) {
    why = 1;
  } else if (*reinterpret_cast<void **>(vis + 0xb30) == nullptr) {
    why = 2;
  } else {
    using FindRes = void *(*)(uint32_t, bool);
    const auto find = reinterpret_cast<FindRes>(g_exeBase + 0x916bd0);
    // A busca devolve o recurso travado (trava exclusiva em +0x10,
    // 0x1408ea730); soltamos na hora com 0x1408fe240, como o 0x140c12660
    // faz, senão o preparo espera para sempre.
    auto *res = static_cast<uint8_t *>(find(*reinterpret_cast<uint32_t *>(vis + 0x194), false));
    if (res == nullptr) {
      why = 3;
    } else {
      using UnlockFn = void(__fastcall *)(void *);
      reinterpret_cast<UnlockFn>(g_exeBase + 0x8fe240)(res);
    }
  }
  if (why != g_visWhy) {
    g_visWhy = why;
    char l[160];
    std::snprintf(l, sizeof l, "LoadView[adiantar]: visibilidade %p: %s, t %lld ms.", static_cast<void *>(vis),
                  why == 1 ? "sem objeto" : why == 2 ? "sem +0xb30" : why == 3 ? "recurso do track.vis ausente" : "recurso pronto",
                  SysMs());
    TraceLog(l);
  }
  if (why != 0) {
    return false;
  }
  const auto *a = *reinterpret_cast<const uint8_t *const *>(game + 0x1b00);
  const auto *b = *reinterpret_cast<const uint8_t *const *>(game + 0x58f0);
  if (a == nullptr || b == nullptr || sys[0x304] != 0 || sys[0x1e7] != 0) {
    return false;
  }
  void *pa = *reinterpret_cast<void *const *>(a + 0x67818);
  void *pb = *reinterpret_cast<void *const *>(b + 0x18);
  void *owner = *reinterpret_cast<void **>(sys + 0xd0);
  if (pa == nullptr || pb == nullptr || owner == nullptr) {
    return false;
  }
  *reinterpret_cast<void **>(vis + 0x5498) = pa;
  *reinterpret_cast<void **>(vis + 0x54a0) = pb;
  char line[200];
  std::snprintf(line, sizeof line, "LoadView[adiantar]: visibilidade %p configurada (%p, %p), t %lld ms.",
                static_cast<void *>(vis), pa, pb, SysMs());
  TraceLog(line);
  return true;
}
// Só a parte das células do preparo de 0x1416ba550: o 0x140c15020 faz
// c0e6b0(vis+0xb0), c150e0(vis+0x5460), a multidão c14a90(vis+0x52e0) e as
// células c12660, e liga [vis+0x5ae0]. A multidão busca nomes nos bancos PSSG
// (0x1408920f0), que a carga do patchup_ot altera ao mesmo tempo (crash por
// corrida); por isso aqui só c0e6b0 + c12660 + 5ae0, com os argumentos que o
// 0x1404c49c0 monta. O sistema fica no estado 2: o jogo faz o preparo inteiro
// depois, como sempre.
bool g_earlyVisBuilt = false;
bool EarlyVisCells(uint8_t *sys) {
  if (g_earlyVisBuilt || !EarlyVisSetup(sys)) {
    return false;
  }
  g_earlyVisBuilt = true;
  auto *vis = *reinterpret_cast<uint8_t **>(sys + 0xf0);
  void *owner = *reinterpret_cast<void **>(sys + 0xd0);
  using LookupFn = void *(__fastcall *)(void *, const char *);
  const auto lookup = reinterpret_cast<LookupFn>(g_exeBase + 0x8466a0);
  void *anim = lookup(owner, reinterpret_cast<const char *>(g_exeBase + 0x122ee10));        // "anim"
  void *crowd = lookup(owner, reinterpret_cast<const char *>(g_exeBase + 0x127e040));       // "build_crowd"
  vis[0x5ae2] = 1;
  if (*reinterpret_cast<int *>(vis + 0x5ae4) == 0) {
    *reinterpret_cast<int *>(vis + 0x5ae4) = 1;
  }
  *reinterpret_cast<void **>(vis + 0x90) = anim;
  void *gfx = *reinterpret_cast<void **>(g_exeBase + 0x1f45258);
  using SlotFn = void(__fastcall *)(void *, bool);
  auto *gvt = *static_cast<uintptr_t **>(gfx);
  g_earlyPrepSince = GetTickCount64();
  AcquireSRWLockExclusive(&g_visFrameLock); // o quadro da visibilidade fica de fora
  reinterpret_cast<SlotFn>(gvt[0x248 / 8])(gfx, true);
  using ResetFn = void(__fastcall *)(void *);
  reinterpret_cast<ResetFn>(g_exeBase + 0xc0e6b0)(vis + 0xb0);
  using CellsFn = uint64_t(__fastcall *)(void *, void *, void *, void *, uint8_t, void *, void *, void *);
  reinterpret_cast<CellsFn>(g_exeBase + 0xc12660)(
      vis + 0xb0, anim, crowd, *reinterpret_cast<void **>(vis + 0x80), 1, *reinterpret_cast<void **>(sys + 0x148),
      *reinterpret_cast<void **>(g_exeBase + 0x15a8850), *reinterpret_cast<void **>(vis + 0x10));
  vis[0x5ae0] = 1;
  reinterpret_cast<SlotFn>(gvt[0x250 / 8])(gfx, true);
  ReleaseSRWLockExclusive(&g_visFrameLock);
  g_earlyPrepSince = 0;
  char line[200];
  std::snprintf(line, sizeof line, "LoadView[adiantar]: celulas da visibilidade montadas (4b0 %p), t %lld ms.",
                *reinterpret_cast<void **>(vis + 0x4b0), SysMs());
  TraceLog(line);
  return true;
}
// O preparo do jogo (0x140c15020) depois das células adiantadas: montar de
// novo por cima crasha (0x140c13b6d, pool de itens cheio) e só completar a
// multidão (c150e0/c14a90) deixa os personagens com esqueleto e clipes que não
// batem (jobs de animação 0x140c2b07d / 0x14089fb18).
constexpr uint8_t kVisBuildPrologue[] = {0x48, 0x89, 0x6c, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24,
                                         0x18, 0x48, 0x89, 0x7c, 0x24, 0x20, 0x41, 0x56};
using VisBuildFn = bool(__fastcall *)(uint8_t *, void *, uint8_t, void *, void *);
VisBuildFn g_visBuild = nullptr;
bool __fastcall HookVisBuild(uint8_t *vis, void *anim, uint8_t flag, void *a4, void *a5) {
  if (!g_earlyVisBuilt || vis[0x5ae0] == 0) {
    return g_visBuild(vis, anim, flag, a4, a5);
  }
  // Nossas células (sem multidão certa: o patchup_ot ainda carregava) saem e a
  // montagem original roda inteira. A desmontagem completa do jogo (c10850)
  // também libera dados da carga (o track.vis em cells+0xa80, via c11820, e
  // vis+0x5610), então aqui só a parte dela que desfaz o c12660: o objeto do
  // 4b0 e as células c0ff10, com o a80 escondido para o c11820 pular.
  AcquireSRWLockExclusive(&g_visFrameLock);
  vis[0x5ae0] = 0;
  if (auto *obj = *reinterpret_cast<uint8_t **>(vis + 0x4b0); obj != nullptr && obj[0xb17aa] != 0) {
    using StopFn = void(__fastcall *)(void *, uint64_t, uint64_t, uint64_t, float, uint64_t);
    reinterpret_cast<StopFn>(g_exeBase + 0xc2df30)(obj, 0, 0, 0, 1.0f, 0);
    using PartFn = void(__fastcall *)(void *);
    reinterpret_cast<PartFn>(g_exeBase + 0xc25a20)(obj);
    auto *sub = obj + 0xb17b8;
    using RelFn = void(__fastcall *)(void *, int);
    reinterpret_cast<RelFn>((*reinterpret_cast<uintptr_t **>(sub))[1])(sub, -1);
  }
  void *trackVis = *reinterpret_cast<void **>(vis + 0xb0 + 0xa80);
  *reinterpret_cast<void **>(vis + 0xb0 + 0xa80) = nullptr;
  using CellsDownFn = void(__fastcall *)(void *);
  reinterpret_cast<CellsDownFn>(g_exeBase + 0xc0ff10)(vis + 0xb0);
  *reinterpret_cast<void **>(vis + 0xb0 + 0xa80) = trackVis;
  const bool ok = g_visBuild(vis, anim, flag, a4, a5);
  ReleaseSRWLockExclusive(&g_visFrameLock);
  char line[200];
  std::snprintf(line, sizeof line,
                "LoadView[adiantar]: preparo do jogo: celulas cedo desmontadas e montagem original (%d, 4b0 %p), t %lld ms.",
                ok ? 1 : 0, *reinterpret_cast<void **>(vis + 0x4b0), SysMs());
  TraceLog(line);
  return ok;
}
void EarlyAllLoop() {
  const ULONGLONG start = GetTickCount64();
  const int callsAtStart = g_sysPrepAllCalls.load();
  char line[200];
  std::vector<std::pair<uint8_t *, int>> tried; // (sistema, estado depois)
  while (GetTickCount64() - start < 30000) {
    std::unique_lock<std::mutex> lock(g_earlyMutex);
    if (g_sysPrepAllCalls.load() != callsAtStart) {
      std::snprintf(line, sizeof line, "LoadView[adiantar]: o jogo chegou ao preparar todos, parei, t %lld ms.", SysMs());
      TraceLog(line);
      return;
    }
    auto *m = static_cast<uint8_t *>(g_sysMgr.load());
    if (m == nullptr) {
      lock.unlock();
      Sleep(10);
      continue;
    }
    auto *it = *reinterpret_cast<uint8_t **>(m + 0x118);
    auto *end = *reinterpret_cast<uint8_t **>(m + 0x120);
    for (; it != end; it += 0x18) {
      auto *sys = *reinterpret_cast<uint8_t **>(it);
      if (sys == nullptr || SysState(sys) != 2) {
        continue;
      }
      if (sys == reinterpret_cast<uint8_t *>(g_exeBase + 0x159d770) && sys[0x100] == 0) {
        continue; // o mundo só com o terreno carregado
      }
      if (sys == reinterpret_cast<uint8_t *>(g_exeBase + 0x16ba550) && g_watchVis && !g_watchVisArmed) {
        auto *vis = *reinterpret_cast<uint8_t **>(sys + 0xf0);
        if (vis != nullptr && *reinterpret_cast<void **>(vis + 0x4b0) == nullptr) {
          g_watchVisArmed = true;
          std::snprintf(line, sizeof line, "LoadView[adiantar]: vigiando %p (vis+0x4b0), t %lld ms.",
                        static_cast<void *>(vis + 0x4b0), SysMs());
          TraceLog(line);
          GhostLabArmWriteWatch({reinterpret_cast<uintptr_t>(vis + 0x4b0)}, {8});
        }
      }
      if (sys == reinterpret_cast<uint8_t *>(g_exeBase + 0x16ba550)) {
        if (g_earlyVis) {
          EarlyVisCells(sys);
        }
        continue;
      }
      const int after = EarlyPrepare(m, sys);
      auto seen = std::find_if(tried.begin(), tried.end(), [&](const auto &p) { return p.first == sys; });
      if (seen == tried.end() || seen->second != after) {
        if (seen == tried.end()) {
          tried.emplace_back(sys, after);
        } else {
          seen->second = after;
        }
        std::snprintf(line, sizeof line, "LoadView[adiantar]: sistema +%llx (vt +%llx) 2 -> %d, t %lld ms.",
                      static_cast<unsigned long long>(Rel(reinterpret_cast<uintptr_t>(sys))),
                      static_cast<unsigned long long>(Rel(*reinterpret_cast<uintptr_t *>(sys))), after, SysMs());
        TraceLog(line);
      }
    }
    lock.unlock();
    Sleep(10);
  }
  TraceLog("LoadView[adiantar]: 30 s sem o preparar todos, parei.");
}
DWORD WINAPI EarlyWorldThread(void *) {
  const ULONGLONG start = GetTickCount64();
  char line[200];
  for (;;) {
    if (g_exeBase == 0) { // o SetActive vem antes do Install
      Sleep(5);
      if (GetTickCount64() - start > 60000) {
        break;
      }
      continue;
    }
    auto *world = reinterpret_cast<uint8_t *>(g_exeBase + 0x159d770);
    if (GetTickCount64() - start > 60000) {
      TraceLog("LoadView[adiantar]: desisti, o mundo nao ficou pronto em 60 s.");
      break;
    }
    const int ws = *reinterpret_cast<volatile int *>(world + 0xd8);
    if (ws > 2) {
      std::snprintf(line, sizeof line, "LoadView[adiantar]: o jogo ja preparou o mundo (estado %d), t %lld ms.", ws, SysMs());
      TraceLog(line);
      break;
    }
    auto *terrain = *reinterpret_cast<uint8_t *volatile *>(world + 0xf8);
    const int flag = *reinterpret_cast<volatile uint8_t *>(world + 0x100);
    const int ts = terrain != nullptr && Readable(terrain + 0x38938, 8) ? *reinterpret_cast<volatile int *>(terrain + 0xd0) : -1;
    void *split = ts >= 0 ? *reinterpret_cast<void *volatile *>(terrain + 0x38938) : nullptr;
    int st = -1;
    if (split != nullptr && Readable(split, 8)) {
      using StatusFn = int(__fastcall *)(void *);
      st = reinterpret_cast<StatusFn>((*static_cast<uintptr_t **>(split))[0xb8 / 8])(split);
    }
    // Loga cada mudança do que a thread espera.
    static int lastKey = -1;
    const int key = (ws & 0xf) | (flag ? 0x10 : 0) | ((ts & 0xf) << 5) | ((st & 0xf) << 9) | (split ? 0x2000 : 0);
    if (key != lastKey) {
      lastKey = key;
      std::snprintf(line, sizeof line, "LoadView[adiantar]: espera: mundo %d, carregado %d, terreno %d, tracksplit %p status %d, t %lld ms.",
                    ws, flag, ts, split, st, SysMs());
      TraceLog(line);
    }
    if (ws != 2 || flag == 0 || ts != 7) {
      Sleep(5);
      continue;
    }
    void *gfx = *reinterpret_cast<void **>(g_exeBase + 0x1f45258);
    using SlotFn = void(__fastcall *)(void *, bool);
    auto *gvt = *static_cast<uintptr_t **>(gfx);
    std::snprintf(line, sizeof line, "LoadView[adiantar]: mundo carregado, preparando na thread %lu, t %lld ms.",
                  GetCurrentThreadId(), SysMs());
    TraceLog(line);
    reinterpret_cast<SlotFn>(gvt[0x248 / 8])(gfx, true);
    const uint8_t prepOk = g_sysPrepOne != nullptr ? g_sysPrepOne(nullptr, world)
                                                   : reinterpret_cast<SysOneFn>(g_exeBase + 0xba8890)(nullptr, world);
    const int afterPrep = SysState(world);
    const uint8_t actOk = g_sysActOne != nullptr ? g_sysActOne(nullptr, world)
                                                 : reinterpret_cast<SysOneFn>(g_exeBase + 0xbbed60)(nullptr, world);
    reinterpret_cast<SlotFn>(gvt[0x250 / 8])(gfx, true);
    std::snprintf(line, sizeof line,
                  "LoadView[adiantar]: preparar %s (estado %d), ativar %s (estado %d), terreno %d, t %lld ms.",
                  prepOk ? "ok" : "nao", afterPrep, actOk ? "ok" : "nao", SysState(world),
                  *reinterpret_cast<int *>(terrain + 0xd0), SysMs());
    TraceLog(line);
    break;
  }
  if (g_earlyAll) {
    EarlyAllLoop();
  }
  g_earlyWorldRunning.store(false);
  return 0;
}
void StartEarlyWorld() {
  if (!g_earlyWorld || g_earlyWorldRunning.exchange(true)) {
    return;
  }
  HANDLE h = CreateThread(nullptr, 0, &EarlyWorldThread, nullptr, 0, nullptr);
  if (h != nullptr) {
    CloseHandle(h);
  } else {
    g_earlyWorldRunning.store(false);
  }
}

constexpr uintptr_t kObjCullRva = 0x39ccb0;
constexpr uint8_t kObjCullPrologue[] = {0x48, 0x8b, 0xc4, 0x55, 0x56, 0x48, 0x81, 0xec, 0x38, 0x01, 0x00, 0x00};
using ObjCullFn = void(__fastcall *)(void *, void *);
ObjCullFn g_objCull = nullptr;
std::atomic<uint32_t> g_secObjCull{0}, g_secObjCullGated{0}, g_secObjCullPush{0};
// Última leitura das travas: modo da cena ([cena+0x1e98], gravado por 0x1404a1580) e o byte global.
std::atomic<int32_t> g_objCullMode{0};
std::atomic<uint8_t> g_objCullFlag{0};
std::atomic<const void *> g_objCullScene{nullptr};
std::atomic<uint32_t> g_objCullCount{0};
void __fastcall HookObjCull(void *scene, void *b) {
  InFlight fl;
  if (!g_measure.load(std::memory_order_relaxed) || scene == nullptr) {
    g_objCull(scene, b);
    return;
  }
  const auto *sc = static_cast<const uint8_t *>(scene);
  const int32_t mode = *reinterpret_cast<const int32_t *>(sc + 0x1e98);
  const uint8_t flag = *reinterpret_cast<const uint8_t *>(g_exeBase + 0x169af48);
  const bool gated = mode != 0 || flag != 0;
  g_objCullMode.store(mode, std::memory_order_relaxed);
  g_objCullFlag.store(flag, std::memory_order_relaxed);
  g_objCullScene.store(scene, std::memory_order_relaxed);
  g_objCullCount.store(*reinterpret_cast<const uint32_t *>(sc + 0x1328), std::memory_order_relaxed);
  const uint64_t before = t_pushes;
  g_objCull(scene, b);
  g_secObjCull.fetch_add(1, std::memory_order_relaxed);
  if (gated) {
    g_secObjCullGated.fetch_add(1, std::memory_order_relaxed);
  }
  g_secObjCullPush.fetch_add(static_cast<uint32_t>(t_pushes - before), std::memory_order_relaxed);
  // Pedido da lista de células (0x140c2de70): olho em [cena+0x15c0], objeto de
  // visibilidade [cena+0x1038] com as travas +0xa8, +0x5ae0 e +0x4b0; a lista
  // volta em [cena+0x1190] (contagem) / +0x1198.
  static ULONGLONG visLogAt = 0;
  static bool dumped = false;
  const ULONGLONG since = g_earlyPrepSince.load();
  if (!dumped && since != 0 && GetTickCount64() - since > 3000) {
    dumped = true;
    TraceLog("LoadView[adiantar]: preparo cedo parado ha 3 s; pilhas:");
    if (HANDLE h = CreateThread(nullptr, 0, [](void *) -> DWORD { GhostLabDumpThreads(); return 0; }, nullptr, 0, nullptr)) {
      CloseHandle(h);
    }
  }
  if (g_traceSystems && GetTickCount64() >= visLogAt) {
    visLogAt = GetTickCount64() + 500;
    const auto *eye = reinterpret_cast<const float *>(sc + 0x15c0);
    const auto *vis = *reinterpret_cast<const uint8_t *const *>(sc + 0x1038);
    char line[240];
    std::snprintf(line, sizeof line,
                  "LoadView[vis]: cena %p modo %d olho [%.1f %.1f %.1f] vis %p a8=%d 5ae0=%d 4b0=%p lista %u, t %lld ms",
                  scene, mode, eye[0], eye[1], eye[2], static_cast<const void *>(vis), vis ? vis[0xa8] : -1,
                  vis ? vis[0x5ae0] : -1, vis ? *reinterpret_cast<void *const *>(vis + 0x4b0) : nullptr,
                  *reinterpret_cast<const uint32_t *>(sc + 0x1190), SysMs());
    TraceLog(line);
    // Entradas e travas do resto do cull (ver track_render.md §10): contagens
    // das caixas, sistemas da cena (0 = ramo pulado) e bytes globais.
    auto u32 = [sc](size_t o) { return *reinterpret_cast<const uint32_t *>(sc + o); };
    auto nz = [sc](size_t o) { return *reinterpret_cast<void *const *>(sc + o) != nullptr ? 1 : 0; };
    auto g8 = [](uintptr_t rva) { return *reinterpret_cast<const uint8_t *>(g_exeBase + rva); };
    char gates[400];
    std::snprintf(gates, sizeof gates,
                  "LoadView[gates]: caixas 1190=%u 11c0=%u 11d8=%u 1310=%u 1328=%u 1340=%u 1358=%u 1370=%u | "
                  "sis 1008=%d 1010=%d 1018=%d 1028=%d 1030=%d 1040=%d 1048=%d 17a0=%d 17b0=%d | "
                  "g 16afae8=%u 15a0048=%u 15a0049=%u 159e614=%u 15a8864=%u 169af48=%u | 14=%u",
                  u32(0x1190), u32(0x11c0), u32(0x11d8), u32(0x1310), u32(0x1328), u32(0x1340), u32(0x1358),
                  u32(0x1370), nz(0x1008), nz(0x1010), nz(0x1018), nz(0x1028), nz(0x1030), nz(0x1040), nz(0x1048),
                  nz(0x17a0), nz(0x17b0), g8(0x16afae8), g8(0x15a0048), g8(0x15a0049), g8(0x159e614),
                  g8(0x15a8864), g8(0x169af48), u32(0x14));
    TraceLog(gates);
  }
}

// 0x140a7f700: job (em thread de trabalho) que põe as células visíveis do
// terreno nas listas da vista; só trabalha com o objeto em [+0xd0] == 6.
// r8 = índices das células, r9d = quantas. Loga cada objeto novo e cada
// troca de estado, e soma as células por segundo.
constexpr uintptr_t kCellJobRva = 0xa7f700;
constexpr uint8_t kCellJobPrologue[] = {0x48, 0x8b, 0xc4, 0x48, 0x89, 0x50, 0x10, 0x57, 0x41, 0x55};
using CellJobFn = void(__fastcall *)(void *, void *, void *, uint64_t, void *, void *, void *, void *, void *, void *,
                                     void *, void *);
CellJobFn g_cellJob = nullptr;
std::atomic<uint64_t> g_secCells{0}, g_secCellCalls{0}, g_secCellReady{0};
// Jobs vindos da vista principal e suas células. A vista da tela chama o job
// de dentro do cull dos objetos 0x14039ccb0 (volta em +39d107) com a lista
// [cena+0x1190]/[cena+0x1198]. O 3bad20 (volta em +3bad93) é a
// "ground_cover_camera" do "top down renderer" (alvo 128x128 da vegetação) e
// o 3bac20 (+3bacb1) são as 6 faces do "fog renderer" (0x140392e40).
constexpr uintptr_t kMainCellRet = 0x39d107;
std::atomic<uint64_t> g_secCellMain{0}, g_secCellMainN{0};
// "todas_celulas=1": na carga o job da vista principal chega com a lista vazia.
// A lista é a saída da consulta do track.vis (0x140c2de70 → job 0x140c11b00),
// que só anda com o sistema de visibilidade preparado, e o preparo dele traz a
// multidão, que espera o patchup_ot. Com a chave, o job recebe todas as
// células do terreno e o próprio 0x140bcb2d0 escolhe o que desenhar pela
// distância (o float do par não é lido aqui).
bool g_allCells = false;
std::atomic<uint64_t> g_secCellFake{0};
std::atomic<uint32_t> g_cellTotal{0}; // células do terreno (job da vista principal, estado 6)
const uint64_t *AllCellsList() {
  static const uint64_t *const list = [] {
    static std::array<uint64_t, 4096> a{};
    for (std::size_t i = 0; i < a.size(); ++i) {
      a[i] = i; // par (u32 índice, f32 0)
    }
    return a.data();
  }();
  return list;
}
std::mutex g_cellMutex;
struct CellObj {
  void *obj;
  int state;
};
std::array<CellObj, 16> g_cellObjs{};
void __fastcall HookCellJob(void *obj, void *a, void *cells, uint64_t count, void *e, void *f, void *g, void *h,
                            void *i, void *j, void *k, void *l) {
  InFlight fl;
  if (g_measure.load(std::memory_order_relaxed) && obj != nullptr) {
    const auto *b = static_cast<const uint8_t *>(obj);
    const int state = *reinterpret_cast<const int *>(b + 0xd0);
    const uint32_t n = static_cast<uint32_t>(count);
    g_secCellCalls.fetch_add(1, std::memory_order_relaxed);
    g_secCells.fetch_add(n, std::memory_order_relaxed);
    if (state == 6) {
      g_secCellReady.fetch_add(1, std::memory_order_relaxed);
    }
    if (reinterpret_cast<uintptr_t>(__builtin_return_address(0)) - g_exeBase == kMainCellRet) {
      g_secCellMain.fetch_add(1, std::memory_order_relaxed);
      g_secCellMainN.fetch_add(n, std::memory_order_relaxed);
      // Argumentos da chamada principal: loga quando mudam (carga x corrida).
      static std::atomic<uint64_t> lastSig{0};
      const uint64_t sig = (reinterpret_cast<uint64_t>(h) & 0xffffffffull) ^
                           ((reinterpret_cast<uint64_t>(i) & 0xff) << 32) ^
                           ((reinterpret_cast<uint64_t>(j) & 0xff) << 40) ^
                           ((reinterpret_cast<uint64_t>(k) & 0xff) << 48) ^
                           ((reinterpret_cast<uint64_t>(l) & 0xff) << 56) ^ (g != nullptr ? 1ull << 31 : 0) ^
                           (f != nullptr ? 1ull << 30 : 0);
      if (lastSig.exchange(sig) != sig) {
        char sigLine[220];
        std::snprintf(sigLine, sizeof sigLine, "LoadView[jobcel]: a=%p e=%p f=%p g=%p mask=%x b9=%u b10=%u b11=%u n12=%u estado %d %s", a, e, f,
                     g, static_cast<unsigned>(reinterpret_cast<uintptr_t>(h)),
                     static_cast<unsigned>(reinterpret_cast<uintptr_t>(i) & 0xff),
                     static_cast<unsigned>(reinterpret_cast<uintptr_t>(j) & 0xff),
                     static_cast<unsigned>(reinterpret_cast<uintptr_t>(k) & 0xff),
                     static_cast<unsigned>(reinterpret_cast<uintptr_t>(l)), state,
                     g_forcedScene.load() != nullptr ? "forcado" : "normal");
        Logger::Info(sigLine);
      }
    }
    std::lock_guard<std::mutex> lock(g_cellMutex);
    std::size_t slot = g_cellObjs.size();
    for (std::size_t s = 0; s < g_cellObjs.size(); ++s) {
      if (g_cellObjs[s].obj == obj || g_cellObjs[s].obj == nullptr) {
        slot = s;
        break;
      }
    }
    if (slot < g_cellObjs.size() && (g_cellObjs[slot].obj == nullptr || g_cellObjs[slot].state != state)) {
      const bool fresh = g_cellObjs[slot].obj == nullptr;
      const int old = g_cellObjs[slot].state;
      g_cellObjs[slot] = {obj, state};
      const auto caller = reinterpret_cast<uintptr_t>(__builtin_return_address(0)) - g_exeBase;
      char line[200];
      std::snprintf(line, sizeof line,
                    "LoadView[celulas]: obj %p estado %d%s%d, %u células (de %u), chamador +%llx, %s", obj,
                    fresh ? state : old, fresh ? "" : " -> ", fresh ? -1 : state, n,
                    *reinterpret_cast<const uint32_t *>(b + 0x38610), static_cast<unsigned long long>(caller),
                    g_active ? "carga" : "corrida");
      Logger::Info(line);
    }
  }
  // Com rastrear_sistemas: a cada ~1 s da vista principal, quantas células têm
  // renderáveis. Célula = terreno+0x210+i*0x120; +0x48 objeto de LOD, +0x50..
  // +0x98 cabeças das cadeias que o 0x140bcb2d0 empurra (0x140bcb230, próximo
  // em +0x68). O tracksplit carrega com bindAfterLoad=1 (token
  // bindTrackAfterLoad): a suspeita é que as cadeias só se enchem no fim.
  // a = culler [cena+0x14e0]: olho +0xe0, LOD +0xf0, +0xf4, +0xf8, byte +0x110.
  if (g_traceSystems && obj != nullptr && a != nullptr &&
      reinterpret_cast<uintptr_t>(__builtin_return_address(0)) - g_exeBase == kMainCellRet) {
    static std::atomic<ULONGLONG> infoAt{0};
    const ULONGLONG now = GetTickCount64();
    ULONGLONG due = infoAt.load(std::memory_order_relaxed);
    if (now >= due && infoAt.compare_exchange_strong(due, now + 1000)) {
      const auto *b = static_cast<const uint8_t *>(obj);
      const uint32_t total = std::min<uint32_t>(*reinterpret_cast<const uint32_t *>(b + 0x38610), 4096);
      uint32_t withAny = 0, withLod = 0, flag40 = 0, links = 0;
      uint32_t perField[10] = {};
      for (uint32_t c = 0; c < total; ++c) {
        const uint8_t *cell = b + 0x210 + static_cast<size_t>(c) * 0x120;
        bool any = false;
        for (int k = 0; k < 10; ++k) {
          const auto *node = *reinterpret_cast<const uint8_t *const *>(cell + 0x50 + k * 8);
          if (node != nullptr) {
            any = true;
            ++perField[k];
            for (int guard = 0; node != nullptr && guard < 64; ++guard) {
              ++links;
              node = *reinterpret_cast<const uint8_t *const *>(node + 0x68);
            }
          }
        }
        withAny += any ? 1 : 0;
        withLod += *reinterpret_cast<void *const *>(cell + 0x48) != nullptr ? 1 : 0;
        flag40 += cell[0x40] != 0 ? 1 : 0;
      }
      const auto *cu = static_cast<const uint8_t *>(a);
      const auto *eye = reinterpret_cast<const float *>(cu + 0xe0);
      const float *box = total > 0 ? reinterpret_cast<const float *>(b + 0x210 + 0xa0) : eye;
      char info[420];
      std::snprintf(info, sizeof info,
                    "LoadView[celinfo]: %s estado %d, %u células: %u com cadeia (%u nós), %u com LOD, %u +40 | "
                    "campos 50..98 = %u %u %u %u %u %u %u %u %u %u | olho [%.1f %.1f %.1f] lod %.4f f4=%u f8=%u "
                    "b110=%u | cel0 [%.0f %.0f %.0f]-[%.0f %.0f %.0f], %u na lista",
                    g_active ? "carga" : "corrida", *reinterpret_cast<const int *>(b + 0xd0), total, withAny, links,
                    withLod, flag40, perField[0], perField[1], perField[2], perField[3], perField[4], perField[5],
                    perField[6], perField[7], perField[8], perField[9], eye[0], eye[1], eye[2],
                    *reinterpret_cast<const float *>(cu + 0xf0), *reinterpret_cast<const uint32_t *>(cu + 0xf4),
                    *reinterpret_cast<const uint32_t *>(cu + 0xf8), cu[0x110], box[0], box[1], box[2], box[4], box[5],
                    box[6], static_cast<unsigned>(count));
      Logger::Info(info);
    }
  }
  if (obj != nullptr && reinterpret_cast<uintptr_t>(__builtin_return_address(0)) - g_exeBase == kMainCellRet) {
    const auto *b = static_cast<const uint8_t *>(obj);
    const uint32_t total = *reinterpret_cast<const uint32_t *>(b + 0x38610);
    if (*reinterpret_cast<const int *>(b + 0xd0) == 6 && total > 0 && total <= 4096) {
      g_cellTotal.store(total, std::memory_order_relaxed);
    }
  }
  if (g_allCells && g_active && count == 0 && obj != nullptr &&
      reinterpret_cast<uintptr_t>(__builtin_return_address(0)) - g_exeBase == kMainCellRet) {
    const auto *b = static_cast<const uint8_t *>(obj);
    const uint32_t total = *reinterpret_cast<const uint32_t *>(b + 0x38610);
    if (*reinterpret_cast<const int *>(b + 0xd0) == 6 && total > 0 && total <= 4096) {
      cells = const_cast<uint64_t *>(AllCellsList());
      count = total;
      g_secCellFake.fetch_add(1, std::memory_order_relaxed);
    }
  }
  g_cellJob(obj, a, cells, count, e, f, g, h, i, j, k, l);
}

// 0x1403c9170: passe do mundo (vt+0xa0 da cena); r9 = vista, lista visível
// em [vista+0x18]. Loga cada vista nova uma vez.
constexpr uintptr_t kWorldPassRva = 0x3c9170;
constexpr uint8_t kWorldPassPrologue[] = {0x4c, 0x89, 0x44, 0x24, 0x18, 0x55, 0x56, 0x57, 0x41, 0x54};
using WorldPassFn = void(__fastcall *)(void *, void *, void *, void *);
WorldPassFn g_worldPass = nullptr;
std::array<void *, 16> g_viewsSeen{};
void __fastcall HookWorldPass(void *scene, void *a, void *b, void *view) {
  InFlight f;
  if (g_measure.load(std::memory_order_relaxed) && view != nullptr) {
    bool seen = false;
    std::size_t freeSlot = g_viewsSeen.size();
    for (std::size_t i = 0; i < g_viewsSeen.size(); ++i) {
      if (g_viewsSeen[i] == view) {
        seen = true;
        break;
      }
      if (g_viewsSeen[i] == nullptr && freeSlot == g_viewsSeen.size()) {
        freeSlot = i;
      }
    }
    if (!seen && freeSlot < g_viewsSeen.size()) {
      g_viewsSeen[freeSlot] = view;
      const auto *q = static_cast<const uint64_t *>(view);
      char line[300];
      std::snprintf(line, sizeof line,
                    "LoadView[vista]: cena %p vista %p %s: %llx %llx %llx %llx %llx %llx %llx %llx", scene, view,
                    g_active ? "carga" : "corrida", (unsigned long long)q[0], (unsigned long long)q[1],
                    (unsigned long long)q[2], (unsigned long long)q[3], (unsigned long long)q[4],
                    (unsigned long long)q[5], (unsigned long long)q[6], (unsigned long long)q[7]);
      Logger::Info(line);
    }
  }
  g_worldPass(scene, a, b, view);
}

// 0x1409d6ee0: render de uma vista (desenha os itens em 9d75e0, depois sombra e luz, que escrevem o CB 5 do
// VS). O bloco de sombra/luz só roda se [rdi+0x40]->vtbl[0xb8]() e o byte +0x71 da config da vista (rdi +
// idx*64, idx = [TLS+0x48]) passarem; dentro, [[rdi+0x30]+0x16118/0x16309/0x16648] escolhem o caminho.
// Com "vigiar_cb0=1" loga essas portas por fase (linha LoadView[vista] por segundo).
constexpr uintptr_t kViewRenderRva = 0x9d6ee0;
constexpr uint8_t kViewRenderPrologue[] = {0x48, 0x89, 0x5c, 0x24, 0x18, 0x48, 0x89, 0x54,
                                           0x24, 0x10, 0x55, 0x56, 0x57, 0x41, 0x54};
using ViewRenderFn = uint64_t(__fastcall *)(uint8_t *, void *, void *, void *, void *, void *);
ViewRenderFn g_viewRender = nullptr;
// "forcar_sombra=1": na carga, liga os bytes [o40+0xe11] e [o40+0xe13] (portas das cascatas em 3c1090) antes
// do render. Só quando as cascatas já existem: o desenho (39bbb0) lê [o40+0xcb8+i*8], que só o preparo
// 3c62d0 (vtbl[0x50] do o40, também barrado por e11) preenche; sem ele, crash em exe+0x8d9dcf (m141).
bool g_forceShadow = false;
std::atomic<uint32_t> g_shadowForced{0};
bool CascadesReady(const uint8_t *o40) {
  const uint32_t n = *reinterpret_cast<const uint32_t *>(o40 + 0xe20);
  if (n == 0 || n > 4) {
    return false;
  }
  for (uint32_t i = 0; i < n; ++i) {
    if (*reinterpret_cast<void *const *>(o40 + 0xcb8 + i * 8) == nullptr) {
      return false;
    }
  }
  return true;
}

// 0x1403c62d0 (vtbl[0x50] do o40): preparo das cascatas do quadro (matrizes e [o40+0xcb8+i*8], i <
// [o40+0xe20]); só roda com e10 && e11 && !e12. Com "vigiar_cb0=1" conta quem chama, por fase; com
// "forcar_sombra=1", na carga, liga o e11 antes, para o desenho achar as cascatas prontas.
constexpr uintptr_t kShadowSetupRva = 0x3c62d0;
constexpr uint8_t kShadowSetupPrologue[] = {0x48, 0x8b, 0xc4, 0x4c, 0x89, 0x48, 0x20,
                                            0x53, 0x56, 0x41, 0x55, 0x41, 0x56};
// 5º argumento na pilha ([rsp+0x28] na entrada, lido em 3c638f); passa 6 por garantia.
using ShadowSetupFn = uint64_t(__fastcall *)(uint8_t *, void *, void *, void *, void *, void *);
ShadowSetupFn g_shadowSetup = nullptr;
uint64_t __fastcall HookShadowSetup(uint8_t *o40, void *a2, void *a3, void *a4, void *a5, void *a6) {
  if (g_watchCb0 && o40 != nullptr) {
    bool forced = false;
    if (g_forceShadow && g_active && o40[0xe10] == 1 && o40[0xe12] == 0 && o40[0xe11] == 0) {
      o40[0xe11] = 1;
      forced = true;
    }
    char what[80];
    std::snprintf(what, sizeof what, "preparo %s e11=%u n=%u%s", g_active ? "carga" : "corrida", o40[0xe11],
                  *reinterpret_cast<const uint32_t *>(o40 + 0xe20), forced ? " forçado" : "");
    const std::string key = StackKey(what, 1);
    std::lock_guard<std::mutex> lock(g_viewMutex);
    ++g_viewGates[key];
  }
  const uint64_t ret = g_shadowSetup(o40, a2, a3, a4, a5, a6);
  if (g_watchCb0 && o40 != nullptr) {
    char what[48];
    std::snprintf(what, sizeof what, "%s cascatas %s", g_active ? "carga" : "corrida",
                  CascadesReady(o40) ? "prontas" : "vazias");
    std::lock_guard<std::mutex> lock(g_viewMutex);
    ++g_viewGates[what];
  }
  return ret;
}
uint64_t __fastcall HookViewRender(uint8_t *r, void *a2, void *a3, void *a4, void *a5, void *a6) {
  if (g_watchCb0 && r != nullptr) {
    const uint32_t slot = *reinterpret_cast<const uint32_t *>(g_exeBase + 0x2049310);
    auto *const *tls = reinterpret_cast<uint8_t *const *>(__readgsqword(0x58));
    const uint32_t idx = tls != nullptr && tls[slot] != nullptr ? *reinterpret_cast<const uint32_t *>(tls[slot] + 0x48)
                                                                : 0xfffffffeu;
    const uint8_t *view = idx == 0xffffffffu || idx == 0xfffffffeu ? r : r + (static_cast<uint64_t>(idx) << 6);
    auto rva = [](const void *p) {
      const auto a = reinterpret_cast<uintptr_t>(p);
      return a >= g_exeBase && a < g_exeEnd ? static_cast<unsigned>(a - g_exeBase) : 0u;
    };
    auto *o40 = *reinterpret_cast<uint8_t *const *>(r + 0x40);
    const void *fnB8 = o40 != nullptr ? (*reinterpret_cast<void *const *const *>(o40))[0xb8 / 8] : nullptr;
    const void *vt40 = o40 != nullptr ? *reinterpret_cast<void *const *>(o40) : nullptr;
    auto *o30 = *reinterpret_cast<uint8_t *const *>(r + 0x30);
    const unsigned e10 = o40 != nullptr && rva(fnB8) == 0x3b14a0 ? o40[0xe10] : 255u;
    // 9d73d0 (cascatas) ainda exige [r+0xf98], [r+0xf50] e o byte +0x70 da config (3ac310 = config+0x60).
    void *f98 = *reinterpret_cast<void *const *>(r + 0xf98);
    void *f50 = *reinterpret_cast<void *const *>(r + 0xf50);
    const unsigned c70 = view[0x70];
    // 3c1090 (vtbl[0x58] de o40) só desenha as cascatas com e10 && e11 && !e12 && e13.
    const unsigned e11 = e10 != 255u ? o40[0xe11] : 255u, e12 = e10 != 255u ? o40[0xe12] : 255u,
                   e13 = e10 != 255u ? o40[0xe13] : 255u;
    if (g_forceShadow && g_active && e10 == 1 && e12 == 0 && (e11 == 0 || e13 == 0) && CascadesReady(o40)) {
      o40[0xe11] = 1;
      o40[0xe13] = 1;
      g_shadowForced.fetch_add(1, std::memory_order_relaxed);
    }
    char key[352];
    std::snprintf(key, sizeof key,
                  "%s vista %p tipo %x idx %d | o40 %p vt %x b8 %x e10=%u e11=%u e12=%u e13=%u | f98 %p f50 %p | cfg 70=%u 71=%u 72=%u 73=%u 64=%.3g | o30 16118=%u 16309=%u "
                  "16648=%p",
                  g_active ? "carga" : "corrida", static_cast<void *>(r), *reinterpret_cast<const uint32_t *>(r + 0x14),
                  static_cast<int>(idx), static_cast<void *>(o40), rva(vt40), rva(fnB8), e10, e11, e12, e13, f98, f50, c70, view[0x71], view[0x72], view[0x73],
                  static_cast<double>(*reinterpret_cast<const float *>(view + 0x64)), o30 ? o30[0x16118] : 255u,
                  o30 ? o30[0x16309] : 255u, o30 ? *reinterpret_cast<void *const *>(o30 + 0x16648) : nullptr);
    std::lock_guard<std::mutex> lock(g_viewMutex);
    ++g_viewGates[key];
  }
  return g_viewRender(r, a2, a3, a4, a5, a6);
}

// 0x1404b4690: escolhe e ordena as sondas IBL (luz ambiente dos objetos, CB 7 "Lights") pela distância a
// uma posição (rdx -> vetor de 16 B). O quadro 4b1990 em modo 0 passa [[obj+0x2050]+0x17c0]+0xd0, que é a
// posição do carro (com NaN o sort 0x1404628a0 trava, track_loading §9.11). Com "vigiar_cb0=1" loga a
// posição por fase; com "luz_olho=1", na carga e com a pose do "olhar", passa o olho no lugar.
constexpr uintptr_t kIblSelectRva = 0x4b4690;
constexpr uint8_t kIblSelectPrologue[] = {0x48, 0x8b, 0xc4, 0x48, 0x89, 0x50, 0x10, 0x55, 0x53, 0x57};
using IblSelectFn = uint64_t(__fastcall *)(uint8_t *, const float *, void *, void *, void *, void *);
IblSelectFn g_iblSelect = nullptr;
bool g_iblEye = false;
uint64_t __fastcall HookIblSelect(uint8_t *obj, const float *pos, void *a3, void *a4, void *a5, void *a6) {
  alignas(16) float eye[4] = {};
  const float *use = pos;
  if (g_iblEye && g_active && g_forceLook && pos != nullptr) {
    eye[0] = g_lookEye[0];
    eye[1] = g_lookEye[1];
    eye[2] = g_lookEye[2];
    eye[3] = pos[3];
    use = eye;
  }
  if (g_watchCb0 && pos != nullptr) {
    char what[128];
    std::snprintf(what, sizeof what, "%s ibl pos %.0f %.0f %.0f%s", g_active ? "carga" : "corrida",
                  static_cast<double>(pos[0]), static_cast<double>(pos[1]), static_cast<double>(pos[2]),
                  use != pos ? " -> olho" : "");
    std::lock_guard<std::mutex> lock(g_viewMutex);
    ++g_viewGates[what];
  }
  return g_iblSelect(obj, use, a3, a4, a5, a6);
}

// 0x1408cc320(st, câmera, r8, jitter): monta o CameraParamsConstantBuffer (CB 3) em st+0x10, que 8cee80
// sobe com vtbl 0x350. O shader declara 320 B, com leftEye em CB+0x100 (st+0x110), mas a função só escreve
// até CB+0xff: leftEye fica com lixo da pilha (110616.5 na corrida, ~0.72 na carga, m160/m161). Suspeito dos
// objetos escuros, refutado: com o valor da corrida gravado na carga (m162–m167) nada muda; a causa era a
// lista de luzes de sonda vazia (ver "todas_zonas"). "vigiar_cb3=1" loga o valor; "luz_cb3=<valor>" o grava.
constexpr uintptr_t kCamParamsRva = 0x8cc320;
constexpr uint8_t kCamParamsPrologue[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24,
                                          0x10, 0x57, 0x48, 0x81, 0xec, 0xa0, 0x00, 0x00, 0x00};
using CamParamsFn = void(__fastcall *)(uint8_t *, void *, void *, void *, void *, void *);
CamParamsFn g_camParams = nullptr;
bool g_leftEyeSet = false;
bool g_watchCb3 = false;
float g_leftEye = 0.0f;
void __fastcall HookCamParams(uint8_t *st, void *cam, void *a3, void *a4, void *a5, void *a6) {
  g_camParams(st, cam, a3, a4, a5, a6);
  if (g_watchCb3 && st != nullptr) {
    float was = 0.0f;
    std::memcpy(&was, st + 0x110, sizeof was);
    char what[64];
    std::snprintf(what, sizeof what, "cb3 leftEye %.4g", static_cast<double>(was));
    const std::string key = StackKey(what, 1);
    std::lock_guard<std::mutex> lock(g_viewMutex);
    ++g_viewGates[key];
  }
  if (g_leftEyeSet && g_active && st != nullptr) {
    float row[4] = {g_leftEye, 0.0f, 0.0f, 0.0f};
    std::memcpy(st + 0x110, row, sizeof row);
  }
}

// 0x1406c4310(cena 0x1416b0440, vista): atualiza as luzes da cena (chamada em 0x1404b1d9d). Só roda com o
// byte "sujo" [cena+0x15d8] ligado: junta as luzes dinâmicas (64 B em +0x1870, conta +0xd50) e as das sondas
// (0x44 B em +0x5870, conta +0xd54), vira o índice duplo +0x1734 e sobe gDynamicLightBuffer/gProbeLightBuffer
// (+0x1768/+0x17a8). As malhas estáticas (s12) e o carro se iluminam pela grade de sondas (PS t3/t21): zerar
// esses dois na corrida deixa o prédio preto igual à carga (m180). "vigiar_luzes=1" conta as chamadas por fase
// com o sujo e as contas; "forcar_luzes=1" liga o sujo na carga antes de cada chamada.
constexpr uintptr_t kSceneLightsRva = 0x6c4310;
constexpr uint8_t kSceneLightsPrologue[] = {0x48, 0x89, 0x5c, 0x24, 0x20, 0x56, 0x48, 0x83, 0xec, 0x40};
using SceneLightsFn = void(__fastcall *)(uint8_t *, void *);
SceneLightsFn g_sceneLights = nullptr;
bool g_watchLights = false;
bool g_forceLights = false;
void __fastcall HookSceneLights(uint8_t *cena, void *vista) {
  if (cena != nullptr && g_forceLights && g_active) {
    cena[0x15d8] = 1;
  }
  if (g_watchLights && cena != nullptr) {
    uint32_t d50 = 0, d54 = 0;
    std::memcpy(&d50, cena + 0xd50, sizeof d50);
    std::memcpy(&d54, cena + 0xd54, sizeof d54);
    const unsigned dirty = cena[0x15d8];
    g_sceneLights(cena, vista);
    uint32_t a50 = 0, a54 = 0;
    std::memcpy(&a50, cena + 0xd50, sizeof a50);
    std::memcpy(&a54, cena + 0xd54, sizeof a54);
    char k[96];
    std::snprintf(k, sizeof k, "luzes %s sujo%u d50 %u->%u d54 %u->%u", g_active ? "carga" : "corrida", dirty, d50,
                  a50, d54, a54);
    std::lock_guard<std::mutex> lock(g_viewMutex);
    ++g_viewGates[k];
    return;
  }
  g_sceneLights(cena, vista);
}

// 0x1406a1320(cena+0xf0, luz): põe uma luz de sonda na lista (+0x5870, conta +0xd54, até 31). Entra pela
// vtbl 0x1412a1750 (+0x40) do objeto em cena+0xf0. Com "vigiar_luzes=1" conta quem chama, por fase.
constexpr uintptr_t kProbeLightAddRva = 0x6a1320;
constexpr uint8_t kProbeLightAddPrologue[] = {0x40, 0x53, 0x48, 0x81, 0xec, 0xb0, 0x00, 0x00, 0x00};
using ProbeLightAddFn = void(__fastcall *)(uint8_t *, uint8_t *);
ProbeLightAddFn g_probeLightAdd = nullptr;
void __fastcall HookProbeLightAdd(uint8_t *lista, uint8_t *luz) {
  if (g_watchLights) {
    const std::string key = StackKey("luz+", 1);
    std::lock_guard<std::mutex> lock(g_viewMutex);
    ++g_viewGates[key];
  }
  g_probeLightAdd(lista, luz);
}

// 0x1406a3f60(0x1415a3ee0, vista+0x17c0, ...13 argumentos): consulta as luzes visíveis (c34680) e chama a
// 0x1406a1320 para cada uma. Vem de 0x14039d564 (função 0x14039ccb0). "vigiar_luzes=1" conta por fase.
constexpr uintptr_t kLightQueryRva = 0x6a3f60;
constexpr uint8_t kLightQueryPrologue[] = {0x48, 0x83, 0xec, 0x68, 0x8b, 0x84, 0x24, 0xd0, 0x00, 0x00, 0x00};
using LightQueryFn = void(__fastcall *)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,
                                        uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
LightQueryFn g_lightQuery = nullptr;
bool g_allZones = false;
std::atomic<uint32_t> g_zonesForced{0};
void __fastcall HookLightQuery(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6,
                               uint64_t a7, uint64_t a8, uint64_t a9, uint64_t a10, uint64_t a11, uint64_t a12,
                               uint64_t a13) {
  // "todas_zonas=1": na carga a lista (a5/a6, pares u32 célula + f32, de [cena+0x1348]/[cena+0x1340]) chega
  // vazia, como a das células do job principal; aí nenhuma luz de sonda entra e as malhas estáticas saem pretas.
  // Pior: as luzes já estão registradas e ligadas, mas com célula -1 até a visibilidade ficar pronta (m186:
  // 11 sondas, células -1 na carga e 42..52 na corrida). Com a chave passa a célula -1 e todas as do terreno.
  if (g_allZones && g_active && (a6 & 0xffffffff) == 0) {
    static const uint64_t *const zones = [] {
      static std::array<uint64_t, 4097> a{};
      a[0] = 0xffffffffull; // par (u32 -1, f32 0)
      for (std::size_t i = 1; i < a.size(); ++i) {
        a[i] = i - 1;
      }
      return a.data();
    }();
    const uint32_t total = std::min<uint32_t>(g_cellTotal.load(std::memory_order_relaxed), 4096);
    a5 = reinterpret_cast<uint64_t>(zones);
    a6 = (a6 & ~0xffffffffull) | (1u + total);
    g_zonesForced.fetch_add(1, std::memory_order_relaxed);
  }
  if (g_watchLights && !g_active && (a6 & 0xffffffff) != 0 && a5 != 0) {
    std::string ids = "zonas corrida:";
    const uint32_t n = std::min<uint32_t>(static_cast<uint32_t>(a6), 24);
    for (uint32_t z = 0; z < n; ++z) {
      uint32_t id = 0;
      std::memcpy(&id, reinterpret_cast<const uint8_t *>(a5) + z * 8ull, sizeof id);
      ids += " " + std::to_string(id);
    }
    std::lock_guard<std::mutex> lock(g_viewMutex);
    ++g_viewGates[ids];
  }
  if (g_watchLights && a1 != 0) {
    // Lista das luzes de sonda do sistema de luzes (a1+0x130 = r14 de c34680): [r14+0x810] -> cabeça, sentinela
    // em +0x20, primeiro em +0x28, próximo em +0x8; item +0xc4 = célula, dados em +0x10, nó em dados+0x98 e o
    // bit 1 de [nó+0x110] precisa estar ligado.
    const auto *sys = reinterpret_cast<const uint8_t *>(a1 + 0x130);
    const auto *head = *reinterpret_cast<const uint8_t *const *>(sys + 0x810);
    uint32_t n = 0, withNode = 0, on = 0, idMin = ~0u, idMax = 0;
    if (head != nullptr) {
      const uint8_t *sentinel = head + 0x20;
      const auto *it = *reinterpret_cast<const uint8_t *const *>(head + 0x28);
      for (int guard = 0; it != nullptr && it != sentinel && guard < 4096; ++guard) {
        ++n;
        uint32_t id = 0;
        std::memcpy(&id, it + 0xc4, sizeof id);
        idMin = std::min(idMin, id);
        idMax = std::max(idMax, id);
        const auto *node = *reinterpret_cast<const uint8_t *const *>(it + 0x10 + 0x98);
        if (node != nullptr) {
          ++withNode;
          on += (node[0x110] >> 1) & 1;
        }
        it = *reinterpret_cast<const uint8_t *const *>(it + 0x8);
      }
    }
    char k[128];
    std::snprintf(k, sizeof k, "sondas %s: %u na lista, %u com nó, %u ligadas, células %u..%u",
                  g_active ? "carga" : "corrida", n, withNode, on, n != 0 ? idMin : 0, idMax);
    std::lock_guard<std::mutex> lock(g_viewMutex);
    ++g_viewGates[k];
  }
  if (g_watchLights) {
    // a5/a6 = lista de ids (zonas) e a conta: o laço de c34680 só põe luzes cujo [+0xc4] esteja nela.
    uint32_t id0 = 0;
    if (a5 != 0 && (a6 & 0xffffffff) != 0) {
      std::memcpy(&id0, reinterpret_cast<const void *>(a5), sizeof id0);
    }
    char what[96];
    std::snprintf(what, sizeof what, "consulta luzes r9 %llu ids %llu (1º %u) a8 %llx",
                  static_cast<unsigned long long>(a4 & 0xffffffff), static_cast<unsigned long long>(a6 & 0xffffffff),
                  id0, static_cast<unsigned long long>(a8 & 0xffffffff));
    const std::string key = StackKey(what, 1);
    std::lock_guard<std::mutex> lock(g_viewMutex);
    ++g_viewGates[key];
  }
  g_lightQuery(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13);
}

// 0x1403c3f10 (vtbl[0xd0] do o40, vtbl 126a5c0): mov [rcx+0xe11], dl; ret. Liga/desliga as cascatas de sombra.
// Com "vigiar_cb0=1" conta quem chama e com que valor (vai junto da linha LoadView[vista]).
constexpr uintptr_t kShadowSetRva = 0x3c3f10;
constexpr uint8_t kShadowSetBytes[] = {0x88, 0x91, 0x11, 0x0e, 0x00, 0x00, 0xc3};
using ShadowSetFn = void(__fastcall *)(uint8_t *, uint8_t);
ShadowSetFn g_shadowSet = nullptr;
void __fastcall HookShadowSet(uint8_t *o40, uint8_t on) {
  if (g_watchCb0) {
    char what[24];
    std::snprintf(what, sizeof what, "e11<-%u", static_cast<unsigned>(on));
    const std::string key = StackKey(what, 1);
    std::lock_guard<std::mutex> lock(g_viewMutex);
    ++g_viewGates[key];
  }
  g_shadowSet(o40, on);
}

// 0x1403c9170: render da cena (rdi = cena; [cena+0x40] = o40 das sombras). Em 3c94f3 desenha a lista
// [r9+0x30] por bbf4a0, que liga nos slots 11..14 do PS as texturas de luz dos objetos (1024 BC1, 256 BC3,
// array 128x128 R11G11B10, 2048 BC7); só se os bytes [[cena+0x1010]+0x19924] e +0x19927 forem 0.
// Na carga esse caminho não roda e os objetos saem pretos. Com vigiar_tex loga os bytes por fase;
// "luz_objetos=1" zera os dois na carga durante a chamada (e devolve depois).
constexpr uintptr_t kSceneRenderRva = 0x3c9170;
constexpr uint8_t kSceneRenderPrologue[] = {0x4c, 0x89, 0x44, 0x24, 0x18, 0x55, 0x56, 0x57, 0x41, 0x54, 0x48, 0x83, 0xec, 0x78};
using SceneRenderFn = uint64_t(__fastcall *)(uint8_t *, void *, void *, void *, void *, void *);
SceneRenderFn g_sceneRender = nullptr;
bool g_lightObjects = false;
std::atomic<uint32_t> g_lightForced{0};
uint64_t __fastcall HookSceneRender(uint8_t *cena, void *a2, void *a3, void *a4, void *a5, void *a6) {
  uint8_t *sys = cena != nullptr ? *reinterpret_cast<uint8_t **>(cena + 0x1010) : nullptr;
  if (g_watchTex && sys != nullptr) {
    // Listas do 4º argumento: [a4+0x30] (bbf4a0, a das texturas de luz), +0x20, +0x18; itens ligados por [nó].
    auto count = [](const uint8_t *lists, int off, std::string &first) {
      if (lists == nullptr) {
        return -1;
      }
      int n = 0;
      for (auto *node = *reinterpret_cast<uint8_t *const *>(lists + off); node != nullptr && n < 100000;
           node = *reinterpret_cast<uint8_t *const *>(node)) {
        if (n == 0) {
          const auto *item = *reinterpret_cast<uint8_t *const *>(node + 0x18);
          const auto vt = item != nullptr ? reinterpret_cast<uintptr_t>(*reinterpret_cast<void *const *>(item)) : 0;
          char one[48];
          std::snprintf(one, sizeof one, "@%llx", vt >= g_exeBase && vt < g_exeEnd
                                                       ? static_cast<unsigned long long>(vt - g_exeBase)
                                                       : 0ull);
          first = one;
        }
        ++n;
      }
      return n;
    };
    const auto *lists = static_cast<const uint8_t *>(a4);
    std::string f30, f20, f18;
    const int n30 = count(lists, 0x30, f30), n20 = count(lists, 0x20, f20), n18 = count(lists, 0x18, f18);
    char key[256];
    std::snprintf(key, sizeof key, "%s cena %p 19924..8=%u,%u,%u,%u,%u 1e98=%u 1ea8=%02x | listas %p 30:%d%s 20:%d%s 18:%d%s",
                  g_active ? "carga" : "corrida", static_cast<void *>(cena), sys[0x19924], sys[0x19925],
                  sys[0x19926], sys[0x19927], sys[0x19928], *reinterpret_cast<const uint32_t *>(cena + 0x1e98),
                  cena[0x1ea8], a4, n30, f30.c_str(), n20, f20.c_str(), n18, f18.c_str());
    std::lock_guard<std::mutex> lock(g_viewMutex);
    ++g_viewGates[key];
  }
  if (g_lightObjects && g_active && sys != nullptr && (sys[0x19924] != 0 || sys[0x19927] != 0)) {
    const uint8_t b24 = sys[0x19924], b27 = sys[0x19927];
    sys[0x19924] = 0;
    sys[0x19927] = 0;
    g_lightForced.fetch_add(1, std::memory_order_relaxed);
    const uint64_t ret = g_sceneRender(cena, a2, a3, a4, a5, a6);
    sys[0x19924] = b24;
    sys[0x19927] = b27;
    return ret;
  }
  return g_sceneRender(cena, a2, a3, a4, a5, a6);
}

// 0x1404a1580: troca o modo de render do mundo ([obj+0x22a8] atual, [obj+0x22ac] pedido; 0 = corrida, 2 =
// carga, 3 = ?). No modo 2 desliga as cascatas (vtbl[0xd0] do o40 = obj+0x590), liga o bit 8 de
// [[obj+0x2050]+0x1ea8] e escreve matrizes fixas em [obj+0x2060]+0x1110. Chamado de 4b261a (por quadro, se pedido
// != atual) e 476ce6. Com "modo_render=N" (N >= 0), na carga troca o pedido 2 por N.
constexpr uintptr_t kRenderModeRva = 0x4a1580;
constexpr uint8_t kRenderModePrologue[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x57, 0x48, 0x83, 0xec, 0x20};
using RenderModeFn = void(__fastcall *)(uint8_t *, int32_t);
RenderModeFn g_renderMode = nullptr;
int g_forceRenderMode = -1;
void __fastcall HookRenderMode(uint8_t *obj, int32_t mode) {
  const int32_t asked = mode;
  // O modo 0 lê [[exe+0x16951e8]+0x32a0] (47d950): só existe com a sessão da corrida montada.
  const auto *game = *reinterpret_cast<uint8_t *const *>(g_exeBase + 0x16951e8);
  const bool session = game != nullptr && *reinterpret_cast<void *const *>(game + 0x32a0) != nullptr;
  if (g_forceRenderMode >= 0 && g_active && mode == 2 && session) {
    mode = g_forceRenderMode;
  }
  {
    char what[48];
    std::snprintf(what, sizeof what, "modo %d->%d (pedido %d%s)", obj ? *reinterpret_cast<const int32_t *>(obj + 0x22a8) : -1,
                  mode, asked, session ? ", sessão" : "");
    const std::string key = StackKey(what, 1);
    std::lock_guard<std::mutex> lock(g_viewMutex);
    ++g_viewGates[key];
  }
  g_renderMode(obj, mode);
}

// 0x1408cf220: envia a cópia do CB à GPU. Cópia = {buf@0, src@0x10, sombra@0x18, off@0x20, tamanho@0x24}:
// só faz Map/memcpy/Unmap se src != sombra, depois liga o buf no estágio r8d. Com "vigiar_cb0=1" loga, para
// os itens do chão no VS, se src mudou e o que src tem em c8/c17-c20. Com "forcar_cb0=1", na carga, estraga
// a sombra antes da chamada, obrigando o reenvio (o buf de 448 B é compartilhado entre shaders).
constexpr uintptr_t kCbUploadRva = 0x8cf220;
constexpr uint8_t kCbUploadPrologue[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57,
                                         0x48, 0x83, 0xec, 0x40};
using CbUploadFn = void(__fastcall *)(void *, void *, int32_t);
CbUploadFn g_cbUpload = nullptr;
bool g_forceCb0 = false;
std::atomic<int> g_uploadLogLeft{0};
std::atomic<uint32_t> g_uploadSame[2], g_uploadDiff[2], g_uploadForced{0}; // [1]=carga
// "vigiar_prebake=1": nos envios de CB de 352 B (VS de objeto: emissiveParams@0x100, ObjectPreBakedShadow@0x110) e
// de 176 B (PS: AmbientLightMapScale@0x80, preBakedShadows@0xa0), conta os valores por fase e estágio
// (linha LoadView[pre]). "forcar_prebake=<v>": na carga, põe v nos campos de sombra pré-calculada antes do envio.
bool g_watchPrebake = false;
bool g_forcePrebake = false;
float g_prebakeValue = 1.0f;
std::map<std::string, uint32_t> g_prebakeSeen; // sob g_viewMutex
std::atomic<uint32_t> g_prebakeForced{0};
void PrebakeUpload(const uint8_t *c, int32_t stage) {
  auto *src = *reinterpret_cast<float *const *>(c + 0x10);
  const uint32_t size = *reinterpret_cast<const uint32_t *>(c + 0x24);
  if (g_watchPrebake) {
    const bool same = src != nullptr && *reinterpret_cast<const void *const *>(c + 0x18) != nullptr &&
                      std::memcmp(src, *reinterpret_cast<const void *const *>(c + 0x18), size) == 0;
    char k[64];
    std::snprintf(k, sizeof k, "n %s e%d t%u%s", g_active ? "carga" : "corrida", stage, size,
                  src == nullptr ? " sem-src" : (same ? " igual" : ""));
    std::lock_guard<std::mutex> lock(g_viewMutex);
    ++g_prebakeSeen[k];
  }
  if (src == nullptr || (size != 352 && size != 176)) {
    return;
  }
  const uint32_t a = size == 352 ? 0x100 / 4 : 0x80 / 4, b = size == 352 ? 0x110 / 4 : 0xa0 / 4;
  if (g_forcePrebake && g_active) {
    for (uint32_t i = 0; i < 4; ++i) {
      src[b + i] = g_prebakeValue;
    }
    g_prebakeForced.fetch_add(1, std::memory_order_relaxed);
  }
  if (g_watchPrebake) {
    char k[160];
    std::snprintf(k, sizeof k, "%s e%d t%u a=(%.3g %.3g %.3g %.3g) b=(%.3g %.3g %.3g %.3g)", g_active ? "carga" : "corrida",
                  stage, size, src[a], src[a + 1], src[a + 2], src[a + 3], src[b], src[b + 1], src[b + 2], src[b + 3]);
    std::lock_guard<std::mutex> lock(g_viewMutex);
    ++g_prebakeSeen[k];
  }
}
void __fastcall HookCbUpload(void *copy, void *ctx, int32_t stage) {
  if ((g_watchPrebake || g_forcePrebake) && copy != nullptr) {
    PrebakeUpload(static_cast<const uint8_t *>(copy), stage);
  }
  if (g_watchCb0 && copy != nullptr && stage == 0 && t_bindItem != nullptr) {
    const auto *c = static_cast<const uint8_t *>(copy);
    void *buf = *reinterpret_cast<void *const *>(c);
    if (buf != nullptr && buf == g_cb0Buf.load(std::memory_order_relaxed)) {
      bool ground = false;
      {
        std::lock_guard<std::mutex> lock(g_cb0Mutex);
        ground = g_groundItems.count(t_bindItem) != 0;
      }
      if (ground) {
        auto *src = *reinterpret_cast<float *const *>(c + 0x10);
        auto *shadow = *reinterpret_cast<uint8_t *const *>(c + 0x18);
        const uint32_t size = *reinterpret_cast<const uint32_t *>(c + 0x24);
        const bool diff = src != nullptr && shadow != nullptr && std::memcmp(src, shadow, size) != 0;
        (diff ? g_uploadDiff : g_uploadSame)[g_active ? 1 : 0].fetch_add(1, std::memory_order_relaxed);
        if (g_uploadLogLeft.fetch_sub(1) > 0 && src != nullptr) {
          const uint32_t nf = size / 4;
          auto F = [&](uint32_t i) { return static_cast<double>(i < nf ? src[i] : -9999.0f); };
          char line[1024];
          std::snprintf(line, sizeof line,
                        "LoadView[upl]: %s item %p copia %p buf %p src %p sombra %p off %u tam %u %s | "
                        "src=[%.5g %.5g %.5g %.5g | %.5g %.5g %.5g %.5g | %.5g %.5g %.5g %.5g]",
                        g_active ? "carga" : "corrida", t_bindItem, copy, buf, static_cast<void *>(src),
                        static_cast<void *>(shadow), *reinterpret_cast<const uint32_t *>(c + 0x20), size,
                        diff ? "MUDOU" : "igual", F(0), F(1), F(2), F(3), F(4), F(5), F(6), F(7), F(8), F(9),
                        F(10), F(11));
          std::string more = line;
          for (uint32_t i = 12; i < nf && i < 64; ++i) {
            char one[24];
            std::snprintf(one, sizeof one, "%s%.5g", i % 4 == 0 ? " | " : " ", F(i));
            more += one;
          }
          std::snprintf(line, sizeof line, "%s", more.c_str());
          Logger::Info(line);
        }
        if (g_forceCb0 && g_active && !diff && shadow != nullptr && size != 0) {
          shadow[0] ^= 0xff;
          g_uploadForced.fetch_add(1, std::memory_order_relaxed);
        }
      }
    }
  }
  g_cbUpload(copy, ctx, stage);
}

// 0x14090aa20: liga os CBs dos 6 estágios do item (r8). Se [item+0x58] (parâmetros do item) for nulo, pula
// o preenchimento (0x14090acf0) e só reenvia o CB como está. Com "vigiar_cb0=1" guarda o último item da
// thread (sai na linha LoadView[draw]) e conta por segundo itens com e sem parâmetros.
constexpr uintptr_t kCbBindRva = 0x90aa20;
constexpr uint8_t kCbBindPrologue[] = {0x4c, 0x8b, 0xdc, 0x4d, 0x89, 0x4b, 0x20, 0x49, 0x89, 0x53, 0x10};
using CbBindFn = uint8_t(__fastcall *)(void *, void *, void *, void *, void *, void *, uint32_t);
CbBindFn g_cbBind = nullptr;
std::atomic<uint32_t> g_bindWith[2], g_bindWithout[2]; // [0]=corrida [1]=carga
uint8_t __fastcall HookCbBind(void *a1, void *a2, void *item, void *a4, void *a5, void *a6, uint32_t a7) {
  if (g_watchCb0 && item != nullptr) {
    t_bindItem = item;
    t_bindParams = *reinterpret_cast<void *const *>(static_cast<const uint8_t *>(item) + 0x58);
    {
      const uint32_t slot = *reinterpret_cast<const uint32_t *>(g_exeBase + 0x2049310);
      auto *const *tls = reinterpret_cast<uint8_t *const *>(__readgsqword(0x58));
      t_bindIdx = tls != nullptr && tls[slot] != nullptr ? *reinterpret_cast<const int32_t *>(tls[slot] + 0x48) : -3;
    }
    (t_bindParams != nullptr ? g_bindWith : g_bindWithout)[g_active ? 1 : 0].fetch_add(1, std::memory_order_relaxed);
  }
  return g_cbBind(a1, a2, item, a4, a5, a6, a7);
}

// 0x14090acf0: monta o CB de um estágio do shader. 1º laço: valores do material; 2º laço: parâmetros
// automáticos, [cb+0x80][i] = id; slot = arg8[id]; provedor = [[arg5+0xa0]+0x58][slot] -> [p] (pulado
// se nulo ou [p+0x18] != 0) -> [[p]+0x10]->vtbl[1](...). Com "vigiar_cb0=1", conta por segundo, por id,
// quantos foram chamados ou pulados (linha LoadView[param]).
constexpr uintptr_t kCbFillRva = 0x90acf0;
constexpr uint8_t kCbFillPrologue[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c, 0x24, 0x10,
                                       0x48, 0x89, 0x74, 0x24, 0x18, 0x57};
using CbFillFn = void(__fastcall *)(void *, void *, void *, void *, void *, void *, void *, void *, uint32_t);
CbFillFn g_cbFill = nullptr;
std::mutex g_paramMutex;
std::map<uint32_t, uint32_t> g_paramSeen;   // (fase<<31 | estado<<24 | id) -> vezes
std::map<uint32_t, uint32_t> g_paramVtbl;   // (fase<<31 | id) -> RVA da vtable do provedor chamado
std::set<std::tuple<uint32_t, void *, void *, uint32_t>> g_paramCombos;
int g_paramDetailLeft = 0;
void __fastcall HookCbFill(void *cb, void *a2, void *a3, void *a4, void *a5, void *a6, void *a7, void *a8,
                           uint32_t stage) {
  // Item do chão: uma linha por estágio com todos os parâmetros, sem filtro de n.
  uint64_t groundIdx = 0;
  if (g_watchCb0 && cb != nullptr && a3 != nullptr && t_bindItem != nullptr) {
    const uint32_t est = *reinterpret_cast<const uint32_t *>(static_cast<const uint8_t *>(a3) + 0x4b8);
    std::lock_guard<std::mutex> lock(g_cb0Mutex);
    if (auto it = g_groundItems.find(t_bindItem); it != g_groundItems.end() &&
        g_groundFillSeen.size() < 4096 && g_groundFillSeen.emplace(g_active, t_bindItem, est).second) {
      groundIdx = it->second;
    }
  }
  if (groundIdx != 0 && g_groundFillLeft.fetch_sub(1) > 0) {
    const auto *c = static_cast<const uint8_t *>(cb);
    const auto *ids = *reinterpret_cast<const uint32_t *const *>(c + 0x80);
    const uint32_t n = *reinterpret_cast<const uint32_t *>(c + 0x88);
    const uint32_t n1 = *reinterpret_cast<const uint32_t *>(c + 0x78);
    const auto *slots = static_cast<const int32_t *>(a8);
    const auto *ctx = static_cast<const uint8_t *>(a5);
    const uint8_t *tbl = ctx != nullptr ? *reinterpret_cast<const uint8_t *const *>(ctx + 0xa0) : nullptr;
    char head[260];
    std::snprintf(head, sizeof head,
                  "LoadView[fillg]: %s item %p I(%llu) est %u a3 %p a5 %p tabela %p a9 %u thread %lu idx %lld mat %u n %u:",
                  g_active ? "carga" : "corrida", t_bindItem, static_cast<unsigned long long>(groundIdx),
                  *reinterpret_cast<const uint32_t *>(static_cast<const uint8_t *>(a3) + 0x4b8), a3, a5,
                  static_cast<const void *>(tbl), stage, GetCurrentThreadId(), static_cast<long long>(t_bindIdx), n1, n);
    std::string line = head;
    if (slots == nullptr) {
      line += " (sem mapa de slots)";
    }
    for (uint32_t i = 0; i < n && i < 64 && ids != nullptr && slots != nullptr; ++i) {
      const uint32_t id = ids[i];
      const int32_t slot = slots[id];
      char one[96];
      if (slot < 0 || tbl == nullptr) {
        std::snprintf(one, sizeof one, " %u>%d", id, slot);
      } else {
        const uint8_t *e = (*reinterpret_cast<const uint8_t *const *const *>(tbl + 0x58))[slot];
        const uint8_t *pp = e != nullptr ? *reinterpret_cast<const uint8_t *const *>(e) : nullptr;
        uintptr_t vt = 0;
        if (pp != nullptr && *reinterpret_cast<const void *const *>(pp + 0x10) != nullptr) {
          vt = **reinterpret_cast<const uintptr_t *const *>(pp + 0x10);
        }
        std::snprintf(one, sizeof one, " %u>%d:%s%u@%x", id, slot, pp == nullptr ? "nulo" : "f",
                      pp == nullptr ? 0u : *reinterpret_cast<const uint32_t *>(pp + 0x18),
                      vt >= g_exeBase && vt < g_exeEnd ? static_cast<uint32_t>(vt - g_exeBase) : 0u);
      }
      line += one;
    }
    Logger::Info(line);
  }
  if (g_watchCb0 && cb != nullptr && a8 != nullptr && a3 != nullptr &&
      *reinterpret_cast<const uint32_t *>(static_cast<const uint8_t *>(a3) + 0x4b8) == 0) { // estágio 0 = VS
    const auto *c = static_cast<const uint8_t *>(cb);
    const auto *ids = *reinterpret_cast<const uint32_t *const *>(c + 0x80);
    const uint32_t n = *reinterpret_cast<const uint32_t *>(c + 0x88);
    const auto *slots = static_cast<const int32_t *>(a8);
    const auto *ctx = static_cast<const uint8_t *>(a5);
    const uint32_t phase = g_active ? 0x80000000u : 0;
    std::lock_guard<std::mutex> lock(g_paramMutex);
    // Uma linha por combinação nova (fase, a3, a5, a9) com n >= 10: id/slot/estado de cada parâmetro.
    if (n >= 10 && g_paramDetailLeft > 0) {
      const auto key = std::make_tuple(phase, a3, a5, stage);
      if (g_paramCombos.insert(key).second) {
        --g_paramDetailLeft;
        const uint8_t *tbl = ctx != nullptr ? *reinterpret_cast<const uint8_t *const *>(ctx + 0xa0) : nullptr;
        char head[200];
        std::snprintf(head, sizeof head, "LoadView[paramx]: %s a3 %p a5 %p a9 %u tabela %p n %u:", g_active ? "carga" : "corrida",
                      a3, a5, stage, static_cast<const void *>(tbl), n);
        std::string line = head;
        for (uint32_t i = 0; i < n && i < 64 && ids != nullptr; ++i) {
          const uint32_t id = ids[i];
          const int32_t slot = slots[id];
          char one[64];
          if (slot < 0 || tbl == nullptr) {
            std::snprintf(one, sizeof one, " %u>%d", id, slot);
          } else {
            const uint8_t *e = (*reinterpret_cast<const uint8_t *const *const *>(tbl + 0x58))[slot];
            const uint8_t *pp = e != nullptr ? *reinterpret_cast<const uint8_t *const *>(e) : nullptr;
            std::snprintf(one, sizeof one, " %u>%d:%s%u", id, slot, pp == nullptr ? "nulo" : "f",
                          pp == nullptr ? 0u : *reinterpret_cast<const uint32_t *>(pp + 0x18));
          }
          line += one;
        }
        Logger::Info(line);
      }
    }
    for (uint32_t i = 0; i < n && i < 256 && ids != nullptr; ++i) {
      const uint32_t id = ids[i];
      const int32_t slot = slots[id];
      uint32_t st = 0;
      uintptr_t vt = 0;
      if (slot < 0) {
        st = 1;
      } else {
        const uint8_t *tbl = ctx != nullptr ? *reinterpret_cast<const uint8_t *const *>(ctx + 0xa0) : nullptr;
        const uint8_t *e = nullptr;
        if (tbl != nullptr) {
          e = (*reinterpret_cast<const uint8_t *const *const *>(tbl + 0x58))[slot];
        }
        const uint8_t *p = e != nullptr ? *reinterpret_cast<const uint8_t *const *>(e) : nullptr;
        if (tbl == nullptr || e == nullptr) {
          st = 2;
        } else if (p == nullptr) {
          st = 3;
        } else if (*reinterpret_cast<const uint32_t *>(p + 0x18) != 0) {
          st = 4;
        } else if (*reinterpret_cast<const void *const *>(p + 0x10) == nullptr) {
          st = 5;
        } else {
          vt = **reinterpret_cast<const uintptr_t *const *>(p + 0x10);
        }
      }
      ++g_paramSeen[phase | (st << 24) | (id & 0xffffff)];
      if (st == 0 && vt >= g_exeBase && vt < g_exeEnd) {
        g_paramVtbl[phase | (id & 0xffffff)] = static_cast<uint32_t>(vt - g_exeBase);
      }
    }
  }
  g_cbFill(cb, a2, a3, a4, a5, a6, a7, a8, stage);
}

bool HookSlot(void **vtbl, int index, void *detour, void **original) {
  void *fn = vtbl[index];
  if (MH_CreateHook(fn, detour, original) != MH_OK) {
    return false;
  }
  if (MH_EnableHook(fn) != MH_OK) {
    MH_RemoveHook(fn);
    return false;
  }
  g_targets.push_back(fn);
  return true;
}

const char *FormatName(DXGI_FORMAT f) {
  switch (f) {
  case DXGI_FORMAT_R8G8B8A8_UNORM:
  case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
  case DXGI_FORMAT_R8G8B8A8_TYPELESS:
    return "rgba8";
  case DXGI_FORMAT_B8G8R8A8_UNORM:
  case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
  case DXGI_FORMAT_B8G8R8A8_TYPELESS:
    return "bgra8";
  case DXGI_FORMAT_R16G16B16A16_FLOAT:
  case DXGI_FORMAT_R16G16B16A16_TYPELESS:
    return "rgba16f";
  case DXGI_FORMAT_R11G11B10_FLOAT:
    return "r11g11b10f";
  case DXGI_FORMAT_R10G10B10A2_UNORM:
  case DXGI_FORMAT_R10G10B10A2_TYPELESS:
    return "rgb10a2";
  case DXGI_FORMAT_R32_FLOAT:
  case DXGI_FORMAT_R32_TYPELESS:
    return "r32";
  case DXGI_FORMAT_D32_FLOAT:
    return "d32";
  case DXGI_FORMAT_R24G8_TYPELESS:
  case DXGI_FORMAT_D24_UNORM_S8_UINT:
    return "d24s8";
  case DXGI_FORMAT_R32G8X24_TYPELESS:
  case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
    return "d32s8";
  case DXGI_FORMAT_R16_TYPELESS:
  case DXGI_FORMAT_D16_UNORM:
    return "d16";
  default:
    return nullptr;
  }
}

DXGI_FORMAT ViewFormat(DXGI_FORMAT f) {
  switch (f) {
  case DXGI_FORMAT_R8G8B8A8_TYPELESS:
    return DXGI_FORMAT_R8G8B8A8_UNORM;
  case DXGI_FORMAT_B8G8R8A8_TYPELESS:
    return DXGI_FORMAT_B8G8R8A8_UNORM;
  case DXGI_FORMAT_B8G8R8X8_TYPELESS:
    return DXGI_FORMAT_B8G8R8X8_UNORM;
  case DXGI_FORMAT_R16G16B16A16_TYPELESS:
    return DXGI_FORMAT_R16G16B16A16_FLOAT;
  case DXGI_FORMAT_R10G10B10A2_TYPELESS:
    return DXGI_FORMAT_R10G10B10A2_UNORM;
  case DXGI_FORMAT_R32G32B32A32_TYPELESS:
    return DXGI_FORMAT_R32G32B32A32_FLOAT;
  default:
    return f;
  }
}

std::string Describe(const Tally &t, uint32_t frames) {
  char buf[96];
  const char *fmt = FormatName(t.format);
  char fmtBuf[16];
  if (fmt == nullptr) {
    std::snprintf(fmtBuf, sizeof fmtBuf, "fmt%d", static_cast<int>(t.format));
    fmt = fmtBuf;
  }
  char msaa[16] = "";
  if (t.samples > 1) {
    std::snprintf(msaa, sizeof msaa, " msaa%u", t.samples);
  }
  std::snprintf(buf, sizeof buf, "%ux%u %s%s%s x%.0f", t.width, t.height, fmt, msaa,
                t.backbuffer ? " (tela)" : (t.depth ? " (prof)" : ""),
                frames ? static_cast<double>(t.draws) / frames : 0.0);
  return buf;
}

void SampleStack(const Target *t, const char *label) {
  if (g_exeBase == 0) {
    return;
  }
  void *frames[64];
  const USHORT n = RtlCaptureStackBackTrace(1, 62, frames, nullptr);
  StackSeen seen;
  for (USHORT i = 0; i < n && seen.count < kStackFrames; ++i) {
    const auto a = reinterpret_cast<uintptr_t>(frames[i]);
    if (a >= g_exeBase && a < g_exeEnd) {
      seen.rva[seen.count++] = static_cast<uint32_t>(a - g_exeBase);
    }
  }
  if (label != nullptr) {
    std::snprintf(seen.target, sizeof seen.target, "%s", label);
  } else {
    const char *fmt = FormatName(t->format);
    std::snprintf(seen.target, sizeof seen.target, "%ux%u %s%s", t->width, t->height, fmt ? fmt : "?",
                  t->depth ? " (prof)" : "");
  }
  seen.load = g_active;
  seen.thread = GetCurrentThreadId();
  std::lock_guard<std::mutex> lock(g_stackMutex);
  for (StackSeen &s : g_stacks) {
    if (s.count == seen.count && s.load == seen.load && s.rva == seen.rva &&
        std::strcmp(s.target, seen.target) == 0) {
      ++s.hits;
      return;
    }
  }
  if (g_stacks.size() < kMaxStacks) {
    seen.hits = 1;
    g_stacks.push_back(seen);
  }
}

void DumpStacks() {
  std::vector<StackSeen> stacks;
  {
    std::lock_guard<std::mutex> lock(g_stackMutex);
    stacks.swap(g_stacks);
  }
  std::stable_sort(stacks.begin(), stacks.end(), [](const StackSeen &a, const StackSeen &b) {
    if (a.load != b.load) {
      return a.load;
    }
    return std::strcmp(a.target, b.target) < 0;
  });
  Logger::Info("LoadView[pilha]: " + std::to_string(stacks.size()) + " pilhas distintas" +
               (stacks.size() >= kMaxStacks ? " (cheio)." : "."));
  for (const StackSeen &s : stacks) {
    std::string line;
    char head[128];
    std::snprintf(head, sizeof head, "LoadView[pilha] %s | %s | x%u | thread %lu | ", s.load ? "carga" : "corrida",
                  s.target, s.hits, static_cast<unsigned long>(s.thread));
    line = head;
    for (int i = 0; i < s.count; ++i) {
      char f[16];
      std::snprintf(f, sizeof f, "%se%x", i ? "|" : "", s.rva[i]);
      line += f;
    }
    Logger::Info(line);
  }
}

bool IsHdr(DXGI_FORMAT f) { return f == DXGI_FORMAT_R11G11B10_FLOAT || f == DXGI_FORMAT_R16G16B16A16_FLOAT; }

// Float pequeno sem sinal do r11g11b10f (expoente de 5 bits); NaN se inf/nan.
float SmallFloat(uint32_t bits, int mantissa) {
  const uint32_t m = bits & ((1u << mantissa) - 1);
  const uint32_t e = bits >> mantissa;
  if (e == 31) {
    return std::numeric_limits<float>::quiet_NaN();
  }
  const float frac = static_cast<float>(m) / static_cast<float>(1u << mantissa);
  return e == 0 ? std::ldexp(frac, -14) : std::ldexp(1.0f + frac, static_cast<int>(e) - 15);
}

float HalfFloat(uint16_t h) {
  const float v = SmallFloat(h & 0x7fff, 10);
  return (h & 0x8000) != 0 ? -v : v;
}

// Lê a cópia pedida antes (sem esperar a GPU) e, de tempos em tempos, pede outra.
void MeasureExposure(ID3D11Resource *src, DXGI_FORMAT format, UINT width, UINT height) {
  if (g_hdrSource != src) {
    if (g_hdrStaging != nullptr) {
      g_hdrStaging->Release();
      g_hdrStaging = nullptr;
    }
    g_hdrPending = false;
    g_hdrSource = src;
  }
  if (g_hdrStaging == nullptr) {
    ID3D11Device *device = nullptr;
    g_context->GetDevice(&device);
    if (device == nullptr) {
      return;
    }
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_STAGING;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    if (FAILED(device->CreateTexture2D(&desc, nullptr, &g_hdrStaging))) {
      g_hdrStaging = nullptr;
    }
    device->Release();
    if (g_hdrStaging == nullptr) {
      return;
    }
  }
  if (!g_hdrPending) {
    if (++g_hdrTick % 15 == 0) {
      g_context->CopyResource(g_hdrStaging, src);
      g_hdrPending = true;
    }
    return;
  }
  D3D11_MAPPED_SUBRESOURCE mapped{};
  const HRESULT hr = g_context->Map(g_hdrStaging, 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped);
  if (hr == DXGI_ERROR_WAS_STILL_DRAWING) {
    return;
  }
  g_hdrPending = false;
  if (FAILED(hr)) {
    return;
  }
  constexpr UINT kStep = 4;
  std::vector<float> rgb;
  const bool dump = !g_hdrDump.empty() && g_hdrDumpN < 80;
  std::vector<float> lit;
  lit.reserve((width / kStep + 1) * (height / kStep + 1));
  uint32_t total = 0;
  double sumLog = 0.0;
  for (UINT y = 0; y < height; y += kStep) {
    const auto *row = static_cast<const uint8_t *>(mapped.pData) + static_cast<size_t>(y) * mapped.RowPitch;
    for (UINT x = 0; x < width; x += kStep) {
      float r, g, b;
      if (format == DXGI_FORMAT_R11G11B10_FLOAT) {
        uint32_t v;
        std::memcpy(&v, row + x * 4, 4);
        r = SmallFloat(v & 0x7ff, 6);
        g = SmallFloat((v >> 11) & 0x7ff, 6);
        b = SmallFloat(v >> 22, 5);
      } else {
        uint16_t h[3];
        std::memcpy(h, row + x * 8, 6);
        r = HalfFloat(h[0]);
        g = HalfFloat(h[1]);
        b = HalfFloat(h[2]);
      }
      ++total;
      if (dump) {
        rgb.insert(rgb.end(), {r, g, b});
      }
      const float lum = 0.2126f * r + 0.7152f * g + 0.0722f * b;
      if (std::isfinite(lum) && lum > 1e-3f) {
        lit.push_back(lum);
        sumLog += std::log(lum);
      }
    }
  }
  g_context->Unmap(g_hdrStaging, 0);
  if (dump && !rgb.empty()) {
    // Exposição do próprio quadro (média log), Reinhard e gama 2,2.
    double sl = 0.0;
    uint32_t nl = 0;
    for (std::size_t i = 0; i + 2 < rgb.size(); i += 3) {
      const float lum = 0.2126f * rgb[i] + 0.7152f * rgb[i + 1] + 0.0722f * rgb[i + 2];
      if (std::isfinite(lum) && lum > 1e-3f) {
        sl += std::log(lum);
        ++nl;
      }
    }
    const float k = nl > 0 ? 0.18f / static_cast<float>(std::exp(sl / nl)) : 1.f;
    const UINT w = (width + kStep - 1) / kStep, h = (height + kStep - 1) / kStep;
    char name[64];
    std::snprintf(name, sizeof name, "_%03d_%s.ppm", g_hdrDumpN++, g_active ? "carga" : "corrida");
    std::ofstream out(g_hdrDump + name, std::ios::binary);
    out << "P6\n" << w << " " << h << "\n255\n";
    std::string px(static_cast<size_t>(w) * h * 3, '\0');
    for (std::size_t i = 0; i < px.size() && i < rgb.size(); ++i) {
      float v = std::isfinite(rgb[i]) ? rgb[i] * k : 0.f;
      v = std::pow(v / (1.f + v), 1.f / 2.2f);
      px[i] = static_cast<char>(std::clamp(static_cast<int>(v * 255.f + 0.5f), 0, 255));
    }
    out.write(px.data(), static_cast<std::streamsize>(px.size()));
  }
  if (lit.size() < 16) { // a ilha do terreno sozinha, de longe, dá umas dezenas
    g_hdrNote = "quase tudo preto (" + std::to_string(lit.size()) + " de " + std::to_string(total) + " acesos)";
    return;
  }
  const float avg = static_cast<float>(std::exp(sumLog / static_cast<double>(lit.size())));
  const float target = std::clamp(0.18f / avg, 1e-4f, 1e4f);
  g_exposure = g_exposure > 0.0f ? g_exposure * std::sqrt(target / g_exposure) : target;
  std::sort(lit.begin(), lit.end());
  char note[192];
  std::snprintf(note, sizeof note, "acesos %.0f%%, media log %.3g, p50 %.3g, p95 %.3g, max %.3g, exposicao %.3g",
                100.0 * static_cast<double>(lit.size()) / total, avg, lit[lit.size() / 2],
                lit[lit.size() * 95 / 100], lit.back(), g_exposure);
  g_hdrNote = note;
}

void ReleaseViews() {
  if (g_hdrStaging != nullptr) {
    g_hdrStaging->Release();
    g_hdrStaging = nullptr;
  }
  g_hdrSource = nullptr;
  g_hdrPending = false;
  g_exposure = 0.0f;
  for (View &v : g_views) {
    if (v.srv != nullptr) {
      v.srv->Release();
    }
    if (v.resolved != nullptr) {
      v.resolved->Release();
    }
    v.res->Release();
  }
  g_views.clear();
  g_sceneView = nullptr;
  if (g_scene != nullptr) {
    g_scene->Release();
    g_scene = nullptr;
  }
}

// A vista que o fundo lê. Alvo com MSAA ganha uma cópia de 1 amostra, que o
// OnFrame resolve a cada quadro (o formato precisa aceitar resolve).
View *ViewFor(ID3D11Resource *res, DXGI_FORMAT format, UINT width, UINT height, UINT samples) {
  for (View &v : g_views) {
    if (v.res == res) {
      return &v;
    }
  }
  if (g_views.size() >= 16) {
    return nullptr;
  }
  ID3D11Device *device = nullptr;
  g_context->GetDevice(&device);
  ID3D11ShaderResourceView *srv = nullptr;
  ID3D11Texture2D *resolved = nullptr;
  const DXGI_FORMAT typed = ViewFormat(format);
  if (device != nullptr) {
    ID3D11Resource *source = res;
    if (samples > 1) {
      UINT support = 0;
      D3D11_TEXTURE2D_DESC desc{};
      desc.Width = width;
      desc.Height = height;
      desc.MipLevels = 1;
      desc.ArraySize = 1;
      desc.Format = typed;
      desc.SampleDesc.Count = 1;
      desc.Usage = D3D11_USAGE_DEFAULT;
      desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
      if (SUCCEEDED(device->CheckFormatSupport(typed, &support)) &&
          (support & D3D11_FORMAT_SUPPORT_MULTISAMPLE_RESOLVE) != 0 &&
          SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &resolved))) {
        source = resolved;
      } else {
        resolved = nullptr;
        source = nullptr;
        Logger::Warn("LoadView: alvo da cena com MSAA sem resolve possivel; fundo fica preto.");
      }
    }
    if (source != nullptr) {
      D3D11_SHADER_RESOURCE_VIEW_DESC desc{};
      desc.Format = typed;
      desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
      desc.Texture2D.MipLevels = 1;
      if (FAILED(device->CreateShaderResourceView(source, &desc, &srv))) {
        srv = nullptr; // fica na lista como "sem vista", para não tentar de novo
      }
    }
    device->Release();
  }
  res->AddRef();
  g_views.push_back({res, srv, resolved, typed});
  return &g_views.back();
}

// "procurar_carro=x,y,z": sonda da pose do carro na carga. Uma thread varre a memória gravável do
// processo a cada 2 s, da carga até 3 s depois da largada, atrás de três floats a até 3 m do ponto.
// Cada endereço novo vai para o log com o float seguinte, as 3 linhas de 16 bytes antes (matriz?) e
// o primeiro ponteiro para o exe até 0x800 bytes antes (vtable do objeto dono).
bool g_carScan = false;
float g_carScanXYZ[3] = {};
std::atomic<bool> g_carScanRunning{false};
std::map<uintptr_t, int> g_carHits; // endereço -> varredura em que apareceu (só a thread usa)

uint64_t g_carScanLoadTick = 0;
DWORD WINAPI CarScanThread(void *) {
  const HANDLE self = GetCurrentProcess();
  std::vector<uint8_t> buf(1 << 20);
  uint64_t endAt = 0;
  for (int i = 0; i < 40 && !g_active; ++i) {
    Sleep(50); // a thread nasce antes do g_active = true
  }
  for (int scan = 0; scan < 40; ++scan) {
    const uint64_t t0 = GetTickCount64();
    if (!g_active && endAt == 0) {
      endAt = t0 + 3000;
    }
    size_t bytes = 0, hits = 0, fresh = 0, logged = 0;
    std::map<uintptr_t, int> seen;
    MEMORY_BASIC_INFORMATION mbi{};
    for (uintptr_t a = 0x10000; a < 0x7fffffff0000ull && VirtualQuery(reinterpret_cast<void *>(a), &mbi, sizeof mbi);
         a = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize) {
      const DWORD prot = mbi.Protect & 0xff;
      if (mbi.State != MEM_COMMIT || (mbi.Protect & PAGE_GUARD) ||
          (prot != PAGE_READWRITE && prot != PAGE_EXECUTE_READWRITE)) {
        continue;
      }
      const auto base = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
      if (base >= g_exeBase && base < g_exeEnd) {
        continue;
      }
      for (size_t off = 0; off < mbi.RegionSize; off += buf.size()) {
        const size_t n = (std::min)(buf.size(), mbi.RegionSize - off);
        SIZE_T got = 0;
        if (!ReadProcessMemory(self, reinterpret_cast<void *>(base + off), buf.data(), n, &got) || got < 16) {
          continue;
        }
        bytes += got;
        const auto *f = reinterpret_cast<const float *>(buf.data());
        const size_t cnt = got / 4;
        for (size_t i = 0; i + 3 < cnt; ++i) {
          if (!(std::fabs(f[i] - g_carScanXYZ[0]) <= 3.f) || !(std::fabs(f[i + 1] - g_carScanXYZ[1]) <= 3.f) ||
              !(std::fabs(f[i + 2] - g_carScanXYZ[2]) <= 3.f)) {
            continue;
          }
          // Só pose: 3 linhas unitárias antes (matriz 4x4 ou 3x4) ou um quaternion unitário depois.
          auto unit = [&](size_t k, size_t len) {
            if (k + len > cnt) return false;
            float q = 0.f;
            for (size_t j = 0; j < len; ++j) q += f[k + j] * f[k + j];
            return q > 0.95f && q < 1.05f;
          };
          int kind = 0;
          if (i >= 12 && unit(i - 12, 3) && unit(i - 8, 3) && unit(i - 4, 3)) kind = 1;
          else if (i >= 9 && unit(i - 9, 3) && unit(i - 6, 3) && unit(i - 3, 3)) kind = 2;
          else if (unit(i + 4, 4) || unit(i + 3, 4)) kind = 3;
          if (kind == 0) continue;
          const uintptr_t addr = base + off + i * 4;
          ++hits;
          auto it = g_carHits.find(addr);
          if (it == g_carHits.end()) {
            g_carHits.emplace(addr, scan);
            ++fresh;
            if (logged < 60) {
              ++logged;
              float rows[12] = {};
              SIZE_T r = 0;
              ReadProcessMemory(self, reinterpret_cast<void *>(addr - 0x30), rows, sizeof rows, &r);
              uintptr_t owner = 0, vt = 0;
              for (uintptr_t q = (addr & ~uintptr_t{7}); q + 0x800 > addr && q >= base; q -= 8) {
                uintptr_t v = 0;
                if (ReadProcessMemory(self, reinterpret_cast<void *>(q), &v, 8, &r) && v >= g_exeBase && v < g_exeEnd) {
                  owner = q;
                  vt = v - g_exeBase;
                  break;
                }
              }
              char line[512];
              std::snprintf(line, sizeof line,
                            "LoadView[carro]: novo%d %p [%.2f %.2f %.2f | %.2f] linhas [%.2f %.2f %.2f %.2f] [%.2f %.2f "
                            "%.2f %.2f] [%.2f %.2f %.2f %.2f] depois [%.2f %.2f %.2f %.2f] dono %p+0x%llx exe+0x%llx",
                            kind, reinterpret_cast<void *>(addr), f[i], f[i + 1], f[i + 2], f[i + 3], rows[0], rows[1],
                            rows[2], rows[3], rows[4], rows[5], rows[6], rows[7], rows[8], rows[9], rows[10], rows[11],
                            i + 7 < cnt ? f[i + 4] : 0.f, i + 7 < cnt ? f[i + 5] : 0.f, i + 7 < cnt ? f[i + 6] : 0.f,
                            i + 7 < cnt ? f[i + 7] : 0.f, reinterpret_cast<void *>(owner), static_cast<unsigned long long>(owner ? addr - owner : 0),
                            static_cast<unsigned long long>(vt));
              Logger::Info(line);
            }
          }
          seen.emplace(addr, it == g_carHits.end() ? scan : it->second);
        }
      }
    }
    // Quantos dos acertos desta varredura já existiam desde cada varredura anterior.
    std::map<int, int> byAge;
    for (const auto &h : seen) {
      ++byAge[h.second];
    }
    std::string ages;
    for (const auto &a : byAge) {
      ages += " v" + std::to_string(a.first) + ":" + std::to_string(a.second);
    }
    char line[400];
    std::snprintf(line, sizeof line, "LoadView[carro]: varredura %d %s t+%.1fs %zu MB em %llu ms; %zu acertos, %zu novos;%s",
                  scan, g_active ? "carga" : "corrida", (t0 - g_carScanLoadTick) / 1000.0, bytes >> 20,
                  static_cast<unsigned long long>(GetTickCount64() - t0), hits, fresh, ages.c_str());
    Logger::Info(line);
    if (endAt != 0 && GetTickCount64() >= endAt) {
      break;
    }
    const uint64_t next = t0 + 2000;
    while (GetTickCount64() < next) {
      Sleep(50);
    }
  }
  g_carHits.clear();
  g_carScanRunning.store(false);
  return 0;
}

} // namespace

bool LoadView::Install(ID3D11DeviceContext *context) {
  if (g_context != nullptr || context == nullptr) {
    return g_context != nullptr;
  }
  const MH_STATUS init = MH_Initialize();
  if (init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED) {
    Logger::Error("LoadView: MH_Initialize falhou.");
    return false;
  }
  g_context = context;
  if (const auto exe = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)); exe != 0) {
    const auto *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(exe);
    const auto *nt = reinterpret_cast<const IMAGE_NT_HEADERS *>(exe + dos->e_lfanew);
    g_exeBase = exe;
    g_exeEnd = exe + nt->OptionalHeader.SizeOfImage;
  }
  void **vtbl = *reinterpret_cast<void ***>(context);
  int ok = 0;
  ok += HookSlot(vtbl, kDrawIndexed, reinterpret_cast<void *>(&HookDrawIndexed),
                 reinterpret_cast<void **>(&g_drawIndexed));
  ok += HookSlot(vtbl, kDraw, reinterpret_cast<void *>(&HookDraw), reinterpret_cast<void **>(&g_draw));
  ok += HookSlot(vtbl, kDrawIndexedInstanced, reinterpret_cast<void *>(&HookDrawIndexedInstanced),
                 reinterpret_cast<void **>(&g_drawIndexedInstanced));
  ok += HookSlot(vtbl, kDrawInstanced, reinterpret_cast<void *>(&HookDrawInstanced),
                 reinterpret_cast<void **>(&g_drawInstanced));
  ok += HookSlot(vtbl, kDrawIndexedInstancedIndirect, reinterpret_cast<void *>(&HookDrawIndexedIndirect),
                 reinterpret_cast<void **>(&g_drawIndexedIndirect));
  ok += HookSlot(vtbl, kDrawInstancedIndirect, reinterpret_cast<void *>(&HookDrawIndirect),
                 reinterpret_cast<void **>(&g_drawIndirect));
  ok += HookSlot(vtbl, kOMSetRenderTargets, reinterpret_cast<void *>(&HookSetRT), reinterpret_cast<void **>(&g_setRT));
  ok += HookSlot(vtbl, kOMSetRenderTargetsAndUAVs, reinterpret_cast<void *>(&HookSetRTUAV),
                 reinterpret_cast<void **>(&g_setRTUAV));
  ok += HookSlot(vtbl, kExecuteCommandList, reinterpret_cast<void *>(&HookExecute),
                 reinterpret_cast<void **>(&g_execute));
  ok += HookSlot(vtbl, kDispatch, reinterpret_cast<void *>(&HookDispatch), reinterpret_cast<void **>(&g_dispatch));
  ok += HookSlot(vtbl, kDispatchIndirect, reinterpret_cast<void *>(&HookDispatchIndirect),
                 reinterpret_cast<void **>(&g_dispatchIndirect));
  ok += HookSlot(vtbl, kClearDepthStencilView, reinterpret_cast<void *>(&HookClearDsv),
                 reinterpret_cast<void **>(&g_clearDsv));
  Logger::Info("LoadView: " + std::to_string(ok) + "/12 hooks do contexto D3D11.");
  {
    constexpr int kPsSetSrv = 8;
    if (!HookSlot(vtbl, kPsSetSrv, reinterpret_cast<void *>(&HookPsSetSrv), reinterpret_cast<void **>(&g_psSetSrv))) {
      Logger::Warn("LoadView: sem hook no PSSetShaderResources.");
    }
    constexpr int kVsSetCb = 7;
    if (!HookSlot(vtbl, kVsSetCb, reinterpret_cast<void *>(&HookVsSetCb), reinterpret_cast<void **>(&g_vsSetCb))) {
      Logger::Warn("LoadView: sem hook no VSSetConstantBuffers.");
    }
    constexpr int kMap = 14;
    constexpr int kUnmap = 15;
    if (!HookSlot(vtbl, kMap, reinterpret_cast<void *>(&HookMap), reinterpret_cast<void **>(&g_map)) ||
        !HookSlot(vtbl, kUnmap, reinterpret_cast<void *>(&HookUnmap), reinterpret_cast<void **>(&g_unmap))) {
      Logger::Warn("LoadView: sem hook no Map/Unmap.");
    }
  }
  {
    ID3D11Device *dev = nullptr;
    context->GetDevice(&dev);
    if (dev != nullptr) {
      constexpr int kCreateInputLayout = 11;
      if (!HookSlot(*reinterpret_cast<void ***>(dev), kCreateInputLayout, reinterpret_cast<void *>(&HookCreateLayout),
                    reinterpret_cast<void **>(&g_createLayout))) {
        Logger::Warn("LoadView: sem hook no CreateInputLayout.");
      }
      dev->Release();
    }
  }
  if (g_exeBase != 0) {
    auto *target = reinterpret_cast<void *>(g_exeBase + kRaceFrameRva);
    if (std::memcmp(target, kRaceFramePrologue, sizeof kRaceFramePrologue) != 0) {
      Logger::Warn("LoadView: prologo do quadro da corrida (0x1404b1990) diferente; sem hook.");
    } else if (MH_CreateHook(target, reinterpret_cast<void *>(&HookRaceFrame),
                             reinterpret_cast<void **>(&g_raceFrame)) == MH_OK) {
      if (MH_EnableHook(target) == MH_OK) {
        g_raceFrameTarget = target;
        g_targets.push_back(target);
        Logger::Info("LoadView: hook no quadro da corrida (0x1404b1990).");
      } else {
        MH_RemoveHook(target);
      }
    }
    auto *items = reinterpret_cast<void *>(g_exeBase + kItemListRva);
    if (std::memcmp(items, kItemListPrologue, sizeof kItemListPrologue) == 0 &&
        MH_CreateHook(items, reinterpret_cast<void *>(&HookItemList), reinterpret_cast<void **>(&g_itemList)) ==
            MH_OK) {
      if (MH_EnableHook(items) == MH_OK) {
        g_targets.push_back(items);
        Logger::Info("LoadView: hook nas listas de itens (0x140bbf4a0).");
      } else {
        MH_RemoveHook(items);
      }
    }
    auto *cbBind = reinterpret_cast<void *>(g_exeBase + kCbBindRva);
    if (std::memcmp(cbBind, kCbBindPrologue, sizeof kCbBindPrologue) == 0 &&
        MH_CreateHook(cbBind, reinterpret_cast<void *>(&HookCbBind), reinterpret_cast<void **>(&g_cbBind)) == MH_OK) {
      if (MH_EnableHook(cbBind) == MH_OK) {
        g_targets.push_back(cbBind);
        Logger::Info("LoadView: hook na ligação dos CBs (0x14090aa20).");
      } else {
        MH_RemoveHook(cbBind);
      }
    } else {
      Logger::Warn("LoadView: prólogo de 0x14090aa20 diferente; sem hook.");
    }
    auto *cbUpload = reinterpret_cast<void *>(g_exeBase + kCbUploadRva);
    if (std::memcmp(cbUpload, kCbUploadPrologue, sizeof kCbUploadPrologue) == 0 &&
        MH_CreateHook(cbUpload, reinterpret_cast<void *>(&HookCbUpload), reinterpret_cast<void **>(&g_cbUpload)) ==
            MH_OK) {
      if (MH_EnableHook(cbUpload) == MH_OK) {
        g_targets.push_back(cbUpload);
        Logger::Info("LoadView: hook no envio dos CBs (0x1408cf220).");
      } else {
        MH_RemoveHook(cbUpload);
      }
    } else {
      Logger::Warn("LoadView: prólogo de 0x1408cf220 diferente; sem hook.");
    }
    auto *viewRender = reinterpret_cast<void *>(g_exeBase + kViewRenderRva);
    if (std::memcmp(viewRender, kViewRenderPrologue, sizeof kViewRenderPrologue) == 0 &&
        MH_CreateHook(viewRender, reinterpret_cast<void *>(&HookViewRender),
                      reinterpret_cast<void **>(&g_viewRender)) == MH_OK) {
      if (MH_EnableHook(viewRender) == MH_OK) {
        g_targets.push_back(viewRender);
        Logger::Info("LoadView: hook no render da vista (0x1409d6ee0).");
      } else {
        MH_RemoveHook(viewRender);
      }
    } else {
      Logger::Warn("LoadView: prólogo de 0x1409d6ee0 diferente; sem hook.");
    }
    auto *renderMode = reinterpret_cast<void *>(g_exeBase + kRenderModeRva);
    if (std::memcmp(renderMode, kRenderModePrologue, sizeof kRenderModePrologue) == 0 &&
        MH_CreateHook(renderMode, reinterpret_cast<void *>(&HookRenderMode),
                      reinterpret_cast<void **>(&g_renderMode)) == MH_OK) {
      if (MH_EnableHook(renderMode) == MH_OK) {
        g_targets.push_back(renderMode);
        Logger::Info("LoadView: hook na troca do modo de render (0x1404a1580).");
      } else {
        MH_RemoveHook(renderMode);
      }
    } else {
      Logger::Warn("LoadView: prólogo de 0x1404a1580 diferente; sem hook.");
    }
    auto *shadowSet = reinterpret_cast<void *>(g_exeBase + kShadowSetRva);
    if (std::memcmp(shadowSet, kShadowSetBytes, sizeof kShadowSetBytes) == 0 &&
        MH_CreateHook(shadowSet, reinterpret_cast<void *>(&HookShadowSet), reinterpret_cast<void **>(&g_shadowSet)) ==
            MH_OK) {
      if (MH_EnableHook(shadowSet) == MH_OK) {
        g_targets.push_back(shadowSet);
        Logger::Info("LoadView: hook no liga/desliga das cascatas (0x1403c3f10).");
      } else {
        MH_RemoveHook(shadowSet);
      }
    } else {
      Logger::Warn("LoadView: bytes de 0x1403c3f10 diferentes; sem hook.");
    }
    auto *iblSelect = reinterpret_cast<void *>(g_exeBase + kIblSelectRva);
    if (std::memcmp(iblSelect, kIblSelectPrologue, sizeof kIblSelectPrologue) == 0 &&
        MH_CreateHook(iblSelect, reinterpret_cast<void *>(&HookIblSelect), reinterpret_cast<void **>(&g_iblSelect)) ==
            MH_OK) {
      if (MH_EnableHook(iblSelect) == MH_OK) {
        g_targets.push_back(iblSelect);
        Logger::Info("LoadView: hook na escolha das sondas IBL (0x1404b4690).");
      } else {
        MH_RemoveHook(iblSelect);
      }
    } else {
      Logger::Warn("LoadView: prólogo de 0x1404b4690 diferente; sem hook.");
    }
    auto *camParams = reinterpret_cast<void *>(g_exeBase + kCamParamsRva);
    if (std::memcmp(camParams, kCamParamsPrologue, sizeof kCamParamsPrologue) == 0 &&
        MH_CreateHook(camParams, reinterpret_cast<void *>(&HookCamParams),
                      reinterpret_cast<void **>(&g_camParams)) == MH_OK) {
      if (MH_EnableHook(camParams) == MH_OK) {
        g_targets.push_back(camParams);
        Logger::Info("LoadView: hook no CB 3 da câmera (0x1408cc320).");
      } else {
        MH_RemoveHook(camParams);
      }
    } else {
      Logger::Warn("LoadView: prólogo de 0x1408cc320 diferente; sem hook.");
    }
    auto *sceneLights = reinterpret_cast<void *>(g_exeBase + kSceneLightsRva);
    if (std::memcmp(sceneLights, kSceneLightsPrologue, sizeof kSceneLightsPrologue) == 0 &&
        MH_CreateHook(sceneLights, reinterpret_cast<void *>(&HookSceneLights),
                      reinterpret_cast<void **>(&g_sceneLights)) == MH_OK) {
      if (MH_EnableHook(sceneLights) == MH_OK) {
        g_targets.push_back(sceneLights);
        Logger::Info("LoadView: hook nas luzes da cena (0x1406c4310).");
      } else {
        MH_RemoveHook(sceneLights);
      }
    } else {
      Logger::Warn("LoadView: prólogo de 0x1406c4310 diferente; sem hook.");
    }
    auto *probeLightAdd = reinterpret_cast<void *>(g_exeBase + kProbeLightAddRva);
    if (std::memcmp(probeLightAdd, kProbeLightAddPrologue, sizeof kProbeLightAddPrologue) == 0 &&
        MH_CreateHook(probeLightAdd, reinterpret_cast<void *>(&HookProbeLightAdd),
                      reinterpret_cast<void **>(&g_probeLightAdd)) == MH_OK) {
      if (MH_EnableHook(probeLightAdd) == MH_OK) {
        g_targets.push_back(probeLightAdd);
        Logger::Info("LoadView: hook na entrada das luzes de sonda (0x1406a1320).");
      } else {
        MH_RemoveHook(probeLightAdd);
      }
    } else {
      Logger::Warn("LoadView: prólogo de 0x1406a1320 diferente; sem hook.");
    }
    auto *lightQuery = reinterpret_cast<void *>(g_exeBase + kLightQueryRva);
    if (std::memcmp(lightQuery, kLightQueryPrologue, sizeof kLightQueryPrologue) == 0 &&
        MH_CreateHook(lightQuery, reinterpret_cast<void *>(&HookLightQuery),
                      reinterpret_cast<void **>(&g_lightQuery)) == MH_OK) {
      if (MH_EnableHook(lightQuery) == MH_OK) {
        g_targets.push_back(lightQuery);
        Logger::Info("LoadView: hook na consulta de luzes (0x1406a3f60).");
      } else {
        MH_RemoveHook(lightQuery);
      }
    } else {
      Logger::Warn("LoadView: prólogo de 0x1406a3f60 diferente; sem hook.");
    }
    auto *sceneRender = reinterpret_cast<void *>(g_exeBase + kSceneRenderRva);
    if (std::memcmp(sceneRender, kSceneRenderPrologue, sizeof kSceneRenderPrologue) == 0 &&
        MH_CreateHook(sceneRender, reinterpret_cast<void *>(&HookSceneRender),
                      reinterpret_cast<void **>(&g_sceneRender)) == MH_OK) {
      if (MH_EnableHook(sceneRender) == MH_OK) {
        g_targets.push_back(sceneRender);
        Logger::Info("LoadView: hook no render da cena (0x1403c9170).");
      } else {
        MH_RemoveHook(sceneRender);
      }
    } else {
      Logger::Warn("LoadView: prólogo de 0x1403c9170 diferente; sem hook.");
    }
    auto *shadowSetup = reinterpret_cast<void *>(g_exeBase + kShadowSetupRva);
    if (std::memcmp(shadowSetup, kShadowSetupPrologue, sizeof kShadowSetupPrologue) == 0 &&
        MH_CreateHook(shadowSetup, reinterpret_cast<void *>(&HookShadowSetup),
                      reinterpret_cast<void **>(&g_shadowSetup)) == MH_OK) {
      if (MH_EnableHook(shadowSetup) == MH_OK) {
        g_targets.push_back(shadowSetup);
        Logger::Info("LoadView: hook no preparo das cascatas (0x1403c62d0).");
      } else {
        MH_RemoveHook(shadowSetup);
      }
    } else {
      Logger::Warn("LoadView: prólogo de 0x1403c62d0 diferente; sem hook.");
    }
    auto *cbFill = reinterpret_cast<void *>(g_exeBase + kCbFillRva);
    if (std::memcmp(cbFill, kCbFillPrologue, sizeof kCbFillPrologue) == 0 &&
        MH_CreateHook(cbFill, reinterpret_cast<void *>(&HookCbFill), reinterpret_cast<void **>(&g_cbFill)) == MH_OK) {
      if (MH_EnableHook(cbFill) == MH_OK) {
        g_targets.push_back(cbFill);
        Logger::Info("LoadView: hook no preenchimento dos CBs (0x14090acf0).");
      } else {
        MH_RemoveHook(cbFill);
      }
    }
    auto *worldUpd = reinterpret_cast<void *>(g_exeBase + kWorldUpdateRva);
    if (std::memcmp(worldUpd, kWorldUpdatePrologue, sizeof kWorldUpdatePrologue) == 0 &&
        MH_CreateHook(worldUpd, reinterpret_cast<void *>(&HookWorldUpdate),
                      reinterpret_cast<void **>(&g_worldUpdate)) == MH_OK) {
      if (MH_EnableHook(worldUpd) == MH_OK) {
        g_targets.push_back(worldUpd);
        Logger::Info("LoadView: hook na atualizacao do mundo (0x1406c2de0).");
      } else {
        MH_RemoveHook(worldUpd);
      }
    }
    auto *prep = reinterpret_cast<void *>(g_exeBase + kFramePrepRva);
    if (std::memcmp(prep, kFramePrepPrologue, sizeof kFramePrepPrologue) == 0 &&
        MH_CreateHook(prep, reinterpret_cast<void *>(&HookFramePrep), reinterpret_cast<void **>(&g_framePrep)) ==
            MH_OK) {
      if (MH_EnableHook(prep) == MH_OK) {
        g_targets.push_back(prep);
        Logger::Info("LoadView: hook no preparo do quadro (0x1404aabd0).");
      } else {
        MH_RemoveHook(prep);
      }
    }
    auto *cellJob = reinterpret_cast<void *>(g_exeBase + kCellJobRva);
    if (std::memcmp(cellJob, kCellJobPrologue, sizeof kCellJobPrologue) == 0 &&
        MH_CreateHook(cellJob, reinterpret_cast<void *>(&HookCellJob), reinterpret_cast<void **>(&g_cellJob)) ==
            MH_OK) {
      if (MH_EnableHook(cellJob) == MH_OK) {
        g_targets.push_back(cellJob);
        Logger::Info("LoadView: hook no job das células do terreno (0x140a7f700).");
      } else {
        MH_RemoveHook(cellJob);
      }
    }
    auto *director = reinterpret_cast<void *>(g_exeBase + kDirectorRva);
    if (std::memcmp(director, kDirectorPrologue, sizeof kDirectorPrologue) == 0 &&
        MH_CreateHook(director, reinterpret_cast<void *>(&HookDirector), reinterpret_cast<void **>(&g_director)) ==
            MH_OK) {
      if (MH_EnableHook(director) == MH_OK) {
        g_targets.push_back(director);
        Logger::Info("LoadView: hook no diretor de camera (0x1409d9b80).");
      } else {
        MH_RemoveHook(director);
      }
    }
    auto *carVis = reinterpret_cast<void *>(g_exeBase + kCarVisRva);
    if (std::memcmp(carVis, kCarVisPrologue, sizeof kCarVisPrologue) == 0 &&
        MH_CreateHook(carVis, reinterpret_cast<void *>(&HookCarVis), reinterpret_cast<void **>(&g_carVis)) == MH_OK) {
      if (MH_EnableHook(carVis) == MH_OK) {
        g_targets.push_back(carVis);
        Logger::Info("LoadView: hook na atualizacao visual do carro (0x1406ee9e0).");
      } else {
        MH_RemoveHook(carVis);
      }
    }
    struct TraceHook {
      uintptr_t rva;
      const uint8_t *prologue;
      std::size_t size;
      void *detour;
      void **original;
      const char *name;
    };
    const TraceHook traceHooks[] = {
        {kFrameResetRva, kFrameResetPrologue, sizeof kFrameResetPrologue, reinterpret_cast<void *>(&HookFrameReset),
         reinterpret_cast<void **>(&g_frameReset), "fim do quadro (0x140497fe0)"},
        {kGpuPrepRva, kGpuPrepPrologue, sizeof kGpuPrepPrologue, reinterpret_cast<void *>(&HookGpuPrep),
         reinterpret_cast<void **>(&g_gpuPrep), "preparo de GPU (0x140b920c0)"},
        {kSetBuildRva, kSetBuildPrologue, sizeof kSetBuildPrologue, reinterpret_cast<void *>(&HookSetBuild),
         reinterpret_cast<void **>(&g_setBuild), "montagem de conjunto de buffers (0x140a9eff0)"},
        {kSetFreeRva, kSetFreePrologue, sizeof kSetFreePrologue, reinterpret_cast<void *>(&HookSetFree),
         reinterpret_cast<void **>(&g_setFree), "destruicao de conjunto de buffers (0x140a92a60)"},
        {0xba88d0, kSysPrepAllPrologue, sizeof kSysPrepAllPrologue, reinterpret_cast<void *>(&HookSysPrepAll),
         reinterpret_cast<void **>(&g_sysPrepAll), "preparacao dos sistemas (0x140ba88d0)"},
        {0xbbeda0, kSysActAllPrologue, sizeof kSysActAllPrologue, reinterpret_cast<void *>(&HookSysActAll),
         reinterpret_cast<void **>(&g_sysActAll), "ativacao dos sistemas (0x140bbeda0)"},
        {0xba8890, kSysPrepOnePrologue, sizeof kSysPrepOnePrologue, reinterpret_cast<void *>(&HookSysPrepOne),
         reinterpret_cast<void **>(&g_sysPrepOne), "preparacao de um sistema (0x140ba8890)"},
        {0xbbed60, kSysActOnePrologue, sizeof kSysActOnePrologue, reinterpret_cast<void *>(&HookSysActOne),
         reinterpret_cast<void **>(&g_sysActOne), "ativacao de um sistema (0x140bbed60)"},
        {0x479860, kSysMsgPrologue, sizeof kSysMsgPrologue, reinterpret_cast<void *>(&HookSysMsg),
         reinterpret_cast<void **>(&g_sysMsg), "mensagem de ativacao (0x140479860)"},
        {0x477b00, kLoadStepPrologue, sizeof kLoadStepPrologue, reinterpret_cast<void *>(&HookLoadStep),
         reinterpret_cast<void **>(&g_loadStep), "fim da carga dos sistemas (0x140477b00)"},
        {0xc1e350, kVisFramePrologue, sizeof kVisFramePrologue, reinterpret_cast<void *>(&HookVisFrame),
         reinterpret_cast<void **>(&g_visFrame), "quadro da visibilidade (0x140c1e350)"},
        {0xc15020, kVisBuildPrologue, sizeof kVisBuildPrologue, reinterpret_cast<void *>(&HookVisBuild),
         reinterpret_cast<void **>(&g_visBuild), "montagem da visibilidade (0x140c15020)"},
    };
    for (const TraceHook &h : traceHooks) {
      auto *fn = reinterpret_cast<void *>(g_exeBase + h.rva);
      if (std::memcmp(fn, h.prologue, h.size) == 0 && MH_CreateHook(fn, h.detour, h.original) == MH_OK) {
        if (MH_EnableHook(fn) == MH_OK) {
          g_targets.push_back(fn);
          Logger::Info(std::string("LoadView: hook na ") + h.name + ".");
        } else {
          MH_RemoveHook(fn);
        }
      }
    }
    auto *ringUpload = reinterpret_cast<void *>(g_exeBase + kRingUploadRva);
    if (std::memcmp(ringUpload, kRingUploadPrologue, sizeof kRingUploadPrologue) == 0 &&
        MH_CreateHook(ringUpload, reinterpret_cast<void *>(&HookRingUpload),
                      reinterpret_cast<void **>(&g_ringUpload)) == MH_OK) {
      if (MH_EnableHook(ringUpload) == MH_OK) {
        g_targets.push_back(ringUpload);
        Logger::Info("LoadView: hook no envio do anel de instancias (0x140ab20e0).");
      } else {
        MH_RemoveHook(ringUpload);
      }
    }
    auto *objCull = reinterpret_cast<void *>(g_exeBase + kObjCullRva);
    if (std::memcmp(objCull, kObjCullPrologue, sizeof kObjCullPrologue) == 0 &&
        MH_CreateHook(objCull, reinterpret_cast<void *>(&HookObjCull), reinterpret_cast<void **>(&g_objCull)) ==
            MH_OK) {
      if (MH_EnableHook(objCull) == MH_OK) {
        g_targets.push_back(objCull);
        Logger::Info("LoadView: hook no cull dos objetos (0x14039ccb0).");
      } else {
        MH_RemoveHook(objCull);
      }
    }
    auto *push = reinterpret_cast<void *>(g_exeBase + kPushRva);
    if (std::memcmp(push, kPushPrologue, sizeof kPushPrologue) == 0 &&
        MH_CreateHook(push, reinterpret_cast<void *>(&HookPush), reinterpret_cast<void **>(&g_push)) == MH_OK) {
      if (MH_EnableHook(push) == MH_OK) {
        g_targets.push_back(push);
        Logger::Info("LoadView: hook no push das listas da cena (0x1406a1a70).");
      } else {
        MH_RemoveHook(push);
      }
    }
    auto *pass = reinterpret_cast<void *>(g_exeBase + kWorldPassRva);
    if (std::memcmp(pass, kWorldPassPrologue, sizeof kWorldPassPrologue) == 0 &&
        MH_CreateHook(pass, reinterpret_cast<void *>(&HookWorldPass), reinterpret_cast<void **>(&g_worldPass)) ==
            MH_OK) {
      if (MH_EnableHook(pass) == MH_OK) {
        g_targets.push_back(pass);
        Logger::Info("LoadView: hook no passe do mundo (0x1403c9170).");
      } else {
        MH_RemoveHook(pass);
      }
    }
  }
  return ok > 0;
}

void LoadView::Shutdown() {
  g_measure.store(false);
  for (void *fn : g_targets) {
    MH_DisableHook(fn);
  }
  if (g_allocFreeTarget != nullptr) {
    MH_DisableHook(g_allocFreeTarget);
  }
  for (int waited = 0; g_inFlight.load() > 0 && waited < 2000; ++waited) {
    Sleep(1);
  }
  for (void *fn : g_targets) {
    MH_RemoveHook(fn);
  }
  g_targets.clear();
  if (g_allocFreeTarget != nullptr) {
    MH_RemoveHook(g_allocFreeTarget);
    g_allocFreeTarget = nullptr;
    g_allocFree = nullptr;
  }
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    for (int i = 0; i < g_frameCount; ++i) {
      g_frame[i].res->Release();
      g_frame[i].res = nullptr;
    }
    g_frameCount = 0;
  }
  ReleaseViews();
  g_context = nullptr;
  g_active = false;
}

void LoadView::SetActive(bool on) {
  if (on && !g_active) {
    g_secStart = GetTickCount64();
    g_secFrames = 0;
    g_secDraws = g_secDeferred = g_secLists = 0;
    g_tally.clear();
    g_sceneLogged = false;
    g_lastState = RenderState{};
    g_streak = 0;
    g_lastCandidate = nullptr;
    g_forceAfterMs = -1;
    g_forcePos = false;
    g_forceLook = false;
    g_lookFov = 0.f;
    g_mode2Since = 0;
    g_forcedFrames = 0;
    g_forceVis = false;
    g_keepSceneMode = false;
    g_watchCar = false;
    g_carInLoad = false;
    g_traceBuffers = false;
    g_traceSystems = false;
    g_earlyWorld = false;
    g_earlyAll = false;
    g_earlyVis = false;
    g_allCells = false;
    g_watchVis = false;
    g_watchVisArmed = false;
    g_visWhy = -1;
    g_earlyVisBuilt = false;
    g_forcedVis = 0;
    g_prepWhy = ~0ull;
    g_sceneCamLogged = false;
    g_worldUpdState = -2;
    g_cbDir.clear();
    g_hdrDump.clear();
    g_skipBigVs = false;
    g_skipStride = 0;
    g_skipStrideLoad = 0;
    g_cbStride = 0;
    g_cbStride2 = 0;
    g_fixCb7 = false;
    g_snapGround = false;
    g_watchCb0 = false;
    g_forceCb0 = false;
    g_zWatch = false;
    g_watchVb = 0;
    g_zEqualFunc = 0;
    g_watchPrebake = false;
    g_forcePrebake = false;
    g_bindCb4 = false;
    g_zAlways = false;
    g_forceShadow = false;
    g_iblEye = false;
    g_leftEyeSet = false;
    g_watchCb3 = false;
    g_allZones = false;
    g_watchLights = false;
    g_forceLights = false;
    g_watchTex = false;
    g_nullSlots = 0;
    g_nullStride = 0;
    g_nullInLoad = false;
    g_swapCb = 0;
    g_probePs = false;
    g_lightObjects = false;
    g_forceRenderMode = -1;
    g_carScan = false;
    g_lookAuto = false;
    {
      std::lock_guard<std::mutex> lock(g_routeMutex);
      g_routeKey.clear();
      g_routeSaved = false;
    }
    std::ifstream ini("dr2hook_loadview.ini");
    for (std::string l; std::getline(ini, l);) {
      if (l.rfind("posicao=", 0) == 0 &&
          std::sscanf(l.c_str() + 8, "%f,%f,%f", &g_forcePosXYZ[0], &g_forcePosXYZ[1], &g_forcePosXYZ[2]) == 3) {
        g_forcePos = true;
        Logger::Info("LoadView: experimento, referencia em " + l.substr(8) + ".");
      }
      if (l.rfind("fov=", 0) == 0) {
        g_lookFov = std::strtof(l.c_str() + 4, nullptr);
      }
      if (l.rfind("olhar=auto", 0) == 0) {
        g_lookAuto = true;
        Logger::Info("LoadView: experimento, olhar automatico pela rota (dr2hook_olhar.ini).");
      }
      if (l.rfind("olhar=", 0) == 0 &&
          std::sscanf(l.c_str() + 6, "%f,%f,%f,%f,%f,%f", &g_lookEye[0], &g_lookEye[1], &g_lookEye[2],
                      &g_lookTarget[0], &g_lookTarget[1], &g_lookTarget[2]) == 6) {
        g_forceLook = true;
        Logger::Info("LoadView: experimento, camera da especial em " + l.substr(6) + ".");
      }
      if (l.rfind("forcar_vis=1", 0) == 0) {
        g_forceVis = true;
        Logger::Info("LoadView: experimento, preparo do quadro forcado como modo 0.");
      }
      if (l.rfind("despejar_hdr=", 0) == 0) {
        g_hdrDump = l.substr(13);
        g_hdrDumpN = 0;
        Logger::Info("LoadView: experimento, HDR da cena em " + g_hdrDump + "_NNN.ppm.");
      }
      if (l.rfind("pular_stride_carga=", 0) == 0) {
        g_skipStrideLoad = static_cast<uint32_t>(std::strtoul(l.c_str() + 19, nullptr, 10));
        Logger::Info("LoadView: experimento, carga sem os draws grandes de stride " + std::to_string(g_skipStrideLoad) + ".");
      }
      if (l.rfind("pular_stride=", 0) == 0) {
        g_skipStride = static_cast<uint32_t>(std::strtoul(l.c_str() + 13, nullptr, 10));
        Logger::Info("LoadView: experimento, corrida sem os draws de stride " + std::to_string(g_skipStride) + ".");
      }
      if (l.rfind("sonda_stride2=", 0) == 0) {
        g_cbStride2 = static_cast<uint32_t>(std::strtoul(l.c_str() + 14, nullptr, 10));
      }
      if (l.rfind("sonda_stride=", 0) == 0) {
        g_cbStride = static_cast<uint32_t>(std::strtoul(l.c_str() + 13, nullptr, 10));
        Logger::Info("LoadView: experimento, sonda_cb no draw de stride " + std::to_string(g_cbStride) + ".");
      }
      if (l.rfind("vigiar_cb0=1", 0) == 0) {
        g_watchCb0 = true;
        Logger::Info("LoadView: experimento, escritas no CB 0 do VS do draw de sonda_stride.");
      }
      if (l.rfind("luz_objetos=1", 0) == 0) {
        g_lightObjects = true;
        Logger::Info("LoadView: experimento, lista de objetos com luz na carga (bytes 19924/19927 zerados).");
      }
      if (l.rfind("sonda_ps=1", 0) == 0) {
        g_probePs = true;
        Logger::Info("LoadView: experimento, sonda_cb lê os CBs do PS dos objetos (stride 24 e 64).");
      }
      if (l.rfind("trocar_cb=", 0) == 0) {
        g_swapCb = static_cast<uint32_t>(std::strtoul(l.c_str() + 10, nullptr, 0));
        Logger::Info("LoadView: experimento, CBs da carga nos objetos da corrida: " + l.substr(10));
      }
      if (l.rfind("zerar_na_carga=1", 0) == 0) {
        g_nullInLoad = true;
      }
      if (l.rfind("zerar_stride=", 0) == 0) {
        g_nullStride = static_cast<UINT>(std::atoi(l.c_str() + 13));
      }
      if (l.rfind("zerar_slots=", 0) == 0) {
        g_nullSlots = static_cast<uint32_t>(std::strtoul(l.c_str() + 12, nullptr, 0));
        Logger::Info("LoadView: experimento, slots do PS soltos nos objetos da corrida: " + l.substr(12));
      }
      if (l.rfind("vigiar_tex=1", 0) == 0) {
        g_watchTex = true;
        Logger::Info("LoadView: vigiando texturas minúsculas nos draws da cena.");
      }
      if (l.rfind("todas_zonas=1", 0) == 0) {
        g_allZones = true;
        Logger::Info("LoadView: luzes de sonda da carga consultadas em todas as células do terreno.");
      }
      if (l.rfind("vigiar_luzes=1", 0) == 0) {
        g_watchLights = true;
        Logger::Info("LoadView: vigiando a atualização das luzes da cena por fase.");
      }
      if (l.rfind("forcar_luzes=1", 0) == 0) {
        g_forceLights = true;
        Logger::Info("LoadView: experimento, luzes da cena marcadas sujas a cada quadro da carga.");
      }
      if (l.rfind("vigiar_cb3=1", 0) == 0) {
        g_watchCb3 = true;
        Logger::Info("LoadView: vigiando o leftEye do CB 3 (quem monta e o valor).");
      }
      if (l.rfind("luz_cb3=", 0) == 0) {
        g_leftEyeSet = true;
        g_leftEye = std::strtof(l.c_str() + 8, nullptr);
        Logger::Info("LoadView: leftEye do CB 3 na carga = " + l.substr(8));
      }
      if (l.rfind("luz_olho=1", 0) == 0) {
        g_iblEye = true;
        Logger::Info("LoadView: experimento, sondas IBL escolhidas pelo olho da pose na carga.");
      }
      if (l.rfind("forcar_sombra=1", 0) == 0) {
        g_forceShadow = true;
        Logger::Info("LoadView: experimento, bloco de sombra/luz da vista ligado na carga.");
      }
      if (l.rfind("modo_render=", 0) == 0) {
        g_forceRenderMode = std::atoi(l.c_str() + 12);
        Logger::Info("LoadView: experimento, modo de render " + std::to_string(g_forceRenderMode) +
                     " no lugar do 2 na carga.");
      }
      if (l.rfind("z_sempre=1", 0) == 0) {
        g_zAlways = true;
        Logger::Info("LoadView: experimento, chão da carga sem teste de profundidade.");
      }
      if (l.rfind("ligar_cb4=1", 0) == 0) {
        g_bindCb4 = true;
        Logger::Info("LoadView: experimento, CB 4 do VS ligado nos draws do chão da carga.");
      }
      if (l.rfind("vigiar_vb=", 0) == 0) {
        g_watchVb = static_cast<UINT>(std::strtoul(l.c_str() + 10, nullptr, 10));
      }
      if (l.rfind("vigiar_z=1", 0) == 0) {
        g_zWatch = true;
      }
      if (l.rfind("z_igual=", 0) == 0) {
        g_zEqualFunc = static_cast<UINT>(std::strtoul(l.c_str() + 8, nullptr, 0));
      }
      if (l.rfind("vigiar_prebake=1", 0) == 0) {
        g_watchPrebake = true;
      }
      if (l.rfind("forcar_prebake=", 0) == 0) {
        g_forcePrebake = true;
        g_prebakeValue = std::strtof(l.c_str() + 15, nullptr);
      }
      if (l.rfind("forcar_cb0=1", 0) == 0) {
        g_forceCb0 = true;
        Logger::Info("LoadView: experimento, reenvio forçado do CB 0 do chão na carga.");
      }
      if (l.rfind("foto_chao=1", 0) == 0) {
        g_snapGround = true;
        g_snapN = 0;
        Logger::Info("LoadView: experimento, foto do alvo antes/depois do draw grande de sonda_stride.");
      }
      if (l.rfind("zerar_cb7=1", 0) == 0) {
        g_fixCb7 = true;
        Logger::Info("LoadView: experimento, CB 7 do VS zerado na carga.");
      }
      if (l.rfind("pular_vs=1", 0) == 0) {
        g_skipBigVs = true;
        Logger::Info("LoadView: experimento, corrida sem o VS do 1o draw grande da carga.");
      }
      if (l.rfind("rastrear_geo=1", 0) == 0) {
        g_traceGeo = true;
        Logger::Info("LoadView: experimento, estado do pipeline nos draws da cena.");
      }
      if (l.rfind("rastrear_buffers=1", 0) == 0) {
        g_traceBuffers = true;
        Logger::Info("LoadView: experimento, rastreio do conjunto de buffers de instancia.");
      }
      if (l.rfind("rastrear_sistemas=1", 0) == 0) {
        g_traceSystems = true;
        g_sysEpoch = static_cast<long long>(GetTickCount64());
        Logger::Info("LoadView: experimento, rastreio da ativacao dos sistemas.");
      }
      if (l.rfind("vigiar_vis=1", 0) == 0) {
        g_watchVis = true;
        Logger::Info("LoadView: experimento, watchpoint nos dados do track.vis.");
      }
      if (l.rfind("adiantar_tudo=1", 0) == 0) {
        g_earlyAll = true;
        l = "adiantar_mundo=1";
        Logger::Info("LoadView: experimento, cada sistema preparado e ativado assim que carrega.");
      }
      if (l.rfind("adiantar_vis=1", 0) == 0) {
        g_earlyVis = true;
        Logger::Info("LoadView: experimento, celulas da visibilidade montadas cedo.");
      }
      if (l.rfind("todas_celulas=1", 0) == 0) {
        g_allCells = true;
        Logger::Info("LoadView: experimento, vista da carga recebe todas as celulas do terreno.");
      }
      if (l.rfind("adiantar_mundo=1", 0) == 0) {
        g_earlyWorld = true;
        if (g_sysEpoch == 0) {
          g_sysEpoch = static_cast<long long>(GetTickCount64());
        }
        Logger::Info("LoadView: experimento, mundo preparado e ativado assim que o terreno carrega.");
      }
      if (l.rfind("sonda_cb=", 0) == 0) {
        g_cbDir = l.substr(9);
        while (!g_cbDir.empty() && (g_cbDir.back() == '\r' || g_cbDir.back() == ' ')) {
          g_cbDir.pop_back();
        }
        g_cbFile = 0;
        Logger::Info("LoadView: experimento, constant buffers da cena em " + g_cbDir + ".");
      }
      if (l.rfind("procurar_carro=", 0) == 0 &&
          std::sscanf(l.c_str() + 15, "%f,%f,%f", &g_carScanXYZ[0], &g_carScanXYZ[1], &g_carScanXYZ[2]) == 3) {
        g_carScan = true;
        Logger::Info("LoadView: experimento, procurando o carro perto de " + l.substr(15) + ".");
      }
      if (l.rfind("carro_na_carga=1", 0) == 0) {
        g_carInLoad = true;
        Logger::Info("LoadView: experimento, visual do carro roda nos quadros forcados da carga.");
      }
      if (l.rfind("vigiar_carro=1", 0) == 0) {
        g_watchCar = true;
        Logger::Info("LoadView: diagnostico, vigiando o objeto do carro ([[carVis+8]+0xa28]).");
      }
      if (l.rfind("manter_cena=1", 0) == 0) {
        g_keepSceneMode = true;
        Logger::Info("LoadView: experimento, modo da cena fica 0 entre os quadros forcados.");
      }
      if (l.rfind("forcar_mundo_ms=", 0) == 0) {
        g_forceAfterMs = std::atoi(l.c_str() + 16);
        Logger::Info("LoadView: experimento, carga forcada como modo 0 depois de " + std::to_string(g_forceAfterMs) +
                     " ms.");
      }
    }
  }
  if (on) {
    StartEarlyWorld();
    if (g_carScan && !g_carScanRunning.exchange(true)) {
      g_carScanLoadTick = GetTickCount64();
      if (HANDLE h = CreateThread(nullptr, 0, &CarScanThread, nullptr, 0, nullptr)) {
        CloseHandle(h);
      } else {
        g_carScanRunning.store(false);
      }
    }
  }
  if (!on && g_active) {
    g_tailUntil = GetTickCount64() + kTailMs;
  }
  g_active = on;
  if (on) {
    g_measure.store(true);
  }
}

void LoadView::NoteRoute(const char *name) {
  if (name == nullptr || *name == '\0') {
    return;
  }
  const std::string key(name);
  {
    std::lock_guard<std::mutex> lock(g_routeMutex);
    if (key == g_routeKey) {
      return;
    }
    g_routeKey = key;
    g_routeSaved = false;
  }
  Logger::Info("LoadView: rota " + key + ".");
  if (g_lookAuto && g_active) {
    LoadSavedLook(key);
  }
}

void LoadView::SetOwnDraws(bool own) { g_own.store(own, std::memory_order_relaxed); }

void LoadView::OnFrame(IDXGISwapChain *swapChain) {
  if (g_context == nullptr) {
    return;
  }
  const ULONGLONG now = GetTickCount64();
  WatchBufferSet();
  WatchWorldSystem();
  if (g_sampleFrame.load(std::memory_order_relaxed)) {
    SampleStack(nullptr, "Present"); // quem chama o Present: o laço de quadros
  }
  g_sampleFrame.store(g_measure.load() && ++g_frameNo % kSampleEvery == 0, std::memory_order_relaxed);
  ID3D11Resource *backbuffer = nullptr;
  UINT backWidth = 0;
  if (swapChain != nullptr) {
    DXGI_SWAP_CHAIN_DESC desc{};
    if (SUCCEEDED(swapChain->GetDesc(&desc))) {
      backWidth = desc.BufferDesc.Width;
    }
    swapChain->GetBuffer(0, __uuidof(ID3D11Resource), reinterpret_cast<void **>(&backbuffer));
    if (backbuffer != nullptr) {
      backbuffer->Release(); // só o ponteiro, para comparar
    }
  }

  // Fecha o quadro: soma no segundo, escolhe a cena e solta as refs.
  ID3D11Resource *scene = nullptr;
  DXGI_FORMAT sceneFormat = DXGI_FORMAT_UNKNOWN;
  float sceneAspect = 1.0f;
  UINT sceneWidth = 0, sceneHeight = 0, sceneSamples = 1;
  uint32_t sceneDraws = 0;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    uint32_t best = kSceneMinDraws - 1;
    for (int i = 0; i < g_frameCount; ++i) {
      Target &t = g_frame[i];
      const uint32_t draws = t.draws.load(std::memory_order_relaxed);
      g_secDraws += draws;
      auto it = std::find_if(g_tally.begin(), g_tally.end(), [&](const Tally &x) { return x.res == t.res; });
      if (it == g_tally.end()) {
        g_tally.push_back({t.res, t.width, t.height, t.format, t.depth, t.res == backbuffer, 0, t.samples});
        it = g_tally.end() - 1;
      }
      it->draws += draws;
      // MSAA vale: o ViewFor resolve numa cópia nossa.
      const bool candidate = !t.depth && t.res != backbuffer && (t.bind & D3D11_BIND_SHADER_RESOURCE) != 0 &&
                             t.height > 0 && backWidth > 0 && t.width * 2 >= backWidth;
      if (candidate && draws > best) {
        best = draws;
        scene = t.res;
        sceneFormat = t.format;
        sceneAspect = static_cast<float>(t.width) / static_cast<float>(t.height);
        sceneWidth = t.width;
        sceneHeight = t.height;
        sceneSamples = t.samples;
        sceneDraws = draws;
      }
    }
    if (scene != nullptr) {
      scene->AddRef();
    }
    for (int i = 0; i < g_frameCount; ++i) {
      g_frame[i].res->Release();
      g_frame[i].res = nullptr;
    }
    g_frameCount = 0;
    g_current.store(-1, std::memory_order_relaxed);
  }
  g_secDraws += g_unbound.exchange(0, std::memory_order_relaxed);
  g_secDeferred += g_deferred.exchange(0, std::memory_order_relaxed);
  g_secLists += g_lists.exchange(0, std::memory_order_relaxed);
  ++g_secFrames;

  g_streak = scene != nullptr && scene == g_lastCandidate ? g_streak + 1 : (scene != nullptr ? 1 : 0);
  g_lastCandidate = scene;
  if (scene != nullptr && g_streak < kSceneStreak) {
    scene->Release();
    scene = nullptr;
  }
  if (scene != nullptr && g_active && !g_sceneLogged) {
    g_sceneLogged = true;
    char note[160];
    std::snprintf(note, sizeof note, "LoadView: a cena 3D comecou (%u draws em %ux%u %s, %u amostras).",
                  sceneDraws, sceneWidth, sceneHeight, FormatName(sceneFormat) ? FormatName(sceneFormat) : "?",
                  sceneSamples);
    Logger::Info(note);
  }
  if (g_scene != nullptr) {
    g_scene->Release();
  }
  g_scene = scene;
  g_sceneAspect = sceneAspect;
  g_sceneView = nullptr;
  // Com despejar_hdr a corrida (os 3 s de cauda) também é medida, sem ir para o fundo.
  const bool raceDump = !g_active && !g_hdrDump.empty() && now < g_tailUntil;
  if (scene != nullptr && (g_active || raceDump)) {
    if (View *v = ViewFor(scene, sceneFormat, sceneWidth, sceneHeight, sceneSamples); v != nullptr && v->srv != nullptr) {
      if (v->resolved != nullptr) {
        g_context->ResolveSubresource(v->resolved, 0, scene, 0, v->resolveFormat);
      }
      if (g_active) {
        g_sceneView = v->srv;
      }
      if (IsHdr(v->resolveFormat)) {
        MeasureExposure(v->resolved != nullptr ? v->resolved : scene, v->resolveFormat, sceneWidth, sceneHeight);
      } else {
        g_exposure = 0.0f;
      }
    }
  }

  const bool logging = g_active || now < g_tailUntil;
  if (!g_cbDir.empty()) {
    CbFlush(g_active);
    static ULONGLONG cbAt = 0;
    const bool want = logging && now >= cbAt;
    if (want) {
      cbAt = now + 500;
      // Câmeras como estão agora (o quadro seguinte é o capturado).
      std::memset(g_cbCams, 0, sizeof g_cbCams);
      if (const uint8_t *sc = g_lastScene.load(std::memory_order_relaxed); sc != nullptr) {
        const auto *cam = *reinterpret_cast<const uint8_t *const *>(sc + 0x17c0);
        const auto *rc = *reinterpret_cast<const uint8_t *const *>(sc + 0x38);
        if (cam != nullptr) {
          std::memcpy(g_cbCams, cam, 0x200);
        }
        if (rc != nullptr) {
          std::memcpy(g_cbCams + 0x200, rc, 0x200);
        }
      }
    }
    g_cbWant.store(want, std::memory_order_relaxed);
  }
  if (now - g_secStart >= 1000) {
    if (logging && g_secFrames > 0) {
      std::sort(g_tally.begin(), g_tally.end(), [](const Tally &a, const Tally &b) { return a.draws > b.draws; });
      char head[160];
      std::snprintf(head, sizeof head, "%u quadros, %.0f draws/quadro em %zu alvos%s", g_secFrames,
                    static_cast<double>(g_secDraws) / g_secFrames, g_tally.size(), g_active ? "" : " (corrida)");
      std::string line = head;
      std::string top;
      for (std::size_t i = 0; i < g_tally.size() && i < 5; ++i) {
        top += (i ? ", " : "") + Describe(g_tally[i], g_secFrames);
      }
      if (!top.empty()) {
        line += ": " + top;
      }
      if (g_secDeferred > 0 || g_secLists > 0) {
        char extra[96];
        std::snprintf(extra, sizeof extra, "; adiados %llu draws, %llu listas",
                      static_cast<unsigned long long>(g_secDeferred), static_cast<unsigned long long>(g_secLists));
        line += extra;
      }
      g_status = line;
      {
        std::string lists = "LoadView[listas]:";
        std::lock_guard<std::mutex> lock(g_itemCallerMutex);
        for (auto &slot : g_itemCallers) {
          if (slot.caller == 0) {
            break;
          }
          if (slot.calls > 0) {
            char one[96];
            std::snprintf(one, sizeof one, " +%llx %.1f/%.0f/%.0f", static_cast<unsigned long long>(slot.caller),
                          static_cast<double>(slot.calls) / g_secFrames, static_cast<double>(slot.items) / g_secFrames,
                          static_cast<double>(slot.draws) / g_secFrames);
            lists += one;
            if (slot.draws > 0) {
              std::snprintf(one, sizeof one, "@%ux%u", slot.width, slot.height);
              lists += one;
            }
          }
          slot.calls = 0;
          slot.items = 0;
          slot.draws = 0;
        }
        Logger::Info(lists.c_str());
      }
      if (g_active && !g_hdrNote.empty()) {
        Logger::Info("LoadView[hdr]: " + g_hdrNote);
        g_hdrNote.clear();
      }
      {
        std::string pushes = "LoadView[push]:";
        for (std::size_t i = 0; i < kPushBuckets; ++i) {
          const uint32_t ok = g_pushOk[i].exchange(0, std::memory_order_relaxed);
          const uint32_t fail = g_pushFail[i].exchange(0, std::memory_order_relaxed);
          if (ok + fail > 0) {
            char one[64];
            std::snprintf(one, sizeof one, " +%zx %.0f/%.0f", i * 8, static_cast<double>(ok) / g_secFrames,
                          static_cast<double>(fail) / g_secFrames);
            pushes += one;
          }
        }
        char other[320];
        std::snprintf(other, sizeof other, " fora %.0f; cull de objetos %.1f/quadro (%.1f barrados), %.0f pushes, %u objetos; modo %d flag %u cena %p; carro pulado %u, buffers liberados pulados %u, resets forcados %u",
                      static_cast<double>(g_pushOther.exchange(0, std::memory_order_relaxed)) / g_secFrames,
                      static_cast<double>(g_secObjCull.exchange(0)) / g_secFrames,
                      static_cast<double>(g_secObjCullGated.exchange(0)) / g_secFrames,
                      static_cast<double>(g_secObjCullPush.exchange(0)) / g_secFrames, g_objCullCount.load(),
                      g_objCullMode.load(), g_objCullFlag.load(), g_objCullScene.load(),
                      g_secCarVisSkip.exchange(0), g_secRingSkip.exchange(0), g_secFrameReset.exchange(0));
        Logger::Info(pushes + other);
      }
      char items[320];
      std::snprintf(items, sizeof items, "; itens %.0f/quadro em %.0f listas; células %.0f/quadro em %.1f jobs (%.1f prontos); mundo %.1f/quadro; preparo %.1f/quadro; principal %.1f jobs/%.0f células (%.1f com todas)",
                    static_cast<double>(g_secItems.exchange(0)) / g_secFrames,
                    static_cast<double>(g_secItemCalls.exchange(0)) / g_secFrames,
                    static_cast<double>(g_secCells.exchange(0)) / g_secFrames,
                    static_cast<double>(g_secCellCalls.exchange(0)) / g_secFrames,
                    static_cast<double>(g_secCellReady.exchange(0)) / g_secFrames,
                    static_cast<double>(g_secWorldUpd.exchange(0)) / g_secFrames,
                    static_cast<double>(g_secPrep.exchange(0)) / g_secFrames,
                    static_cast<double>(g_secCellMain.exchange(0)) / g_secFrames,
                    static_cast<double>(g_secCellMainN.exchange(0)) / g_secFrames,
                    static_cast<double>(g_secCellFake.exchange(0)) / g_secFrames);
      Logger::Info("LoadView: " + line + items);
      if (g_traceGeo) {
        const double fr = g_secFrames;
        char geo[512];
        std::snprintf(geo, sizeof geo,
                      "LoadView[geo]: %.0f draws/quadro na cena, %.0fk vértices/quadro, %.0f grandes, %.0f indiretos | "
                      "sem dsv %.0f, prof desligada %.0f, sem escrita %.0f, stencil %.0f, funcs n%.0f l%.0f e%.0f le%.0f "
                      "g%.0f ne%.0f ge%.0f a%.0f | predicado %.0f, sem cull %.0f | vp %ux%u z %.3f-%.3f | clears prof "
                      "%.1f/quadro (%.3f, flags %u) | dispatch %.1f/quadro",
                      g_geo.draws.exchange(0) / fr, g_geo.verts.exchange(0) / fr / 1000.0, g_geo.big.exchange(0) / fr,
                      g_geo.indirect.exchange(0) / fr, g_geo.noDsv.exchange(0) / fr, g_geo.depthOff.exchange(0) / fr,
                      g_geo.noWrite.exchange(0) / fr, g_geo.stencil.exchange(0) / fr, g_geo.func[1].exchange(0) / fr,
                      g_geo.func[2].exchange(0) / fr, g_geo.func[3].exchange(0) / fr, g_geo.func[4].exchange(0) / fr,
                      g_geo.func[5].exchange(0) / fr, g_geo.func[6].exchange(0) / fr, g_geo.func[7].exchange(0) / fr,
                      g_geo.func[8].exchange(0) / fr, g_geo.predicated.exchange(0) / fr, g_geo.cullNone.exchange(0) / fr,
                      g_geo.vpW.load(), g_geo.vpH.load(), g_geo.vpMinZ.load() / 1000.0, g_geo.vpMaxZ.load() / 1000.0,
                      g_geo.clears.exchange(0) / fr, g_geo.clearDepth.load() / 1000.0, g_geo.clearFlags.load(),
                      g_geo.dispatches.exchange(0) / fr);
        Logger::Info(geo);
        g_drawLogLeft.store(3, std::memory_order_relaxed);
        g_drawLogLeft2.store(3, std::memory_order_relaxed);
        if (g_watchVb != 0) {
          std::string lb = "LoadView[vb]:";
          std::lock_guard<std::mutex> lockb(g_texMutex);
          for (const auto &[k, v] : g_vbSeen) {
            lb += " [" + k + " x" + std::to_string(v) + "]";
          }
          g_vbSeen.clear();
          Logger::Info(lb);
        }
        if (g_watchTex) {
          std::string lt = "LoadView[tex]:";
          std::lock_guard<std::mutex> lockt(g_texMutex);
          for (const auto &[k, v] : g_texSeen) {
            lt += " [" + k + " x" + std::to_string(v.first) + " =" + v.second + "]";
          }
          g_texSeen.clear();
          std::string ls = "LoadView[srv]: luz forçada " + std::to_string(g_lightForced.exchange(0)) + "; trocas " + std::to_string(g_swapDone.exchange(0)) + ";";
          for (const auto &[k, v] : g_srvCallers) {
            ls += " [" + k + " x" + std::to_string(v) + "]";
          }
          g_srvCallers.clear();
          Logger::Info(ls);
          Logger::Info(lt);
        }
        {
          std::string lv = "LoadView[vista]: sombra forçada " + std::to_string(g_shadowForced.exchange(0)) +
                           "; z igual trocado " + std::to_string(g_zEqualDraws.exchange(0)) + ";";
          std::lock_guard<std::mutex> lockv(g_viewMutex);
          for (const auto &[k, v] : g_viewGates) {
            lv += " [" + k + " x" + std::to_string(v) + "]";
          }
          g_viewGates.clear();
          Logger::Info(lv);
          if (g_watchPrebake || g_forcePrebake) {
            std::string lp = "LoadView[pre]: forçados " + std::to_string(g_prebakeForced.exchange(0)) + ";";
            for (const auto &[k, v] : g_prebakeSeen) {
              lp += " [" + k + " x" + std::to_string(v) + "]";
            }
            g_prebakeSeen.clear();
            Logger::Info(lp);
          }
        }
        if (g_watchCb0) {
          std::string w = "LoadView[cb0]: escritas/s";
          std::lock_guard<std::mutex> lock(g_cb0Mutex);
          for (const auto &[k, v] : g_cb0Callers) {
            w += " [" + k + " x" + std::to_string(v) + "]";
          }
          g_cb0Callers.clear();
          std::string w3 = "LoadView[cb3]: escritas/s";
          for (const auto &[k, v] : g_cb3Writers) {
            w3 += " [" + k + " x" + std::to_string(v) + "]";
          }
          g_cb3Writers.clear();
          Logger::Info(w3);
          g_cb0LogLeft.store(6, std::memory_order_relaxed);
          std::string pl = "LoadView[param]:";
          {
            std::lock_guard<std::mutex> plock(g_paramMutex);
            for (const auto &[k, v] : g_paramSeen) {
              const uint32_t st = (k >> 24) & 0x7f;
              if (st == 1) {
                continue;
              }
              char one[64];
              std::snprintf(one, sizeof one, " %s%u:%u", (k & 0x80000000u) ? "c" : "r", k & 0xffffff, st);
              pl += one;
              if (st == 0) {
                if (auto it = g_paramVtbl.find(k & 0x80ffffffu); it != g_paramVtbl.end()) {
                  std::snprintf(one, sizeof one, "@%x", it->second);
                  pl += one;
                }
              }
              pl += "x" + std::to_string(v);
            }
            g_paramSeen.clear();
            g_paramDetailLeft = 6;
            g_groundFillLeft.store(12);
            g_uploadLogLeft.store(6);
            pl += " | envio do chão: carga " + std::to_string(g_uploadDiff[1].exchange(0)) + " mudou/" +
                  std::to_string(g_uploadSame[1].exchange(0)) + " igual (forçados " +
                  std::to_string(g_uploadForced.exchange(0)) + "), corrida " +
                  std::to_string(g_uploadDiff[0].exchange(0)) + "/" + std::to_string(g_uploadSame[0].exchange(0));
            // g_cb0Mutex já está travado acima.
            pl += " | itens do chão " + std::to_string(g_groundItems.size()) + ", logados " +
                  std::to_string(g_groundFillSeen.size());
          }
          Logger::Info(pl);
          {
            std::string l4 = "LoadView[cb4]: forçados " + std::to_string(g_cb4Forced.exchange(0)) + ", z sempre " + std::to_string(g_zAlwaysDraws.exchange(0)) + ";";
            std::lock_guard<std::mutex> lock4(g_cb4Mutex);
            for (const auto &[k, v] : g_cb4Callers) {
              l4 += " [" + k + " x" + std::to_string(v) + "]";
            }
            std::string l5 = "LoadView[cb5]:";
            for (const auto &[k, v] : g_cb5Callers) {
              l5 += " [" + k + " x" + std::to_string(v) + "]";
            }
            g_cb5Callers.clear();
            g_cb4Callers.clear();
            Logger::Info(l4);
            Logger::Info(l5);
          }
          char bl[160];
          std::snprintf(bl, sizeof bl, "LoadView[bind]: carga %u com params, %u sem | corrida %u com, %u sem",
                        g_bindWith[1].exchange(0), g_bindWithout[1].exchange(0), g_bindWith[0].exchange(0),
                        g_bindWithout[0].exchange(0));
          Logger::Info(bl);
          Logger::Info(w);
        }
        std::string cs = "LoadView[cs]:";
        {
          std::lock_guard<std::mutex> lock(g_csMutex);
          for (const auto &[k, v] : g_csSeen) {
            char one[24];
            std::snprintf(one, sizeof one, " x%.1f", v / fr);
            cs += " [" + k + one + "]";
          }
          g_csSeen.clear();
        }
        Logger::Info(cs);
        std::vector<std::pair<void *, VsTally>> vsl(g_vsTally.begin(), g_vsTally.end());
        g_vsTally.clear();
        std::sort(vsl.begin(), vsl.end(), [](const auto &a, const auto &b) { return a.second.verts > b.second.verts; });
        std::string vline = "LoadView[vs]:";
        for (std::size_t i = 0; i < vsl.size() && i < 24; ++i) {
          char one[112];
          std::snprintf(one, sizeof one, " %p:%.0f/%.0fk/vb%u/p%u/eq%.0f/rt%ux%u", vsl[i].first, vsl[i].second.draws / fr,
                        vsl[i].second.verts / fr / 1000.0, vsl[i].second.nvb, vsl[i].second.stride0,
                        vsl[i].second.eq / fr, vsl[i].second.rtFmt, vsl[i].second.nrt);
          vline += one;
        }
        Logger::Info(vline);
      }
    }
    g_secStart = now;
    g_secFrames = 0;
    g_secDraws = g_secDeferred = g_secLists = 0;
    g_tally.clear();
  }

  if (!logging && g_measure.load()) {
    g_measure.store(false);
    g_sampleFrame.store(false);
    ReleaseViews();
    CbRelease();
    g_cbWant.store(false);
    DumpStacks();
  }
}

ID3D11ShaderResourceView *LoadView::SceneView(float *aspect) {
  if (aspect != nullptr) {
    *aspect = g_sceneAspect;
  }
  return g_sceneView;
}

float LoadView::SceneExposure() { return g_sceneView != nullptr ? g_exposure : 0.0f; }

std::string LoadView::StatusLine() { return g_status.empty() ? std::string() : "GPU: " + g_status; }

} // namespace dr2hook
