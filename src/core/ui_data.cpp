#include "dr2hook/ui_data.h"
#include "dr2hook/bxml.h"
#include "dr2hook/host.h"
#include "dr2hook/ui_patch.h"

#include <MinHook.h>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <mutex>
#include <string>
#include <vector>

#if defined(_MSC_VER)
#include <intrin.h>
#define DR2HOOK_RETURN_ADDRESS() _ReturnAddress()
#else
#define DR2HOOK_RETURN_ADDRESS() __builtin_return_address(0)
#endif

namespace dr2hook {
namespace {

constexpr uintptr_t kImageBase = 0x140000000;
constexpr uintptr_t kLoadDocumentRva = 0x140c54270 - kImageBase;
constexpr uintptr_t kJobCallReturnRva = 0x140c62176 - kImageBase;
constexpr uint8_t kLoadDocumentBytes[] = {
    0x48, 0x89, 0x5c, 0x24, 0x20, 0x56, 0x48, 0x83, 0xec, 0x30, 0x48, 0x89,
    0x6c, 0x24, 0x40, 0x48, 0x8b, 0xf2, 0x4c, 0x89, 0x74, 0x24, 0x50};
constexpr uint8_t kJobCallBytes[] = {0xe8};

// O handle do documento fica em job+0x2a0; o caminho em job+0x50 e o userdata
// em job+0x170 são char*.
constexpr uintptr_t kJobHandleOffset = 0x2a0;
constexpr uintptr_t kJobPathOffset = 0x50;
constexpr uintptr_t kJobUserdataOffset = 0x170;

constexpr size_t kMaxRecords = 64;
constexpr size_t kTextSize = 96;
constexpr size_t kNoteSize = 160;
constexpr wchar_t kDisableFile[] = L"dr2hook_ui_patch.disabled";

enum class Document { Other, States, Flow, Screens };

struct Record {
  char path[kTextSize];
  char userdata[kTextSize];
  char note[kNoteSize];
  size_t size;
  uint32_t magic;
  DWORD tick;
  std::atomic<bool> ready;
  std::atomic<bool> logged;
};

using LoadDocumentFn = int (*)(void *handle, void *allocator, const void *buffer,
                               size_t size);

uintptr_t g_base = 0;
LoadDocumentFn g_originalLoadDocument = nullptr;
DWORD g_installTick = 0;
Record g_records[kMaxRecords];
std::atomic<size_t> g_recordCount{0};
std::atomic<size_t> g_otherCalls{0};
std::atomic<bool> g_logReady{false};
bool g_patchEnabled = true;
std::mutex g_patchMutex;

enum class Outcome { Pending, Patched, Kept };

bool IsReadable(uintptr_t address, size_t size) {
  if (address < 0x10000 || address + size < address) {
    return false;
  }
  MEMORY_BASIC_INFORMATION info{};
  if (VirtualQuery(reinterpret_cast<const void *>(address), &info,
                   sizeof(info)) == 0) {
    return false;
  }
  constexpr DWORD kReadable = PAGE_READONLY | PAGE_READWRITE |
                              PAGE_WRITECOPY | PAGE_EXECUTE_READ |
                              PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
  const auto regionEnd =
      reinterpret_cast<uintptr_t>(info.BaseAddress) + info.RegionSize;
  return info.State == MEM_COMMIT && (info.Protect & kReadable) != 0 &&
         (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) == 0 &&
         address + size <= regionEnd;
}

// Copia até o NUL sem passar do fim da região legível.
void CopyText(uintptr_t pointerSlot, char *out) {
  out[0] = '\0';
  if (!IsReadable(pointerSlot, sizeof(uintptr_t))) {
    return;
  }
  uintptr_t text = 0;
  std::memcpy(&text, reinterpret_cast<const void *>(pointerSlot), sizeof(text));
  for (size_t i = 0; i + 1 < kTextSize; ++i) {
    if (!IsReadable(text + i, 1)) {
      out[i] = '\0';
      return;
    }
    out[i] = reinterpret_cast<const char *>(text)[i];
    if (out[i] == '\0') {
      return;
    }
  }
  out[kTextSize - 1] = '\0';
}

void LogRecord(Record &record) {
  if (!record.ready.load(std::memory_order_acquire) ||
      record.logged.exchange(true)) {
    return;
  }
  char line[480];
  std::snprintf(line, sizeof(line),
                "UiData: documento '%s' userdata='%s' tamanho=%zu "
                "magia=0x%08x t=+%lums%s%s",
                record.path, record.userdata, record.size,
                static_cast<unsigned>(record.magic),
                static_cast<unsigned long>(record.tick - g_installTick),
                record.note[0] != '\0' ? " | " : "", record.note);
  HostLog(line);
}

Document Classify(const char *path) {
  if (std::strcmp(path, "/data/system/states.bin") == 0) {
    return Document::States;
  }
  if (std::strcmp(path, "/data/system/flow.bin") == 0) {
    return Document::Flow;
  }
  if (std::strcmp(path, "/data/system/screens.bin") == 0) {
    return Document::Screens;
  }
  return Document::Other;
}

Outcome &OutcomeOf(Document document) {
  static Outcome outcomes[4] = {};
  return outcomes[static_cast<size_t>(document)];
}

// A ordem entre os documentos muda a cada boot. Um link para o estado novo
// só é seguro se states.bin não foi mantido original; a tela só serve se o
// link existe ou ainda pode existir.
const char *BlockedBy(Document document) {
  const bool statesKept = OutcomeOf(Document::States) == Outcome::Kept;
  const bool flowKept = OutcomeOf(Document::Flow) == Outcome::Kept;
  if (document == Document::Flow && statesKept) {
    return "states.bin foi mantido original";
  }
  if (document == Document::Screens && (statesKept || flowKept)) {
    return statesKept ? "states.bin foi mantido original"
                      : "flow.bin foi mantido original";
  }
  return nullptr;
}

// Chamado com g_patchMutex travado.
const std::vector<uint8_t> *PatchLocked(Document document, const uint8_t *bytes,
                                        size_t payload, char *note) {
  try {
    bxml::Node root;
    if (!bxml::Decode(bytes, payload, root)) {
      std::snprintf(note, kNoteSize, "original mantido: decodificacao falhou");
      return nullptr;
    }
    std::string error;
    size_t linkedNodes = 0;
    bool ok = false;
    switch (document) {
    case Document::States:
      ok = ui_patch::PatchStates(root, error);
      break;
    case Document::Flow:
      ok = ui_patch::PatchFlow(root, linkedNodes, error);
      break;
    case Document::Screens:
      ok = ui_patch::PatchScreens(root, error);
      break;
    case Document::Other:
      return nullptr;
    }
    if (!ok) {
      std::snprintf(note, kNoteSize, "original mantido: %s", error.c_str());
      return nullptr;
    }
    auto *patched = new std::vector<uint8_t>(bxml::Encode(root));
    patched->push_back(0);
    if (document == Document::States) {
      std::snprintf(note, kNoteSize, "alterado: estado %s, %zu bytes",
                    ui_patch::kStateId, patched->size());
    } else if (document == Document::Flow) {
      std::snprintf(note, kNoteSize,
                    "alterado: link em %zu no(s) com options, %zu bytes",
                    linkedNodes, patched->size());
    } else {
      std::snprintf(note, kNoteSize, "alterado: tela dr2modloader, %zu bytes",
                    patched->size());
    }
    return patched;
  } catch (const std::exception &e) {
    std::snprintf(note, kNoteSize, "original mantido: excecao %s", e.what());
  }
  return nullptr;
}

// Devolve o documento alterado, ou nullptr para o jogo seguir com o original.
// O buffer devolvido nunca é liberado: o documento do jogo aponta para ele.
const std::vector<uint8_t> *Patch(Document document, const void *buffer,
                                  size_t size, char *note) {
  const auto *bytes = static_cast<const uint8_t *>(buffer);
  const size_t payload = size > 0 && bytes[size - 1] == 0 ? size - 1 : size;
  std::lock_guard<std::mutex> lock(g_patchMutex);
  const std::vector<uint8_t> *patched = nullptr;
  if (!g_patchEnabled) {
    std::snprintf(note, kNoteSize, "patch desligado por %ls", kDisableFile);
  } else if (const char *reason = BlockedBy(document)) {
    std::snprintf(note, kNoteSize, "original mantido: %s", reason);
  } else {
    patched = PatchLocked(document, bytes, payload, note);
    if (patched == nullptr && document == Document::States &&
        OutcomeOf(Document::Flow) == Outcome::Patched) {
      const size_t used = std::strlen(note);
      std::snprintf(note + used, kNoteSize - used,
                    " | INCONSISTENTE: flow.bin ja aponta para o estado %s",
                    ui_patch::kStateId);
    }
  }
  OutcomeOf(document) = patched != nullptr ? Outcome::Patched : Outcome::Kept;
  return patched;
}

int DetourLoadDocument(void *handle, void *allocator, const void *buffer,
                       size_t size) {
  if (reinterpret_cast<uintptr_t>(DR2HOOK_RETURN_ADDRESS()) !=
      g_base + kJobCallReturnRva) {
    g_otherCalls.fetch_add(1, std::memory_order_relaxed);
    return g_originalLoadDocument(handle, allocator, buffer, size);
  }

  const auto job = reinterpret_cast<uintptr_t>(handle) - kJobHandleOffset;
  char path[kTextSize];
  CopyText(job + kJobPathOffset, path);
  const Document document = Classify(path);
  char note[kNoteSize] = "";
  const std::vector<uint8_t> *patched =
      document != Document::Other && buffer != nullptr
          ? Patch(document, buffer, size, note)
          : nullptr;

  const size_t index = g_recordCount.fetch_add(1);
  if (index < kMaxRecords) {
    Record &record = g_records[index];
    std::memcpy(record.path, path, sizeof(path));
    CopyText(job + kJobUserdataOffset, record.userdata);
    std::memcpy(record.note, note, sizeof(note));
    record.size = size;
    record.magic = 0;
    if (buffer != nullptr && size >= sizeof(record.magic)) {
      std::memcpy(&record.magic, buffer, sizeof(record.magic));
    }
    record.tick = GetTickCount();
    record.ready.store(true, std::memory_order_release);
    if (g_logReady.load(std::memory_order_acquire)) {
      LogRecord(record);
    }
  }
  if (patched != nullptr) {
    return g_originalLoadDocument(handle, allocator, patched->data(),
                                  patched->size());
  }
  return g_originalLoadDocument(handle, allocator, buffer, size);
}

bool Matches(uintptr_t rva, const uint8_t *expected, size_t size) {
  return std::memcmp(reinterpret_cast<const void *>(g_base + rva), expected,
                     size) == 0;
}

} // namespace

bool InstallUiDataHook(HMODULE hostModule) {
  g_base = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
  g_installTick = GetTickCount();
  wchar_t flag[MAX_PATH];
  const DWORD length = GetModuleFileNameW(hostModule, flag, MAX_PATH);
  if (length > 0 && length < MAX_PATH) {
    std::wstring path(flag, length);
    path.resize(path.find_last_of(L"\\/") + 1);
    path += kDisableFile;
    g_patchEnabled = GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES;
  }
  if (g_base == 0 ||
      !Matches(kLoadDocumentRva, kLoadDocumentBytes,
               sizeof(kLoadDocumentBytes)) ||
      !Matches(kJobCallReturnRva - 5, kJobCallBytes, sizeof(kJobCallBytes))) {
    return false;
  }
  const MH_STATUS init = MH_Initialize();
  if (init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED) {
    return false;
  }
  void *target = reinterpret_cast<void *>(g_base + kLoadDocumentRva);
  return MH_CreateHook(target, reinterpret_cast<void *>(&DetourLoadDocument),
                       reinterpret_cast<void **>(&g_originalLoadDocument)) ==
             MH_OK &&
         MH_EnableHook(target) == MH_OK;
}

void StartUiDataLog() {
  if (g_originalLoadDocument == nullptr) {
    HostLog("UiData: hook de documentos inativo (executavel diferente do "
            "esperado ou falha do MinHook).");
    return;
  }
  g_logReady.store(true, std::memory_order_release);
  const size_t seen = g_recordCount.load();
  char line[160];
  std::snprintf(line, sizeof(line),
                "UiData: hook de documentos ativo desde o attach; %zu "
                "documento(s) de job antes do log, %zu chamada(s) de outra "
                "origem.",
                seen, g_otherCalls.load());
  HostLog(line);
  for (size_t i = 0; i < seen && i < kMaxRecords; ++i) {
    LogRecord(g_records[i]);
  }
}

} // namespace dr2hook
