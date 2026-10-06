#pragma once

#include <string>

namespace dr2hook {

// Sonda de engenharia reversa do carregamento de pista. Ligada por dr2hook_loadprobe.ini
// (ao lado do exe); sem o ini não instala nada. Grava uma linha por evento em
// <out>/trace_<data>.tsv: arquivos abertos, leituras dos pacotes com a posição no arquivo,
// recursos D3D11 criados (buffers, texturas, layouts de vértice, sombreadores) e eventos
// nomeados da corrida, cada um com a pilha de chamadas do exe. O bytecode dos sombreadores
// vai para <out>/shaders/. Instalar depois de InitializeHooks e antes de InstallLoadTrace.
bool InstallLoadProbe();
void UninstallLoadProbe();

// Chamados pelos detours do LoadTrace (CreateFileW/ReadFile já têm hook lá).
void LoadProbeOnOpen(void *handle, const wchar_t *path, unsigned long access);
void LoadProbeOnRead(void *handle, unsigned long size, void *overlapped);
// Marca na linha do tempo (eventos nomeados da corrida, abertura de especial).
void LoadProbeMark(const char *what);
// Overlay experimental (overlay_dir= no ini): um caminho sob <jogo>\dr2hook_overlay\ vira o mesmo
// caminho sob overlay_dir. Devolve true e preenche *out quando reescreve.
bool LoadProbeRewritePath(const wchar_t *path, std::wstring *out);

} // namespace dr2hook
