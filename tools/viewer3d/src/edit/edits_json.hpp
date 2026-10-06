// edits.json do Track Explorer (formato "dr2-track-edits", versão 1), que tools/uiview/track/edit.py
// aplica num .nefs novo. Especificação em docs/plans/viewer3d/formatos.md#pistaeditsjson.
#pragma once

#include "core/dr2i.hpp"
#include "core/json.hpp"
#include "core/track.hpp"

#include <string>
#include <vector>

namespace dr2::edit {

struct RouteEdits {
    const Route* route;
    const Instances* inst;
};

// Uma entrada por instância movida ou apagada, e uma por cópia visível (added, src = idnum copiado,
// index = -1), de todas as rotas dadas (como tvEditList, que junta as rotas abertas).
// Floats com "%.9g": ida e volta exata de float32.
std::string edits_json(const Track& track, const std::vector<RouteEdits>& routes, std::size_t* count = nullptr);
inline std::string edits_json(const Track& track, const Route& route, const Instances& inst, std::size_t* count = nullptr) {
    return edits_json(track, std::vector<RouteEdits>{{&route, &inst}}, count);
}

// Leitura de um edits.json (de volta para a sessão): o que aplicou e o que ficou de fora, com o motivo.
struct ApplyReport {
    std::size_t applied = 0;
    std::vector<std::string> skipped;
};

// Entradas do edits.json `doc` para a rota `route`, aplicadas em `inst`: movidas e apagadas pelo idnum
// (o "index") e pelo tipo; cópias (added) recriadas no fim a partir do `src`. Uma entrada cujo m0 não
// bate com o arquivo (a pista foi exportada de novo) fica de fora. Lança std::runtime_error se `doc`
// não é um "dr2-track-edits" v1 desta pista.
void apply_edits(const json::Value& doc, const Track& track, const Route& route, Instances& inst, ApplyReport& report);

// Rotas com alguma entrada no edits.json.
std::vector<std::string> routes_in_edits(const json::Value& doc);

// Caminho dentro da pasta de instalação do jogo? (".../steamapps/common/DiRT Rally 2.0/..."); a
// comparação ignora maiúsculas e aceita barra invertida.
bool inside_game_folder(const std::string& path);

// Grava `text` em `path`, criando as pastas: escreve em `path`.tmp e renomeia, então uma falha não
// estraga o arquivo que já existia. Lança std::runtime_error se `path` está na pasta do jogo ou se a
// gravação falha.
void write_text(const std::string& path, const std::string& text);

// Se `path` existe, copia para `path`.<n>.bak (o primeiro n livre) e devolve o nome da cópia; senão "".
std::string backup_existing(const std::string& path);

}  // namespace dr2::edit
