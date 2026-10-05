#include "render/texture.hpp"

#include "core/io.hpp"

#include <webp/decode.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <exception>

namespace dr2::render {

TextureCache::TextureCache(std::string dir, const std::map<std::string, std::string, std::less<>>& materials)
    : dir_(std::move(dir)), materials_(materials) {
    if (GLEW_EXT_texture_filter_anisotropic) {
        GLfloat max = 0.0f;
        glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &max);
        aniso_ = std::min(8.0f, max);
    }
}

TextureCache::~TextureCache() {
    for (const auto& [file, id] : by_file_)
        if (id) glDeleteTextures(1, &id);
}

GLuint TextureCache::for_material(const std::string& material) {
    if (auto it = by_material_.find(material); it != by_material_.end()) return it->second;
    GLuint id = 0;
    if (auto m = materials_.find(material); m != materials_.end()) {
        if (auto f = by_file_.find(m->second); f != by_file_.end()) id = f->second;
        else id = by_file_[m->second] = load(m->second);
    }
    by_material_[material] = id;
    return id;
}

GLuint TextureCache::load(const std::string& file) {
    const auto t0 = std::chrono::steady_clock::now();
    std::vector<std::uint8_t> data;
    try {
        data = read_file(join_path(dir_, file));
    } catch (const std::exception& e) {
        std::fprintf(stderr, "viewer3d: textura %s: %s\n", file.c_str(), e.what());
        ++failed_;
        return 0;
    }
    int w = 0, h = 0;
    std::uint8_t* rgba = WebPDecodeRGBA(data.data(), data.size(), &w, &h);
    if (!rgba) {
        std::fprintf(stderr, "viewer3d: textura %s: WebP inválido\n", file.c_str());
        ++failed_;
        return 0;
    }
    GLuint id = 0;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    // linha 0 da imagem = primeira linha da textura, como o web (UNPACK_FLIP_Y_WEBGL = false)
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    WebPFree(rgba);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    // repetir sempre: a única diferença deliberada do web, que só repete em potência de dois (WebGL 1)
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    if (aniso_ > 1.0f) glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, aniso_);
    ++created_;
    bytes_ += static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4;
    decode_s_ += std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    return id;
}

}  // namespace dr2::render
