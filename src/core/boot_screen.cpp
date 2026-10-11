#include "dr2hook/boot_screen.h"
#include "dr2hook/common.h"
#include "dr2hook/editor_launch.h"
#include "dr2hook/logger.h"

#include <algorithm>
#include <cstdio>
#include <mutex>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <windows.h>

#include "miniz.h"
#endif

namespace dr2hook {
namespace {

struct Line {
  BootLine kind;
  std::string text;
};

std::mutex g_mutex;
std::string g_step = "Starting";
double g_fraction = 0;
std::vector<Line> g_feed;
std::vector<Line> g_summary; // core, pistas e problemas, na ordem do resumo final
bool g_tracksDone = false, g_coreDone = false;
bool g_enabled = false;

#if defined(_WIN32)

#include "boot_screen_assets.inc"

// Janela de 800x450 desenhada com GDI num bitmap fora da tela (ainda não há Direct3D). Fundo da cor do
// fundo do logo; layout do mockup aprovado em 2026-10-09.
constexpr int kWidth = 800, kHeight = 450;
constexpr int kMargin = 60;
constexpr int kFeedLines = 4;
constexpr DWORD kHoldMs = 2000;        // resumo na tela antes de fechar
constexpr DWORD kHoldProblemMs = 4000; // com problema, mais tempo para ler
constexpr DWORD kTracksTimeoutMs = 120000; // o mesmo limite da espera no boot_data.xml
constexpr DWORD kCoreTimeoutMs = 20000;
constexpr COLORREF kWhite = RGB(235, 235, 235), kGrey = RGB(130, 130, 135), kDim = RGB(70, 70, 75),
                   kRed = RGB(225, 30, 40), kGreen = RGB(80, 200, 120), kYellow = RGB(240, 190, 60),
                   kTrack = RGB(35, 35, 40), kRule = RGB(30, 30, 34);

HANDLE g_closed = nullptr; // sinalizado quando a tela fecha (ou se ela não aparece)
HWND g_hwnd = nullptr;
HFONT g_font = nullptr, g_fontBold = nullptr, g_fontSmall = nullptr;
std::vector<uint8_t> g_logo; // BGRA de cima para baixo

std::wstring Wide(const std::string &s) {
  if (s.empty()) return {};
  const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
  std::wstring w(static_cast<size_t>(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
  return w;
}

void LoadAssets() {
  DWORD count = 0;
  AddFontMemResourceEx(const_cast<unsigned char *>(kBootFontRegular), sizeof kBootFontRegular, nullptr, &count);
  AddFontMemResourceEx(const_cast<unsigned char *>(kBootFontSemiBold), sizeof kBootFontSemiBold, nullptr, &count);
  auto font = [](int px, const wchar_t *face) {
    return CreateFontW(-px, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_TT_PRECIS,
                       CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, face);
  };
  g_font = font(12, L"Inter");
  g_fontSmall = font(12, L"Inter");
  g_fontBold = font(15, L"Inter SemiBold");

  std::vector<uint8_t> bgr(static_cast<size_t>(kBootLogoWidth) * kBootLogoHeight * 3);
  mz_ulong size = static_cast<mz_ulong>(bgr.size());
  if (mz_uncompress(bgr.data(), &size, kBootLogoBgr, sizeof kBootLogoBgr) != MZ_OK || size != bgr.size()) return;
  g_logo.resize(static_cast<size_t>(kBootLogoWidth) * kBootLogoHeight * 4);
  for (size_t i = 0, n = static_cast<size_t>(kBootLogoWidth) * kBootLogoHeight; i < n; ++i) {
    g_logo[i * 4 + 0] = bgr[i * 3 + 0];
    g_logo[i * 4 + 1] = bgr[i * 3 + 1];
    g_logo[i * 4 + 2] = bgr[i * 3 + 2];
    g_logo[i * 4 + 3] = 255;
  }
}

void Fill(HDC dc, int l, int t, int r, int b, COLORREF c) {
  const RECT rc{l, t, r, b};
  HBRUSH br = CreateSolidBrush(c);
  FillRect(dc, &rc, br);
  DeleteObject(br);
}

void Text(HDC dc, int x, int y, const std::string &s, HFONT font, COLORREF c, UINT align = TA_LEFT) {
  SelectObject(dc, font);
  SetTextColor(dc, c);
  SetTextAlign(dc, align | TA_TOP);
  const std::wstring w = Wide(s);
  TextOutW(dc, x, y, w.c_str(), static_cast<int>(w.size()));
}

void Icon(HDC dc, int x, int cy, BootLine kind, bool running) {
  if (kind == BootLine::Problem) {
    Text(dc, x + 4, cy - 8, "!", g_fontBold, kYellow, TA_CENTER);
    return;
  }
  HPEN pen = CreatePen(PS_SOLID, 2, running ? kWhite : kGreen);
  HGDIOBJ old = SelectObject(dc, pen);
  if (running) {
    SelectObject(dc, GetStockObject(NULL_BRUSH));
    Ellipse(dc, x + 1, cy - 3, x + 8, cy + 4);
  } else {
    const POINT check[] = {{x, cy}, {x + 3, cy + 3}, {x + 9, cy - 4}};
    Polyline(dc, check, 3);
  }
  SelectObject(dc, old);
  DeleteObject(pen);
}

void Draw(HDC dc) {
  std::string step;
  double fraction;
  std::vector<Line> lines;
  bool final, clean = true;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    final = g_tracksDone && g_coreDone;
    step = g_step;
    fraction = std::clamp(g_fraction, 0.0, 1.0);
    if (final) {
      lines = g_summary;
      size_t problems = 0;
      for (const Line &l : lines) problems += l.kind == BootLine::Problem;
      clean = problems == 0;
      if (lines.size() >= kFeedLines) {
        // Cabem 4 linhas: as do core e das pistas ficam, os problemas resumem o resto.
        lines.resize(kFeedLines - 1);
        lines.push_back({BootLine::Problem, "More problems in dr2hook.log"});
      } else {
        lines.push_back({BootLine::Done, problems ? "Started with warnings" : "Everything is ready"});
      }
    } else {
      const size_t from = g_feed.size() > kFeedLines - 1 ? g_feed.size() - (kFeedLines - 1) : 0;
      lines.assign(g_feed.begin() + static_cast<long>(from), g_feed.end());
    }
  }
  SetBkMode(dc, TRANSPARENT);
  Fill(dc, 0, 0, kWidth, kHeight, static_cast<COLORREF>(kBootLogoBackground));
  if (!g_logo.empty()) {
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = kBootLogoWidth;
    bi.bmiHeader.biHeight = -kBootLogoHeight;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    SetDIBitsToDevice(dc, (kWidth - kBootLogoWidth) / 2, 28, kBootLogoWidth, kBootLogoHeight, 0, 0, 0,
                      kBootLogoHeight, g_logo.data(), &bi, DIB_RGB_COLORS);
  }
  Text(dc, kWidth - 16, 12, std::string("v") + DR2HOOK_VERSION, g_fontSmall, kGrey, TA_RIGHT);

  int y = 262;
  const char *header = clean ? "All set. Starting DiRT Rally 2.0\xe2\x80\xa6" : "Starting DiRT Rally 2.0\xe2\x80\xa6";
  Text(dc, kMargin, y, final ? header : step, g_fontBold, final ? (clean ? kGreen : kYellow) : kWhite);
  Text(dc, kWidth - kMargin, y, std::to_string(static_cast<int>(final ? 100 : fraction * 100)) + "%", g_fontBold,
       kGrey, TA_RIGHT);
  y += 26;
  Fill(dc, kMargin, y, kWidth - kMargin, y + 8, kTrack);
  Fill(dc, kMargin, y, kMargin + static_cast<int>((kWidth - 2 * kMargin) * (final ? 1.0 : fraction)), y + 8,
       final ? (clean ? kGreen : kYellow) : kRed);
  y += 24;
  for (const Line &l : lines) {
    Icon(dc, kMargin + 2, y + 8, l.kind, false);
    Text(dc, kMargin + 20, y, l.text, g_font, l.kind == BootLine::Problem ? kYellow : kGrey);
    y += 19;
  }
  if (!final) {
    Icon(dc, kMargin + 2, y + 8, BootLine::Done, true);
    Text(dc, kMargin + 20, y, step + "\xe2\x80\xa6", g_font, kWhite);
  }
  Fill(dc, kMargin, kHeight - 38, kWidth - kMargin, kHeight - 37, kRule);
  Text(dc, kWidth / 2, kHeight - 31,
       final ? "DR2Hook \xe2\x80\x94 DiRT Rally 2.0 starts next" : "DR2Hook \xe2\x80\x94 preparing DiRT Rally 2.0",
       g_fontSmall, kDim, TA_CENTER);
}

// Depuração (salvar_tela_boot=1 no dr2hook_intro.ini): grava o quadro como BMP na pasta do jogo. A janela não
// pode ser fotografada por fora no Proton com Wayland.
void SaveFrame(const char *path) {
  BITMAPINFO bi{};
  bi.bmiHeader.biSize = sizeof bi.bmiHeader;
  bi.bmiHeader.biWidth = kWidth;
  bi.bmiHeader.biHeight = kHeight;
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 24;
  bi.bmiHeader.biCompression = BI_RGB;
  void *bits = nullptr;
  HDC mem = CreateCompatibleDC(nullptr);
  HBITMAP bmp = CreateDIBSection(mem, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
  if (bmp == nullptr) {
    DeleteDC(mem);
    return;
  }
  HGDIOBJ old = SelectObject(mem, bmp);
  Draw(mem);
  GdiFlush();
  const DWORD stride = (kWidth * 3 + 3) & ~3u, size = stride * kHeight;
  BITMAPFILEHEADER fh{};
  fh.bfType = 0x4d42;
  fh.bfOffBits = sizeof fh + sizeof bi.bmiHeader;
  fh.bfSize = fh.bfOffBits + size;
  if (FILE *f = std::fopen(path, "wb")) {
    std::fwrite(&fh, sizeof fh, 1, f);
    std::fwrite(&bi.bmiHeader, sizeof bi.bmiHeader, 1, f);
    std::fwrite(bits, size, 1, f);
    std::fclose(f);
  }
  SelectObject(mem, old);
  DeleteObject(bmp);
  DeleteDC(mem);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
  case WM_PAINT: {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    HDC mem = CreateCompatibleDC(dc);
    HBITMAP bmp = CreateCompatibleBitmap(dc, kWidth, kHeight);
    HGDIOBJ old = SelectObject(mem, bmp);
    Draw(mem);
    BitBlt(dc, 0, 0, kWidth, kHeight, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
    return 0;
  }
  case WM_ERASEBKGND:
    return 1;
  case WM_CLOSE:
    return 0; // só a própria tela fecha
  default:
    return DefWindowProcW(hwnd, msg, wp, lp);
  }
}

DWORD WINAPI ScreenThread(LPVOID) {
  LoadAssets();
  WNDCLASSW wc{};
  wc.lpfnWndProc = WndProc;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32650)); // IDC_APPSTARTING
  wc.lpszClassName = L"DR2HookBootScreen";
  RegisterClassW(&wc);
  const int x = (GetSystemMetrics(SM_CXSCREEN) - kWidth) / 2, y = (GetSystemMetrics(SM_CYSCREEN) - kHeight) / 2;
  g_hwnd = CreateWindowExW(WS_EX_APPWINDOW, wc.lpszClassName, L"DR2Hook", WS_POPUP, x, y, kWidth, kHeight, nullptr,
                           nullptr, wc.hInstance, nullptr);
  if (g_hwnd == nullptr) {
    Logger::Warn("BootScreen: janela não criada; o jogo segue sem a tela.");
    SetEvent(g_closed);
    return 0;
  }
  ShowWindow(g_hwnd, SW_SHOWNORMAL);
  UpdateWindow(g_hwnd);

  const ULONGLONG t0 = GetTickCount64();
  ULONGLONG finalAt = 0;
  bool problems = false;
  bool saveFrames = IntroOption("salvar_tela_boot", false), savedProgress = false;
  for (;;) {
    MSG m;
    while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage(&m);
      DispatchMessageW(&m);
    }
    const ULONGLONG now = GetTickCount64();
    {
      std::lock_guard<std::mutex> lock(g_mutex);
      if (!g_coreDone && now - t0 > kCoreTimeoutMs) {
        g_coreDone = true;
        g_summary.insert(g_summary.begin(), {BootLine::Problem, "DR2Hook core did not report back (see dr2hook.log)"});
      }
      if (!g_tracksDone && now - t0 > kTracksTimeoutMs) {
        g_tracksDone = true;
        g_summary.push_back({BootLine::Problem, "Custom tracks took too long (see dr2hook.log)"});
      }
      if (finalAt == 0 && g_tracksDone && g_coreDone) {
        finalAt = now;
        for (const Line &l : g_summary) problems |= l.kind == BootLine::Problem;
      }
    }
    if (saveFrames && !savedProgress && finalAt == 0) {
      bool half;
      {
        std::lock_guard<std::mutex> lock(g_mutex);
        half = g_fraction >= 0.5 || g_tracksDone;
      }
      if (half) {
        SaveFrame("dr2hook_boot_progress.bmp");
        savedProgress = true;
      }
    }
    if (finalAt != 0 && now - finalAt >= (problems ? kHoldProblemMs : kHoldMs)) {
      if (saveFrames) SaveFrame("dr2hook_boot_final.bmp");
      break;
    }
    InvalidateRect(g_hwnd, nullptr, FALSE);
    MsgWaitForMultipleObjects(0, nullptr, FALSE, 33, QS_ALLINPUT);
  }
  DestroyWindow(g_hwnd);
  g_hwnd = nullptr;
  Logger::Info("BootScreen: fechada depois de " + std::to_string(GetTickCount64() - t0) + " ms.");
  SetEvent(g_closed);
  return 0;
}

#endif

} // namespace

void InstallBootScreen() {
#if defined(_WIN32)
  g_closed = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  g_enabled = !IntroOption("pular_tela_boot", IsEditorLaunch());
  if (!g_enabled && g_closed != nullptr) SetEvent(g_closed);
#endif
}

void StartBootScreen() {
#if defined(_WIN32)
  if (!g_enabled) {
    Logger::Info(IsEditorLaunch() ? "BootScreen: desligada (aberto pelo editor)."
                                  : "BootScreen: desligada (pular_tela_boot=1).");
    return;
  }
  HANDLE t = CreateThread(nullptr, 0, ScreenThread, nullptr, 0, nullptr);
  if (t == nullptr) {
    Logger::Warn("BootScreen: thread não criada; o jogo segue sem a tela.");
    SetEvent(g_closed);
    return;
  }
  CloseHandle(t);
  Logger::Info("BootScreen: aberta.");
#endif
}

void BootScreenStep(const std::string &text, double fraction) {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_step = text;
  if (fraction >= 0) g_fraction = fraction;
}

void BootScreenLine(BootLine kind, const std::string &text) {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_feed.push_back({kind, text});
  if (kind == BootLine::Problem) g_summary.push_back({kind, text});
}

void BootScreenTracksDone(const std::string &summary) {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_feed.push_back({BootLine::Done, summary});
  // O resumo das pistas fica logo depois da linha do core.
  auto at = std::find_if(g_summary.begin(), g_summary.end(), [](const Line &l) { return l.kind == BootLine::Problem; });
  g_summary.insert(at, {BootLine::Done, summary});
  g_tracksDone = true;
  g_fraction = 1;
  // a barra cheia não pode ficar com o texto da última etapa das pistas
  if (!g_coreDone) g_step = "Loading the DR2Hook core...";
}

void BootScreenCoreDone(bool ok, const std::string &summary) {
  std::lock_guard<std::mutex> lock(g_mutex);
  const Line line{ok ? BootLine::Done : BootLine::Problem, summary};
  g_feed.push_back(line);
  g_summary.insert(g_summary.begin(), line);
  g_coreDone = true;
}

void WaitBootScreen() {
#if defined(_WIN32)
  if (g_closed != nullptr) WaitForSingleObject(g_closed, kTracksTimeoutMs + kCoreTimeoutMs);
#endif
}

} // namespace dr2hook
