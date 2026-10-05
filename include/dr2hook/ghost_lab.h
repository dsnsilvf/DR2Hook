#pragma once

#include <cstdint>
#include <string>

namespace dr2hook {

// Carros fantasma (docs/reverse_engineering/ghosts.md). Hooks no core, para
// iterar com F8: EvaluateGhostState (posicao de cada fantasma a cada frame)
// e o packer de GhostCarValues (opacidade do fantasma solido).
class GhostLab {
public:
  static bool Install(uintptr_t gameBase);
  static void Shutdown();

  // Chamado a cada frame pelo core: projeta jogador e fantasma no trajeto
  // gravado e atualiza a diferenca.
  static void Update();

  // Eventos de especial (core_module). Carregar e largar desligam o
  // experimento de colisao com os fantasmas.
  static void OnStageLoad();
  static void OnStageStart();

  // Sem tecla (F11 virou insta crash): liga/desliga a colisao com os fantasmas (aplicada no proximo
  // frame da especial; pausar desliga). Devolve o novo estado.
  static bool ToggleCollision();

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

  // Teste de limite (F7): pede mais uma copia a cada chamada (mesmo
  // espacamento da ultima, 1 s se nunca houve) e registra no log a contagem
  // antes de aplicar, para saber quantas existiam se o jogo cair. A conta (copias existentes + 1) e feita na
  // thread do jogo, entao o Reiniciar zera sozinha.
  static int SpawnClone();

  // Texto do ultimo F7 (copias de dados x carros desenhados), uma vez.
  static bool TakeSpawnNotice(std::string &out);

  // Fantasma solido na hora: GhostCarValues.x volta a 1, o fator de
  // esmaecimento fica abaixo de 1 (com 1,0 o desenho descarta o carro colado
  // no jogador) e, so durante a submissao do desenho, o tipo do fantasma
  // entra no passe opaco. Desligar devolve o esmaecimento.
  static void SetOpaque(bool opaque);
  static bool IsOpaque();

  // Soma ao tempo de todos os fantasmas (global do jogo, 0 por padrao):
  // > 0 adianta, < 0 atrasa.
  static bool SetTimeOffset(float seconds);

  static void SetHudVisible(bool visible);
  static bool IsHudVisible();
};

} // namespace dr2hook
