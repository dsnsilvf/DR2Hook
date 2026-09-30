#include "dr2hook/bxml.h"

#include <algorithm>
#include <cstring>
#include <unordered_map>

namespace dr2hook::bxml {
namespace {

constexpr uint32_t kRoot = 0x7252221a;
constexpr uint32_t kNodes = 0x7252221b;
constexpr uint32_t kAttributes = 0x7252221c;
constexpr uint32_t kStrings = 0x7252221d;
constexpr uint32_t kStringOffsets = 0x7252221e;
constexpr uint32_t kStringTable = 0x72522217;
constexpr size_t kMaxDepth = 256;

struct Span {
  size_t offset = 0;
  size_t size = 0;
  bool found = false;
};

struct RawNode {
  uint32_t name, text, attributeCount, firstAttribute, childCount, firstChild;
};

uint32_t ReadU32(const uint8_t *data, size_t offset) {
  uint32_t value;
  std::memcpy(&value, data + offset, sizeof(value));
  return value;
}

void AppendU32(std::vector<uint8_t> &out, uint32_t value) {
  const auto *bytes = reinterpret_cast<const uint8_t *>(&value);
  out.insert(out.end(), bytes, bytes + sizeof(value));
}

bool FindChunks(const uint8_t *data, size_t begin, size_t end, Span &strings,
                Span &offsets, Span &nodes, Span &attributes) {
  while (begin < end) {
    if (end - begin < 8) {
      return false;
    }
    const uint32_t kind = ReadU32(data, begin);
    const size_t size = ReadU32(data, begin + 4);
    const size_t body = begin + 8;
    if (size > end - body) {
      return false;
    }
    if (kind == kStringTable) {
      if (!FindChunks(data, body, body + size, strings, offsets, nodes,
                      attributes)) {
        return false;
      }
    } else {
      Span *target = kind == kStrings          ? &strings
                     : kind == kStringOffsets ? &offsets
                     : kind == kNodes         ? &nodes
                     : kind == kAttributes    ? &attributes
                                              : nullptr;
      if (target != nullptr) {
        *target = {body, size, true};
      }
    }
    begin = body + size;
  }
  return true;
}

struct Decoder {
  std::vector<std::string> strings;
  std::vector<RawNode> nodes;
  std::vector<std::pair<uint32_t, uint32_t>> attributes;

  bool Build(uint32_t index, size_t depth, Node &out) const {
    if (index >= nodes.size() || depth > kMaxDepth) {
      return false;
    }
    const RawNode &raw = nodes[index];
    if (raw.name >= strings.size() || raw.text >= strings.size()) {
      return false;
    }
    out.name = strings[raw.name];
    if (raw.text != 0) {
      out.text = strings[raw.text];
    }
    if (raw.attributeCount > 0) {
      if (raw.firstAttribute > attributes.size() ||
          raw.attributeCount > attributes.size() - raw.firstAttribute) {
        return false;
      }
      for (uint32_t i = 0; i < raw.attributeCount; ++i) {
        const auto &[key, value] = attributes[raw.firstAttribute + i];
        if (key >= strings.size() || value >= strings.size()) {
          return false;
        }
        out.attributes.emplace_back(strings[key], strings[value]);
      }
    }
    if (raw.childCount > 0) {
      if (raw.firstChild > nodes.size() ||
          raw.childCount > nodes.size() - raw.firstChild ||
          raw.firstChild <= index) {
        return false;
      }
      out.children.resize(raw.childCount);
      for (uint32_t i = 0; i < raw.childCount; ++i) {
        if (!Build(raw.firstChild + i, depth + 1, out.children[i])) {
          return false;
        }
      }
    }
    return true;
  }
};

struct Encoder {
  std::vector<const std::string *> strings;
  std::unordered_map<std::string, uint32_t> index;
  std::vector<RawNode> nodes;
  std::vector<std::pair<uint32_t, uint32_t>> attributes;

  uint32_t Intern(const std::string &text) {
    const auto [it, inserted] =
        index.emplace(text, static_cast<uint32_t>(strings.size()));
    if (inserted) {
      strings.push_back(&it->first);
    }
    return it->second;
  }

  void InternTree(const Node &node) {
    Intern(node.name);
    for (const auto &[key, value] : node.attributes) {
      Intern(key);
      Intern(value);
    }
    for (const Node &child : node.children) {
      InternTree(child);
    }
    if (node.text) {
      Intern(*node.text);
    }
  }

  void Place(size_t slot, const Node &node) {
    const auto firstAttribute = static_cast<uint32_t>(attributes.size());
    for (const auto &[key, value] : node.attributes) {
      attributes.emplace_back(index.at(key), index.at(value));
    }
    const auto firstChild = static_cast<uint32_t>(nodes.size());
    nodes.resize(nodes.size() + node.children.size());
    nodes[slot] = {index.at(node.name),
                   node.text ? index.at(*node.text) : 0u,
                   static_cast<uint32_t>(node.attributes.size()),
                   node.attributes.empty() ? 0u : firstAttribute,
                   static_cast<uint32_t>(node.children.size()), firstChild};
    for (size_t i = 0; i < node.children.size(); ++i) {
      Place(firstChild + i, node.children[i]);
    }
  }
};

void AppendChunk(std::vector<uint8_t> &out, uint32_t kind,
                 const std::vector<uint8_t> &payload) {
  AppendU32(out, kind);
  AppendU32(out, static_cast<uint32_t>(payload.size()));
  out.insert(out.end(), payload.begin(), payload.end());
}

} // namespace

const std::string *Node::Attribute(std::string_view key) const {
  for (const auto &[k, v] : attributes) {
    if (k == key) {
      return &v;
    }
  }
  return nullptr;
}

void Node::SetAttribute(std::string_view key, std::string_view value) {
  for (auto &[k, v] : attributes) {
    if (k == key) {
      v = value;
      return;
    }
  }
  attributes.emplace_back(std::string(key), std::string(value));
}

bool Node::RemoveChild(std::string_view childName) {
  const auto it = std::find_if(
      children.begin(), children.end(),
      [&](const Node &child) { return child.name == childName; });
  if (it == children.end()) {
    return false;
  }
  children.erase(it);
  return true;
}

Node *Node::Child(std::string_view childName) {
  for (Node &child : children) {
    if (child.name == childName) {
      return &child;
    }
  }
  return nullptr;
}

bool Decode(const uint8_t *data, size_t size, Node &root) {
  if (data == nullptr || size < 8 || ReadU32(data, 0) != kRoot) {
    return false;
  }
  const size_t rootSize = ReadU32(data, 4);
  if (rootSize > size - 8) {
    return false;
  }
  Span strings, offsets, nodes, attributes;
  if (!FindChunks(data, 8, 8 + rootSize, strings, offsets, nodes,
                  attributes) ||
      !strings.found || !offsets.found || !nodes.found || !attributes.found ||
      offsets.size % 4 != 0 || nodes.size % 24 != 0 || attributes.size % 8 != 0 ||
      nodes.size == 0) {
    return false;
  }

  Decoder decoder;
  const char *blob = reinterpret_cast<const char *>(data + strings.offset);
  for (size_t i = 0; i < offsets.size / 4; ++i) {
    const size_t start = ReadU32(data, offsets.offset + i * 4);
    if (start >= strings.size) {
      return false;
    }
    const void *terminator =
        std::memchr(blob + start, '\0', strings.size - start);
    if (terminator == nullptr) {
      return false;
    }
    decoder.strings.emplace_back(blob + start,
                                 static_cast<const char *>(terminator));
  }
  for (size_t i = 0; i < nodes.size / 24; ++i) {
    RawNode raw;
    std::memcpy(&raw, data + nodes.offset + i * 24, sizeof(raw));
    decoder.nodes.push_back(raw);
  }
  for (size_t i = 0; i < attributes.size / 8; ++i) {
    decoder.attributes.emplace_back(ReadU32(data, attributes.offset + i * 8),
                                    ReadU32(data, attributes.offset + i * 8 + 4));
  }
  root = Node{};
  return decoder.Build(0, 0, root);
}

std::vector<uint8_t> Encode(const Node &root) {
  Encoder encoder;
  encoder.InternTree(root);
  encoder.nodes.resize(1);
  encoder.Place(0, root);

  std::vector<uint8_t> blob;
  std::vector<uint8_t> offsets;
  for (const std::string *text : encoder.strings) {
    AppendU32(offsets, static_cast<uint32_t>(blob.size()));
    blob.insert(blob.end(), text->begin(), text->end());
    blob.push_back(0);
  }
  blob.resize((blob.size() + 15) / 16 * 16, 0);

  std::vector<uint8_t> table;
  AppendChunk(table, kStrings, blob);
  AppendChunk(table, kStringOffsets, offsets);

  std::vector<uint8_t> nodeBytes;
  for (const RawNode &raw : encoder.nodes) {
    const auto *bytes = reinterpret_cast<const uint8_t *>(&raw);
    nodeBytes.insert(nodeBytes.end(), bytes, bytes + sizeof(raw));
  }
  std::vector<uint8_t> attributeBytes;
  for (const auto &[key, value] : encoder.attributes) {
    AppendU32(attributeBytes, key);
    AppendU32(attributeBytes, value);
  }

  std::vector<uint8_t> body;
  AppendChunk(body, kStringTable, table);
  AppendChunk(body, kNodes, nodeBytes);
  AppendChunk(body, kAttributes, attributeBytes);

  std::vector<uint8_t> out;
  AppendChunk(out, kRoot, body);
  return out;
}

} // namespace dr2hook::bxml
