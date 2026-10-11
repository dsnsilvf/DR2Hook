// Informações da pista para o pacote `.dr2track` (Arquivo > Informações da pista): nome, país, descrição, autor,
// o alfinete do mapa-múndi da tela de locations e a câmera da foto do card. Ficam em <pista>/package.json, que o
// scripts/research/ring_deploy.py passa ao track_package.py (--info) e de onde renderiza a foto do card.
#pragma once

#include <string>

namespace dr2::app {

struct TrackInfo {
    std::string name, country, description, author, version = "1";
    bool has_map = false;
    double lat = 0.0, lon = 0.0;  // graus
    // Foto do card: a câmera orbital (yaw, pitch, dist, alvo x, y, z; o --camera do viewer3d) e a proporção da
    // vista quando foi marcada. O track_package.py recorta no centro cada imagem da location dessa foto.
    bool has_card = false;
    float card[6] = {};
    float card_aspect = 16.0f / 9.0f;

    // Lê <dir>/package.json; sem o arquivo, devolve um vazio e `exists` falso. Lança std::runtime_error se o
    // arquivo existe e não é JSON válido.
    static TrackInfo load(const std::string& dir, bool* exists = nullptr);
    void save(const std::string& dir) const;  // lança std::runtime_error se não gravou
    static std::string path(const std::string& dir);
    bool operator==(const TrackInfo&) const;
};

// Países prontos do diálogo: nome em inglês (o texto que o jogo mostra no card) e um ponto do país em graus.
struct CountryPreset {
    const char* name;
    double lat, lon;
};
extern const CountryPreset kCountries[];
extern const int kCountryCount;

// Mapa-múndi do jogo (world_map, scripts/research/world_map_image.py) <-> graus, em frações da imagem (0..1).
// O mesmo ajuste linear com que o builder da dxgi move o marcador (kMapX0... em src/trackpkg/builder.cpp), sobre a
// caixa da imagem no persistentDB (x 0..4,4, y -2,64..0).
void map_uv(double lat, double lon, float* u, float* v);
void map_latlon(float u, float v, double* lat, double* lon);

}  // namespace dr2::app
