#include "render/texture.hpp"

#include "core/io.hpp"

#include <webp/decode.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <exception>

namespace dr2::render {

namespace {

double seconds_since(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
}

// Bytes de uma textura RGBA8 com a cadeia de mipmaps.
std::size_t with_mips(int w, int h) { return static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4 * 4 / 3; }

}  // namespace

TextureCache::Image& TextureCache::Image::operator=(Image&& o) noexcept {
    if (this != &o) {
        if (rgba) WebPFree(rgba);
        w = o.w;
        h = o.h;
        scaled = o.scaled;
        translucent = o.translucent;
        rgba = o.rgba;
        o.rgba = nullptr;
    }
    return *this;
}

TextureCache::Image::~Image() {
    if (rgba) WebPFree(rgba);
}

TextureCache::TextureCache(std::string dir, const std::map<std::string, std::string, std::less<>>& materials, Options opt)
    : dir_(std::move(dir)), materials_(materials), opt_(opt) {
    if (GLEW_EXT_texture_filter_anisotropic) {
        GLfloat max = 0.0f;
        glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &max);
        aniso_ = std::min(8.0f, max);
    }
    opt_.threads = std::clamp(opt_.threads, 1u, 8u);
    for (unsigned k = 0; k < opt_.threads; ++k) threads_.emplace_back([this] { worker(); });
}

TextureCache::~TextureCache() {
    {
        std::lock_guard lock(mu_);
        stop_ = true;
    }
    cv_.notify_all();
    for (std::thread& t : threads_) t.join();
    for (const Entry& e : entries_)
        if (e.id) glDeleteTextures(1, &e.id);
}

int TextureCache::handle(const std::string& material) {
    if (auto it = by_material_.find(material); it != by_material_.end()) return it->second;
    int h = -1;
    if (auto m = materials_.find(material); m != materials_.end()) {
        if (auto f = by_file_.find(m->second); f != by_file_.end()) {
            h = f->second;
        } else {
            h = static_cast<int>(entries_.size());
            entries_.push_back({m->second});
            by_file_[m->second] = h;
        }
    }
    by_material_[material] = h;
    return h;
}

GLuint TextureCache::use(int h) {
    if (h < 0) return 0;
    Entry& e = entries_[static_cast<std::size_t>(h)];
    e.last_used = frame_;
    if (e.state == State::Idle) {
        e.state = State::Queued;
        {
            std::lock_guard lock(mu_);
            jobs_.push_back({static_cast<std::size_t>(h), e.file});
        }
        cv_.notify_one();
    }
    return e.state == State::Loaded ? e.id : 0;
}

void TextureCache::worker() {
    for (;;) {
        Job job;
        {
            std::unique_lock lock(mu_);
            cv_.wait(lock, [this] { return stop_ || !jobs_.empty(); });
            if (stop_) return;
            job = std::move(jobs_.front());
            jobs_.pop_front();
            ++decoding_;
        }
        const std::size_t entry = job.entry;
        const std::string& file = job.file;
        const auto t0 = std::chrono::steady_clock::now();
        Done done{entry, false, {}, {}};
        try {
            const std::vector<std::uint8_t> data = read_file(join_path(dir_, file));
            WebPDecoderConfig cfg;
            if (!WebPInitDecoderConfig(&cfg) || WebPGetFeatures(data.data(), data.size(), &cfg.input) != VP8_STATUS_OK)
                throw std::runtime_error("WebP inválido");
            int tw = cfg.input.width, th = cfg.input.height;
            const int side = std::max(tw, th);
            if (opt_.max_side > 0 && static_cast<std::size_t>(side) > opt_.max_side) {
                const double k = static_cast<double>(opt_.max_side) / side;
                tw = std::max(1, static_cast<int>(std::lround(tw * k)));
                th = std::max(1, static_cast<int>(std::lround(th * k)));
                cfg.options.use_scaling = 1;  // a libwebp reduz enquanto decodifica: o RGBA inteiro nunca existe
                cfg.options.scaled_width = tw;
                cfg.options.scaled_height = th;
                done.image.scaled = true;
            }
            // RGBA com alfa não premultiplicado, linha 0 da imagem = linha 0 da textura (como o web)
            std::uint8_t* rgba = nullptr;
            if (!done.image.scaled) {
                rgba = WebPDecodeRGBA(data.data(), data.size(), nullptr, nullptr);
            } else if (cfg.output.colorspace = MODE_RGBA, WebPDecode(data.data(), data.size(), &cfg) == VP8_STATUS_OK) {
                // a libwebp alocou o buffer; este código assume a posse (WebPFree = free, o mesmo da libwebp)
                rgba = cfg.output.u.RGBA.rgba;
            }
            if (!rgba) throw std::runtime_error("WebP inválido");
            done.image.rgba = rgba;
            if (cfg.input.has_alpha) {
                // Transparência de verdade = pelo menos 2 % dos pixels quase invisíveis (alfa < 16). O alfa das
                // outras texturas do terreno (asfalto, bordas, grama) é mapa de brilho: média ~200, mínimo ~100.
                const std::size_t px = static_cast<std::size_t>(tw) * static_cast<std::size_t>(th);
                std::size_t clear = 0;
                for (std::size_t k = 0; k < px; ++k) clear += rgba[4 * k + 3] < 16;
                done.image.translucent = clear * 50 > px;
            }
            done.image.w = tw;
            done.image.h = th;
            done.ok = true;
        } catch (const std::exception& e) {
            done.error = e.what();
        }
        {
            std::lock_guard lock(mu_);
            decode_s_ += seconds_since(t0);
            --decoding_;
            done_.push_back(std::move(done));
        }
    }
}

void TextureCache::upload(std::size_t index, Image& image) {
    static const bool log = std::getenv("VIEWER3D_TEXLOG") != nullptr;
    const auto t0 = std::chrono::steady_clock::now();
    Entry& e = entries_[index];
    GLuint id = 0;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    // linha 0 da imagem = primeira linha da textura, como o web (UNPACK_FLIP_Y_WEBGL = false)
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, image.w, image.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, image.rgba);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    // repetir sempre: a única diferença deliberada do web, que só repete em potência de dois (WebGL 1)
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    if (aniso_ > 1.0f) glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, aniso_);
    e.id = id;
    e.translucent = image.translucent;
    e.state = State::Loaded;
    e.gpu_bytes = with_mips(image.w, image.h);
    gpu_bytes_ += e.gpu_bytes;
    bytes_ += static_cast<std::size_t>(image.w) * static_cast<std::size_t>(image.h) * 4;
    downscaled_ += image.scaled;
    ++loaded_;
    ++created_;
    if (log) {
        glFinish();
        std::fprintf(stderr, "viewer3d: [tex] quadro %llu: %s %dx%d%s%s envio %.1f ms\n", static_cast<unsigned long long>(frame_), e.file.c_str(),
                     image.w, image.h, image.scaled ? " (reduzida)" : "", image.translucent ? " (translúcida)" : "", seconds_since(t0) * 1000.0);
    }
}

void TextureCache::begin_frame() {
    ++frame_;
    {
        std::lock_guard lock(mu_);
        while (!done_.empty()) {
            uploads_.push_back(std::move(done_.front()));
            done_.pop_front();
        }
    }
    std::size_t sent = 0;
    while (!uploads_.empty()) {
        Done& d = uploads_.front();
        const std::size_t bytes = static_cast<std::size_t>(d.image.w) * static_cast<std::size_t>(d.image.h) * 4;
        // o orçamento limita o que se envia por quadro; a primeira textura do quadro entra sempre
        if (sent > 0 && sent + bytes > opt_.upload_per_frame) break;
        Entry& e = entries_[d.entry];
        if (d.ok) {
            upload(d.entry, d.image);
            sent += bytes;
        } else {
            std::fprintf(stderr, "viewer3d: textura %s: %s\n", e.file.c_str(), d.error.c_str());
            e.state = State::Failed;
            ++failed_;
        }
        uploads_.pop_front();
    }
    evict();
}

void TextureCache::evict() {
    over_budget_ = false;
    if (gpu_bytes_ <= opt_.budget_bytes) return;
    // sai a menos usada primeiro; o que foi usado neste quadro ou no anterior fica (é o que está na tela)
    std::vector<std::size_t> order;
    for (std::size_t k = 0; k < entries_.size(); ++k)
        if (entries_[k].state == State::Loaded && entries_[k].last_used + 1 < frame_) order.push_back(k);
    std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) { return entries_[a].last_used < entries_[b].last_used; });
    // desce até 90 % do limite, para não descartar de novo no quadro seguinte
    const std::size_t target = opt_.budget_bytes / 10 * 9;
    for (std::size_t k : order) {
        if (gpu_bytes_ <= target) break;
        Entry& e = entries_[k];
        glDeleteTextures(1, &e.id);
        gpu_bytes_ -= e.gpu_bytes;
        e.id = 0;
        e.gpu_bytes = 0;
        e.state = State::Idle;
        --loaded_;
        ++evicted_;
    }
    over_budget_ = gpu_bytes_ > opt_.budget_bytes;
    if (over_budget_ && !warned_over_) {
        warned_over_ = true;
        std::fprintf(stderr,
                     "viewer3d: aviso: as texturas na tela passam do limite de VRAM (%.0f de %.0f MB); aumente --tex-mb ou reduza --tex-max-side\n",
                     static_cast<double>(gpu_bytes_) / 1048576.0, static_cast<double>(opt_.budget_bytes) / 1048576.0);
    }
}

std::size_t TextureCache::pending() const {
    std::lock_guard lock(mu_);
    return jobs_.size() + decoding_ + done_.size() + uploads_.size();
}

bool TextureCache::busy() const { return pending() > 0; }

double TextureCache::decode_seconds() const {
    std::lock_guard lock(mu_);
    return decode_s_;
}

}  // namespace dr2::render
