#include "dr2hook/path_redirect.h"

#include <cassert>
#include <iostream>

using namespace dr2hook;

int main() {
  const auto rules = ParseRedirectRules(
      "; comentário\n"
      "\n"
      "Locations\\Portugal__Montalegre_Rallycross.nefs = Z:\\tmp\\copia.nefs\r\n"
      "/locations/other.nefs=Z:/tmp/outra.nefs\n"
      "sem igual\n"
      "vazio =\n"
      "= vazio\n"
      "# outro comentário\n");
  assert(rules.size() == 2);
  assert(rules[0].from == L"locations\\portugal__montalegre_rallycross.nefs");
  assert(rules[0].to == L"Z:\\tmp\\copia.nefs");

  std::wstring out;
  // sufixo casa sem diferenciar maiúsculas nem barras
  assert(ApplyRedirect(rules,
                       L"S:\\steamapps\\common\\DiRT Rally 2.0\\locations\\portugal__montalegre_rallycross.nefs", &out));
  assert(out == L"Z:\\tmp\\copia.nefs");
  assert(ApplyRedirect(rules, L"S:/Steam/LOCATIONS/other.NEFS", &out));
  assert(out == L"Z:/tmp/outra.nefs");
  // limite de pasta: "xlocations\..." não casa
  assert(!ApplyRedirect(rules, L"S:\\xlocations\\other.nefs", &out));
  // outra localidade e caminho curto demais
  assert(!ApplyRedirect(rules, L"S:\\game\\locations\\uk__wales_rally_01.nefs", &out));
  assert(!ApplyRedirect(rules, L"other.nefs", &out));
  // caminho que é exatamente o sufixo
  assert(ApplyRedirect(rules, L"locations\\other.nefs", &out));
  // sem regras, nada muda
  assert(!ApplyRedirect({}, L"locations\\other.nefs", &out));
  std::cout << "path_redirect: ok\n";
  return 0;
}
