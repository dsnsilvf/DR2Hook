#include "dr2hook/pssg_patch.h"
#include "dr2hook/logger.h"

#include <MinHook.h>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#if defined(_WIN32)
#include <windows.h>

namespace dr2hook {
namespace {

constexpr uintptr_t kImageBase = 0x140000000;
// Leitura do stream usada pela leitora de PSSG binário (0x1408d3520): lê o
// cabeçalho de 8 bytes ("PSSG" + tamanho BE) e depois esquema e nós aos
// pedaços. Assinatura: int read(stream, buffer, bytes) -> bytes lidos.
constexpr uintptr_t kStreamReadRva = 0x1408e6860 - kImageBase;
constexpr uint8_t kStreamReadPrologue[] = {
    0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c, 0x24, 0x10, 0x48, 0x89, 0x74,
    0x24, 0x18, 0x48, 0x89, 0x7c, 0x24, 0x20, 0x41, 0x54, 0x41, 0x56, 0x41, 0x57};

// frontend/databases/persistentDB.pssg desta versão: tamanho do cabeçalho.
constexpr uint32_t kPersistentDbSize = 5338789;

// A leitora lê o cabeçalho de cada nó (tipo, tamanho, tamanho dos atributos;
// u32 BE) numa leitura de 12 bytes, mas parte do arquivo (blocos binários)
// passa por outro caminho, então a posição no arquivo não é confiável. O nó é
// achado pelo conteúdo: N-ésimo cabeçalho igual a `header`, conferido depois
// pelo apelido (`nickname`) lido em seguida.
struct NodePatch {
  uint8_t header[12];
  unsigned ordinal; // 0 = primeiro cabeçalho igual no arquivo
  uint8_t newType[4];
  const char *nickname;
  const char *what;
};
// etex115 (texto de smart_contextual_info, painel da direita das smart_screen):
// UINODETEXT (273) -> UINODETEXTDOC (274), o tipo que interpreta {v}{s:...}.
// 54 nós têm este cabeçalho; etex115 é o 26º (offset 887796 da cópia de
// game_1.dat).
constexpr NodePatch kPatches[] = {
    {{0x00, 0x00, 0x01, 0x11, 0x00, 0x00, 0x00, 0x55, 0x00, 0x00, 0x00, 0x51},
     25,
     {0x00, 0x00, 0x01, 0x12},
     "etex115",
     "etex115 UINODETEXT -> UINODETEXTDOC"},
};
constexpr size_t kPatchCount = sizeof(kPatches) / sizeof(kPatches[0]);

using StreamReadFn = int (*)(void *stream, uint8_t *buffer, uint32_t bytes);
StreamReadFn g_originalRead = nullptr;

std::atomic<void *> g_target{nullptr}; // stream do persistentDB em leitura
// Só a thread que lê o alvo toca nestes.
uint64_t g_position = 0;
unsigned g_seen[kPatchCount] = {};
int g_awaitingNickname = -1; // patch aplicado esperando a conferência
std::atomic<unsigned> g_applied{0};
std::atomic<unsigned> g_mismatch{0};
bool g_installed = false;

int DetourRead(void *stream, uint8_t *buffer, uint32_t bytes) {
  const int read = g_originalRead(stream, buffer, bytes);
  if (read <= 0 || buffer == nullptr) {
    return read;
  }
  if (read == 8 && std::memcmp(buffer, "PSSG", 4) == 0) {
    const uint32_t size = (uint32_t{buffer[4]} << 24) | (uint32_t{buffer[5]} << 16) |
                          (uint32_t{buffer[6]} << 8) | buffer[7];
    if (size == kPersistentDbSize) {
      g_position = 8;
      for (unsigned &seen : g_seen) seen = 0;
      g_awaitingNickname = -1;
      g_target.store(stream);
      Logger::Info("PssgPatch: lendo persistentDB.pssg.");
    }
    return read;
  }
  if (g_target.load(std::memory_order_relaxed) != stream) {
    return read;
  }
  g_position += static_cast<uint64_t>(read);
  if (g_awaitingNickname >= 0 && read >= 4 && std::memcmp(buffer, "etex", 4) == 0) {
    const NodePatch &patch = kPatches[g_awaitingNickname];
    const std::string got(reinterpret_cast<const char *>(buffer),
                          strnlen(reinterpret_cast<const char *>(buffer), static_cast<size_t>(read)));
    if (got == patch.nickname) {
      Logger::Info(std::string("PssgPatch: aplicado e conferido (") + patch.what + ").");
    } else {
      g_mismatch.fetch_add(1);
      Logger::Warn(std::string("PssgPatch: trocado o no errado (") + got + ", esperado " +
                   patch.nickname + ").");
    }
    g_awaitingNickname = -1;
  }
  if (read == 12) {
    for (size_t i = 0; i < kPatchCount; ++i) {
      const NodePatch &patch = kPatches[i];
      if (std::memcmp(buffer, patch.header, 12) != 0) {
        continue;
      }
      if (g_seen[i]++ == patch.ordinal) {
        std::memcpy(buffer, patch.newType, 4);
        g_applied.fetch_add(1);
        g_awaitingNickname = static_cast<int>(i);
      }
    }
  }
  if (g_position >= uint64_t{kPersistentDbSize} + 8) {
    g_target.store(nullptr);
  }
  return read;
}

} // namespace

bool InstallPssgPatch() {
  const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
  void *target = reinterpret_cast<void *>(base + kStreamReadRva);
  if (base == 0 ||
      std::memcmp(target, kStreamReadPrologue, sizeof(kStreamReadPrologue)) != 0) {
    return false;
  }
  const MH_STATUS init = MH_Initialize();
  if (init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED) {
    return false;
  }
  if (MH_CreateHook(target, reinterpret_cast<void *>(&DetourRead),
                    reinterpret_cast<void **>(&g_originalRead)) != MH_OK) {
    return false;
  }
  if (MH_EnableHook(target) != MH_OK) {
    MH_RemoveHook(target);
    return false;
  }
  g_installed = true;
  return true;
}

void LogPssgPatchStatus() {
  if (!g_installed) {
    Logger::Warn("PssgPatch: leitura de PSSG diferente do esperado; sem patch.");
    return;
  }
  char line[160];
  std::snprintf(line, sizeof(line),
                "PssgPatch: ativo desde o attach; %u troca(s) aplicada(s), %u com bytes "
                "diferentes do esperado (%s).",
                g_applied.load(), g_mismatch.load(), kPatches[0].what);
  Logger::Info(line);
}

} // namespace dr2hook

#else

namespace dr2hook {
bool InstallPssgPatch() { return false; }
void LogPssgPatchStatus() {}
} // namespace dr2hook

#endif
