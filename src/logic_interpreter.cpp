#include "openaslc/logic_interpreter.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <functional>
#include <mutex>
#include <unordered_map>

namespace {

using openaslc::LogicNode;
using openaslc::LogicParseError;
using openaslc::MemoryArea;
using openaslc::SignalRef;

// Forward declaration -- parse_bool_combinator and parse_condition_node recurse
// into each other.
std::unique_ptr<LogicNode> parse_condition_node(const nlohmann::json& j,
                                                 std::chrono::milliseconds period,
                                                 const std::string& path);

MemoryArea parse_area(const std::string& s, const std::string& path) {
    if (s == "I") return MemoryArea::Input;
    if (s == "Q") return MemoryArea::Output;
    if (s == "M") return MemoryArea::Memory;
    throw LogicParseError(path + ": unknown area '" + s + "' (expected \"I\", \"Q\", or \"M\")");
}

std::size_t area_size(MemoryArea area) {
    switch (area) {
        case MemoryArea::Input:  return openaslc::MemoryMap::INPUT_SIZE;
        case MemoryArea::Output: return openaslc::MemoryMap::OUTPUT_SIZE;
        case MemoryArea::Memory: return openaslc::MemoryMap::MEMORY_SIZE;
    }
    return 0;
}

bool is_valid_node_uid_format(const std::string& s) {
    if (s.size() != 2 && s.size() != 4 && s.size() != 8) {
        return false;
    }
    return std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isalnum(c) != 0; });
}

const nlohmann::json& require_field(const nlohmann::json& j, const std::string& key,
                                     const std::string& path) {
    if (!j.contains(key)) {
        throw LogicParseError(path + ": missing required field '" + key + "'");
    }
    return j.at(key);
}

int64_t require_int(const nlohmann::json& j, const std::string& key, const std::string& path) {
    const auto& field = require_field(j, key, path);
    if (!field.is_number_integer()) {
        throw LogicParseError(path + "." + key + ": expected an integer");
    }
    return field.get<int64_t>();
}

std::string require_string(const nlohmann::json& j, const std::string& key,
                            const std::string& path) {
    const auto& field = require_field(j, key, path);
    if (!field.is_string()) {
        throw LogicParseError(path + "." + key + ": expected a string");
    }
    return field.get<std::string>();
}

SignalRef parse_signal_ref(const nlohmann::json& j, const std::string& path,
                            bool allow_input_area) {
    if (!j.is_object()) {
        throw LogicParseError(path + ": expected a signal reference object");
    }

    if (j.contains("node")) {
        const auto& node_field = j.at("node");
        if (!node_field.is_string()) {
            throw LogicParseError(path + ".node: expected a string");
        }
        std::string node_id = node_field.get<std::string>();
        if (!is_valid_node_uid_format(node_id)) {
            throw LogicParseError(path + ".node: '" + node_id +
                                  "' is not a valid node UID (expected 2, 4, or 8 alphanumeric "
                                  "characters)");
        }
        throw LogicParseError(path + ".node: remote signal references are not supported yet -- "
                              "hive IO forwarding is a future feature; omit 'node' to reference "
                              "this board's own memory map");
    }

    std::string area_str = require_string(j, "area", path);
    MemoryArea area = parse_area(area_str, path + ".area");
    if (!allow_input_area && area == MemoryArea::Input) {
        throw LogicParseError(path + ".area: rule outputs cannot target %I (reserved for driver "
                              "writes)");
    }

    int64_t byte = require_int(j, "byte", path);
    if (byte < 0 || static_cast<std::size_t>(byte) >= area_size(area)) {
        throw LogicParseError(path + ".byte: " + std::to_string(byte) +
                              " is out of range for this area (0.." +
                              std::to_string(area_size(area) - 1) + ")");
    }

    int64_t bit = require_int(j, "bit", path);
    if (bit < 0 || bit > 7) {
        throw LogicParseError(path + ".bit: " + std::to_string(bit) + " must be 0..7");
    }

    return SignalRef{std::nullopt, area, static_cast<std::size_t>(byte), static_cast<uint8_t>(bit)};
}

std::unique_ptr<LogicNode> parse_bool_combinator(const nlohmann::json& j,
                                                  std::chrono::milliseconds period,
                                                  const std::string& path, bool is_and) {
    const auto& inputs_field = require_field(j, "inputs", path);
    if (!inputs_field.is_array() || inputs_field.empty()) {
        throw LogicParseError(path + ".inputs: expected a non-empty array");
    }
    std::vector<std::unique_ptr<LogicNode>> children;
    children.reserve(inputs_field.size());
    for (std::size_t i = 0; i < inputs_field.size(); ++i) {
        children.push_back(parse_condition_node(inputs_field[i], period,
                                                  path + ".inputs[" + std::to_string(i) + "]"));
    }
    if (is_and) {
        return std::make_unique<openaslc::AndNode>(std::move(children));
    }
    return std::make_unique<openaslc::OrNode>(std::move(children));
}

std::unique_ptr<LogicNode> parse_condition_node(const nlohmann::json& j,
                                                 std::chrono::milliseconds period,
                                                 const std::string& path) {
    if (!j.is_object()) {
        throw LogicParseError(path + ": expected a condition node object");
    }
    std::string type = require_string(j, "type", path);

    using NodeParser = std::function<std::unique_ptr<LogicNode>(
        const nlohmann::json&, std::chrono::milliseconds, const std::string&)>;

    static const std::unordered_map<std::string, NodeParser> dispatch = {
        {"INPUT",
         [](const nlohmann::json& node, std::chrono::milliseconds, const std::string& p) {
             return std::unique_ptr<LogicNode>(
                 std::make_unique<openaslc::SignalRefNode>(parse_signal_ref(node, p, true)));
         }},
        {"AND",
         [](const nlohmann::json& node, std::chrono::milliseconds per, const std::string& p) {
             return parse_bool_combinator(node, per, p, true);
         }},
        {"OR",
         [](const nlohmann::json& node, std::chrono::milliseconds per, const std::string& p) {
             return parse_bool_combinator(node, per, p, false);
         }},
        {"NOT",
         [](const nlohmann::json& node, std::chrono::milliseconds per, const std::string& p) {
             auto child = parse_condition_node(require_field(node, "input", p), per, p + ".input");
             return std::unique_ptr<LogicNode>(
                 std::make_unique<openaslc::NotNode>(std::move(child)));
         }},
        {"TON_TIMER",
         [](const nlohmann::json& node, std::chrono::milliseconds per, const std::string& p) {
             auto child = parse_condition_node(require_field(node, "input", p), per, p + ".input");
             int64_t preset_ms = require_int(node, "preset_ms", p);
             if (preset_ms < 0) {
                 throw LogicParseError(p + ".preset_ms: must be >= 0");
             }
             return std::unique_ptr<LogicNode>(std::make_unique<openaslc::TonTimerNode>(
                 std::move(child), static_cast<uint64_t>(preset_ms), per));
         }},
    };

    auto it = dispatch.find(type);
    if (it == dispatch.end()) {
        throw LogicParseError(path + ".type: unknown node type '" + type + "'");
    }
    return it->second(j, period, path);
}

openaslc::Rule parse_rule(const nlohmann::json& j, std::size_t index,
                           std::chrono::milliseconds period) {
    std::string path = "rules[" + std::to_string(index) + "]";
    if (!j.is_object()) {
        throw LogicParseError(path + ": expected a rule object");
    }

    std::string id = j.contains("id") ? require_string(j, "id", path) : path;

    const auto& output_field = require_field(j, "output", path);
    SignalRef output = parse_signal_ref(output_field, path + ".output", false);

    const auto& condition_field = require_field(j, "condition", path);
    auto condition = parse_condition_node(condition_field, period, path + ".condition");

    return openaslc::Rule{std::move(id), output, std::move(condition)};
}

} // namespace

namespace openaslc {

SignalRefNode::SignalRefNode(SignalRef ref) : ref_(std::move(ref)) {}

bool SignalRefNode::evaluate(const MemoryMap& mem) {
    return mem.read_bit(ref_.area, ref_.byte, ref_.bit);
}

AndNode::AndNode(std::vector<std::unique_ptr<LogicNode>> inputs) : inputs_(std::move(inputs)) {}

bool AndNode::evaluate(const MemoryMap& mem) {
    // Every child evaluates every cycle -- no short-circuiting -- so a stateful
    // node (e.g. TonTimerNode) placed after an earlier false sibling still ticks
    // and resets correctly instead of silently freezing.
    bool result = true;
    for (auto& child : inputs_) {
        result = child->evaluate(mem) && result;
    }
    return result;
}

OrNode::OrNode(std::vector<std::unique_ptr<LogicNode>> inputs) : inputs_(std::move(inputs)) {}

bool OrNode::evaluate(const MemoryMap& mem) {
    bool result = false;
    for (auto& child : inputs_) {
        result = child->evaluate(mem) || result;
    }
    return result;
}

NotNode::NotNode(std::unique_ptr<LogicNode> input) : input_(std::move(input)) {}

bool NotNode::evaluate(const MemoryMap& mem) {
    return !input_->evaluate(mem);
}

TonTimerNode::TonTimerNode(std::unique_ptr<LogicNode> input, uint64_t preset_ms,
                           std::chrono::milliseconds period)
    : input_(std::move(input)),
      preset_ms_(preset_ms),
      period_ms_(static_cast<uint64_t>(period.count())) {}

bool TonTimerNode::evaluate(const MemoryMap& mem) {
    if (!input_->evaluate(mem)) {
        elapsed_cycles_ = 0;
        return false;
    }
    elapsed_cycles_++;
    return (elapsed_cycles_ * period_ms_) >= preset_ms_;
}

std::shared_ptr<LogicProgram> LogicProgram::from_json(const nlohmann::json& doc,
                                                       std::chrono::milliseconds period) {
    if (!doc.is_object()) {
        throw LogicParseError("document: expected a JSON object");
    }
    const auto& rules_field = require_field(doc, "rules", "document");
    if (!rules_field.is_array()) {
        throw LogicParseError("document.rules: expected an array");
    }

    auto program = std::make_shared<LogicProgram>();
    program->rules_.reserve(rules_field.size());
    for (std::size_t i = 0; i < rules_field.size(); ++i) {
        program->rules_.push_back(parse_rule(rules_field[i], i, period));
    }
    return program;
}

std::shared_ptr<LogicProgram> LogicProgram::from_file(const std::filesystem::path& path,
                                                       std::chrono::milliseconds period) {
    std::ifstream file(path);
    if (!file.is_open()) {
        throw LogicParseError("could not open logic file: " + path.string());
    }
    nlohmann::json doc;
    try {
        file >> doc;
    } catch (const nlohmann::json::parse_error& e) {
        throw LogicParseError("failed to parse " + path.string() + ": " + e.what());
    }
    return from_json(doc, period);
}

void LogicProgram::execute(MemoryMap& mem) {
    for (auto& rule : rules_) {
        bool result = rule.condition->evaluate(mem);
        mem.write_bit(rule.output.area, rule.output.byte, rule.output.bit, result);
    }
}

std::size_t LogicProgram::rule_count() const noexcept {
    return rules_.size();
}

LogicRuntime::LogicRuntime(std::chrono::milliseconds period, std::shared_ptr<LogicProgram> initial)
    : period_(period), current_(std::move(initial)) {}

void LogicRuntime::execute_cycle(MemoryMap& mem, uint64_t /*cycle_count*/) {
    std::shared_ptr<LogicProgram> local;
    {
        std::shared_lock lock(mutex_);
        local = current_;
    }
    if (local) {
        local->execute(mem);
    }
}

void LogicRuntime::reload(std::shared_ptr<LogicProgram> new_program) {
    std::unique_lock lock(mutex_);
    current_ = std::move(new_program);
}

std::shared_ptr<LogicProgram> LogicRuntime::reload_from_json(const nlohmann::json& doc) {
    auto parsed = LogicProgram::from_json(doc, period_);
    reload(parsed);
    return parsed;
}

std::shared_ptr<LogicProgram> LogicRuntime::get_current() const {
    std::shared_lock lock(mutex_);
    return current_;
}

} // namespace openaslc
