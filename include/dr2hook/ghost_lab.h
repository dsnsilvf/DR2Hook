#pragma once

#include <cstdint>

namespace dr2hook {

// Carros fantasma (docs/reverse_engineering/ghosts.md). Hooks no core, para
// iterar com F8: EvaluateGhostState (posicao de cada fantasma a cada frame)
// e a troca de materiais do carro fantasma (opaco).
class GhostLab {
public:
  static bool Install(uintptr_t gameBase);
  static void Shutdown();

  // Chamado a cada frame pelo core: projeta jogador e fantasma no trajeto
  // gravado e atualiza a diferenca.
  static void Update();

  struct Status {
    bool active = false;      // fantasma avaliado ha pouco e trajeto lido
    int slots = 0;            // slots no gerenciador
    int readySlots = 0;       // slots com dados (estado 2)
    float lapSeconds = 0.f;   // ultima amostra de posicao do trajeto
    float ghostSeconds = 0.f; // onde o fantasma esta no trajeto agora
    float playerSeconds = 0.f; // quando o fantasma passou onde o jogador esta
    float deltaSeconds = 0.f; // ghost - player: > 0 = jogador atras
    float gapMeters = 0.f;    // distancia no trajeto: > 0 = fantasma a frente
    float offTrackMeters = 0.f; // jogador ate o trajeto (confianca)
  };
  static Status GetStatus();

  // Copia o fantasma pronto (1o slot com dados) para os `count` slots
  // seguintes, cada um `stepSeconds` mais atrasado (k * stepSeconds). Feito
  // na thread do jogo, no proximo EvaluateGhostState. count = 0 desliga os
  // clones (estado do slot volta a 0).
  static void RequestClones(int count, float stepSeconds);

  // Pula a troca para os materiais *_ghost na criacao do carro fantasma:
  // carro solido e com sombra. Vale a partir do proximo carregamento.
  static void SetOpaque(bool opaque);
  static bool IsOpaque();

  // Soma ao tempo de todos os fantasmas (global do jogo, 0 por padrao):
  // > 0 adianta, < 0 atrasa.
  static bool SetTimeOffset(float seconds);

  static void SetHudVisible(bool visible);
  static bool IsHudVisible();
};

} // namespace dr2hook
