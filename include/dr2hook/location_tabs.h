#pragma once

#include <cstdint>

namespace dr2hook {

// Terceira aba "DR2Hook" na barra de modos (F3/F4) da tela "Selecionar local".
// Os locais criados pelo DR2Hook (id de location acima dos do jogo) vão para o
// fim da lista e ganham uma aba própria; o F4 do jogo passa por ela sem mudança
// nenhuma. A aba e o selo do card usam icon_dis_dr2hook(_small), que a overlay
// acrescenta em frontend/bundles/b_persistent.pssg
// (scripts/research/discipline_badge.py). Doc: docs/reverse_engineering/location_tabs.md.
class LocationTabs {
public:
  static void Install(uintptr_t gameBase);
  // Desfaz os patches antes do core ser descarregado (F8).
  static void Shutdown();
};

} // namespace dr2hook
