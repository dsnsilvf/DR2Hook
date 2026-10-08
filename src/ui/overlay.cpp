#include "dr2hook/ui/overlay.h"
#include "dr2hook/ghost_lab.h"
#include "dr2hook/logger.h"
#include "dr2hook/player.h"
#include "dr2hook/safety.h"
#include "dr2hook/savestate.h"
#include "dr2hook/script/lua_engine.h"
#include "dr2hook/script/mod_manager.h"

#include "lua.h"
#include "imgui.h"

#if defined(_WIN32) || defined(DR2HOOK_USE_BACKENDS)
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"
#include <d3dcompiler.h>
#endif

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

namespace {

bool g_initialized = false;
bool g_showMenu = false;
bool g_loadCover = false;
HWND g_hWnd = nullptr;
ID3D11Device *g_device = nullptr;
ID3D11DeviceContext *g_deviceContext = nullptr;
ID3D11RenderTargetView *g_renderTargetView = nullptr;

std::mutex g_notificationMutex;
std::vector<dr2hook::ToastNotification> g_notifications;

// Terminal da tela preta (inicialização rápida): o dr2hook.log ao vivo, lido
// do buffer da proxy, com a fase, o tempo e os arquivos abertos.
struct TermLine {
  std::string time;
  std::string text;
  ImU32 color;
  int repeats = 1; // a mesma linha seguida (o jogo reabre o mesmo arquivo)
};

struct LoadTerminal {
  unsigned long long seq = 0;
  std::deque<TermLine> lines;
  double start = -1.0;     // ImGui::GetTime() do primeiro frame
  double lastOpen = -1.0;  // quando chegou o último "LoadTrace: open"
  const char *phase = "Iniciando o jogo";
  bool track = false;      // depois do "RaceEvent: carregando"
  int opens = 0;
  int overlayOpens = 0;
  double megabytes = 0.0;
  int frames = 0;
  float maxGap = 0.0f;
};

LoadTerminal g_term;

// Cena do jogo atrás do terminal (LoadView, no core) e a linha da GPU.
ID3D11ShaderResourceView *g_coverScene = nullptr;
float g_coverAspect = 1.0f;
float g_coverExposure = 0.0f; // > 0: cena HDR, passa pela curva abaixo
std::string g_coverGpu;
ID3D11BlendState *g_opaqueBlend = nullptr;
// Foto da pista atrás do terminal (a mesma da tela de carregamento, gerada
// pelo loading_screen.py): PPM binário RGB, carregado no 1º quadro da tela
// preta. Tem prioridade sobre a cena do jogo.
std::string g_coverImagePath;
bool g_coverImageTried = false;
ID3D11ShaderResourceView *g_coverImage = nullptr;
float g_coverImageAspect = 1.0f;
#if defined(_WIN32) || defined(DR2HOOK_USE_BACKENDS)
// Cena HDR do jogo (luz linear) para a tela: exposição, curva ACES
// (aproximação do Narkowicz) e gama 2,2; params = {exposição, escurecer}.
// Mesma entrada do vertex shader do ImGui.
constexpr char kTonemapPS[] = R"(
cbuffer Tonemap : register(b0) { float4 params; };
struct PS_INPUT { float4 pos : SV_POSITION; float4 col : COLOR0; float2 uv : TEXCOORD0; };
sampler sampler0;
Texture2D texture0;
float4 main(PS_INPUT input) : SV_Target {
  float3 c = max(texture0.Sample(sampler0, input.uv).rgb, 0.0) * params.x;
  c = saturate((c * (2.51 * c + 0.03)) / (c * (2.43 * c + 0.59) + 0.14));
  return float4(pow(c, 1.0 / 2.2) * params.y, 1.0);
}
)";
ID3D11PixelShader *g_tonemapPS = nullptr;
ID3D11Buffer *g_tonemapCB = nullptr;
bool g_tonemapTried = false;

bool EnsureTonemap() {
  if (g_tonemapTried) {
    return g_tonemapPS != nullptr && g_tonemapCB != nullptr;
  }
  g_tonemapTried = true;
  ID3DBlob *blob = nullptr;
  ID3DBlob *errors = nullptr;
  if (FAILED(D3DCompile(kTonemapPS, sizeof kTonemapPS - 1, nullptr, nullptr, nullptr, "main", "ps_4_0", 0, 0,
                        &blob, &errors))) {
    Logger::Warn(std::string("Overlay: shader do tonemap do fundo nao compilou: ") +
                 (errors != nullptr ? static_cast<const char *>(errors->GetBufferPointer()) : "?"));
    if (errors != nullptr) {
      errors->Release();
    }
    return false;
  }
  if (errors != nullptr) {
    errors->Release();
  }
  if (FAILED(g_device->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &g_tonemapPS))) {
    g_tonemapPS = nullptr;
  }
  blob->Release();
  D3D11_BUFFER_DESC desc{};
  desc.ByteWidth = 16;
  desc.Usage = D3D11_USAGE_DYNAMIC;
  desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
  desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
  if (FAILED(g_device->CreateBuffer(&desc, nullptr, &g_tonemapCB))) {
    g_tonemapCB = nullptr;
  }
  if (g_tonemapPS == nullptr || g_tonemapCB == nullptr) {
    Logger::Warn("Overlay: tonemap do fundo indisponivel; cena HDR vai crua.");
    return false;
  }
  Logger::Info("Overlay: tonemap do fundo pronto.");
  return true;
}
#endif

constexpr std::size_t kTermKeep = 300;
constexpr ImU32 kTermText = IM_COL32(170, 170, 170, 255);
constexpr ImU32 kTermDim = IM_COL32(110, 110, 110, 255);
constexpr ImU32 kTermMark = IM_COL32(120, 210, 255, 255);
constexpr ImU32 kTermOverlay = IM_COL32(110, 230, 120, 255);
constexpr ImU32 kTermWarn = IM_COL32(240, 200, 80, 255);
constexpr ImU32 kTermError = IM_COL32(255, 90, 90, 255);

bool StartsWith(const std::string &s, std::size_t at, const char *prefix) {
  return s.compare(at, std::char_traits<char>::length(prefix), prefix) == 0;
}

// Caminho a partir da pasta do jogo ("locations\x.nefs").
std::string GameRelative(std::string path) {
  std::string lower = path;
  std::transform(lower.begin(), lower.end(), lower.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  const std::string root = "dirt rally 2.0\\";
  const std::size_t at = lower.find(root);
  return at == std::string::npos ? path : path.substr(at + root.size());
}

// "[08:34:51.178] [INFO] LoadTrace: open S:\...": a linha como o terminal a
// mostra, e as contas da fase.
void TermFeed(const std::string &raw, double now) {
  TermLine out{std::string(), raw, kTermText};
  const std::size_t close = raw.find("] [");
  const std::size_t levelEnd =
      close == std::string::npos ? std::string::npos : raw.find("] ", close + 3);
  if (raw.size() > 1 && raw[0] == '[' && levelEnd != std::string::npos) {
    out.time = raw.substr(4, close - 4); // sem a hora
    const std::string level = raw.substr(close + 3, levelEnd - close - 3);
    const std::string msg = raw.substr(levelEnd + 2);
    std::string shown = msg;
    if (level == "WARN") {
      out.color = kTermWarn;
    } else if (level == "ERROR") {
      out.color = kTermError;
    }
    if (StartsWith(msg, 0, "LoadTrace: open ")) {
      ++g_term.opens;
      g_term.lastOpen = now;
      const std::string rel = GameRelative(msg.substr(16));
      if (StartsWith(rel, 0, "dr2hook_overlay")) {
        ++g_term.overlayOpens;
        out.color = kTermOverlay;
      }
      shown = "abre " + rel;
    } else if (StartsWith(msg, 0, "LoadTrace: IO ")) {
      g_term.megabytes += std::atof(msg.c_str() + 14);
      out.color = kTermDim;
    } else if (StartsWith(msg, 0, "AutoStage: Fast-path")) {
      g_term.phase = "Carregando os dados do jogo";
      out.color = kTermMark;
    } else if (StartsWith(msg, 0, "RaceEvent: carregando")) {
      g_term.phase = "Carregando a pista";
      g_term.track = true;
      out.color = kTermMark;
    } else if (StartsWith(msg, 0, "RaceEvent") ||
               StartsWith(msg, 0, "AutoStage") ||
               StartsWith(msg, 0, "LoadProbe") ||
               StartsWith(msg, 0, "LoadCover")) {
      if (out.color == kTermText) {
        out.color = kTermMark;
      }
    }
    out.text = std::move(shown);
  }
  if (!g_term.lines.empty() && g_term.lines.back().text == out.text &&
      g_term.lines.back().color == out.color) {
    g_term.lines.back().time = std::move(out.time);
    ++g_term.lines.back().repeats;
    return;
  }
  g_term.lines.push_back(std::move(out));
  if (g_term.lines.size() > kTermKeep) {
    g_term.lines.pop_front();
  }
}

void ReleaseCoverImage() {
  if (g_coverImage != nullptr) {
    g_coverImage->Release();
    g_coverImage = nullptr;
  }
  g_coverImageTried = false;
}

#if defined(_WIN32) || defined(DR2HOOK_USE_BACKENDS)
// PPM "P6 <largura> <altura> 255" seguido dos pixels RGB.
bool LoadCoverImage() {
  if (g_coverImageTried || g_device == nullptr) {
    return g_coverImage != nullptr;
  }
  g_coverImageTried = true;
  if (g_coverImagePath.empty()) {
    return false;
  }
  std::ifstream in(g_coverImagePath, std::ios::binary);
  std::string magic;
  int w = 0, h = 0, maxval = 0;
  in >> magic >> w >> h >> maxval;
  in.get();
  if (!in || magic != "P6" || maxval != 255 || w <= 0 || h <= 0 || w > 8192 || h > 8192) {
    Logger::Warn("Overlay: foto da carga ilegivel: " + g_coverImagePath);
    return false;
  }
  std::vector<uint8_t> rgb(static_cast<size_t>(w) * h * 3);
  in.read(reinterpret_cast<char *>(rgb.data()), static_cast<std::streamsize>(rgb.size()));
  if (!in) {
    Logger::Warn("Overlay: foto da carga incompleta: " + g_coverImagePath);
    return false;
  }
  std::vector<uint32_t> rgba(static_cast<size_t>(w) * h);
  for (size_t i = 0; i < rgba.size(); ++i) {
    rgba[i] = rgb[i * 3] | (rgb[i * 3 + 1] << 8) | (rgb[i * 3 + 2] << 16) | 0xff000000u;
  }
  D3D11_TEXTURE2D_DESC desc{};
  desc.Width = static_cast<UINT>(w);
  desc.Height = static_cast<UINT>(h);
  desc.MipLevels = 1;
  desc.ArraySize = 1;
  desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.SampleDesc.Count = 1;
  desc.Usage = D3D11_USAGE_IMMUTABLE;
  desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
  D3D11_SUBRESOURCE_DATA data{rgba.data(), static_cast<UINT>(w) * 4, 0};
  ID3D11Texture2D *tex = nullptr;
  if (FAILED(g_device->CreateTexture2D(&desc, &data, &tex))) {
    Logger::Warn("Overlay: sem textura para a foto da carga.");
    return false;
  }
  const HRESULT hr = g_device->CreateShaderResourceView(tex, nullptr, &g_coverImage);
  tex->Release();
  if (FAILED(hr)) {
    g_coverImage = nullptr;
    return false;
  }
  g_coverImageAspect = static_cast<float>(w) / static_cast<float>(h);
  char note[96];
  std::snprintf(note, sizeof note, "Overlay: foto da carga %dx%d no fundo do terminal.", w, h);
  Logger::Info(note);
  return true;
}
#endif

// A foto cobre a tela inteira (corta o que sobra), escurecida como a cena.
bool DrawCoverImage() {
#if defined(_WIN32) || defined(DR2HOOK_USE_BACKENDS)
  if (!LoadCoverImage()) {
    return false;
  }
  const ImVec2 screen = ImGui::GetIO().DisplaySize;
  const float screenAspect = screen.x / std::max(screen.y, 1.0f);
  ImVec2 uv0(0.0f, 0.0f), uv1(1.0f, 1.0f);
  if (g_coverImageAspect > screenAspect) {
    const float keep = screenAspect / g_coverImageAspect;
    uv0.x = (1.0f - keep) * 0.5f;
    uv1.x = 1.0f - uv0.x;
  } else {
    const float keep = g_coverImageAspect / screenAspect;
    uv0.y = (1.0f - keep) * 0.5f;
    uv1.y = 1.0f - uv0.y;
  }
  ImGui::GetBackgroundDrawList()->AddImage(reinterpret_cast<ImTextureID>(g_coverImage), ImVec2(0.0f, 0.0f),
                                           screen, uv0, uv1, IM_COL32(140, 140, 140, 255));
  return true;
#else
  return false;
#endif
}

// A cena sem mistura: o alfa de um alvo do jogo não é transparência.
void DrawCoverScene() {
  if (DrawCoverImage()) {
    return;
  }
#if defined(_WIN32) || defined(DR2HOOK_USE_BACKENDS)
  if (g_coverScene == nullptr || g_device == nullptr) {
    return;
  }
  if (g_opaqueBlend == nullptr) {
    D3D11_BLEND_DESC desc{};
    desc.RenderTarget[0].BlendEnable = FALSE;
    desc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    if (FAILED(g_device->CreateBlendState(&desc, &g_opaqueBlend))) {
      g_opaqueBlend = nullptr;
      return;
    }
  }
  const ImVec2 screen = ImGui::GetIO().DisplaySize;
  float w = screen.x;
  float h = w / std::max(g_coverAspect, 0.1f);
  if (h > screen.y) {
    h = screen.y;
    w = h * g_coverAspect;
  }
  const ImVec2 a((screen.x - w) * 0.5f, (screen.y - h) * 0.5f);
  ImDrawList *draw = ImGui::GetBackgroundDrawList();
  // Escurecida para o texto do terminal ficar legível.
  constexpr float kDim = 140.0f / 255.0f;
  if (g_coverExposure > 0.0f && EnsureTonemap()) {
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (SUCCEEDED(g_deviceContext->Map(g_tonemapCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
      const float params[4] = {g_coverExposure, kDim, 0.0f, 0.0f};
      std::memcpy(mapped.pData, params, sizeof params);
      g_deviceContext->Unmap(g_tonemapCB, 0);
    }
    draw->AddCallback(
        [](const ImDrawList *, const ImDrawCmd *) {
          g_deviceContext->OMSetBlendState(g_opaqueBlend, nullptr, 0xffffffffu);
          g_deviceContext->PSSetShader(g_tonemapPS, nullptr, 0);
          g_deviceContext->PSSetConstantBuffers(0, 1, &g_tonemapCB);
        },
        nullptr);
    draw->AddImage(reinterpret_cast<ImTextureID>(g_coverScene), a,
                   ImVec2(a.x + w, a.y + h));
  } else {
    draw->AddCallback(
        [](const ImDrawList *, const ImDrawCmd *) {
          g_deviceContext->OMSetBlendState(g_opaqueBlend, nullptr, 0xffffffffu);
        },
        nullptr);
    draw->AddImage(reinterpret_cast<ImTextureID>(g_coverScene), a,
                   ImVec2(a.x + w, a.y + h), ImVec2(0, 0), ImVec2(1, 1),
                   IM_COL32(140, 140, 140, 255));
  }
  draw->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
#endif
}

void DrawLoadTerminal() {
  DrawCoverScene();
  ImGuiIO &io = ImGui::GetIO();
  const double now = ImGui::GetTime();
  if (g_term.start < 0.0) {
    g_term.start = now;
  } else {
    // Intervalo grande = o jogo não chamou Present (a carga segurou a
    // thread de render; a LoadTrace loga cada um como "frame travado").
    ++g_term.frames;
    g_term.maxGap = std::max(g_term.maxGap, io.DeltaTime);
  }

  static char buffer[65536];
  for (int i = 0; i < 8; ++i) {
    const int used =
        dr2hook::Logger::ReadSince(&g_term.seq, buffer, sizeof buffer);
    if (used <= 0) {
      break;
    }
    const char *p = buffer;
    const char *end = buffer + used;
    while (p < end) {
      const char *nl = static_cast<const char *>(std::memchr(p, '\n', end - p));
      if (nl == nullptr) {
        nl = end;
      }
      TermFeed(std::string(p, nl), now);
      p = nl + 1;
    }
    if (used < static_cast<int>(sizeof buffer) / 2) {
      break;
    }
  }

  ImDrawList *draw = ImGui::GetBackgroundDrawList();
  ImFont *font = ImGui::GetFont();
  // A fonte padrão é bitmap: só escala inteira fica nítida.
  const float scale = std::max(1.0f, std::round(io.DisplaySize.y / 1080.0f));
  const float size = ImGui::GetFontSize() * scale;
  const float lineH = size + 2.0f * scale;
  const float margin = 24.0f * scale;
  float y = margin;

  const char *phase = g_term.phase;
  char quiet[64] = "";
  if (g_term.track && g_term.lastOpen >= 0.0 && now - g_term.lastOpen > 1.5) {
    phase = "Preparando a largada";
    std::snprintf(quiet, sizeof quiet, "   (%.1f s sem abrir arquivos)",
                  now - g_term.lastOpen);
  }
  const char *spin = "|/-\\";
  char status[256];
  std::snprintf(status, sizeof status,
                "%c %s   %5.1f s   arquivos %d (overlay %d)   IO %.0f MB%s",
                spin[static_cast<int>(now * 8.0) & 3], phase,
                now - g_term.start, g_term.opens, g_term.overlayOpens,
                g_term.megabytes, quiet);
  draw->AddText(font, size, ImVec2(margin, y), kTermMark,
                "DR2Hook - inicializacao rapida (dr2hook.log ao vivo)");
  y += lineH;
  draw->AddText(font, size, ImVec2(margin, y), IM_COL32(235, 235, 235, 255),
                status);
  y += lineH;
  if (!g_coverGpu.empty()) {
    draw->AddText(font, size, ImVec2(margin, y), kTermWarn, g_coverGpu.c_str());
    y += lineH;
  }
  y += lineH * 0.5f;

  const float bottom = io.DisplaySize.y - margin;
  const int room = std::max(1, static_cast<int>((bottom - y) / lineH) - 1);
  const std::size_t count = g_term.lines.size();
  const std::size_t first =
      count > static_cast<std::size_t>(room) ? count - room : 0;
  std::string text;
  for (std::size_t i = first; i < count; ++i) {
    const TermLine &line = g_term.lines[i];
    text = line.time.empty() ? line.text : line.time + "  " + line.text;
    if (line.repeats > 1) {
      text += "  (x" + std::to_string(line.repeats) + ")";
    }
    draw->AddText(font, size, ImVec2(margin, y), line.color, text.c_str());
    y += lineH;
  }
  if (static_cast<int>(now * 2.0) % 2 == 0) {
    draw->AddText(font, size, ImVec2(margin, y), kTermText, "_");
  }
}

} // namespace

namespace dr2hook {

#if defined(_WIN32) || defined(DR2HOOK_USE_BACKENDS)
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd,
                                                             UINT msg,
                                                             WPARAM wParam,
                                                             LPARAM lParam);
#endif

bool OverlayManager::Initialize(HWND hWnd, ID3D11Device *pDevice,
                                ID3D11DeviceContext *pContext) {
  if (g_initialized) {
    return true;
  }

  if (ImGui::GetCurrentContext() == nullptr) {
    ImGui::CreateContext();
  }

  ImGuiIO &io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  io.IniFilename = nullptr;
  ImGui::StyleColorsDark();

  if (!io.Fonts->IsBuilt()) {
    unsigned char *pixels = nullptr;
    int width = 0, height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
  }

  g_hWnd = hWnd;
  g_device = pDevice;
  g_deviceContext = pContext;
  if (g_device != nullptr) {
    g_device->AddRef();
  }
  if (g_deviceContext != nullptr) {
    g_deviceContext->AddRef();
  }

#if defined(_WIN32) || defined(DR2HOOK_USE_BACKENDS)
  if (g_hWnd != nullptr) {
    ImGui_ImplWin32_Init(g_hWnd);
  }
  if (g_device != nullptr && g_deviceContext != nullptr) {
    ImGui_ImplDX11_Init(g_device, g_deviceContext);
  }
#endif

  g_initialized = true;
  Logger::Info("OverlayManager inicializado com sucesso.");
  return true;
}

void OverlayManager::Shutdown() {
  if (!g_initialized) {
    return;
  }

#if defined(_WIN32) || defined(DR2HOOK_USE_BACKENDS)
  if (g_renderTargetView != nullptr) {
    g_renderTargetView->Release();
    g_renderTargetView = nullptr;
  }
  if (g_opaqueBlend != nullptr) {
    g_opaqueBlend->Release();
    g_opaqueBlend = nullptr;
  }
  if (g_tonemapPS != nullptr) {
    g_tonemapPS->Release();
    g_tonemapPS = nullptr;
  }
  if (g_tonemapCB != nullptr) {
    g_tonemapCB->Release();
    g_tonemapCB = nullptr;
  }
  g_tonemapTried = false;
  g_coverScene = nullptr;
  ReleaseCoverImage();
  ImGui_ImplDX11_Shutdown();
  ImGui_ImplWin32_Shutdown();
#endif

  if (g_renderTargetView != nullptr) {
    g_renderTargetView->Release();
    g_renderTargetView = nullptr;
  }
  if (g_deviceContext != nullptr) {
    g_deviceContext->Release();
    g_deviceContext = nullptr;
  }
  if (g_device != nullptr) {
    g_device->Release();
    g_device = nullptr;
  }
  g_hWnd = nullptr;
  g_showMenu = false;

  {
    std::lock_guard<std::mutex> lock(g_notificationMutex);
    g_notifications.clear();
  }

  if (ImGui::GetCurrentContext() != nullptr) {
    ImGui::DestroyContext();
  }

  g_initialized = false;
  Logger::Info("OverlayManager encerrado com sucesso.");
}

bool OverlayManager::IsInitialized() { return g_initialized; }

void OverlayManager::ToggleMenu() { g_showMenu = !g_showMenu; }

bool OverlayManager::IsMenuVisible() { return g_showMenu; }

void OverlayManager::SetMenuVisible(bool visible) { g_showMenu = visible; }

void OverlayManager::AddNotification(std::string message, float duration,
                                     ToastType type) {
  std::lock_guard<std::mutex> lock(g_notificationMutex);
  g_notifications.push_back(
      ToastNotification{std::move(message), duration, 0.0f, type});
}

void OverlayManager::UpdateNotifications(float deltaTime) {
  std::lock_guard<std::mutex> lock(g_notificationMutex);
  for (auto &toast : g_notifications) {
    toast.elapsed += deltaTime;
  }
  g_notifications.erase(std::remove_if(g_notifications.begin(),
                                       g_notifications.end(),
                                       [](const ToastNotification &t) {
                                         return t.elapsed >= t.duration;
                                       }),
                        g_notifications.end());
}

size_t OverlayManager::GetNotificationCount() {
  std::lock_guard<std::mutex> lock(g_notificationMutex);
  return g_notifications.size();
}

std::vector<ToastNotification> OverlayManager::GetActiveNotifications() {
  std::lock_guard<std::mutex> lock(g_notificationMutex);
  return g_notifications;
}

bool OverlayManager::HandleWndProc(HWND hWnd, UINT msg, WPARAM wParam,
                                   LPARAM lParam) {
  if (!g_initialized || !g_showMenu) {
    return false;
  }

#if defined(_WIN32) || defined(DR2HOOK_USE_BACKENDS)
  ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam);
#endif

  ImGuiIO &io = ImGui::GetIO();
  if (io.WantCaptureMouse && (msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST)) {
    return true;
  }
  if (io.WantCaptureKeyboard && (msg >= WM_KEYFIRST && msg <= WM_KEYLAST)) {
    return true;
  }
  return false;
}

void OverlayManager::SetLoadCover(bool on) {
  if (on && !g_loadCover) {
    g_term = LoadTerminal{};
  } else if (!on && g_loadCover && g_term.frames > 0) {
    char note[128];
    std::snprintf(note, sizeof note,
                  "LoadCover: %d frames no terminal, maior intervalo %.0f ms.",
                  g_term.frames, g_term.maxGap * 1000.0f);
    Logger::Info(note);
  }
  g_loadCover = on;
}

bool OverlayManager::IsLoadCoverOn() { return g_loadCover; }

void OverlayManager::SetLoadCoverImage(std::string path) {
  g_coverImagePath = std::move(path);
  g_coverImageTried = false;
}

void OverlayManager::SetLoadCoverScene(ID3D11ShaderResourceView *scene,
                                       float aspect, std::string gpuStatus,
                                       float exposure) {
  g_coverScene = scene;
  g_coverAspect = aspect;
  g_coverExposure = exposure;
  g_coverGpu = std::move(gpuStatus);
}

void OverlayManager::RenderUI() {
  if (g_loadCover) {
    ImGui::GetBackgroundDrawList()->AddRectFilled(
        ImVec2(0.0f, 0.0f), ImGui::GetIO().DisplaySize, IM_COL32(0, 0, 0, 255));
    DrawLoadTerminal();
  } else if (g_coverImage != nullptr) {
    ReleaseCoverImage();
  }

  // 1. HUD Toast Overlay
  float dt = ImGui::GetIO().DeltaTime;
  if (dt > 0.0f) {
    UpdateNotifications(dt);
  }

  std::vector<ToastNotification> activeToasts;
  {
    std::lock_guard<std::mutex> lock(g_notificationMutex);
    activeToasts = g_notifications;
  }

  if (!activeToasts.empty()) {
    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    ImVec2 workPos = viewport->WorkPos;
    ImVec2 workSize = viewport->WorkSize;
    float startX = workPos.x + workSize.x - 20.0f;
    float currentY = workPos.y + 20.0f;

    for (size_t i = 0; i < activeToasts.size(); ++i) {
      const auto &toast = activeToasts[i];

      float alpha = 1.0f;
      constexpr float fadeTime = 0.4f;
      if (toast.elapsed < fadeTime) {
        alpha = toast.elapsed / fadeTime;
      } else if (toast.duration - toast.elapsed < fadeTime) {
        alpha = (toast.duration - toast.elapsed) / fadeTime;
      }
      alpha = std::clamp(alpha, 0.05f, 1.0f);

      ImVec4 badgeColor;
      const char *badgeText = "[INFO]";
      if (toast.type == ToastType::Warning) {
        badgeColor = ImVec4(1.0f, 0.75f, 0.0f, alpha);
        badgeText = "[AVISO]";
      } else if (toast.type == ToastType::Error) {
        badgeColor = ImVec4(1.0f, 0.25f, 0.25f, alpha);
        badgeText = "[ERRO]";
      } else {
        badgeColor = ImVec4(0.2f, 0.7f, 1.0f, alpha);
        badgeText = "[INFO]";
      }

      ImGui::SetNextWindowPos(ImVec2(startX, currentY), ImGuiCond_Always,
                              ImVec2(1.0f, 0.0f));
      ImGui::SetNextWindowBgAlpha(0.85f * alpha);

      std::string winName = "##Toast_" + std::to_string(i);
      ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
                               ImGuiWindowFlags_AlwaysAutoResize |
                               ImGuiWindowFlags_NoSavedSettings |
                               ImGuiWindowFlags_NoFocusOnAppearing |
                               ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove;

      if (ImGui::Begin(winName.c_str(), nullptr, flags)) {
        ImGui::TextColored(badgeColor, "%s", badgeText);
        ImGui::SameLine();
        ImVec4 textColor = ImGui::GetStyleColorVec4(ImGuiCol_Text);
        textColor.w = alpha;
        ImGui::TextColored(textColor, "%s", toast.message.c_str());

        currentY += ImGui::GetWindowHeight() + 8.0f;
      }
      ImGui::End();
    }
  }

  // 2. Diferenca ao vivo para o fantasma
  if (dr2hook::GhostLab::IsHudVisible()) {
    const dr2hook::GhostLab::Status ghost = dr2hook::GhostLab::GetStatus();
    if (ghost.active) {
      const ImGuiViewport *viewport = ImGui::GetMainViewport();
      ImGui::SetNextWindowPos(
          ImVec2(viewport->WorkPos.x + viewport->WorkSize.x * 0.5f,
                 viewport->WorkPos.y + 12.0f),
          ImGuiCond_Always, ImVec2(0.5f, 0.0f));
      ImGui::SetNextWindowBgAlpha(0.6f);
      const ImGuiWindowFlags flags =
          ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
          ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
          ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove |
          ImGuiWindowFlags_NoInputs;
      if (ImGui::Begin("##GhostDelta", nullptr, flags)) {
        // delta > 0: o fantasma passou por aqui antes, jogador atras.
        const bool behind = ghost.deltaSeconds > 0.0f;
        const ImVec4 color = behind ? ImVec4(1.0f, 0.35f, 0.3f, 1.0f)
                                    : ImVec4(0.35f, 1.0f, 0.45f, 1.0f);
        ImGui::SetWindowFontScale(1.6f);
        ImGui::TextColored(color, "%+.2f s", ghost.deltaSeconds);
        ImGui::SetWindowFontScale(1.0f);
        ImGui::SameLine();
        ImGui::TextDisabled("%+.0f m", -ghost.gapMeters);
        if (ghost.offTrackMeters > 25.0f) {
          ImGui::TextDisabled("off the ghost's line (%.0f m)",
                              ghost.offTrackMeters);
        }
      }
      ImGui::End();
    }
  }

  // 3. Main In-Game Menu (DR2Hook)
  if (g_showMenu) {
    ImGui::SetNextWindowSize(ImVec2(660, 540), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("DR2Hook v0.1.0", &g_showMenu)) {
      // Tab Navigation
      if (ImGui::BeginTabBar("DR2TabBar", ImGuiTabBarFlags_None)) {

        // ===================================================================
        // TAB 1: DIAGNOSTICS
        // ===================================================================
        if (ImGui::BeginTabItem("Diagnostics")) {
          VehicleTelemetryInfo vInfo;
          Player::GetVehicleTelemetry(vInfo);

          TrackTelemetryInfo tInfo;
          Player::GetTrackTelemetry(tInfo);

          CarState state{};
          bool hasState = Player::CaptureState(state);

          // Card 1: Vehicle
          if (ImGui::CollapsingHeader("Vehicle",
                                      ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Text("Model: ");
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.2f, 0.9f, 1.0f, 1.0f), "%s",
                               vInfo.model.c_str());

            ImGui::SameLine(360.0f);
            ImGui::Text("Class: ");
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.9f, 0.8f, 0.3f, 1.0f), "%s",
                               vInfo.category.c_str());

            ImGui::Text("Status: ");
            ImGui::SameLine();
            if (vInfo.isAnchored) {
              ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.3f, 1.0f), "%s",
                                 vInfo.state.c_str());
            } else {
              ImGui::TextColored(ImVec4(0.8f, 0.4f, 0.2f, 1.0f), "%s",
                                 vInfo.state.c_str());
            }

            ImGui::SameLine(360.0f);
            uintptr_t vehAddr = Player::GetVehicleAddress();
            if (vehAddr != 0) {
              ImGui::TextDisabled("Rig: 0x%llX",
                                  static_cast<unsigned long long>(vehAddr));
            } else {
              ImGui::TextDisabled("Rig: Disconnected");
            }

            ImGui::Spacing();

            // Speed in prominent display
            ImGui::Text("Speed:");
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.95f, 0.1f, 1.0f),
                               "[ %.1f km/h ]", vInfo.speedKmh);
            ImGui::SameLine();
            ImGui::TextDisabled("(%.1f mph)", vInfo.speedMph);

            ImGui::SameLine(360.0f);
            ImGui::Text("Acceleration:");
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.4f, 0.9f, 1.0f, 1.0f), "%.2f G",
                               vInfo.accelerationG);

            // Gear & RPM
            ImGui::Text("Gear: ");
            ImGui::SameLine();
            if (vInfo.gear > 0) {
              ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.5f, 1.0f), "%d / %d",
                                 vInfo.gear, vInfo.forwardGears);
            } else if (vInfo.gear == 0) {
              ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "N");
            } else if (vInfo.gear == -1) {
              ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.2f, 1.0f), "R");
            } else {
              ImGui::TextDisabled("--");
            }

            ImGui::SameLine(200.0f);
            ImGui::Text("RPM: ");
            ImGui::SameLine();
            if (vInfo.rpm > 0.0f) {
              ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "%.0f RPM",
                                 vInfo.rpm);
            } else {
              ImGui::TextDisabled("N/A");
            }
            ImGui::SameLine();
            float redline = vInfo.redlineRpm > 0.0f ? vInfo.redlineRpm
                                                     : vInfo.maxPowerRpm;
            ImGui::TextDisabled("(Idle: %.0f | Redline: %.0f)", vInfo.idleRpm,
                                redline);

            // Engine RPM graphical bar
            float rpmRatio = 0.0f;
            if (redline > 0.0f && vInfo.rpm > 0.0f) {
              rpmRatio = std::clamp(vInfo.rpm / (redline * 1.05f), 0.0f, 1.0f);
            }
            char rpmBuf[32];
            if (vInfo.rpm > 0.0f) {
              std::snprintf(rpmBuf, sizeof(rpmBuf), "%.0f RPM", vInfo.rpm);
            } else {
              std::snprintf(rpmBuf, sizeof(rpmBuf), "Telemetry N/A");
            }

            if (rpmRatio > 0.85f) {
              ImGui::PushStyleColor(ImGuiCol_PlotHistogram,
                                    ImVec4(0.95f, 0.2f, 0.2f, 0.9f));
            } else if (rpmRatio > 0.70f) {
              ImGui::PushStyleColor(ImGuiCol_PlotHistogram,
                                    ImVec4(1.0f, 0.75f, 0.1f, 0.9f));
            } else {
              ImGui::PushStyleColor(ImGuiCol_PlotHistogram,
                                    ImVec4(0.2f, 0.85f, 0.4f, 0.9f));
            }
            ImGui::ProgressBar(rpmRatio, ImVec2(-1.0f, 15.0f), rpmBuf);
            ImGui::PopStyleColor();
          }

          ImGui::Spacing();

          // Card 2: Track / Session
          if (ImGui::CollapsingHeader("Track / Session",
                                      ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Text("Stage: ");
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.2f, 0.9f, 1.0f, 1.0f), "%s",
                               tInfo.trackName.c_str());

            ImGui::Text("Location: ");
            ImGui::SameLine();
            ImGui::TextUnformatted(tInfo.location.c_str());

            ImGui::Text("Surface: ");
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.85f, 0.75f, 0.55f, 1.0f), "%s",
                               tInfo.surface.c_str());

            ImGui::SameLine(360.0f);
            ImGui::Text("Conditions: ");
            ImGui::SameLine();
            ImGui::TextUnformatted(tInfo.conditions.c_str());

            ImGui::Text("Session Mode: ");
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "%s",
                               tInfo.sessionState.c_str());
          }

          ImGui::Spacing();

          // Card 3: Telemetry
          if (ImGui::CollapsingHeader("Telemetry",
                                      ImGuiTreeNodeFlags_DefaultOpen)) {
            if (hasState) {
              // World position
              ImGui::Text("World Position (X, Y, Z):");
              ImGui::SameLine();
              ImGui::TextColored(ImVec4(0.3f, 0.9f, 1.0f, 1.0f),
                                 "X: %.2f  |  Y: %.2f  |  Z: %.2f",
                                 state.position.x, state.position.y,
                                 state.position.z);

              // Orientation Euler (Pitch, Roll, Yaw)
              float qNormSq = state.quaternion.x * state.quaternion.x +
                              state.quaternion.y * state.quaternion.y +
                              state.quaternion.z * state.quaternion.z +
                              state.quaternion.w * state.quaternion.w;
              float roll = 0.0f, pitch = 0.0f, yaw = 0.0f;
              if (qNormSq > 0.001f) {
                float sinr_cosp = 2.0f * (state.quaternion.w * state.quaternion.x +
                                          state.quaternion.y * state.quaternion.z);
                float cosr_cosp = 1.0f - 2.0f * (state.quaternion.x * state.quaternion.x +
                                                state.quaternion.y * state.quaternion.y);
                roll = std::atan2(sinr_cosp, cosr_cosp) * (180.0f / 3.14159265f);

                float sinp = 2.0f * (state.quaternion.w * state.quaternion.y -
                                     state.quaternion.z * state.quaternion.x);
                if (std::abs(sinp) >= 1.0f) {
                  pitch = std::copysign(90.0f, sinp);
                } else {
                  pitch = std::asin(sinp) * (180.0f / 3.14159265f);
                }

                float siny_cosp = 2.0f * (state.quaternion.w * state.quaternion.z +
                                          state.quaternion.x * state.quaternion.y);
                float cosy_cosp = 1.0f - 2.0f * (state.quaternion.y * state.quaternion.y +
                                                state.quaternion.z * state.quaternion.z);
                yaw = std::atan2(siny_cosp, cosy_cosp) * (180.0f / 3.14159265f);
              }

              ImGui::Text("Orientação (Euler):");
              ImGui::SameLine();
              ImGui::TextColored(ImVec4(0.9f, 0.7f, 1.0f, 1.0f),
                                 "Pitch: %+.1f°  |  Roll: %+.1f°  |  Yaw: %+.1f°",
                                 pitch, roll, yaw);

              ImGui::Text("Quaternion:");
              ImGui::SameLine();
              ImGui::TextDisabled("(x: %.3f, y: %.3f, z: %.3f, w: %.3f)",
                                  state.quaternion.x, state.quaternion.y,
                                  state.quaternion.z, state.quaternion.w);

              // Linear and Angular Velocity
              ImGui::Text("Linear Velocity (m/s):");
              ImGui::SameLine();
              ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.6f, 1.0f),
                                 "Vx: %+.2f  |  Vy: %+.2f  |  Vz: %+.2f",
                                 state.linearVelocity.x, state.linearVelocity.y,
                                 state.linearVelocity.z);

              ImGui::Text("Angular Velocity (rad/s):");
              ImGui::SameLine();
              ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f),
                                 "Wx: %+.3f  |  Wy: %+.3f  |  Wz: %+.3f",
                                 state.angularVelocity.x,
                                 state.angularVelocity.y,
                                 state.angularVelocity.z);

              // 4 Wheels & Suspension Table
              ImGui::Spacing();
              ImGui::Text("Suspension & Wheel Contact:");
              if (ImGui::BeginTable("WheelsTelemetryTable", 4,
                                    ImGuiTableFlags_Borders |
                                        ImGuiTableFlags_RowBg |
                                        ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn("Front Left (FL)");
                ImGui::TableSetupColumn("Front Right (FR)");
                ImGui::TableSetupColumn("Rear Left (RL)");
                ImGui::TableSetupColumn("Rear Right (RR)");
                ImGui::TableHeadersRow();

                // Compression row
                ImGui::TableNextRow();
                for (int i = 0; i < 4; ++i) {
                  ImGui::TableSetColumnIndex(i);
                  float compPercent =
                      state.wheels[i].suspensionCompression * 100.0f;
                  ImGui::Text("Comp: %.1f%%", compPercent);
                }

                // Ground contact row
                ImGui::TableNextRow();
                for (int i = 0; i < 4; ++i) {
                  ImGui::TableSetColumnIndex(i);
                  if (state.wheels[i].inContact) {
                    ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f),
                                       "● Grounded");
                  } else {
                    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                                       "○ Airborne");
                  }
                }

                ImGui::EndTable();
              }
            } else {
              ImGui::TextDisabled("Waiting for vehicle to spawn for "
                                  "real-time telemetry...");
            }
          }

          ImGui::EndTabItem();
        }

        // ===================================================================
        // TAB 2: MODS
        // ===================================================================
        if (ImGui::BeginTabItem("Mods")) {
          ImGui::TextColored(ImVec4(0.2f, 0.8f, 1.0f, 1.0f),
                             "Lua Mod Manager & Extensions");
          ImGui::SameLine();
          if (ImGui::Button("Reload Scripts (Hot-Reload)")) {
            ModManager::ReloadMods();
          }
          ImGui::SameLine();
          if (ImGui::Button("Reload Native Core (F8)")) {
#if defined(_WIN32)
            HMODULE host = GetModuleHandleA("dxgi.dll");
            auto requestReload =
                host == nullptr
                    ? nullptr
                    : reinterpret_cast<void (*)()>(
                          GetProcAddress(host, "Dr2Host_RequestReload"));
            if (requestReload != nullptr) {
              requestReload();
            }
#endif
          }
          if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Reloads dr2hook_core.dll from disk without closing the game. "
                "The in-memory checkpoint is cleared.");
          }

          ImGui::Separator();

          const auto &mods = ModManager::GetLoadedMods();
          static int selectedModIndex = 0;

          if (mods.empty()) {
            ImGui::TextDisabled(
                "No mods loaded in 'mods/' directory.");
          } else {
            if (ImGui::BeginTable("ModsManagerTable", 5,
                                  ImGuiTableFlags_Borders |
                                      ImGuiTableFlags_RowBg |
                                      ImGuiTableFlags_SizingStretchProp)) {
              ImGui::TableSetupColumn("Active",
                                      ImGuiTableColumnFlags_WidthFixed, 55.0f);
              ImGui::TableSetupColumn("Name",
                                      ImGuiTableColumnFlags_WidthStretch);
              ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed,
                                      130.0f);
              ImGui::TableSetupColumn("Version",
                                      ImGuiTableColumnFlags_WidthFixed, 65.0f);
              ImGui::TableSetupColumn("Author",
                                      ImGuiTableColumnFlags_WidthFixed, 130.0f);
              ImGui::TableHeadersRow();

              for (size_t i = 0; i < mods.size(); ++i) {
                const auto &mod = mods[i];
                ImGui::TableNextRow();

                ImGui::TableSetColumnIndex(0);
                ImGui::PushID(static_cast<int>(i));
                bool enabled = mod.enabled;
                if (ImGui::Checkbox("##enabled", &enabled)) {
                  const_cast<ModInstance &>(mod).enabled = enabled;
                }
                ImGui::PopID();

                ImGui::TableSetColumnIndex(1);
                bool isSelected = (selectedModIndex == static_cast<int>(i));
                if (ImGui::Selectable(mod.name.c_str(), isSelected,
                                      ImGuiSelectableFlags_SpanAllColumns)) {
                  selectedModIndex = static_cast<int>(i);
                }

                ImGui::TableSetColumnIndex(2);
                ImGui::TextDisabled("%s", mod.id.c_str());

                ImGui::TableSetColumnIndex(3);
                ImGui::TextDisabled("v%s", mod.version.c_str());

                ImGui::TableSetColumnIndex(4);
                ImGui::TextDisabled("%s", mod.author.c_str());
              }
              ImGui::EndTable();
            }

            // Selected mod details
            if (selectedModIndex >= 0 &&
                selectedModIndex < static_cast<int>(mods.size())) {
              const auto &selMod = mods[selectedModIndex];
              ImGui::Spacing();
              if (ImGui::CollapsingHeader("Mod Details",
                                          ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::TextColored(ImVec4(0.2f, 0.8f, 1.0f, 1.0f), "%s",
                                   selMod.name.c_str());
                ImGui::SameLine();
                ImGui::TextDisabled("(%s) v%s by %s", selMod.id.c_str(),
                                    selMod.version.c_str(),
                                    selMod.author.c_str());

                ImGui::TextWrapped("%s", selMod.description.c_str());
                ImGui::Spacing();
                ImGui::Text("Main Script: %s",
                            selMod.mainScriptPath.c_str());
                ImGui::Text("Directory: %s", selMod.directoryPath.c_str());

                ImGui::Text("Registered Callbacks:");
                ImGui::BulletText("onInit: %s",
                                  (selMod.refOnInit != LUA_NOREF) ? "Yes" : "No");
                ImGui::BulletText("onTick: %s",
                                  (selMod.refOnTick != LUA_NOREF) ? "Yes" : "No");
                ImGui::BulletText("onKeyDown: %s",
                                  (selMod.refOnKeyDown != LUA_NOREF) ? "Yes"
                                                                     : "No");
                ImGui::BulletText("onStageStart: %s",
                                  (selMod.refOnStageStart != LUA_NOREF)
                                      ? "Yes"
                                      : "No");
                ImGui::BulletText("onRenderUI: %s",
                                  (selMod.refOnRenderUI != LUA_NOREF) ? "Yes"
                                                                      : "No");
              }
            }
          }

          // Lua Engine Stats
          ImGui::Spacing();
          if (ImGui::CollapsingHeader("Lua Engine Statistics",
                                      ImGuiTreeNodeFlags_DefaultOpen)) {
            lua_State *L = LuaEngine::GetState();
            int memKb = (L != nullptr) ? lua_gc(L, LUA_GCCOUNT, 0) : 0;
            ImGui::Text("Allocated Lua Memory: %d KB", memKb);
            ImGui::Text("Total Loaded Mods: %zu", mods.size());
          }

          ImGui::EndTabItem();
        }

        // ===================================================================
        // DYNAMIC TABS: EACH ACTIVE MOD RECEIVES ITS OWN DEDICATED TAB
        // ===================================================================
        const auto &activeMods = ModManager::GetLoadedMods();
        for (size_t i = 0; i < activeMods.size(); ++i) {
          const auto &mod = activeMods[i];
          if (!mod.enabled) {
            continue;
          }

          if (ImGui::BeginTabItem(mod.name.c_str())) {
            ImGui::TextColored(ImVec4(0.2f, 0.8f, 1.0f, 1.0f), "%s",
                               mod.name.c_str());
            ImGui::SameLine();
            ImGui::TextDisabled("v%s | %s", mod.version.c_str(),
                                mod.author.c_str());
            ImGui::Separator();

            // Trigger Lua onRenderUI hook if implemented
            if (mod.refOnRenderUI != LUA_NOREF) {
              ModManager::DispatchRenderUI(const_cast<ModInstance &>(mod));
            }

            // Specialized native panel for Practice Mode
            if (mod.id == "dr2.practice_mode" || mod.id == "practice_mode" ||
                mod.name == "Practice Mode") {
              ImGui::TextColored(
                  ImVec4(1.0f, 0.9f, 0.2f, 1.0f),
                  "Corner & Sector Practice Panel");
              ImGui::TextWrapped(
                  "Save and restore instant checkpoints in real time to practice "
                  "difficult corners, braking zones, and jumps repeatedly.");

              ImGui::Spacing();
              if (ImGui::CollapsingHeader("Practice Shortcuts",
                                          ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::BulletText(
                    "F5: Save instant checkpoint (Position and Velocity).");
                ImGui::BulletText(
                    "F6: Restore checkpoint in active mode (Normal or Momentum).");
                ImGui::BulletText(
                    "F7: Restore checkpoint directly WITH FULL MOMENTUM.");
              }

              ImGui::Spacing();
              if (ImGui::CollapsingHeader("Restore Configuration",
                                          ImGuiTreeNodeFlags_DefaultOpen)) {
                int pmMode =
                    static_cast<int>(SavestateManager::GetRestoreMode());
                ImGui::Text("Default Restore Behavior:");
                if (ImGui::RadioButton("Normal (Stationary Teleport)##dyn",
                                       pmMode == 0)) {
                  SavestateManager::SetRestoreMode(RestoreMode::Normal);
                }
                ImGui::SameLine();
                if (ImGui::RadioButton(
                        "With Momentum (Preserve Velocity)##dyn",
                        pmMode == 1)) {
                  SavestateManager::SetRestoreMode(RestoreMode::WithMomentum);
                }

                ImGui::Spacing();
                if (ImGui::Button("Save Checkpoint (F5)",
                                 ImVec2(210, 32))) {
                  SavestateManager::OnKeyAction(0x74, true);
                }
                ImGui::SameLine();
                if (ImGui::Button("Restore Normal (F6)",
                                 ImVec2(170, 32))) {
                  SavestateManager::RestoreCheckpoint(RestoreMode::Normal);
                }
                ImGui::SameLine();
                if (ImGui::Button("Restore with Momentum (F7)",
                                 ImVec2(210, 32))) {
                  SavestateManager::RestoreCheckpoint(
                      RestoreMode::WithMomentum);
                }
              }

              ImGui::Spacing();
              if (ImGui::CollapsingHeader("Real-Time Practice Status",
                                          ImGuiTreeNodeFlags_DefaultOpen)) {
                if (SavestateManager::HasSavedState()) {
                  const auto &saved = SavestateManager::GetSavedState();
                  float sSpd =
                      std::sqrt(saved.linearVelocity.x *
                                    saved.linearVelocity.x +
                                 saved.linearVelocity.y *
                                    saved.linearVelocity.y +
                                 saved.linearVelocity.z *
                                    saved.linearVelocity.z) *
                      3.6f;
                  ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f),
                                     "● Checkpoint Saved & Ready");
                  ImGui::Text("Position: (X: %.2f, Y: %.2f, Z: %.2f)",
                              saved.position.x, saved.position.y,
                              saved.position.z);
                  ImGui::Text("Saved Speed: %.1f km/h", sSpd);
                } else {
                  ImGui::TextDisabled("No checkpoint saved yet. Press F5 "
                                      "to save your first point.");
                }
              }
            } else if (mod.refOnRenderUI == LUA_NOREF) {
              // General information for mods without custom UI
              ImGui::Spacing();
              ImGui::Text("Description: %s", mod.description.c_str());
              ImGui::Text("Directory: %s", mod.directoryPath.c_str());
              ImGui::Text("File: %s", mod.mainScriptPath.c_str());
            }

            ImGui::EndTabItem();
          }
        }

        ImGui::EndTabBar();
      }
    }
    ImGui::End();
  }
}

void OverlayManager::Render(IDXGISwapChain *pSwapChain) {
  if (!g_initialized) {
    return;
  }

#if defined(_WIN32) || defined(DR2HOOK_USE_BACKENDS)
  if (pSwapChain != nullptr) {
    if (g_renderTargetView == nullptr && g_device != nullptr) {
      ID3D11Texture2D *pBackBuffer = nullptr;
      HRESULT hr =
          pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D),
                                reinterpret_cast<void **>(&pBackBuffer));
      if (SUCCEEDED(hr) && pBackBuffer != nullptr) {
        g_device->CreateRenderTargetView(pBackBuffer, nullptr,
                                         &g_renderTargetView);
        pBackBuffer->Release();
      }
    }

    if (g_renderTargetView != nullptr && g_deviceContext != nullptr) {
      g_deviceContext->OMSetRenderTargets(1, &g_renderTargetView, nullptr);
    }

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
  }
#endif

  ImGuiIO &io = ImGui::GetIO();
  if (io.DisplaySize.x <= 0.0f || io.DisplaySize.y <= 0.0f) {
    io.DisplaySize = ImVec2(1920.0f, 1080.0f);
  }
  if (!io.Fonts->IsBuilt()) {
    unsigned char *pixels = nullptr;
    int width = 0, height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
  }

  ImGui::NewFrame();
  RenderUI();
  ImGui::Render();

#if defined(_WIN32) || defined(DR2HOOK_USE_BACKENDS)
  if (pSwapChain != nullptr && g_renderTargetView != nullptr) {
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
  }
#endif
}

} // namespace dr2hook
