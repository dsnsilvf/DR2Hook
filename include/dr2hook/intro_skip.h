#pragma once

namespace dr2hook {

// Pula o vídeo do logo na abertura do jogo (video/studio_logo.bk2, 7 s).
// Intercepta o BinkOpen do bink2w64.dll: loga cada vídeo aberto e, para o
// logo, devolve "não abriu", como se o arquivo faltasse. Só pula numa abertura
// pelo editor (editor_launch.h) ou com pular_logo=1 no dr2hook_intro.ini; numa
// abertura normal só loga e o vídeo toca.
class IntroSkip {
public:
  static void Install();
  // Tira o hook antes do core ser descarregado (F8).
  static void Shutdown();
};

} // namespace dr2hook
