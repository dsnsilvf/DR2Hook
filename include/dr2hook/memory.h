#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <string_view>
#include <vector>

namespace dr2hook {

class IMemoryAccessor {
public:
  virtual ~IMemoryAccessor() = default;
  virtual bool Read(uintptr_t address, void *buffer, size_t size) = 0;
  virtual bool Write(uintptr_t address, const void *buffer, size_t size) = 0;
  virtual bool IsValidAddress(uintptr_t address) const = 0;
};

class DirectMemoryAccessor : public IMemoryAccessor {
public:
  bool Read(uintptr_t address, void *buffer, size_t size) override;
  bool Write(uintptr_t address, const void *buffer, size_t size) override;
  bool IsValidAddress(uintptr_t address) const override;
};

class MockMemoryAccessor : public IMemoryAccessor {
public:
  MockMemoryAccessor() = default;
  explicit MockMemoryAccessor(size_t size, uintptr_t baseAddress = 0x1000);

  void SetMemory(uintptr_t address, const void *buffer, size_t size);
  void SetMemory(uintptr_t address, const std::vector<uint8_t> &data);

  template <typename T> void SetValue(uintptr_t address, const T &value) {
    SetMemory(address, &value, sizeof(T));
  }

  void InvalidateAddress(uintptr_t address);
  void InvalidateRange(uintptr_t address, size_t size);
  void Clear();

  bool Read(uintptr_t address, void *buffer, size_t size) override;
  bool Write(uintptr_t address, const void *buffer, size_t size) override;
  bool IsValidAddress(uintptr_t address) const override;

private:
  std::map<uintptr_t, uint8_t> m_memory;
};

class MemoryScanner {
public:
  explicit MemoryScanner(IMemoryAccessor *accessor = nullptr);

  void SetAccessor(IMemoryAccessor *accessor);
  IMemoryAccessor *GetAccessor() const;

  uintptr_t FindPattern(uintptr_t startAddress, size_t searchSize,
                        std::string_view pattern);
  uintptr_t ResolvePointerChain(uintptr_t baseAddress,
                                const std::vector<uintptr_t> &offsets);

private:
  IMemoryAccessor *m_accessor = nullptr;
};

} // namespace dr2hook

using dr2hook::DirectMemoryAccessor;
using dr2hook::IMemoryAccessor;
using dr2hook::MemoryScanner;
using dr2hook::MockMemoryAccessor;
