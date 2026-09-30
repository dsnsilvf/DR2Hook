#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// XML binário em blocos da EGO Engine (system/*.bin). O formato está em
// docs/reverse_engineering/ui_data.md; Encode(Decode(x)) reproduz x byte a byte.
namespace dr2hook::bxml {

struct Node {
  std::string name;
  std::optional<std::string> text;
  std::vector<std::pair<std::string, std::string>> attributes;
  std::vector<Node> children;

  const std::string *Attribute(std::string_view key) const;
  void SetAttribute(std::string_view key, std::string_view value);
  bool RemoveChild(std::string_view childName);
  Node *Child(std::string_view childName);
};

bool Decode(const uint8_t *data, size_t size, Node &root);
std::vector<uint8_t> Encode(const Node &root);

} // namespace dr2hook::bxml
