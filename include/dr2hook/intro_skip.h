#pragma once

namespace dr2hook {

// Pula o vídeo do logo na abertura do jogo (video/studio_logo.bk2, 7 s).
// Intercepta o BinkOpen do bink2w64.dll: loga cada vídeo aberto e, para o
// logo, devolve "não abriu", como se o arquivo faltasse. Desliga com
// pular_logo=0 no dr2hook_intro.ini da pasta do jogo.
class IntroSkip {
public:
  static void Install();
  // Tira o hook antes do core ser descarregado (F8).
  static void Shutdown();
};

} // namespace dr2hook
