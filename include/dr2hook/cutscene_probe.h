#pragma once

#include <cstdint>

namespace dr2hook {

// Hooks dos roteiros de cutscene (largada). Ficam no core para iterar com F8:
// Install no Core_Initialize, Shutdown no Core_Shutdown (espera as chamadas em
// andamento antes da DLL ser descarregada).
class CutsceneProbe {
public:
  static bool Install(uintptr_t gameBase);
  static void Shutdown();

  enum class StartMode {
    Normal,      // jogo padrao: segurar freio de mao + 5 luzes
    NoCountdown, // segurar freio de mao, largada sem luzes
    Automatic,   // larga sozinho assim que o carro aparece na linha
    OnThrottle,  // larga ao pisar no acelerador
  };

  // Fora de Normal, a largada (StartLights com <Start>) e imediata, sem
  // contagem nem fase so-acelerador, e o OSD de tempo do rival que segura o
  // staging e encurtado. Voltar para Normal devolve os tempos originais.
  static void SetStartMode(StartMode mode);
  static StartMode GetStartMode();
  // "normal", "no_countdown", "automatic", "on_throttle"; false se invalido.
  static bool SetStartMode(const char *name);
};

} // namespace dr2hook
