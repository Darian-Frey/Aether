// RuleIR <-> JSON (SPEC §11 "rule.ir").
//
// The one place an IR is constructed from external data. Deserialisation
// validates before returning, so nothing downstream sees a malformed IR.

#pragma once

#include "rule/ir.hpp"

#include <nlohmann/json_fwd.hpp>

#include <string>
#include <variant>

namespace aether::rule {

nlohmann::json irToJson(const RuleIR& ir);

// Returns an error message on malformed input or a failed validation.
std::variant<RuleIR, std::string> irFromJson(const nlohmann::json& j);

// Base64 of a byte string, used for tables and grids.
std::string base64Encode(const std::vector<uint8_t>& bytes);
std::variant<std::vector<uint8_t>, std::string> base64Decode(const std::string& text);

}  // namespace aether::rule
