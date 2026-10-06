// Testes do dr2edit (histórico, giro, edits.json), sem GL. Executável simples com asserts.
#undef NDEBUG
#include "core/json.hpp"
#include "edit/edits_json.hpp"
#include "edit/history.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace dr2;

namespace {

int checks = 0;

void check(bool ok, const char* what) {
    ++checks;
    if (!ok) {
        std::fprintf(stderr, "FALHOU: %s\n", what);
        std::abort();
    }
}

Instances make_instances(std::uint32_t n) {
    Instances inst;
    inst.n = n;
    for (std::uint32_t i = 0; i < n; ++i) {
        inst.type.push_back(static_cast<std::uint16_t>(i % 3));
        inst.idnum.push_back(100 + i);  // idnum diferente da posição: o edits.json usa o idnum
        const float m[12] = {1, 0, 0, 0, 1, 0, 0, 0, 1, 10.0f * static_cast<float>(i), 1430.25f, -430.125f};
        inst.m.insert(inst.m.end(), m, m + 12);
    }
    inst.m0 = inst.m;
    inst.hidden.assign(n, 0);
    return inst;
}

Track make_track() {
    Track t;
    t.id = "pista_teste";
    t.src = "locations/pista_teste.nefs";
    t.routes.push_back({});
    t.routes[0].name = "route_0";
    t.types = {{"e:core_barr_aframe_a~a", 0, 1}, {"o:cone|x", 1, 1}, {"t:tree_dist_\"q\"", 2, 1}};
    return t;
}

void test_history() {
    Instances inst = make_instances(5);
    edit::History h;
    const std::vector<float> original = inst.m;

    auto before = edit::snapshot(inst, {2});
    inst.matrix(2)[9] += 3.5f;
    inst.matrix(2)[11] -= 1.25f;
    check(h.commit("Mover", before, edit::snapshot(inst, {2})), "history: commit de mover");
    const std::vector<float> moved = inst.m;
    check(!h.commit("nada", edit::snapshot(inst, {2}), edit::snapshot(inst, {2})), "history: commit sem mudança é ignorado");
    check(h.undo(inst) && inst.m == original, "history: desfazer devolve as mesmas floats");
    check(h.redo(inst) && inst.m == moved, "history: refazer devolve as floats movidas");
    check(!h.redo(inst), "history: nada para refazer");

    before = edit::snapshot(inst, {4});
    inst.hidden[4] = 1;
    h.commit("Apagar", before, edit::snapshot(inst, {4}));
    check(h.undo(inst) && inst.hidden[4] == 0, "history: desfazer apagar devolve hidden = 0");
    // empilhar depois de desfazer descarta o futuro
    before = edit::snapshot(inst, {0});
    inst.matrix(0)[10] += 1.0f;
    h.commit("Mover", before, edit::snapshot(inst, {0}));
    check(h.size() == 2 && h.pos() == 2 && !h.redo(inst), "history: futuro truncado");

    edit::History big;
    for (int k = 0; k < 350; ++k) {
        before = edit::snapshot(inst, {1});
        inst.matrix(1)[9] += 1.0f;
        big.commit("Mover", before, edit::snapshot(inst, {1}));
    }
    check(big.size() == edit::History::kMax && big.pos() == edit::History::kMax, "history: limite de 300 passos");
    int undone = 0;
    while (big.undo(inst)) ++undone;
    check(undone == 300 && inst.matrix(1)[9] == 10.0f + 50.0f, "history: desfaz só os 300 últimos");
}

void test_spin() {
    const float base[12] = {2, 0, 0, 0, 1, 0, 0, 0, 3, 5, 6, 7};
    float m[12];
    edit::spin(m, base, static_cast<float>(M_PI / 2));
    // linha 0 (2,0,0) -> (0,0,-2); linha 2 (0,0,3) -> (3,0,0); posição e linha Y intactas
    check(std::fabs(m[0]) < 1e-6f && std::fabs(m[2] + 2) < 1e-6f, "spin: linha X");
    check(std::fabs(m[6] - 3) < 1e-6f && std::fabs(m[8]) < 1e-6f, "spin: linha Z");
    check(m[4] == 1 && m[9] == 5 && m[10] == 6 && m[11] == 7, "spin: Y e posição");
}

void test_edits_json() {
    const Track track = make_track();
    Instances inst = make_instances(6);
    inst.matrix(1)[9] = 0.1f;          // movida (valor sem representação curta)
    inst.matrix(3)[0] = 0.70710677f;   // girada
    inst.hidden[5] = 1;                // apagada
    std::size_t count = 0;
    const std::string text = edit::edits_json(track, track.routes[0], inst, &count);
    check(count == 3, "edits: só as alteradas");
    const json::Value doc = json::parse(text);
    check(doc["format"].as_string() == "dr2-track-edits" && doc["version"].as_number() == 1, "edits: cabeçalho");
    check(doc["track"].as_string() == track.id && doc["src"].as_string() == track.src, "edits: track e src");
    const auto& e = doc["edits"];
    check(e.size() == 3, "edits: três entradas");
    check(e[0]["route"].as_string() == "route_0" && e[0]["kind"].as_string() == "o" && e[0]["type"].as_string() == "cone|x",
          "edits: kind e type sem o prefixo");
    check(e[0]["index"].as_number() == 101 && !e[0]["deleted"].as_bool(), "edits: index é o idnum");
    check(e[2]["kind"].as_string() == "t" && e[2]["type"].as_string() == "tree_dist_\"q\"" && e[2]["deleted"].as_bool(),
          "edits: apagada, com aspas no nome");
    for (std::size_t k = 0; k < 3; ++k) {
        const std::size_t i = k == 0 ? 1 : k == 1 ? 3 : 5;
        for (std::size_t f = 0; f < 12; ++f) {
            check(static_cast<float>(e[k]["m"][f].as_number()) == inst.matrix(i)[f], "edits: m exato em float32");
            check(static_cast<float>(e[k]["m0"][f].as_number()) == inst.m0[i * 12 + f], "edits: m0 exato em float32");
        }
    }
    inst = make_instances(2);
    check(edit::edits_json(track, track.routes[0], inst, &count).find("\"edits\":[\n]") != std::string::npos && count == 0,
          "edits: nada alterado, lista vazia");
}

void test_duplicate_restore_turn() {
    const Track track = make_track();
    Instances inst = make_instances(3);
    edit::History h;
    const std::uint32_t c = edit::duplicate(inst, h, 0);
    check(c == 3 && inst.n == 4 && inst.idnum[3] == kAdded + 100 && inst.hidden[3] == 0, "duplicar: cópia no fim, visível");
    check(inst.matrix(3)[9] == inst.matrix(0)[9] + 2.0f && inst.m0[3 * 12 + 9] == inst.matrix(3)[9], "duplicar: 2 m em x, m0 = cópia");
    std::size_t count = 0;
    auto doc = json::parse(edit::edits_json(track, track.routes[0], inst, &count));
    check(count == 1 && doc["edits"][0]["added"].as_bool() && doc["edits"][0]["src"].as_number() == 100 &&
              doc["edits"][0]["index"].as_number() == -1 && !doc["edits"][0]["deleted"].as_bool(),
          "duplicar: added, src = idnum copiado, index -1");
    const std::uint32_t c2 = edit::duplicate(inst, h, 3);  // cópia da cópia aponta para o original
    check(inst.idnum[c2] == kAdded + 100, "duplicar: cópia da cópia mantém o src original");
    check(h.undo(inst) && h.undo(inst) && inst.hidden[3] == 1 && inst.hidden[4] == 1, "duplicar: desfazer esconde as cópias");
    edit::edits_json(track, track.routes[0], inst, &count);
    check(count == 0, "duplicar: cópia desfeita não entra no JSON");

    edit::turn(inst, h, 1, 90.0f);
    edit::turn(inst, h, 1, 90.0f);
    check(std::fabs(inst.matrix(1)[0] + 1.0f) < 1e-5f && std::fabs(inst.matrix(1)[8] + 1.0f) < 1e-5f, "girar: 90 + 90 = 180 graus");
    inst.hidden[1] = 1;
    edit::restore(inst, h, 1);
    check(std::memcmp(inst.matrix(1), &inst.m0[12], 48) == 0 && inst.hidden[1] == 0, "restaurar: matriz do arquivo e visível");
    check(h.undo(inst) && inst.hidden[1] == 1, "restaurar: desfazer volta ao estado anterior");

    Track two = make_track();
    two.routes.push_back({});
    two.routes[1].name = "route_1";
    Instances a = make_instances(2), b = make_instances(2);
    a.hidden[0] = 1;
    b.matrix(1)[9] += 1.0f;
    doc = json::parse(edit::edits_json(two, {{&two.routes[0], &a}, {&two.routes[1], &b}}, &count));
    check(count == 2 && doc["edits"][0]["route"].as_string() == "route_0" && doc["edits"][1]["route"].as_string() == "route_1",
          "edits: junta as rotas abertas");
}

void test_game_folder() {
    check(edit::inside_game_folder("/mnt/Jogos/SteamLibrary/steamapps/common/DiRT Rally 2.0/x.json"), "pasta do jogo: dentro");
    check(edit::inside_game_folder("/mnt/Jogos/SteamLibrary/steamapps/common/DiRT Rally 2.0"), "pasta do jogo: a própria");
    check(edit::inside_game_folder("/home/x/.steam/steamapps/common/dirt rally 2.0/sub/a.json"), "pasta do jogo: sem diferenciar maiúsculas");
    check(!edit::inside_game_folder("build/uiview/saves/pista.edits.json"), "pasta do jogo: build/ é permitido");
    bool refused = false;
    try {
        edit::write_text("/tmp/x/steamapps/common/DiRT Rally 2.0/edits.json", "{}");
    } catch (const std::runtime_error&) {
        refused = true;
    }
    check(refused, "pasta do jogo: write_text recusa");
}

// Girar 360° em passos volta exatamente ao arquivo: sem edição fantasma no edits.json (R1 P2-1).
void test_full_turn() {
    const Track track = make_track();
    for (float step : {15.0f, 90.0f, -15.0f}) {
        Instances inst = make_instances(2);
        inst.matrix(1)[0] = 0.8f;  // escala não unitária e não alinhada
        inst.matrix(1)[2] = 0.3f;
        inst.m0 = inst.m;
        edit::History h;
        for (int k = 0; k < static_cast<int>(360.0f / std::fabs(step)); ++k) edit::turn(inst, h, 1, step);
        std::size_t count = 9;
        edit::edits_json(track, track.routes[0], inst, &count);
        check(std::memcmp(inst.matrix(1), &inst.m0[12], 48) == 0 && count == 0, "girar 360 em passos: volta exatamente ao arquivo");
    }
    Instances inst = make_instances(1);
    inst.matrix(0)[9] += 0.5f;
    edit::snap_to_file(inst, 0);
    check(inst.matrix(0)[9] != inst.m0[9], "snap_to_file: não desfaz uma edição de verdade");
}

std::string slurp(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// Gravação: falha não estraga o arquivo antigo; cópia .bak do arquivo de outra sessão (R1 P0-1, P0-3).
void test_write_backup() {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / ("dr2_edit_test_" + std::to_string(::getpid()));
    fs::remove_all(dir);
    fs::create_directories(dir);
    const std::string out = (dir / "e.json").string();
    check(edit::backup_existing(out).empty(), "backup: sem arquivo, sem cópia");
    edit::write_text(out, "velho");
    check(slurp(out) == "velho" && !fs::exists(out + ".tmp"), "write_text: grava e não deixa .tmp");
    const std::string bak = edit::backup_existing(out);
    check(bak == out + ".1.bak" && slurp(bak) == "velho", "backup: copia para .1.bak");
    check(edit::backup_existing(out) == out + ".2.bak", "backup: não sobrescreve a cópia anterior");
    edit::write_text(out, "novo");
    check(slurp(out) == "novo" && slurp(bak) == "velho", "write_text: substitui, a cópia fica");
    bool threw = false;
    try {
        edit::write_text((dir / "e.json" / "dentro.json").string(), "x");  // pai é um arquivo
    } catch (const std::exception&) {
        threw = true;
    }
    check(threw && slurp(out) == "novo", "write_text: caminho impossível lança e não toca no resto");
    fs::create_directories(dir / "pasta.json");
    threw = false;
    try {
        edit::write_text((dir / "pasta.json").string(), "x");  // destino é uma pasta
    } catch (const std::exception&) {
        threw = true;
    }
    check(threw && fs::is_directory(dir / "pasta.json") && !fs::exists(dir / "pasta.json.tmp"), "write_text: destino pasta lança e limpa o .tmp");
    fs::remove_all(dir);
}

// edits.json de volta para a sessão (retomar): gravar, ler e gravar de novo dá o mesmo texto (R2 P0-2).
void test_apply_edits() {
    Track track = make_track();
    track.types[0].name = "e:barreira";
    Instances inst = make_instances(6);
    edit::History h;
    inst.matrix(0)[9] += 3.5f;
    inst.hidden[1] = 1;
    edit::turn(inst, h, 3, 30.0f);
    edit::duplicate(inst, h, 0);  // tipo 0 é e:
    const std::string text = edit::edits_json(track, track.routes[0], inst);
    const json::Value doc = json::parse(text);
    check(edit::routes_in_edits(doc) == std::vector<std::string>{"route_0"}, "retomar: rotas do arquivo");
    Instances fresh = make_instances(6);
    edit::ApplyReport rep;
    edit::apply_edits(doc, track, track.routes[0], fresh, rep);
    check(rep.applied == 4 && rep.skipped.empty() && fresh.n == 7, "retomar: aplica mover, apagar, girar e cópia");
    check(edit::edits_json(track, track.routes[0], fresh) == text, "retomar: gravar de novo dá o mesmo arquivo");

    Instances moved = make_instances(6);
    moved.m0[0 * 12 + 9] += 10.0f;  // a pista foi exportada de novo: o original mudou
    moved.m = moved.m0;
    edit::ApplyReport rep2;
    edit::apply_edits(doc, track, track.routes[0], moved, rep2);
    check(rep2.applied == 3 && rep2.skipped.size() == 1, "retomar: original mudado fica de fora");

    Track other = make_track();
    other.id = "outra";
    bool threw = false;
    try {
        edit::ApplyReport r;
        edit::apply_edits(doc, other, other.routes[0], fresh, r);
    } catch (const std::exception&) {
        threw = true;
    }
    check(threw, "retomar: recusa edits.json de outra pista");
}

}  // namespace

int main() {
    test_history();
    test_spin();
    test_edits_json();
    test_duplicate_restore_turn();
    test_game_folder();
    test_full_turn();
    test_write_backup();
    test_apply_edits();
    std::printf("edit_test OK (%d verificações)\n", checks);
    return 0;
}
