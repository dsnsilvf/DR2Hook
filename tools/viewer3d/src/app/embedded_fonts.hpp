// Fontes e a imagem do Sobre dentro do executável (assets/fonts, assets/about): o CMake grava os bytes em
// generated/embedded_fonts.cpp.
#pragma once

#include <cstddef>

namespace dr2::app::fonts {

struct Blob {
    const unsigned char* data;
    std::size_t size;
};

extern const Blob kInterRegular;   // texto
extern const Blob kInterSemiBold;  // títulos das seções
extern const Blob kLucide;         // ícones (macros ICON_* em icons.hpp)
extern const Blob kAboutImage;     // imagem da janela Sobre (WebP)

}  // namespace dr2::app::fonts
