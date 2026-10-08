#pragma once

#include "dr2hook/common.h"

#include <cstdint>
#include <string>

namespace dr2hook {

// Câmera livre da especial. O hook fica no core: Install no Core_Initialize,
// Shutdown no Core_Shutdown. F9 liga e desliga. Com ela ligada, o mouse olha
// e WASD se move; Espaço e Q sobem e descem. Ctrl segurado trava o teclado
// e deixa o mouse. + e - mudam a velocidade do teclado. Shift multiplica
// essa velocidade por 4. A pose é gravada em câmera+0x210..+0x240 depois do
// tick nativo 0x140a51ea0, no bloco que a especial acabou de preencher.
class FreeCamera {
public:
  static bool Install(uintptr_t gameBase);
  static void Shutdown();
  static void OnFrame(HWND hwnd);
  // 1 quando a mensagem foi consumida e não deve seguir para o jogo.
  static int OnWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
  // Comandos remotos (`cam`): posição do olho e alvo em metros do mundo. A pose só é
  // aplicada com a câmera livre ligada (F9, ou `key f9`).
  static std::string RemoteLookAt(float ex, float ey, float ez, float tx, float ty, float tz);
  static std::string RemotePose();
  // Pose da câmera da especial no último tick (a livre, se ligada): olho, frente e
  // cima em metros do mundo. false se não houve tick nos últimos 500 ms.
  static bool StageView(float eye[3], float forward[3], float up[3]);
};

} // namespace dr2hook
