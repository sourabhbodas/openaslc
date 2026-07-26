#ifndef OPENASLC_LOGIC_INTERPRETER_HPP
#define OPENASLC_LOGIC_INTERPRETER_HPP

#include "openaslc/memory_map.hpp"
#include <nlohmann/json_fwd.hpp>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <stdexcept>
#include <string>
#include <vector>

namespace openaslc {

// Thrown by LogicProgram::from_json/from_file on any malformed or invalid logic
// document. Message is path-qualified, e.g.
// "rules[1].condition.inputs[0]: unknown node type 'XOR'".
class LogicParseError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Shared by rule "output" fields and INPUT-node leaves. `node` is reserved for the
// future hive addressing model (Concepts/private/hive_io_forwarding.md: a 2/4/8
// character [a-zA-Z0-9] node UID prefixing a global reference like <uid>:%I[0].0).
// It is accepted and format-checked by the parser so the wire schema never needs a
// breaking change later, but any signal ref with `node` set is currently rejected --
// Phase 2 has no node registry or network transport to resolve it against.
struct SignalRef {
    std::optional<std::string> node;
    MemoryArea area;
    std::size_t byte;
    uint8_t bit;
};

// evaluate() is a non-const method (TonTimerNode mutates its own elapsed-cycle
// counter) but only ever reads the MemoryMap -- no node type writes to it.
class LogicNode {
public:
    virtual ~LogicNode() = default;
    virtual bool evaluate(const MemoryMap& mem) = 0;
};

class SignalRefNode : public LogicNode {   // JSON "type": "INPUT"
public:
    explicit SignalRefNode(SignalRef ref);
    bool evaluate(const MemoryMap& mem) override;

private:
    SignalRef ref_;
};

class AndNode : public LogicNode {         // JSON "type": "AND", n-ary, no short-circuit
public:
    explicit AndNode(std::vector<std::unique_ptr<LogicNode>> inputs);
    bool evaluate(const MemoryMap& mem) override;

private:
    std::vector<std::unique_ptr<LogicNode>> inputs_;
};

class OrNode : public LogicNode {          // JSON "type": "OR", n-ary, no short-circuit
public:
    explicit OrNode(std::vector<std::unique_ptr<LogicNode>> inputs);
    bool evaluate(const MemoryMap& mem) override;

private:
    std::vector<std::unique_ptr<LogicNode>> inputs_;
};

class NotNode : public LogicNode {         // JSON "type": "NOT", unary
public:
    explicit NotNode(std::unique_ptr<LogicNode> input);
    bool evaluate(const MemoryMap& mem) override;

private:
    std::unique_ptr<LogicNode> input_;
};

// Non-retentive on-delay timer (TON). elapsed_cycles_ resets to 0 the instant
// input_ evaluates false; increments once per true evaluate() call; output goes
// true once elapsed_cycles_ * period_ms_ >= preset_ms_. Hot-reloading a program
// discards this state -- a swapped-in program's timers always start at 0.
class TonTimerNode : public LogicNode {    // JSON "type": "TON_TIMER"
public:
    TonTimerNode(std::unique_ptr<LogicNode> input, uint64_t preset_ms,
                 std::chrono::milliseconds period);
    bool evaluate(const MemoryMap& mem) override;

private:
    std::unique_ptr<LogicNode> input_;
    uint64_t preset_ms_;
    uint64_t period_ms_;
    uint64_t elapsed_cycles_ = 0;
};

struct Rule {
    std::string id;
    SignalRef output;
    std::unique_ptr<LogicNode> condition;
};

class LogicProgram {
public:
    // Throws LogicParseError on any structural/semantic problem. Never partially
    // constructs a program -- either fully succeeds or throws before returning, so
    // a bad reload attempt can never disturb a live LogicRuntime.
    static std::shared_ptr<LogicProgram> from_json(const nlohmann::json& doc,
                                                    std::chrono::milliseconds period);
    static std::shared_ptr<LogicProgram> from_file(const std::filesystem::path& path,
                                                    std::chrono::milliseconds period);

    // Runs all rules top-to-bottom, writing each rule's output bit. Rule order in
    // the JSON array is semantically significant: a later rule's condition may
    // observe an earlier rule's %M/%Q write from the SAME execute() call
    // (ladder-rung-style same-scan chaining).
    void execute(MemoryMap& mem);

    [[nodiscard]] std::size_t rule_count() const noexcept;

private:
    std::vector<Rule> rules_;
};

// Thread-safe hot-swap wrapper. Only the scan thread is expected to call
// execute_cycle(); reload()/reload_from_json() may be called from any other
// thread concurrently (e.g. a future Phase 3 REST handler).
class LogicRuntime {
public:
    explicit LogicRuntime(std::chrono::milliseconds period,
                           std::shared_ptr<LogicProgram> initial = nullptr);

    // Matches AslcEngine::CycleCallback exactly:
    //   engine.set_cycle_callback([&](MemoryMap& m, uint64_t c){ runtime.execute_cycle(m, c); });
    // No-op if no program has been loaded yet.
    void execute_cycle(MemoryMap& mem, uint64_t cycle_count);

    // Swaps in an already-parsed program (never throws).
    void reload(std::shared_ptr<LogicProgram> new_program);

    // Parses first (using this runtime's stored period), then swaps only on
    // success. Throws LogicParseError and leaves the current program untouched if
    // parsing fails.
    std::shared_ptr<LogicProgram> reload_from_json(const nlohmann::json& doc);

    [[nodiscard]] std::shared_ptr<LogicProgram> get_current() const;

private:
    std::chrono::milliseconds period_;
    std::shared_ptr<LogicProgram> current_;
    mutable std::shared_mutex mutex_;
};

} // namespace openaslc

#endif // OPENASLC_LOGIC_INTERPRETER_HPP
