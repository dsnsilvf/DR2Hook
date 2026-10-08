#pragma once

#include "dr2hook/common.h"
#include <d3d11.h>
#include <dxgi.h>
#include <string>

namespace dr2hook {

// O que o jogo desenha durante a carga: engancha os draws do contexto
// imediato do D3D11 e conta, por quadro, em que alvos (render targets) o jogo
// desenha. Liga com a tela preta da inicialização rápida; loga um resumo por
// segundo ("LoadView:") e acha o alvo grande onde a cena 3D seria desenhada,
// para o terminal mostrá-lo de fundo.
class LoadView {
public:
  // Engancha o contexto (uma vez); depois disso só custa um contador por draw.
  static bool Install(ID3D11DeviceContext *context);
  static void Shutdown();
  // Liga/desliga a medição (a tela preta). Desligar mantém mais 3 s de log,
  // para comparar com a corrida.
  static void SetActive(bool on);
  // No Present, antes do overlay desenhar: fecha o quadro do jogo.
  static void OnFrame(IDXGISwapChain *swapChain);
  // Rota da carga ("<pista>_route_N", evento kDr2StageRoute da proxy): chave do
  // "olhar=auto" e da pose da largada gravada em dr2hook_olhar.ini.
  static void NoteRoute(const char *key);
  // Os draws do próprio overlay não contam.
  static void SetOwnDraws(bool own);
  // Alvo com a cena do jogo neste quadro (nullptr se não há): SRV pronto
  // para o ImGui, válido até o próximo OnFrame.
  static ID3D11ShaderResourceView *SceneView(float *aspect);
  // Exposição para a curva de tonemap do fundo quando a cena é HDR (luz
  // linear); 0 = cena LDR ou ainda sem medida (mostrar como está).
  static float SceneExposure();
  // Uma linha para o terminal ("GPU: ...").
  static std::string StatusLine();
};

} // namespace dr2hook
