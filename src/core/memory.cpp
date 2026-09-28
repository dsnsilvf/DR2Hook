#include "dr2hook/memory.h"
#include "dr2hook/logger.h"

#if defined(_WIN32)
#include <windows.h>
#endif

#include <cctype>
#include <cstring>
#include <string>

namespace dr2hook {

// --- DirectMemoryAccessor ---

bool DirectMemoryAccessor::IsValidAddress(uintptr_t address) const {
  if (address < 0x10000) {
    return false;
  }
#if defined(_WIN32)
  MEMORY_BASIC_INFORMATION mbi{};
  if (VirtualQuery(reinterpret_cast<LPCVOID>(address), &mbi, sizeof(mbi)) ==
      0) {
    return false;
  }
  if (mbi.State != MEM_COMMIT || (mbi.Protect & PAGE_NOACCESS) ||
      (mbi.Protect & PAGE_GUARD)) {
    return false;
  }
#endif
  return true;
}

bool DirectMemoryAccessor::Read(uintptr_t address, void *buffer, size_t size) {
  if (!IsValidAddress(address) || buffer == nullptr || size == 0) {
    return false;
  }
  std::memcpy(buffer, reinterpret_cast<const void *>(address), size);
  return true;
}

bool DirectMemoryAccessor::Write(uintptr_t address, const void *buffer,
                                 size_t size) {
  if (!IsValidAddress(address) || buffer == nullptr || size == 0) {
    return false;
  }
#if defined(_WIN32)
  DWORD oldProtect = 0;
  if (VirtualProtect(reinterpret_cast<LPVOID>(address), size,
                     PAGE_EXECUTE_READWRITE, &oldProtect)) {
    std::memcpy(reinterpret_cast<void *>(address), buffer, size);
    VirtualProtect(reinterpret_cast<LPVOID>(address), size, oldProtect,
                   &oldProtect);
    return true;
  }
#endif
  std::memcpy(reinterpret_cast<void *>(address), buffer, size);
  return true;
}

// --- MockMemoryAccessor ---

MockMemoryAccessor::MockMemoryAccessor(size_t size, uintptr_t baseAddress) {
  if (size > 0 && baseAddress != 0) {
    for (size_t i = 0; i < size; ++i) {
      m_memory[baseAddress + i] = 0;
    }
  }
}

void MockMemoryAccessor::SetMemory(uintptr_t address, const void *buffer,
                                   size_t size) {
  if (address == 0 || buffer == nullptr || size == 0) {
    return;
  }
  const auto *bytes = static_cast<const uint8_t *>(buffer);
  for (size_t i = 0; i < size; ++i) {
    m_memory[address + i] = bytes[i];
  }
}

void MockMemoryAccessor::SetMemory(uintptr_t address,
                                   const std::vector<uint8_t> &data) {
  SetMemory(address, data.data(), data.size());
}

void MockMemoryAccessor::InvalidateAddress(uintptr_t address) {
  m_memory.erase(address);
}

void MockMemoryAccessor::InvalidateRange(uintptr_t address, size_t size) {
  for (size_t i = 0; i < size; ++i) {
    m_memory.erase(address + i);
  }
}

void MockMemoryAccessor::Clear() { m_memory.clear(); }

bool MockMemoryAccessor::IsValidAddress(uintptr_t address) const {
  if (address == 0) {
    return false;
  }
  return m_memory.find(address) != m_memory.end();
}

bool MockMemoryAccessor::Read(uintptr_t address, void *buffer, size_t size) {
  if (address == 0 || buffer == nullptr || size == 0) {
    return false;
  }
  for (size_t i = 0; i < size; ++i) {
    if (m_memory.find(address + i) == m_memory.end()) {
      return false;
    }
  }
  auto *out = static_cast<uint8_t *>(buffer);
  for (size_t i = 0; i < size; ++i) {
    out[i] = m_memory.at(address + i);
  }
  return true;
}

bool MockMemoryAccessor::Write(uintptr_t address, const void *buffer,
                               size_t size) {
  if (address == 0 || buffer == nullptr || size == 0) {
    return false;
  }
  for (size_t i = 0; i < size; ++i) {
    if (m_memory.find(address + i) == m_memory.end()) {
      return false;
    }
  }
  const auto *in = static_cast<const uint8_t *>(buffer);
  for (size_t i = 0; i < size; ++i) {
    m_memory[address + i] = in[i];
  }
  return true;
}

// --- MemoryScanner ---

MemoryScanner::MemoryScanner(IMemoryAccessor *accessor)
    : m_accessor(accessor) {}

void MemoryScanner::SetAccessor(IMemoryAccessor *accessor) {
  m_accessor = accessor;
}

IMemoryAccessor *MemoryScanner::GetAccessor() const { return m_accessor; }

struct PatternToken {
  uint8_t value = 0;
  bool isWildcard = false;
};

static std::vector<PatternToken> ParsePattern(std::string_view pattern) {
  std::vector<PatternToken> tokens;
  size_t i = 0;
  while (i < pattern.size()) {
    while (i < pattern.size() && (pattern[i] == ' ' || pattern[i] == '\t')) {
      ++i;
    }
    if (i >= pattern.size()) {
      break;
    }

    size_t start = i;
    while (i < pattern.size() && pattern[i] != ' ' && pattern[i] != '\t') {
      ++i;
    }
    std::string_view token = pattern.substr(start, i - start);

    if (token == "?" || token == "??") {
      tokens.push_back({0, true});
    } else if (token.size() <= 2) {
      bool validHex = true;
      for (char c : token) {
        if (!std::isxdigit(static_cast<unsigned char>(c))) {
          validHex = false;
          break;
        }
      }
      if (!validHex) {
        return {};
      }
      auto val =
          static_cast<uint8_t>(std::stoul(std::string(token), nullptr, 16));
      tokens.push_back({val, false});
    } else {
      return {};
    }
  }
  return tokens;
}

uintptr_t MemoryScanner::FindPattern(uintptr_t startAddress, size_t searchSize,
                                     std::string_view pattern) {
  if (m_accessor == nullptr) {
    Logger::Warn("MemoryScanner::FindPattern: accessor nulo.");
    return 0;
  }
  if (startAddress == 0 || searchSize == 0 || pattern.empty()) {
    Logger::Warn("MemoryScanner::FindPattern: parametros invalidos.");
    return 0;
  }
  if (!m_accessor->IsValidAddress(startAddress)) {
    Logger::Warn("MemoryScanner::FindPattern: endereco inicial invalido.");
    return 0;
  }

  std::vector<PatternToken> tokens = ParsePattern(pattern);
  if (tokens.empty()) {
    Logger::Warn("MemoryScanner::FindPattern: padrao invalido ou vazio.");
    return 0;
  }
  if (searchSize < tokens.size()) {
    Logger::Warn("MemoryScanner::FindPattern: searchSize menor que padrao.");
    return 0;
  }

  std::vector<uint8_t> buffer(searchSize);
  if (!m_accessor->Read(startAddress, buffer.data(), searchSize)) {
    Logger::Warn("MemoryScanner::FindPattern: falha na leitura da memoria.");
    return 0;
  }

  const size_t maxOffset = searchSize - tokens.size();
  for (size_t offset = 0; offset <= maxOffset; ++offset) {
    bool match = true;
    for (size_t j = 0; j < tokens.size(); ++j) {
      if (!tokens[j].isWildcard && buffer[offset + j] != tokens[j].value) {
        match = false;
        break;
      }
    }
    if (match) {
      return startAddress + offset;
    }
  }

  return 0;
}

uintptr_t
MemoryScanner::ResolvePointerChain(uintptr_t baseAddress,
                                   const std::vector<uintptr_t> &offsets) {
  if (m_accessor == nullptr) {
    Logger::Warn("MemoryScanner::ResolvePointerChain: accessor nulo.");
    return 0;
  }
  if (baseAddress == 0 || !m_accessor->IsValidAddress(baseAddress)) {
    Logger::Warn("MemoryScanner::ResolvePointerChain: endereco base invalido.");
    return 0;
  }

  uintptr_t current = baseAddress;
  for (size_t i = 0; i < offsets.size(); ++i) {
    if (current == 0 || !m_accessor->IsValidAddress(current)) {
      Logger::Warn(
          "MemoryScanner::ResolvePointerChain: ponteiro invalido na cadeia.");
      return 0;
    }

    uintptr_t pointerValue = 0;
    if (!m_accessor->Read(current, &pointerValue, sizeof(pointerValue))) {
      Logger::Warn("MemoryScanner::ResolvePointerChain: falha na leitura do "
                   "ponteiro na cadeia.");
      return 0;
    }

    if (pointerValue == 0) {
      Logger::Warn("MemoryScanner::ResolvePointerChain: ponteiro nulo "
                   "encontrado na cadeia.");
      return 0;
    }

    current = pointerValue + offsets[i];
  }

  if (current == 0 || !m_accessor->IsValidAddress(current)) {
    Logger::Warn("MemoryScanner::ResolvePointerChain: endereco final resolvido "
                 "invalido.");
    return 0;
  }

  return current;
}

} // namespace dr2hook
