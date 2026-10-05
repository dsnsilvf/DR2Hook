// Texturas de cor dos materiais: WebP de tex/ decodificado (libwebp) e enviado com mipmaps.
// Uma textura GL por arquivo, compartilhada pelos materiais que apontam para ele.
#pragma once

#include "render/gl.hpp"

#include <map>
#include <string>
#include <unordered_map>

namespace dr2::render {

class TextureCache {
public:
    // `materials`: material -> caminho relativo a `dir` (track.json -> materials).
    TextureCache(std::string dir, const std::map<std::string, std::string, std::less<>>& materials);
    ~TextureCache();
    TextureCache(const TextureCache&) = delete;
    TextureCache& operator=(const TextureCache&) = delete;

    // Textura do material, carregada na primeira chamada; 0 se o material não tem arquivo ou se a
    // leitura falhou (o aviso sai uma vez no stderr e o material fica na cor fixa).
    GLuint for_material(const std::string& material);

    std::size_t created() const { return created_; }
    std::size_t failed() const { return failed_; }
    double decode_seconds() const { return decode_s_; }
    std::size_t bytes_rgba() const { return bytes_; }

private:
    GLuint load(const std::string& file);

    std::string dir_;
    const std::map<std::string, std::string, std::less<>>& materials_;
    std::unordered_map<std::string, GLuint> by_file_;   // 0 = falhou
    std::unordered_map<std::string, GLuint> by_material_;
    std::size_t created_ = 0, failed_ = 0, bytes_ = 0;
    double decode_s_ = 0.0;
    float aniso_ = 0.0f;
};

}  // namespace dr2::render
