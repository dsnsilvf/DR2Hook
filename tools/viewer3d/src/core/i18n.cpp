#include "core/i18n.hpp"

#include <atomic>
#include <cctype>
#include <cstdlib>
#include <mutex>
#include <unordered_map>

namespace dr2::i18n {

// i18n_en.cpp
extern const char* const kEnglish[][2];
extern const std::size_t kEnglishCount;

namespace {

std::atomic<Lang> g_lang{Lang::Pt};

const std::unordered_map<std::string_view, const char*>& table() {
    static const auto map = [] {
        std::unordered_map<std::string_view, const char*> m;
        m.reserve(kEnglishCount);
        for (std::size_t i = 0; i < kEnglishCount; ++i) m.emplace(kEnglish[i][0], kEnglish[i][1]);
        return m;
    }();
    return map;
}

// Ícone do Lucide em UTF-8: U+E000..U+F8FF começa com 0xEE/0xEF (3 bytes).
bool icon_at(std::string_view s, std::size_t i) {
    return i + 2 < s.size() && (static_cast<unsigned char>(s[i]) == 0xEE || static_cast<unsigned char>(s[i]) == 0xEF);
}

struct Parts {
    std::string_view head, key, tail;
};

Parts split(std::string_view s) {
    std::size_t a = 0;
    for (;;) {
        if (a < s.size() && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
        else if (icon_at(s, a)) a += 3;
        else break;
    }
    std::size_t b = s.find("##", a);
    if (b == std::string_view::npos) b = s.size();
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return {s.substr(0, a), s.substr(a, b - a), s.substr(b)};
}

const char* lookup(std::string_view key) {
    const auto& t = table();
    const auto it = t.find(key);
    return it == t.end() ? nullptr : it->second;
}

}  // namespace

Lang language() { return g_lang.load(std::memory_order_relaxed); }
void set_language(Lang lang) { g_lang.store(lang, std::memory_order_relaxed); }

bool from_locale(std::string_view loc, Lang& out) {
    std::string lang;
    for (char c : loc) {
        if (c == '_' || c == '-' || c == '.' || c == '@') break;
        lang += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    if (lang == "pt") out = Lang::Pt;
    else if (lang == "en") out = Lang::En;
    else return false;
    return true;
}

Lang pick(const std::vector<std::string>& preferred) {
    for (const std::string& loc : preferred) {
        Lang l;
        if (from_locale(loc, l)) return l;
    }
    return Lang::En;
}

std::vector<std::string> env_locales() {
    std::vector<std::string> out;
    if (const char* v = std::getenv("LANGUAGE"); v && *v) {
        std::string_view s = v;
        while (!s.empty()) {
            const std::size_t k = s.find(':');
            if (k) out.emplace_back(s.substr(0, k));
            if (k == std::string_view::npos) break;
            s.remove_prefix(k + 1);
        }
    }
    for (const char* name : {"LC_ALL", "LC_MESSAGES", "LANG"})
        if (const char* v = std::getenv(name); v && *v && std::string_view(v) != "C" && std::string_view(v) != "POSIX") {
            out.emplace_back(v);
            break;  // o primeiro definido manda, como no setlocale
        }
    return out;
}

const char* code(Lang lang) { return lang == Lang::Pt ? "pt" : "en"; }

bool parse_code(std::string_view c, Lang& out) { return from_locale(c, out); }

const char* tr(const char* pt) {
    if (!pt || language() == Lang::Pt) return pt;
    const std::string_view s = pt;
    const Parts p = split(s);
    const char* en = lookup(p.key);
    if (!en) return pt;
    if (p.head.empty() && p.tail.empty()) return en;
    // com ícone ou id em volta: monta uma vez e guarda (os nós do unordered_map não mudam de lugar)
    static std::mutex mutex;
    static std::unordered_map<std::string, std::string> cache;
    std::lock_guard lock(mutex);
    auto [it, fresh] = cache.try_emplace(std::string(s));
    if (fresh) it->second = std::string(p.head) + en + std::string(p.tail);
    return it->second.c_str();
}

std::string tr(const std::string& pt) {
    if (language() == Lang::Pt) return pt;
    const Parts p = split(pt);
    const char* en = lookup(p.key);
    return en ? std::string(p.head) + en + std::string(p.tail) : pt;
}

std::size_t table_size() { return kEnglishCount; }
const char* const* table_entry(std::size_t i) { return kEnglish[i]; }

}  // namespace dr2::i18n
