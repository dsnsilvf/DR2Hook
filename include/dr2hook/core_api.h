#pragma once

#include "common.h"

#include <dxgi.h>

namespace dr2hook {

inline constexpr unsigned int kCoreAbiVersion = 1;

// ABI estável entre a dxgi.dll residente e a dr2hook_core.dll recarregável.
// Ponteiros crus só: cada DLL tem a própria libstdc++.
struct Dr2CoreApi {
  unsigned int abiVersion;
  int (*Initialize)(int truncateLog);
  void (*Shutdown)();
  void (*OnFrame)(IDXGISwapChain *swapChain, HWND hwnd, double deltaTime);
  int (*OnWndProc)(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
};

enum Dr2MenuOptionKind : int {
  kDr2MenuToggle = 0,
  kDr2MenuChoice = 1,
  kDr2MenuButton = 2,
};

// Uma opção de mod na tela nativa. `values` são os textos do combo
// ({"Off", "On"} no toggle; vazio no botão) e `index` o valor atual.
struct Dr2MenuOption {
  const char *label;
  const char *const *values;
  int valueCount;
  int index;
  int kind;
};

// Um mod na tela nativa. A proxy copia tudo em Dr2Host_NativeMenuPublish; os
// ponteiros só precisam valer durante a chamada.
struct Dr2MenuMod {
  const char *name;
  const char *description;
  const Dr2MenuOption *options;
  int optionCount;
};

} // namespace dr2hook

// Exports da dxgi.dll que o core chama por GetProcAddress. `value` do evento é
// o índice novo escolhido no combo, ou -1 quando a linha foi selecionada (A).
using Dr2HostMenuPublishFn = void (*)(const dr2hook::Dr2MenuMod *mods,
                                      int count);
using Dr2HostMenuConsumeEventFn = int (*)(int *modIndex, int *optionIndex,
                                          int *value);
