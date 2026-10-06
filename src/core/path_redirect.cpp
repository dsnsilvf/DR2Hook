#include "dr2hook/path_redirect.h"

#include <cwctype>

namespace dr2hook {
namespace {

std::string Trim(const std::string &s) {
  const char *ws = " \t\r\n";
  const size_t a = s.find_first_not_of(ws);
  if (a == std::string::npos) return {};
  return s.substr(a, s.find_last_not_of(ws) - a + 1);
}

// UTF-8 para wchar_t; bytes inválidos viram U+FFFD.
std::wstring Widen(const std::string &s) {
  std::wstring out;
  for (size_t i = 0; i < s.size();) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    unsigned len = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
    if (len == 0 || i + len > s.size()) {
      out.push_back(L'\xFFFD');
      ++i;
      continue;
    }
    unsigned long cp = len == 1 ? c : c & (0xFFu >> (len + 1));
    for (unsigned k = 1; k < len; ++k) cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3F);
    i += len;
    if (cp > 0xFFFF && sizeof(wchar_t) == 2) {
      cp -= 0x10000;
      out.push_back(static_cast<wchar_t>(0xD800 + (cp >> 10)));
      out.push_back(static_cast<wchar_t>(0xDC00 + (cp & 0x3FF)));
    } else {
      out.push_back(static_cast<wchar_t>(cp));
    }
  }
  return out;
}

std::wstring Normalize(const std::wstring &s) {
  std::wstring out = s;
  for (auto &c : out) c = c == L'/' ? L'\\' : static_cast<wchar_t>(std::towlower(c));
  return out;
}

} // namespace

std::vector<RedirectRule> ParseRedirectRules(const std::string &text) {
  std::vector<RedirectRule> rules;
  size_t pos = 0;
  while (pos <= text.size()) {
    size_t end = text.find('\n', pos);
    if (end == std::string::npos) end = text.size();
    const std::string line = Trim(text.substr(pos, end - pos));
    pos = end + 1;
    if (line.empty() || line[0] == ';' || line[0] == '#') continue;
    const size_t eq = line.find('=');
    if (eq == std::string::npos) continue;
    const std::string from = Trim(line.substr(0, eq));
    const std::string to = Trim(line.substr(eq + 1));
    if (from.empty() || to.empty()) continue;
    std::wstring key = Normalize(Widen(from));
    while (!key.empty() && key.front() == L'\\') key.erase(key.begin());
    if (key.empty()) continue;
    rules.push_back({key, Widen(to)});
  }
  return rules;
}

bool ApplyRedirect(const std::vector<RedirectRule> &rules, const std::wstring &path,
                   std::wstring *out) {
  if (rules.empty()) return false;
  const std::wstring norm = Normalize(path);
  for (const RedirectRule &r : rules) {
    if (norm.size() < r.from.size()) continue;
    const size_t start = norm.size() - r.from.size();
    if (norm.compare(start, r.from.size(), r.from) != 0) continue;
    if (start != 0 && norm[start - 1] != L'\\') continue;
    *out = r.to;
    return true;
  }
  return false;
}

} // namespace dr2hook
