// Leitor de JSON mínimo: o que o json.dump do Python grava (objetos, listas, números, strings,
// true/false/null). Sem dependências. Erro de sintaxe lança std::runtime_error com o deslocamento.
#pragma once

#include <cstddef>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace dr2::json {

class Value;
using Array = std::vector<Value>;
using Object = std::map<std::string, Value, std::less<>>;

class Value {
public:
    Value() = default;
    explicit Value(bool b) : v_(b) {}
    explicit Value(double d) : v_(d) {}
    explicit Value(std::string s) : v_(std::move(s)) {}
    explicit Value(Array a) : v_(std::make_shared<Array>(std::move(a))) {}
    explicit Value(Object o) : v_(std::make_shared<Object>(std::move(o))) {}

    bool is_null() const { return std::holds_alternative<std::monostate>(v_); }
    bool is_bool() const { return std::holds_alternative<bool>(v_); }
    bool is_number() const { return std::holds_alternative<double>(v_); }
    bool is_string() const { return std::holds_alternative<std::string>(v_); }
    bool is_array() const { return std::holds_alternative<std::shared_ptr<Array>>(v_); }
    bool is_object() const { return std::holds_alternative<std::shared_ptr<Object>>(v_); }

    // Acessores que lançam std::runtime_error se o tipo não bate.
    bool as_bool() const;
    double as_number() const;
    const std::string& as_string() const;
    const Array& as_array() const;
    const Object& as_object() const;

    // Campo de objeto; lança se não é objeto ou se falta a chave.
    const Value& operator[](std::string_view key) const;
    // Campo de objeto ou nullptr.
    const Value* find(std::string_view key) const;
    // Elemento de lista; lança se não é lista ou fora da faixa.
    const Value& operator[](std::size_t i) const;
    std::size_t size() const;  // lista ou objeto

    double number_or(std::string_view key, double fallback) const;

private:
    std::variant<std::monostate, bool, double, std::string, std::shared_ptr<Array>, std::shared_ptr<Object>> v_;
};

Value parse(std::string_view text);
Value parse_file(const std::string& path);

// Escreve uma string JSON com aspas e escapes (para gravar o edits.json).
std::string quote(std::string_view s);

}  // namespace dr2::json
