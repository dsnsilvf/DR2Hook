#include "dr2hook/remote_commands.h"

#include "dr2hook/logger.h"
#include "dr2hook/safety.h"
#include "dr2hook/script/mod_manager.h"
#include "dr2hook/terminal_damage.h"
#include "dr2hook/ui/overlay.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace dr2hook {
namespace {

constexpr const char kCommandFile[] = "dr2hook_cmd.txt";
constexpr const char kOutputFile[] = "dr2hook_cmd.out";
constexpr ULONGLONG kPollMs = 150;

ULONGLONG g_lastPoll = 0;
unsigned g_batch = 0;

std::string Lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s;
}

std::vector<std::string> Split(const std::string &line) {
  std::vector<std::string> out;
  std::istringstream in(line);
  std::string word;
  while (in >> word) out.push_back(word);
  return out;
}

// Resto da linha depois de `skip` palavras (texto livre, com espacos).
std::string Rest(const std::string &line, size_t skip) {
  size_t pos = 0;
  for (size_t i = 0; i < skip; ++i) {
    pos = line.find_first_not_of(" \t", pos);
    if (pos == std::string::npos) return "";
    pos = line.find_first_of(" \t", pos);
    if (pos == std::string::npos) return "";
  }
  pos = line.find_first_not_of(" \t", pos);
  return pos == std::string::npos ? "" : line.substr(pos);
}

// "f8", "esc", "enter", "a", "0x77" ou numero decimal -> codigo VK; 0 se nao
// reconhece.
UINT ParseKey(const std::string &raw) {
  const std::string k = Lower(raw);
  static const struct { const char *name; UINT vk; } kNames[] = {
      {"esc", VK_ESCAPE},   {"escape", VK_ESCAPE}, {"enter", VK_RETURN},
      {"return", VK_RETURN}, {"space", VK_SPACE},  {"tab", VK_TAB},
      {"up", VK_UP},        {"down", VK_DOWN},     {"left", VK_LEFT},
      {"right", VK_RIGHT},  {"insert", VK_INSERT}, {"backspace", VK_BACK},
      {"pageup", VK_PRIOR}, {"pagedown", VK_NEXT}, {"home", VK_HOME},
      {"end", VK_END},      {"delete", VK_DELETE}};
  for (const auto &n : kNames) {
    if (k == n.name) return n.vk;
  }
  if (k.size() >= 2 && k[0] == 'f' && std::isdigit(static_cast<unsigned char>(k[1]))) {
    const int n = std::atoi(k.c_str() + 1);
    if (n >= 1 && n <= 24) return VK_F1 + (n - 1);
  }
  if (k.size() == 1 && std::isalnum(static_cast<unsigned char>(k[0]))) {
    return static_cast<UINT>(std::toupper(static_cast<unsigned char>(k[0])));
  }
  char *end = nullptr;
  const unsigned long v = std::strtoul(k.c_str(), &end, 0);
  return (end != nullptr && *end == '\0' && v > 0 && v < 256) ? static_cast<UINT>(v) : 0;
}

// Ultimo estado da pilha (topo) em linguagem simples, a partir do texto de
// FlowStack ("[st=<hex> id=<n> +59=<n>]").
unsigned long long TopRva() {
  const std::string stack = TerminalDamage::FlowStack();
  const size_t at = stack.rfind("st=");
  return at == std::string::npos ? 0 : std::strtoull(stack.c_str() + at + 3, nullptr, 16);
}

std::string Status() {
  const std::string stack = TerminalDamage::FlowStack();
  const size_t at = stack.rfind("st=");
  std::string top = "desconhecido";
  unsigned long long rva = 0;
  if (at != std::string::npos) {
    rva = std::strtoull(stack.c_str() + at + 3, nullptr, 16);
    switch (rva) {
    case 0x1251500: top = "corrida"; break;
    case 0x1250b50: top = "menu de pausa"; break;
    case 0x124b810: top = "cutscene"; break;
    case 0x1256e98: top = "tela de opcoes/menu"; break;
    case 0x1252600: top = "contagem/largada"; break;
    case 0x124c768: top = "carregando/estado auxiliar"; break;
    default: break;
    }
  } else {
    top = stack; // "sem coordenador" etc.: jogo ainda subindo
  }
  char buf[200];
  std::snprintf(buf, sizeof(buf), "topo=%s (st=%llx) escrita_liberada=%d", top.c_str(), rva,
                SafetyGuard::CanWriteState() ? 1 : 0);
  return buf;
}

std::string Help() {
  return "comandos: help | status | stack | pause | unpause | link <nome> | key <tecla> | crash | toast <texto> | "
         "mods | opt <mod> <opcao> [valor] | log <texto>";
}

std::string Mods() {
  std::ostringstream out;
  const auto snapshot = ModManager::MenuSnapshot();
  const auto &loaded = ModManager::GetLoadedMods();
  for (size_t m = 0; m < snapshot.size(); ++m) {
    out << "\n  [" << m << "] " << snapshot[m].name
        << (m < loaded.size() ? " (id " + loaded[m].id + ")" : "");
    for (size_t o = 0; o < snapshot[m].options.size(); ++o) {
      const ModOption &opt = snapshot[m].options[o];
      out << "\n      [" << o << "] " << opt.id << " = " << opt.DisplayLabel();
    }
  }
  return out.str();
}

// opt <mod> <opcao> [valor]: mod e opcao por indice ou id; valor e o indice do
// combo (toggle: 0 = Off, 1 = On) ou o texto do valor. Sem valor, age como a
// linha selecionada (botao chama; toggle e choice avancam).
std::string Option(const std::vector<std::string> &args) {
  if (args.size() < 3) return "uso: opt <mod> <opcao> [valor]";
  const auto snapshot = ModManager::MenuSnapshot();
  const auto &loaded = ModManager::GetLoadedMods();
  size_t modIndex = snapshot.size();
  for (size_t m = 0; m < snapshot.size() && m < loaded.size(); ++m) {
    if (loaded[m].id == args[1] || std::to_string(m) == args[1]) { modIndex = m; break; }
  }
  if (modIndex == snapshot.size()) return "mod nao encontrado (use: mods)";
  const auto &options = snapshot[modIndex].options;
  size_t optIndex = options.size();
  for (size_t o = 0; o < options.size(); ++o) {
    if (options[o].id == args[2] || std::to_string(o) == args[2]) { optIndex = o; break; }
  }
  if (optIndex == options.size()) return "opcao nao encontrada (use: mods)";
  int value = -1;
  if (args.size() >= 4) {
    const auto names = options[optIndex].ValueNames();
    const std::string want = Lower(args[3]);
    for (size_t v = 0; v < names.size(); ++v) {
      if (Lower(names[v]) == want) { value = static_cast<int>(v); break; }
    }
    if (value < 0) {
      char *end = nullptr;
      const long n = std::strtol(args[3].c_str(), &end, 10);
      if (end == nullptr || *end != '\0' || n < 0 || static_cast<size_t>(n) >= names.size()) {
        return "valor invalido";
      }
      value = static_cast<int>(n);
    }
  }
  ModManager::DispatchMenuEvent(modIndex, optIndex, value);
  const auto after = ModManager::MenuSnapshot();
  return "ok: " + after[modIndex].options[optIndex].DisplayLabel();
}

std::string Run(const std::string &line, HWND hwnd) {
  const auto args = Split(line);
  if (args.empty()) return "";
  const std::string cmd = Lower(args[0]);
  if (cmd == "help") return Help();
  if (cmd == "status") return Status();
  if (cmd == "stack") return TerminalDamage::FlowStack();
  if (cmd == "mods") return Mods();
  if (cmd == "opt") return Option(args);
  if (cmd == "log") {
    Logger::Info("[remoto] " + Rest(line, 1));
    return "ok";
  }
  if (cmd == "toast") {
    if (!OverlayManager::IsInitialized()) return "overlay ainda nao iniciado";
    OverlayManager::AddNotification(Rest(line, 1), 3.0f, ToastType::Info);
    return "ok";
  }
  if (cmd == "key") {
    if (args.size() < 2) return "uso: key <tecla>";
    const UINT vk = ParseKey(args[1]);
    if (vk == 0) return "tecla desconhecida: " + args[1];
    // Pelo WndProc do jogo: F8, F9, F11, Insert e o onKeyDown dos mods passam
    // por aqui como se o jogador tivesse apertado. O jogo le o teclado por
    // Raw Input, entao isto nao move os menus nativos (use `link`).
    PostMessageW(hwnd, WM_KEYDOWN, vk, 1);
    PostMessageW(hwnd, WM_KEYUP, vk, (1u << 31) | (1u << 30) | 1);
    return "ok";
  }
  if (cmd == "link") {
    if (args.size() < 2) return "uso: link <nome>";
    if (!SafetyGuard::CanWriteState()) return "bloqueado: evento online";
    return TerminalDamage::PostLink(args[1]);
  }
  if (cmd == "pause") {
    if (!SafetyGuard::CanWriteState()) return "bloqueado: evento online";
    return TerminalDamage::PostPause();
  }
  if (cmd == "unpause") {
    return TerminalDamage::PostLink("continue");
  }
  if (cmd == "crash") {
    if (TopRva() != 0x1251500) return "recusado: espere a corrida (status = corrida)";
    using R = TerminalDamage::Result;
    switch (TerminalDamage::Crash()) {
    case R::Done: return "ok: carro destruido";
    case R::Blocked: return "bloqueado: evento online";
    case R::NoController: return "fora de uma especial";
    case R::BadChain: return "estado do jogo inesperado";
    case R::AlreadyDown: return "carro ja destruido";
    }
  }
  return "comando desconhecido: " + args[0] + " (help)";
}

} // namespace

void RemoteCommands::Poll(HWND hwnd) {
  const ULONGLONG now = GetTickCount64();
  if (now - g_lastPoll < kPollMs) return;
  g_lastPoll = now;

  std::ifstream in(kCommandFile);
  if (!in) return;
  std::vector<std::string> lines;
  for (std::string line; std::getline(in, line);) {
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
    if (!line.empty() && line[0] != '#') lines.push_back(line);
  }
  in.close();
  std::remove(kCommandFile);
  if (lines.empty()) return;

  std::ofstream out(kOutputFile, std::ios::trunc);
  out << "# lote " << ++g_batch << "\n";
  for (const std::string &line : lines) {
    std::string result;
    try {
      result = Run(line, hwnd);
    } catch (...) {
      result = "excecao";
    }
    Logger::Info("[remoto] " + line + " -> " + result);
    out << "> " << line << "\n" << result << "\n";
  }
}

} // namespace dr2hook
