// Testes do dr2core (sem SDL, sem GL). Executável simples com asserts.
//
//   core_tests                       só os casos embutidos
//   core_tests --track DIR           também lê a pista exportada em DIR
//
// Com --track, confere o DR2M do terreno contra track.json (routes[0].terrain); se DIR tiver
// expected.json (pista sintética de `python -m tools.synthtrack`, contagens lidas pelo unpack_geom
// do Python), confere também contra ele; na Montalegre, confere os números do plano.
#undef NDEBUG
#include "core/dr2m.hpp"
#include "core/io.hpp"
#include "core/json.hpp"
#include "core/track.hpp"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

int checks = 0;

void check(bool ok, const char* what) {
    ++checks;
    if (!ok) {
        std::fprintf(stderr, "FALHOU: %s\n", what);
        std::abort();
    }
}

bool throws(const std::function<void()>& fn, const char* contains = nullptr) {
    try {
        fn();
    } catch (const std::runtime_error& e) {
        return !contains || std::strstr(e.what(), contains) != nullptr;
    }
    return false;
}

void test_json() {
    using dr2::json::parse;
    const auto v = parse(R"({"a":[1,-1.5e-05,2E3,0],"b":{"c":"x\"y\\z\/\n\té😀","d":true,"e":null},"f":false})");
    check(v["a"].size() == 4, "json: tamanho da lista");
    check(v["a"][0].as_number() == 1.0, "json: inteiro");
    check(std::fabs(v["a"][1].as_number() + 1.5e-05) < 1e-12, "json: expoente negativo");
    check(v["a"][2].as_number() == 2000.0, "json: expoente maiúsculo");
    check(v["b"]["c"].as_string() == "x\"y\\z/\n\t\xc3\xa9\xf0\x9f\x98\x80", "json: escapes e \\u");
    check(v["b"]["d"].as_bool() && v["b"]["e"].is_null() && !v["f"].as_bool(), "json: literais");
    check(v.find("nada") == nullptr, "json: find ausente");
    check(parse("  [ ]  ").size() == 0 && parse("{}").size() == 0, "json: vazios");
    check(parse(R"({"k|a~b!c>d:e":1})")["k|a~b!c>d:e"].as_number() == 1.0, "json: chave com símbolos de tipo");
    check(throws([] { parse("[1,]"); }, "byte"), "json: vírgula sobrando");
    check(throws([] { parse("{\"a\":1"); }), "json: objeto sem fim");
    check(throws([] { parse("\"abc"); }), "json: string sem fim");
    check(throws([] { parse("[1] x"); }, "lixo"), "json: lixo no fim");
    check(throws([] { parse("NaN"); }), "json: NaN não é JSON");
    check(throws([] { parse("01x"); }), "json: número inválido");
    check(throws([] { (void)parse("{}")["x"]; }, "falta a chave"), "json: chave ausente");
    check(dr2::json::quote("a\"b\\c\n") == "\"a\\\"b\\\\c\\n\"", "json: quote");
    const auto round = parse(dr2::json::quote("tipo|x~y\x01"));
    check(round.as_string() == "tipo|x~y\x01", "json: quote ida e volta");
}

// DR2M montado à mão: uma malha de 3 vértices, índices de 16 bits.
std::vector<std::uint8_t> tiny_dr2m(std::uint16_t bad_index = 2) {
    std::vector<std::uint8_t> b;
    auto put = [&](const void* p, std::size_t n) { b.insert(b.end(), static_cast<const std::uint8_t*>(p), static_cast<const std::uint8_t*>(p) + n); };
    auto u32 = [&](std::uint32_t v) { put(&v, 4); };
    auto u16 = [&](std::uint16_t v) { put(&v, 2); };
    put("DR2M", 4);
    u32(1);
    u16(3);  // nome "tri"
    u16(5);  // material "g|abc"
    u32(3);
    u32(3);
    u32(0);
    put("tri", 3);
    put("g|abc", 5);
    while (b.size() % 4) b.push_back(0);
    const float pos[9] = {0, 0, 0, 1, 0, 0, 0, 0, 1};
    const float uv[6] = {0, 0, 1, 0, 0, 1};
    put(pos, sizeof pos);
    put(uv, sizeof uv);
    u16(0);
    u16(1);
    u16(bad_index);
    while (b.size() % 4) b.push_back(0);
    return b;
}

void test_dr2m_synthetic() {
    const auto good = tiny_dr2m();
    const auto meshes = dr2::read_dr2m(good);
    check(meshes.size() == 1 && meshes[0].name == "tri" && meshes[0].material == "g|abc", "dr2m: nome e material");
    check(meshes[0].verts == 3 && meshes[0].idx.size() == 3 && meshes[0].idx[2] == 2 && !meshes[0].wide, "dr2m: índices");
    check(meshes[0].pos[3] == 1.0f && meshes[0].uv[5] == 1.0f, "dr2m: posição e uv");

    for (std::size_t cut : {std::size_t{3}, std::size_t{9}, std::size_t{30}, good.size() - 4}) {
        std::vector<std::uint8_t> trunc(good.begin(), good.begin() + static_cast<std::ptrdiff_t>(cut));
        check(throws([&] { dr2::read_dr2m(trunc); }), "dr2m: truncado lança");
    }
    auto bad_magic = good;
    bad_magic[0] = 'X';
    check(throws([&] { dr2::read_dr2m(bad_magic); }, "magia"), "dr2m: magia errada lança");
    const auto bad = tiny_dr2m(7);
    check(throws([&] { dr2::read_dr2m(bad); }, "\"tri\""), "dr2m: índice fora da faixa lança com o nome da malha");
    auto huge = good;
    const std::uint32_t lots = 0x7fffffff;
    std::memcpy(huge.data() + 4, &lots, 4);
    check(throws([&] { dr2::read_dr2m(huge); }), "dr2m: contagem absurda lança sem alocar");
}

void test_track(const std::string& dir) {
    const dr2::Track track = dr2::read_track(dir);
    const dr2::Route& r0 = track.routes.at(0);
    const auto bytes = dr2::read_file(dr2::join_path(dir, r0.terrain_file));
    const auto terrain = dr2::read_dr2m(bytes);
    const auto t = dr2::totals(terrain);
    std::printf("%s: %s %zu malhas, %zu vértices, %zu triângulos; track.json diz %zu malhas, %zu vértices\n", track.id.c_str(),
                r0.terrain_file.c_str(), t.meshes, t.verts, t.tris, r0.terrain_meshes, r0.terrain_verts);
    check(t.meshes == r0.terrain_meshes, "track: malhas do terreno batem com routes[0].terrain.meshes");
    check(t.verts == r0.terrain_verts, "track: vértices do terreno batem com routes[0].terrain.verts");
    check(!r0.gates.empty() || !r0.ai.empty(), "track: rota com portões ou linha da IA");

    const std::string expected_path = dr2::join_path(dir, "expected.json");
    if (std::filesystem::exists(expected_path)) {
        const auto exp = dr2::json::parse_file(expected_path);
        const auto& et = exp["terrain"];
        check(t.meshes == et["meshes"].as_number() && t.verts == et["verts"].as_number() && t.tris == et["tris"].as_number(),
              "track: terreno igual ao unpack_geom do Python");
        std::size_t wide = 0, colored = 0;
        for (const auto& m : terrain) {
            wide += m.wide;
            colored += !m.col.empty();
        }
        check(wide == et["wide"].as_number() && colored == et["colored"].as_number(), "track: índices de 32 bits e cor por vértice");
        const auto& fm = exp["first_mesh"];
        check(terrain[0].name == fm["name"].as_string() && terrain[0].material == fm["material"].as_string() &&
                  terrain[0].verts == fm["verts"].as_number() && terrain[0].indices == fm["indices"].as_number(),
              "track: primeira malha igual à do Python");
        std::printf("  expected.json (oráculo Python): igual\n");
    }
    if (track.id == "portugal__montalegre_rallycross") {
        check(t.meshes == 1324 && t.verts == 428789 && t.tris == 557900, "montalegre: 1324 / 428 789 / 557 900");
        std::printf("  Montalegre: números do plano conferem\n");
    }
}

}  // namespace

int main(int argc, char** argv) {
    std::string track_dir;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--track") == 0 && i + 1 < argc) {
            track_dir = argv[++i];
        } else {
            std::fprintf(stderr, "uso: core_tests [--track DIR]\n");
            return 2;
        }
    }
    try {
        test_json();
        test_dr2m_synthetic();
        if (!track_dir.empty()) test_track(track_dir);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "FALHOU com exceção: %s\n", e.what());
        return 1;
    }
    std::printf("core_tests OK (%d verificações)\n", checks);
    return 0;
}
