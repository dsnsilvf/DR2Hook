// edits.json do Track Explorer (formato "dr2-track-edits", versão 1), que tools/uiview/track/edit.py
// aplica num .nefs novo. Especificação em docs/plans/viewer3d/formatos.md#pistaeditsjson.
#pragma once

#include "core/dr2i.hpp"
#include "core/track.hpp"

#include <string>

namespace dr2::edit {

// Uma entrada por instância movida ou apagada da rota (cópias não existem no viewer nativo).
// Floats com "%.9g": ida e volta exata de float32.
std::string edits_json(const Track& track, const Route& route, const Instances& inst, std::size_t* count = nullptr);

// Caminho dentro da pasta de instalação do jogo? (".../steamapps/common/DiRT Rally 2.0/..."); a
// comparação ignora maiúsculas e aceita barra invertida.
bool inside_game_folder(const std::string& path);

// Grava `text` em `path`, criando as pastas. Lança std::runtime_error se `path` está na pasta do jogo.
void write_text(const std::string& path, const std::string& text);

}  // namespace dr2::edit
