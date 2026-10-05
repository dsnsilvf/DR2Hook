// Cursor de leitura little-endian com checagem de limites (memcpy: não assume alinhamento).
#pragma once

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace dr2 {

class ByteReader {
public:
    ByteReader(const std::uint8_t* data, std::size_t size, const char* what) : data_(data), size_(size), what_(what) {}

    std::size_t pos() const { return pos_; }
    std::size_t size() const { return size_; }
    std::size_t left() const { return size_ - pos_; }

    void need(std::size_t n) const {
        if (n > left()) {
            throw std::runtime_error(std::string(what_) + ": arquivo truncado (precisa de " + std::to_string(n) +
                                     " bytes em " + std::to_string(pos_) + ", tem " + std::to_string(left()) + ")");
        }
    }
    void skip(std::size_t n) {
        need(n);
        pos_ += n;
    }
    void align4() {
        const std::size_t pad = (4 - (pos_ & 3)) & 3;
        skip(pad);
    }
    template <typename T>
    T get() {
        need(sizeof(T));
        T v;
        std::memcpy(&v, data_ + pos_, sizeof(T));  // o formato e as plataformas-alvo são little-endian
        pos_ += sizeof(T);
        return v;
    }
    // Copia `count` valores T para `out` (redimensionado).
    template <typename T>
    void get_array(std::vector<T>& out, std::size_t count) {
        if (count > left() / sizeof(T)) need(count * sizeof(T) + 1);  // mensagem de truncado sem estourar a conta
        out.resize(count);
        if (count) std::memcpy(out.data(), data_ + pos_, count * sizeof(T));
        pos_ += count * sizeof(T);
    }
    std::string get_string(std::size_t n) {
        need(n);
        std::string s(reinterpret_cast<const char*>(data_ + pos_), n);
        pos_ += n;
        return s;
    }

private:
    const std::uint8_t* data_;
    std::size_t size_;
    std::size_t pos_ = 0;
    const char* what_;
};

}  // namespace dr2
