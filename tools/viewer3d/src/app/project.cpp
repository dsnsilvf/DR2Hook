#include "app/project.hpp"
#include "core/i18n.hpp"

#include "core/json.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <stdexcept>

using dr2::i18n::tr;

namespace dr2::app {

namespace {

std::string str_or(const json::Value& v, const char* key, const std::string& fallback = {}) {
    const json::Value* f = v.find(key);
    return f && f->is_string() ? f->as_string() : fallback;
}

std::vector<std::string> strings(const json::Value& v, const char* key) {
    std::vector<std::string> out;
    if (const json::Value* f = v.find(key); f && f->is_array())
        for (const json::Value& s : f->as_array())
            if (s.is_string()) out.push_back(s.as_string());
    return out;
}

// O inglês do JSON, se o editor está em inglês e o script o gravou; senão o português.
const std::string& in_language(const std::string& pt, const std::string& en) {
    return i18n::language() == i18n::Lang::En && !en.empty() ? en : pt;
}

}  // namespace

// ---- sons

const std::string& SoundIndex::Bank::surface_text() const { return in_language(surface, surface_en); }

SoundIndex SoundIndex::load(const std::string& dir) {
    const json::Value doc = json::parse_file((std::filesystem::path(dir) / "index.json").string());
    SoundIndex out;
    out.dir = dir;
    out.adpcm_jump = doc.number_or("adpcm_block_jump", -1.0);
    for (const json::Value& b : doc["banks"].as_array()) {
        Bank bank;
        bank.bank = str_or(b, "bank");
        bank.surface = str_or(b, "surface", bank.bank);
        bank.surface_en = str_or(b, "surface_en");
        bank.vorbis = static_cast<int>(b.number_or("vorbis", 0.0));
        if (const json::Value* list = b.find("sounds"); list && list->is_array())
            for (const json::Value& s : list->as_array()) {
                Sound snd;
                snd.id = static_cast<std::uint32_t>(s.number_or("id", 0.0));
                snd.file = str_or(s, "file");
                snd.codec = str_or(s, "codec", "pcm");
                snd.channels = static_cast<int>(s.number_or("channels", 1.0));
                snd.rate = static_cast<int>(s.number_or("rate", 48000.0));
                snd.seconds = s.number_or("seconds", 0.0);
                bank.sounds.push_back(std::move(snd));
            }
        out.banks.push_back(std::move(bank));
    }
    return out;
}

std::size_t SoundIndex::count() const {
    std::size_t n = 0;
    for (const Bank& b : banks) n += b.sounds.size();
    return n;
}

SoundPlayer::~SoundPlayer() {
    stop();
    if (audio_ready_) SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

bool SoundPlayer::play(const std::string& wav_path, std::string& error) {
    stop();
    if (!audio_ready_) {
        if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
            error = std::string(tr("áudio do SDL: ")) + SDL_GetError();
            return false;
        }
        audio_ready_ = true;
    }
    SDL_AudioSpec spec;
    Uint8* buf = nullptr;
    Uint32 len = 0;
    if (!SDL_LoadWAV(wav_path.c_str(), &spec, &buf, &len)) {
        error = std::string(tr("não li o som: ")) + SDL_GetError();
        return false;
    }
    data_.assign(buf, buf + len);
    SDL_free(buf);
    stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (!stream_) {
        error = std::string(tr("sem saída de áudio: ")) + SDL_GetError();
        data_.clear();
        return false;
    }
    SDL_SetAudioStreamGain(stream_, volume);
    SDL_PutAudioStreamData(stream_, data_.data(), static_cast<int>(data_.size()));
    if (!loop) SDL_FlushAudioStream(stream_);
    SDL_ResumeAudioStreamDevice(stream_);
    current_ = wav_path;
    return true;
}

void SoundPlayer::stop() {
    if (stream_) SDL_DestroyAudioStream(stream_);
    stream_ = nullptr;
    current_.clear();
}

void SoundPlayer::update() {
    if (!stream_) return;
    SDL_SetAudioStreamGain(stream_, volume);
    const int queued = SDL_GetAudioStreamQueued(stream_);
    // repetir: põe outra volta antes de a fila secar (sem emenda audível); sem repetir, acabou quando a fila zera
    if (loop && queued < static_cast<int>(data_.size()) / 2)
        SDL_PutAudioStreamData(stream_, data_.data(), static_cast<int>(data_.size()));
    else if (!loop && queued <= 0 && SDL_GetAudioStreamAvailable(stream_) <= 0)
        stop();
}

bool SoundPlayer::playing() const { return stream_ != nullptr; }

float SoundPlayer::position() const {
    if (!stream_ || data_.empty()) return 0.0f;
    const auto n = static_cast<float>(data_.size());
    const auto q = static_cast<float>(SDL_GetAudioStreamQueued(stream_));
    const float left = loop ? q - n * std::floor(q / n) : q;  // com repetir pode haver mais de uma volta na fila
    return std::clamp(1.0f - left / n, 0.0f, 1.0f);
}

// ---- inventário

const std::string& Inventory::Item::label_text() const { return in_language(label, label_en); }
const std::string& Inventory::Item::effect_text() const { return in_language(effect, effect_en); }

Inventory Inventory::load(const std::string& path) {
    const json::Value doc = json::parse_file(path);
    Inventory out;
    for (const json::Value& it : doc["items"].as_array()) {
        Item item;
        item.id = str_or(it, "id");
        item.label = str_or(it, "label", item.id);
        item.level = str_or(it, "level");
        item.state = str_or(it, "state");
        item.effect = str_or(it, "effect");
        item.label_en = str_or(it, "label_en");
        item.effect_en = str_or(it, "effect_en");
        item.own = strings(it, "own");
        item.borrowed = strings(it, "borrowed");
        out.items.push_back(std::move(item));
    }
    out.unlisted = strings(doc, "unlisted");
    out.track_dir = str_or(doc, "track_dir");
    out.host = str_or(doc, "host");
    if (const json::Value* s = doc.find("service"); s && s->is_object()) {
        const json::Value* ok = s->find("ok");
        out.service_ok = ok && ok->is_bool() ? (ok->as_bool() ? 1 : 0) : -1;
        if (const json::Value* car = s->find("car"); car && car->is_array() && car->size() == 3)
            for (std::size_t k = 0; k < 3; ++k) out.service_car[k] = (*car)[k].as_number();
        if (const json::Value* cams = s->find("cameras"); cams && cams->is_object())
            for (const auto& [name, d] : cams->as_object())
                if (d.is_number()) out.service_cameras.emplace_back(name, d.as_number());
        out.service_lights = s->number_or("lights", 0.0);
    }
    return out;
}

int Inventory::own_count() const {
    return static_cast<int>(std::count_if(items.begin(), items.end(), [](const Item& it) { return it.state == "proprio"; }));
}

int Inventory::needed_count() const {
    return static_cast<int>(std::count_if(items.begin(), items.end(), [](const Item& it) { return it.level != "generico"; }));
}

bool Inventory::is_own(const std::string& rel) const {
    for (const Item& it : items)
        if (std::find(it.own.begin(), it.own.end(), rel) != it.own.end()) return true;
    return false;
}

// ---- pastas

namespace {

void scan_into(FileNode& node, std::size_t& budget) {
    namespace fs = std::filesystem;
    std::error_code ec;
    std::vector<FileNode> kids;
    for (fs::directory_iterator it(node.path, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end;
         it.increment(ec)) {
        if (budget == 0) break;
        --budget;
        FileNode k;
        k.name = it->path().filename().string();
        k.path = it->path().string();
        k.dir = it->is_directory(ec);
        if (!k.dir) k.size = it->file_size(ec);
        kids.push_back(std::move(k));
    }
    std::sort(kids.begin(), kids.end(), [](const FileNode& a, const FileNode& b) {
        return a.dir != b.dir ? a.dir : a.name < b.name;
    });
    for (FileNode& k : kids) {
        if (k.dir) scan_into(k, budget);
        node.size += k.size;
        node.files += k.dir ? k.files : 1;
    }
    node.kids = std::move(kids);
}

}  // namespace

FileNode scan_dir(const std::string& root, std::size_t max_entries) {
    FileNode node;
    node.path = root;
    node.name = std::filesystem::path(root).filename().string();
    std::error_code ec;
    node.dir = std::filesystem::is_directory(root, ec);
    if (node.dir) scan_into(node, max_entries);
    return node;
}

std::string overlay_track_dir(const std::string& repo, const std::string& track_id) {
    if (repo.empty() || track_id != "synthetic__dr2hook_ring") return {};
    return repo + "/captures/overlay/tracks/locations/portugal/dr2hook_ring";
}

std::string human_size(std::uintmax_t bytes) {
    char buf[32];
    if (bytes < 1024) std::snprintf(buf, sizeof buf, "%ju B", bytes);
    else if (bytes < 1024 * 1024) std::snprintf(buf, sizeof buf, "%.1f KB", static_cast<double>(bytes) / 1024.0);
    else if (bytes < 1024ull * 1024 * 1024) std::snprintf(buf, sizeof buf, "%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
    else std::snprintf(buf, sizeof buf, "%.2f GB", static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0));
    std::string s = buf;
    if (dr2::i18n::language() == dr2::i18n::Lang::Pt) std::replace(s.begin(), s.end(), '.', ',');
    return s;
}

}  // namespace dr2::app
