// Texturas de cor dos materiais: WebP de tex/ decodificado (libwebp) em threads de trabalho e enviado
// à GPU no quadro, com orçamento de bytes por quadro, lado máximo e limite de VRAM.
//
// Uma textura GL por arquivo, compartilhada pelos materiais que apontam para ele. O desenho nunca
// espera: enquanto a textura não chegou, `use` devolve 0 e o material fica na cor fixa (o mesmo que
// já acontece quando o arquivo falha). Passou do limite de VRAM, as menos usadas saem e voltam a ser
// carregadas se o desenho pedir de novo.
#pragma once

#include "render/gl.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace dr2::render {

class TextureCache {
public:
    struct Options {
        std::size_t max_side = 2048;                      // maior lado na GPU; acima disso reduz ao decodificar (0 = sem limite)
        std::size_t budget_bytes = 1536ull << 20;         // VRAM estimada (com mipmaps) acima da qual as menos usadas saem
        std::size_t upload_per_frame = 16ull << 20;       // bytes RGBA enviados por quadro (sempre ao menos uma textura)
        unsigned threads = 2;                             // threads de decodificação
    };

    // `materials`: material -> caminho relativo a `dir` (track.json -> materials).
    TextureCache(std::string dir, const std::map<std::string, std::string, std::less<>>& materials, Options opt);
    TextureCache(std::string dir, const std::map<std::string, std::string, std::less<>>& materials)
        : TextureCache(std::move(dir), materials, Options{}) {}
    ~TextureCache();
    TextureCache(const TextureCache&) = delete;
    TextureCache& operator=(const TextureCache&) = delete;

    // Índice estável do arquivo do material; -1 se o material não tem arquivo. Barato depois da 1ª vez.
    int handle(const std::string& material);
    // Textura GL pronta; 0 se ainda não chegou, se o arquivo falhou ou se `h` é -1. Marca como usada neste
    // quadro e, se ninguém pediu ainda, pede a carga às threads.
    GLuint use(int h);
    // handle + use, para quem não guarda o handle (miniaturas dos painéis).
    GLuint for_material(const std::string& material) { return use(handle(material)); }
    // Chame uma vez por quadro, no thread do GL, antes de desenhar: envia à GPU o que as threads
    // terminaram (dentro do orçamento) e descarta as menos usadas se passou do limite.
    void begin_frame();
    // Ainda há arquivo na fila, sendo decodificado ou esperando envio (para esperar nos testes).
    bool busy() const;

    std::size_t loaded() const { return loaded_; }           // texturas na GPU agora
    std::size_t created() const { return created_; }         // enviadas desde o começo (inclui as que saíram e voltaram)
    std::size_t failed() const { return failed_; }
    std::size_t evicted() const { return evicted_; }
    std::size_t pending() const;                             // na fila, decodificando ou esperando envio
    double decode_seconds() const;                           // soma do tempo das threads
    std::size_t bytes_rgba() const { return bytes_; }        // RGBA enviado desde o começo, sem mipmaps
    std::size_t gpu_bytes() const { return gpu_bytes_; }     // estimativa na GPU agora (com mipmaps)
    std::size_t budget() const { return opt_.budget_bytes; }
    std::size_t downscaled() const { return downscaled_; }   // reduzidas pelo lado máximo
    bool over_budget() const { return over_budget_; }        // nada a descartar e ainda acima do limite

private:
    enum class State : std::uint8_t { Idle, Queued, Decoded, Loaded, Failed };
    struct Entry {
        std::string file;
        State state = State::Idle;
        GLuint id = 0;
        std::size_t gpu_bytes = 0;
        std::uint64_t last_used = 0;
    };
    // RGBA decodificado, dono do buffer da libwebp.
    struct Image {
        int w = 0, h = 0;
        bool scaled = false;
        std::uint8_t* rgba = nullptr;
        Image() = default;
        Image(const Image&) = delete;
        Image& operator=(const Image&) = delete;
        Image(Image&& o) noexcept { *this = std::move(o); }
        Image& operator=(Image&& o) noexcept;
        ~Image();
    };
    struct Done {
        std::size_t entry;
        bool ok;
        Image image;
        std::string error;
    };

    void worker();
    void upload(std::size_t entry, Image& image);
    void evict();

    std::string dir_;
    const std::map<std::string, std::string, std::less<>>& materials_;
    Options opt_;
    std::vector<Entry> entries_;
    std::unordered_map<std::string, int> by_file_;
    std::unordered_map<std::string, int> by_material_;  // -1 = sem arquivo
    std::uint64_t frame_ = 0;

    // fila de pedidos e resultados, entre o thread do GL e os de trabalho
    mutable std::mutex mu_;
    std::condition_variable cv_;
    struct Job {
        std::size_t entry;
        std::string file;  // cópia: as threads nunca leem `entries_`, que o thread do GL faz crescer
    };
    std::deque<Job> jobs_;
    std::deque<Done> done_;
    bool stop_ = false;
    std::size_t decoding_ = 0;
    double decode_s_ = 0.0;
    std::vector<std::thread> threads_;
    std::deque<Done> uploads_;  // só o thread do GL: decodificadas esperando orçamento

    std::size_t loaded_ = 0, created_ = 0, failed_ = 0, evicted_ = 0, bytes_ = 0, gpu_bytes_ = 0, downscaled_ = 0;
    bool over_budget_ = false;
    bool warned_over_ = false;
    float aniso_ = 0.0f;
};

}  // namespace dr2::render
