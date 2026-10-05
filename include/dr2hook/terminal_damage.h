#pragma once

#include <cstdint>
#include <string>

namespace dr2hook {

// Dano terminal (carro destruido), docs/reverse_engineering/terminal_damage.md.
// Hook no tick do TerminalDamageController so para achar o controlador vivo.
class TerminalDamage {
public:
  static bool Install(uintptr_t gameBase);
  static void Shutdown();

  enum class Result {
    Done,         // motivo gravado; o tick seguinte destroi o carro
    Blocked,      // SafetyGuard nao libera escrita (evento online)
    NoController, // tick nao rodou ha pouco: fora de especial
    BadChain,     // cadeia de ponteiros invalida
    AlreadyDown,  // motivo ja diferente de 7
  };

  // Insta crash: grava o motivo de impacto (4) no componente do carro.
  static Result Crash();

  // Chamado a cada frame. Depois do insta crash, registra no log (so leitura)
  // a pilha de estados do fluxo sempre que ela mudar, por 40 s.
  static void Update();

  // Esc durante a cutscene/freeze do dano terminal: posta o evento `pause`
  // (nenhum estado do dano o produz). Devolve true se postou.
  static bool RequestPause();

  // Pilha de estados do fluxo (base -> topo), texto para o log e os comandos.
  static std::string FlowStack();
  // Pede a transicao `name` ao estado do topo, se ele for uma tela. Devolve o
  // resultado em texto (comeca com "ok" quando postou).
  // Pausa a corrida (topo = StateRace).
  static std::string PostPause();
  static std::string PostLink(const std::string &name);
};

} // namespace dr2hook
