#pragma once

#include "dr2hook/bxml.h"

#include <cstddef>
#include <string>

// Acréscimos aos documentos de UI do boot para abrir uma tela própria a partir
// do menu de pausa. O jogo interpreta states.bin, flow.bin e screens.bin em
// ordem variável, então cada função depende só do próprio documento: o id do
// estado é fixo e o link vai para todo nó com link `options`. Cada função só
// altera a árvore quando consegue aplicar tudo; em caso de falha a árvore fica
// como estava e `error` diz o motivo.
namespace dr2hook::ui_patch {

inline constexpr char kScreenName[] = "dr2modloader";
inline constexpr char kPauseEvent[] = "dr2modloader";
inline constexpr char kOverlayEvent[] = "dr2modloader_open_overlay";
inline constexpr char kLabel[] = "DR2 ModLoader";
inline constexpr char kStateId[] = "1146224640"; // 0x44520000

bool PatchStates(bxml::Node &root, std::string &error);
bool PatchFlow(bxml::Node &root, size_t &linkedNodes, std::string &error);
bool PatchScreens(bxml::Node &root, std::string &error);

} // namespace dr2hook::ui_patch
