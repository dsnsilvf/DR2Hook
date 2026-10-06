#pragma once

#include <string>
#include <vector>

namespace dr2hook {

// Uma regra do dr2hook_redirect.ini: todo caminho que termina em `from` passa a abrir `to`.
// `from` fica em minúsculas e com '\' (sem diferenciar maiúsculas nem o tipo de barra).
struct RedirectRule {
  std::wstring from;
  std::wstring to;
};

// Texto do ini: uma regra por linha, "sufixo = destino". Linhas vazias e as que começam
// com ';' ou '#' são ignoradas, assim como as sem '=' ou com um dos lados vazio.
std::vector<RedirectRule> ParseRedirectRules(const std::string &text);

// Se `path` termina com o sufixo de uma regra (e o sufixo começa num limite de pasta),
// devolve true e grava o destino em `*out`. A primeira regra que casa vale.
bool ApplyRedirect(const std::vector<RedirectRule> &rules, const std::wstring &path,
                   std::wstring *out);

} // namespace dr2hook
