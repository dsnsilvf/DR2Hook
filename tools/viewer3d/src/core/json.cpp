#include "core/json.hpp"

#include "core/io.hpp"

#include <cstdio>
#include <cstdlib>
#include <stdexcept>

namespace dr2::json {

namespace {

[[noreturn]] void fail(const char* what, std::size_t at) {
    char msg[160];
    std::snprintf(msg, sizeof msg, "json: %s no byte %zu", what, at);
    throw std::runtime_error(msg);
}

class Parser {
public:
    explicit Parser(std::string_view s) : s_(s) {}

    Value document() {
        Value v = value(0);
        ws();
        if (i_ != s_.size()) fail("lixo depois do valor", i_);
        return v;
    }

private:
    static constexpr int kMaxDepth = 256;

    void ws() {
        while (i_ < s_.size() && (s_[i_] == ' ' || s_[i_] == '\t' || s_[i_] == '\n' || s_[i_] == '\r')) ++i_;
    }
    char peek() {
        ws();
        if (i_ >= s_.size()) fail("fim inesperado", i_);
        return s_[i_];
    }
    void expect(char c) {
        if (peek() != c) {
            char what[32];
            std::snprintf(what, sizeof what, "esperava '%c'", c);
            fail(what, i_);
        }
        ++i_;
    }
    void literal(std::string_view word) {
        if (s_.substr(i_, word.size()) != word) fail("literal inválido", i_);
        i_ += word.size();
    }

    Value value(int depth) {
        if (depth > kMaxDepth) fail("aninhamento fundo demais", i_);
        switch (peek()) {
        case '{': return object(depth);
        case '[': return array(depth);
        case '"': return Value(string());
        case 't': literal("true"); return Value(true);
        case 'f': literal("false"); return Value(false);
        case 'n': literal("null"); return Value();
        default: return Value(number());
        }
    }

    Value object(int depth) {
        expect('{');
        Object out;
        if (peek() == '}') {
            ++i_;
            return Value(std::move(out));
        }
        for (;;) {
            if (peek() != '"') fail("esperava chave", i_);
            std::string key = string();
            expect(':');
            out.insert_or_assign(std::move(key), value(depth + 1));
            char c = peek();
            ++i_;
            if (c == '}') break;
            if (c != ',') fail("esperava ',' ou '}'", i_ - 1);
        }
        return Value(std::move(out));
    }

    Value array(int depth) {
        expect('[');
        Array out;
        if (peek() == ']') {
            ++i_;
            return Value(std::move(out));
        }
        for (;;) {
            out.push_back(value(depth + 1));
            char c = peek();
            ++i_;
            if (c == ']') break;
            if (c != ',') fail("esperava ',' ou ']'", i_ - 1);
        }
        return Value(std::move(out));
    }

    static void put_utf8(std::string& out, unsigned cp) {
        if (cp < 0x80) {
            out += static_cast<char>(cp);
        } else if (cp < 0x800) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }

    unsigned hex4() {
        if (i_ + 4 > s_.size()) fail("\\u curto", i_);
        unsigned v = 0;
        for (int k = 0; k < 4; ++k) {
            char c = s_[i_++];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= static_cast<unsigned>(c - '0');
            else if (c >= 'a' && c <= 'f') v |= static_cast<unsigned>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= static_cast<unsigned>(c - 'A' + 10);
            else fail("\\u inválido", i_ - 1);
        }
        return v;
    }

    std::string string() {
        expect('"');
        std::string out;
        for (;;) {
            if (i_ >= s_.size()) fail("string sem fim", i_);
            char c = s_[i_++];
            if (c == '"') break;
            if (static_cast<unsigned char>(c) < 0x20) fail("caractere de controle na string", i_ - 1);
            if (c != '\\') {
                out += c;
                continue;
            }
            if (i_ >= s_.size()) fail("escape sem fim", i_);
            char e = s_[i_++];
            switch (e) {
            case '"': out += '"'; break;
            case '\\': out += '\\'; break;
            case '/': out += '/'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'u': {
                unsigned cp = hex4();
                if (cp >= 0xD800 && cp <= 0xDBFF) {  // par substituto
                    if (i_ + 2 > s_.size() || s_[i_] != '\\' || s_[i_ + 1] != 'u') fail("par substituto incompleto", i_);
                    i_ += 2;
                    unsigned lo = hex4();
                    if (lo < 0xDC00 || lo > 0xDFFF) fail("par substituto inválido", i_);
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                }
                put_utf8(out, cp);
                break;
            }
            default: fail("escape inválido", i_ - 1);
            }
        }
        return out;
    }

    double number() {
        const std::size_t start = i_;
        if (i_ < s_.size() && s_[i_] == '-') ++i_;
        auto digits = [&] {
            const std::size_t d0 = i_;
            while (i_ < s_.size() && s_[i_] >= '0' && s_[i_] <= '9') ++i_;
            return i_ > d0;
        };
        if (!digits()) fail("número inválido", start);
        if (i_ < s_.size() && s_[i_] == '.') {
            ++i_;
            if (!digits()) fail("número inválido", start);
        }
        if (i_ < s_.size() && (s_[i_] == 'e' || s_[i_] == 'E')) {
            ++i_;
            if (i_ < s_.size() && (s_[i_] == '+' || s_[i_] == '-')) ++i_;
            if (!digits()) fail("número inválido", start);
        }
        // Infinity/NaN do Python não são JSON e caem no erro acima.
        std::string text(s_.substr(start, i_ - start));
        return std::strtod(text.c_str(), nullptr);
    }

    std::string_view s_;
    std::size_t i_ = 0;
};

[[noreturn]] void type_error(const char* want) { throw std::runtime_error(std::string("json: valor não é ") + want); }

}  // namespace

bool Value::as_bool() const {
    if (!is_bool()) type_error("booleano");
    return std::get<bool>(v_);
}
double Value::as_number() const {
    if (!is_number()) type_error("número");
    return std::get<double>(v_);
}
const std::string& Value::as_string() const {
    if (!is_string()) type_error("string");
    return std::get<std::string>(v_);
}
const Array& Value::as_array() const {
    if (!is_array()) type_error("lista");
    return *std::get<std::shared_ptr<Array>>(v_);
}
const Object& Value::as_object() const {
    if (!is_object()) type_error("objeto");
    return *std::get<std::shared_ptr<Object>>(v_);
}

const Value* Value::find(std::string_view key) const {
    if (!is_object()) return nullptr;
    const Object& o = as_object();
    auto it = o.find(key);
    return it == o.end() ? nullptr : &it->second;
}

const Value& Value::operator[](std::string_view key) const {
    const Value* v = find(key);
    if (!v) {
        if (!is_object()) type_error("objeto");
        throw std::runtime_error("json: falta a chave \"" + std::string(key) + "\"");
    }
    return *v;
}

const Value& Value::operator[](std::size_t i) const {
    const Array& a = as_array();
    if (i >= a.size()) throw std::runtime_error("json: índice fora da lista");
    return a[i];
}

std::size_t Value::size() const {
    if (is_array()) return as_array().size();
    if (is_object()) return as_object().size();
    return 0;
}

double Value::number_or(std::string_view key, double fallback) const {
    const Value* v = find(key);
    return v && v->is_number() ? v->as_number() : fallback;
}

Value parse(std::string_view text) {
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF && static_cast<unsigned char>(text[1]) == 0xBB &&
        static_cast<unsigned char>(text[2]) == 0xBF)
        text.remove_prefix(3);  // BOM
    return Parser(text).document();
}

Value parse_file(const std::string& path) {
    const std::vector<std::uint8_t> bytes = read_file(path);
    return parse(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
}

std::string quote(std::string_view s) {
    std::string out = "\"";
    for (char c : s) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof buf, "\\u%04x", static_cast<unsigned>(static_cast<unsigned char>(c)));
                out += buf;
            } else {
                out += c;
            }
        }
    }
    out += '"';
    return out;
}

}  // namespace dr2::json
