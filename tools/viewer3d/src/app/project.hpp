// Dados das janelas do projeto da pista (Arquivos do projeto, Sons e Configurações da pista): o índice dos sons
// que o scripts/research/track_sounds.py extrai do jogo, o tocador (áudio do SDL), o inventário do
// scripts/research/track_inventory.py e a lista das pastas do projeto. Sem ImGui: a interface fica no ui.cpp.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct SDL_AudioStream;

namespace dr2::app {

// <repo>/build/re/track_sounds/index.json: os bancos de som do chão que o audio_dataset.xml da pista carrega.
struct SoundIndex {
    struct Sound {
        std::uint32_t id = 0;
        std::string file;   // relativo à pasta do índice
        std::string codec;  // "pcm" ou "adpcm" (o jogo guarda os dois; aqui já são .wav)
        int channels = 1, rate = 48000;
        double seconds = 0.0;
    };
    struct Bank {
        std::string bank, surface;  // "s_sfc_gravel", "cascalho"
        std::string surface_en;      // "gravel" (índices antigos não têm: fica o português)
        const std::string& surface_text() const;  // no idioma da interface
        std::vector<Sound> sounds;
        int vorbis = 0;  // sons em Vorbis do Wwise, que ainda não tocam
    };
    std::string dir;  // pasta do índice
    std::vector<Bank> banks;
    double adpcm_jump = -1.0;  // salto médio entre blocos do ADPCM (conferência do decodificador)

    // Lança std::runtime_error se o arquivo não existe ou não é o JSON esperado.
    static SoundIndex load(const std::string& dir);
    std::size_t count() const;
};

// Um som por vez no dispositivo de saída padrão. Abre o áudio do SDL na primeira vez que toca.
class SoundPlayer {
public:
    SoundPlayer() = default;
    SoundPlayer(const SoundPlayer&) = delete;
    SoundPlayer& operator=(const SoundPlayer&) = delete;
    ~SoundPlayer();

    bool play(const std::string& wav_path, std::string& error);
    void stop();
    // A cada quadro: com `loop`, recoloca o som quando acaba.
    void update();
    bool playing() const;
    float position() const;  // 0..1 do som atual
    const std::string& current() const { return current_; }

    float volume = 0.7f;
    bool loop = false;

private:
    SDL_AudioStream* stream_ = nullptr;
    std::vector<std::uint8_t> data_;
    std::string current_;
    bool audio_ready_ = false;
};

// <repo>/build/re/track_inventory.json: o que a pista tem de próprio e o que ainda vem da hospedeira.
struct Inventory {
    struct Item {
        std::string id, label, level, state, effect;  // level: obrigatorio/posicional/generico; state: proprio/parcial/emprestado
        std::string label_en, effect_en;               // o script grava os dois idiomas
        const std::string& label_text() const;         // no idioma da interface
        const std::string& effect_text() const;
        std::vector<std::string> own, borrowed;
    };
    std::vector<Item> items;
    std::vector<std::string> unlisted;
    std::string track_dir, host;
    // área de serviço: -1 sem dados, 0 longe, 1 certa
    int service_ok = -1;
    double service_car[3] = {};
    std::vector<std::pair<std::string, double>> service_cameras;  // câmera service_* e a distância ao carro
    double service_lights = 0.0;

    static Inventory load(const std::string& path);  // lança std::runtime_error
    int own_count() const;     // itens próprios
    int needed_count() const;  // itens que não são genéricos (os que a pista precisa ter seus)
    bool is_own(const std::string& rel) const;  // arquivo (relativo à pasta da pista) é próprio
};

// Uma pasta do projeto lida do disco (até `max_entries` itens, pastas primeiro, por nome).
struct FileNode {
    std::string name, path;  // path: absoluto
    bool dir = false;
    std::uintmax_t size = 0;  // arquivo: bytes; pasta: soma do que foi lido
    int files = 0;            // pasta: arquivos dentro (todas as subpastas)
    std::vector<FileNode> kids;
};
FileNode scan_dir(const std::string& root, std::size_t max_entries = 5000);

// Pasta da pista na overlay do jogo (captures/overlay/tracks/locations/<loc>/<pista>), ou "" se a pista do editor
// não tem porte (só o Ring, por enquanto).
std::string overlay_track_dir(const std::string& repo, const std::string& track_id);

// "12,3 KB", "4,5 MB"
std::string human_size(std::uintmax_t bytes);

}  // namespace dr2::app
