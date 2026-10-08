#include "dr2hook/live_link.h"

#include "dr2hook/free_camera.h"
#include "dr2hook/logger.h"
#include "dr2hook/player.h"
#include "dr2hook/terminal_damage.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

namespace dr2hook {
namespace {

#if defined(_WIN32)

// Estados do topo da pilha em que o carro existe (os mesmos do `status` remoto).
constexpr uint32_t kTopRace = 0x1251500;
constexpr uint32_t kTopPause = 0x1250b50;
constexpr uint32_t kTopCountdown = 0x1252600;
constexpr ULONGLONG kMinGapMs = 16;   // até ~60 pacotes por segundo
constexpr ULONGLONG kIdleGapMs = 500; // sem carro nem câmera: só um sinal de vida

SOCKET g_socket = INVALID_SOCKET;
bool g_wsa = false;
sockaddr_in g_to{};
uint32_t g_seq = 0;
ULONGLONG g_lastSend = 0;
// Nada roda antes da primeira largada: no boot, o winsock e as leituras da pilha
// de fluxo coincidiram com travamentos da leitura de arquivos do jogo (2026-10-08).
bool g_armed = false;
bool g_opened = false;

// Abre o socket na primeira vez que o elo tem algo a mandar.
bool Open() {
  if (g_opened) return g_socket != INVALID_SOCKET;
  g_opened = true;
  WSADATA data{};
  if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
    Logger::Warn("LiveLink: WSAStartup falhou; sem elo com o editor.");
    return false;
  }
  g_wsa = true;
  g_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (g_socket == INVALID_SOCKET) {
    Logger::Warn("LiveLink: socket UDP nao abriu; sem elo com o editor.");
    return false;
  }
  u_long nonBlocking = 1;
  ioctlsocket(g_socket, FIONBIO, &nonBlocking);
  g_to.sin_family = AF_INET;
  g_to.sin_port = htons(LiveLink::kPort);
  g_to.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  Logger::Info("LiveLink: carro e camera vao para 127.0.0.1:" + std::to_string(LiveLink::kPort) + " (UDP).");
  return true;
}

uint32_t TopRva() {
  const std::string stack = TerminalDamage::FlowStack();
  const size_t at = stack.rfind("st=");
  return at == std::string::npos ? 0 : static_cast<uint32_t>(std::strtoull(stack.c_str() + at + 3, nullptr, 16));
}

bool Finite(const float *v, int n) {
  for (int i = 0; i < n; ++i) {
    if (!std::isfinite(v[i]) || std::fabs(v[i]) > 1e6f) return false;
  }
  return true;
}

#endif

} // namespace

void LiveLink::Install() {
#if defined(_WIN32)
  Logger::Info("LiveLink: espera a primeira largada para abrir 127.0.0.1:" + std::to_string(kPort) +
               " (UDP).");
#endif
}

void LiveLink::OnStageStart() {
#if defined(_WIN32)
  g_armed = true;
#endif
}

void LiveLink::Shutdown() {
#if defined(_WIN32)
  if (g_socket != INVALID_SOCKET) closesocket(g_socket);
  g_socket = INVALID_SOCKET;
  if (g_wsa) WSACleanup();
  g_wsa = false;
  g_opened = false;
  g_armed = false;
#endif
}

void LiveLink::OnFrame() {
#if defined(_WIN32)
  if (!g_armed || !Open()) return;
  const ULONGLONG now = GetTickCount64();
  if (now - g_lastSend < kMinGapMs) return;

  LivePacket p{};
  std::memcpy(p.magic, "DR2L", 4);
  p.version = 1;
  p.top = TopRva();
  if (p.top == kTopPause) p.flags |= kLivePaused;
  if (FreeCamera::StageView(p.eye, p.forward, p.up) && Finite(p.eye, 3) && Finite(p.forward, 3)) {
    p.flags |= kLiveView;
  }
  const bool racing = p.top == kTopRace || p.top == kTopPause || p.top == kTopCountdown;
  CarState car{};
  if (racing && Player::GetVehicleAddress() != 0 && Player::CaptureState(car, true)) {
    std::memcpy(p.carPos, &car.position, sizeof p.carPos);
    std::memcpy(p.carRot, car.rotationMatrix.m, sizeof p.carRot);
    std::memcpy(p.carVel, &car.linearVelocity, sizeof p.carVel);
    if (Finite(p.carPos, 3) && Finite(p.carRot, 9) && Finite(p.carVel, 3)) {
      p.flags |= kLiveCar;
      p.speedKmh = std::sqrt(p.carVel[0] * p.carVel[0] + p.carVel[1] * p.carVel[1] +
                             p.carVel[2] * p.carVel[2]) * 3.6f;
    }
  }
  if ((p.flags & (kLiveCar | kLiveView)) == 0 && now - g_lastSend < kIdleGapMs) return;
  p.seq = ++g_seq;
  g_lastSend = now;
  sendto(g_socket, reinterpret_cast<const char *>(&p), sizeof p, 0,
         reinterpret_cast<const sockaddr *>(&g_to), sizeof g_to);
#endif
}

} // namespace dr2hook
