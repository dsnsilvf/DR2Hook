#pragma once

namespace dr2hook {

// Som do jogo inteiro (música, efeitos, o bipe da pista pronta): muta a sessão
// de áudio padrão do processo (ISimpleAudioVolume), onde o XAudio2 do Wwise
// toca. Vale para sons criados depois, no Windows e no Wine. Usado pela tela
// preta da inicialização rápida (LoadCover).
class SessionAudio {
public:
  // Roda numa thread curta com COM próprio (não trava o frame); a última
  // chamada vence.
  static void SetMuted(bool muted);
  // Espera as threads em andamento (o core pode ser descarregado pelo F8).
  static void Shutdown();
};

} // namespace dr2hook
