// Acerto do carro para Testar no jogo > Acerto. As faixas vêm do próprio jogo: a dxgi anota min, máx, passos e
// padrão quando o carro carrega (<jogo>/dr2hook_tuning/<carro>.json) e o ring_deploy.py copia para
// <viewer3d>/cars/tuning/. Os dados do carro são cifrados no .nefs: sem um teste com o carro, não há faixas.
// RE em docs/reverse_engineering/vehicle_setup.md, "Acerto do carro".
#pragma once

#include "core/json.hpp"

#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace dr2 {

struct TuningParam {
    std::string name;  // "brakes_bias": a chave tuning_<nome> do dr2hook_autostage.ini
    int state = 0;     // 1 ajustável, 0 o carro não tem, 2 fixo
    float def = 0.0f, min = 0.0f, max = 0.0f;
    int steps = 0;     // valores possíveis de min a max, passo igual
    bool adjustable() const { return state == 1 && steps >= 2 && max > min; }
    float step() const { return (max - min) / static_cast<float>(steps - 1); }
    int index(float v) const;  // passo mais perto de v (0..steps-1)
    float at(int k) const;     // valor do passo k
};

struct CarTuning {
    std::string car;
    std::vector<TuningParam> params;  // na ordem do jogo (51)
    const TuningParam* find(std::string_view name) const;
};

// Lança std::runtime_error se o documento não tem o formato (versão 1).
CarTuning parse_car_tuning(const json::Value& doc);
CarTuning load_car_tuning(const std::string& path);

// Grupos da tela, na ordem em que aparecem; tuning_group(nome) devolve o índice.
constexpr int kTuningGroups = 10;
int tuning_group(std::string_view name);
const char* tuning_group_name(int group);  // no idioma da interface
// Nome prático ("Distribuição para a frente") e o valor na unidade de quem acerta carro ("75% frente", "-40 mm").
std::string tuning_label(std::string_view name);
std::string tuning_value_text(std::string_view name, float v);

// O que o usuário mudou num carro (só o que difere do padrão do jogo).
using TuningValues = std::map<std::string, float>;
// "brakes_bias=0.6,gearbox_1=0.4" (o --tuning do ring_deploy.py); vazio = padrão do jogo.
std::string tuning_arg(const TuningValues& values);
// O contrário; false (e `out` como estava) se algum item não é nome=número.
bool parse_tuning_arg(std::string_view text, TuningValues& out);

}  // namespace dr2
