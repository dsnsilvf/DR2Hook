// Idioma da interface: português (o texto do código) ou inglês. O código escreve o texto em português e passa
// por tr(); em inglês, tr() devolve a tradução da tabela do i18n_en.cpp (ou o próprio texto, se faltar).
//
// tr() ignora na busca o que fica em volta do texto: ícones do Lucide e espaços no começo, espaços no fim e o id
// do ImGui ("##..."/"###...") — tr(ICON_SAVE "  Gravar##b") acha "Gravar" e devolve ICON_SAVE "  Save##b".
//
// O idioma vem do sistema (from_locale com o primeiro idioma suportado da lista do usuário); qualquer outro idioma
// fica em inglês. Sem SDL nem GL: o app passa a lista do SDL_GetPreferredLocales, e os testes usam o padrão.
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace dr2::i18n {

enum class Lang { Pt, En };

// Começa em português (o texto do código), para os testes e as ferramentas de linha de comando.
Lang language();
void set_language(Lang lang);

// "pt_BR.UTF-8", "pt-PT", "pt" -> Pt; "en_US" -> En; vazio ou não suportado -> false (`out` fica como estava).
bool from_locale(std::string_view locale, Lang& out);
// Primeiro idioma suportado de uma lista em ordem de preferência; nenhum -> En.
Lang pick(const std::vector<std::string>& preferred);
// LANGUAGE (lista com ':'), LC_ALL, LC_MESSAGES e LANG, nessa ordem, como o gettext.
std::vector<std::string> env_locales();

const char* code(Lang lang);  // "pt" / "en"
bool parse_code(std::string_view code, Lang& out);

#if defined(__GNUC__)
#define DR2_FORMAT_ARG __attribute__((format_arg(1)))
#else
#define DR2_FORMAT_ARG
#endif

// O ponteiro devolvido vale enquanto o programa roda (texto estático ou cópia guardada).
const char* tr(const char* pt) DR2_FORMAT_ARG;
// Para textos que chegam em tempo de execução (rótulos de scripts, por exemplo).
std::string tr(const std::string& pt);

// A tabela inglesa, para os testes: quantas entradas e o par {português, inglês} de cada uma.
std::size_t table_size();
const char* const* table_entry(std::size_t i);

}  // namespace dr2::i18n
