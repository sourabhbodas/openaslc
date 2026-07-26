#include <catch2/catch_test_macros.hpp>
#include "openaslc/logic_interpreter.hpp"
#include "openaslc/aslc_engine.hpp"
#include <nlohmann/json.hpp>
#include <chrono>
#include <thread>

using namespace openaslc;
using namespace std::chrono_literals;

TEST_CASE("LogicProgram evaluates (%I[0] AND %I[1]) -> %Q[0]", "[logic]") {
    auto doc = nlohmann::json::parse(R"({
        "version": 1,
        "rules": [{
            "id": "and_rule",
            "output": {"area": "Q", "byte": 0, "bit": 0},
            "condition": {
                "type": "AND",
                "inputs": [
                    {"type": "INPUT", "area": "I", "byte": 0, "bit": 0},
                    {"type": "INPUT", "area": "I", "byte": 0, "bit": 1}
                ]
            }
        }]
    })");
    auto program = LogicProgram::from_json(doc, 20ms);
    MemoryMap mem;

    for (bool a : {false, true}) {
        for (bool b : {false, true}) {
            mem.write_bit(MemoryArea::Input, 0, 0, a);
            mem.write_bit(MemoryArea::Input, 0, 1, b);
            program->execute(mem);
            REQUIRE(mem.read_bit(MemoryArea::Output, 0, 0) == (a && b));
        }
    }
}

TEST_CASE("LogicProgram evaluates OR rule", "[logic]") {
    auto doc = nlohmann::json::parse(R"({
        "version": 1,
        "rules": [{
            "id": "or_rule",
            "output": {"area": "Q", "byte": 0, "bit": 0},
            "condition": {
                "type": "OR",
                "inputs": [
                    {"type": "INPUT", "area": "I", "byte": 0, "bit": 0},
                    {"type": "INPUT", "area": "I", "byte": 0, "bit": 1}
                ]
            }
        }]
    })");
    auto program = LogicProgram::from_json(doc, 20ms);
    MemoryMap mem;

    for (bool a : {false, true}) {
        for (bool b : {false, true}) {
            mem.write_bit(MemoryArea::Input, 0, 0, a);
            mem.write_bit(MemoryArea::Input, 0, 1, b);
            program->execute(mem);
            REQUIRE(mem.read_bit(MemoryArea::Output, 0, 0) == (a || b));
        }
    }
}

TEST_CASE("LogicProgram evaluates NOT rule", "[logic]") {
    auto doc = nlohmann::json::parse(R"({
        "version": 1,
        "rules": [{
            "id": "not_rule",
            "output": {"area": "Q", "byte": 0, "bit": 0},
            "condition": {
                "type": "NOT",
                "input": {"type": "INPUT", "area": "I", "byte": 0, "bit": 0}
            }
        }]
    })");
    auto program = LogicProgram::from_json(doc, 20ms);
    MemoryMap mem;

    mem.write_bit(MemoryArea::Input, 0, 0, false);
    program->execute(mem);
    REQUIRE(mem.read_bit(MemoryArea::Output, 0, 0) == true);

    mem.write_bit(MemoryArea::Input, 0, 0, true);
    program->execute(mem);
    REQUIRE(mem.read_bit(MemoryArea::Output, 0, 0) == false);
}

TEST_CASE("LogicProgram supports nested boolean trees", "[logic]") {
    // OR(AND(a, b), NOT(c))
    auto doc = nlohmann::json::parse(R"({
        "version": 1,
        "rules": [{
            "id": "nested_rule",
            "output": {"area": "Q", "byte": 0, "bit": 0},
            "condition": {
                "type": "OR",
                "inputs": [
                    {
                        "type": "AND",
                        "inputs": [
                            {"type": "INPUT", "area": "I", "byte": 0, "bit": 0},
                            {"type": "INPUT", "area": "I", "byte": 0, "bit": 1}
                        ]
                    },
                    {
                        "type": "NOT",
                        "input": {"type": "INPUT", "area": "I", "byte": 0, "bit": 2}
                    }
                ]
            }
        }]
    })");
    auto program = LogicProgram::from_json(doc, 20ms);
    MemoryMap mem;

    for (bool a : {false, true}) {
        for (bool b : {false, true}) {
            for (bool c : {false, true}) {
                mem.write_bit(MemoryArea::Input, 0, 0, a);
                mem.write_bit(MemoryArea::Input, 0, 1, b);
                mem.write_bit(MemoryArea::Input, 0, 2, c);
                program->execute(mem);
                bool expected = (a && b) || !c;
                REQUIRE(mem.read_bit(MemoryArea::Output, 0, 0) == expected);
            }
        }
    }
}

TEST_CASE("LogicProgram rules chain through %M within one cycle", "[logic]") {
    auto doc = nlohmann::json::parse(R"({
        "version": 1,
        "rules": [
            {
                "id": "stage1",
                "output": {"area": "M", "byte": 0, "bit": 0},
                "condition": {
                    "type": "AND",
                    "inputs": [
                        {"type": "INPUT", "area": "I", "byte": 0, "bit": 0},
                        {"type": "INPUT", "area": "I", "byte": 0, "bit": 1}
                    ]
                }
            },
            {
                "id": "stage2",
                "output": {"area": "Q", "byte": 0, "bit": 0},
                "condition": {"type": "INPUT", "area": "M", "byte": 0, "bit": 0}
            }
        ]
    })");
    auto program = LogicProgram::from_json(doc, 20ms);
    MemoryMap mem;

    mem.write_bit(MemoryArea::Input, 0, 0, true);
    mem.write_bit(MemoryArea::Input, 0, 1, true);
    program->execute(mem);

    // Single execute() call -- stage2 must observe stage1's %M write from the SAME call.
    REQUIRE(mem.read_bit(MemoryArea::Memory, 0, 0) == true);
    REQUIRE(mem.read_bit(MemoryArea::Output, 0, 0) == true);
}

TEST_CASE("TON_TIMER trips only after preset elapses", "[logic][timer]") {
    // period 20ms, preset 100ms => trips on the 5th consecutive true cycle.
    auto doc = nlohmann::json::parse(R"({
        "version": 1,
        "rules": [{
            "id": "timer_rule",
            "output": {"area": "Q", "byte": 0, "bit": 0},
            "condition": {
                "type": "TON_TIMER",
                "preset_ms": 100,
                "input": {"type": "INPUT", "area": "I", "byte": 0, "bit": 0}
            }
        }]
    })");
    auto program = LogicProgram::from_json(doc, 20ms);
    MemoryMap mem;
    mem.write_bit(MemoryArea::Input, 0, 0, true);

    for (int cycle = 1; cycle <= 4; ++cycle) {
        program->execute(mem);
        REQUIRE(mem.read_bit(MemoryArea::Output, 0, 0) == false);
    }
    program->execute(mem); // 5th cycle
    REQUIRE(mem.read_bit(MemoryArea::Output, 0, 0) == true);
}

TEST_CASE("TON_TIMER resets when input drops before preset elapses", "[logic][timer]") {
    auto doc = nlohmann::json::parse(R"({
        "version": 1,
        "rules": [{
            "id": "timer_rule",
            "output": {"area": "Q", "byte": 0, "bit": 0},
            "condition": {
                "type": "TON_TIMER",
                "preset_ms": 100,
                "input": {"type": "INPUT", "area": "I", "byte": 0, "bit": 0}
            }
        }]
    })");
    auto program = LogicProgram::from_json(doc, 20ms);
    MemoryMap mem;

    mem.write_bit(MemoryArea::Input, 0, 0, true);
    program->execute(mem); // elapsed=1
    REQUIRE(mem.read_bit(MemoryArea::Output, 0, 0) == false);
    program->execute(mem); // elapsed=2
    REQUIRE(mem.read_bit(MemoryArea::Output, 0, 0) == false);

    mem.write_bit(MemoryArea::Input, 0, 0, false);
    program->execute(mem); // reset to 0
    REQUIRE(mem.read_bit(MemoryArea::Output, 0, 0) == false);

    mem.write_bit(MemoryArea::Input, 0, 0, true);
    for (int cycle = 1; cycle <= 4; ++cycle) {
        program->execute(mem);
        REQUIRE(mem.read_bit(MemoryArea::Output, 0, 0) == false);
    }
    program->execute(mem); // 5th consecutive true cycle since the reset
    REQUIRE(mem.read_bit(MemoryArea::Output, 0, 0) == true);
}

TEST_CASE("TON_TIMER nested behind a false AND sibling still ticks every cycle", "[logic][timer]") {
    // AND(gate, TON_TIMER(some_input, preset_ms=80)) -- period 20ms => 4 cycles to trip.
    // Keep gate false for 3 cycles (AND result is false regardless of the timer), then
    // flip gate true on the 4th cycle. If the timer had been silently ticking every
    // cycle (correct, no-short-circuit behavior), it already reached its preset by the
    // 4th call and the AND flips true immediately. If AND had short-circuited on the
    // false gate instead (the bug this test guards against), the timer would never have
    // been evaluated during cycles 1-3 and would only start counting on cycle 4.
    auto doc = nlohmann::json::parse(R"({
        "version": 1,
        "rules": [{
            "id": "gated_timer",
            "output": {"area": "Q", "byte": 0, "bit": 0},
            "condition": {
                "type": "AND",
                "inputs": [
                    {"type": "INPUT", "area": "I", "byte": 0, "bit": 1},
                    {
                        "type": "TON_TIMER",
                        "preset_ms": 80,
                        "input": {"type": "INPUT", "area": "I", "byte": 0, "bit": 0}
                    }
                ]
            }
        }]
    })");
    auto program = LogicProgram::from_json(doc, 20ms);
    MemoryMap mem;

    mem.write_bit(MemoryArea::Input, 0, 0, true);  // some_input: held true throughout
    mem.write_bit(MemoryArea::Input, 0, 1, false); // gate: false for the first 3 cycles

    for (int cycle = 1; cycle <= 3; ++cycle) {
        program->execute(mem);
        REQUIRE(mem.read_bit(MemoryArea::Output, 0, 0) == false);
    }

    mem.write_bit(MemoryArea::Input, 0, 1, true); // open the gate on the 4th cycle
    program->execute(mem);
    REQUIRE(mem.read_bit(MemoryArea::Output, 0, 0) == true);
}

TEST_CASE("LogicProgram::from_json rejects malformed documents", "[logic]") {
    auto period = 20ms;

    SECTION("missing rules key") {
        auto doc = nlohmann::json::parse(R"({"version": 1})");
        REQUIRE_THROWS_AS(LogicProgram::from_json(doc, period), LogicParseError);
    }
    SECTION("unknown node type") {
        auto doc = nlohmann::json::parse(R"({"rules": [{
            "output": {"area": "Q", "byte": 0, "bit": 0},
            "condition": {"type": "XOR", "inputs": []}
        }]})");
        REQUIRE_THROWS_AS(LogicProgram::from_json(doc, period), LogicParseError);
    }
    SECTION("unknown area") {
        auto doc = nlohmann::json::parse(R"({"rules": [{
            "output": {"area": "Z", "byte": 0, "bit": 0},
            "condition": {"type": "INPUT", "area": "I", "byte": 0, "bit": 0}
        }]})");
        REQUIRE_THROWS_AS(LogicProgram::from_json(doc, period), LogicParseError);
    }
    SECTION("output area I is rejected") {
        auto doc = nlohmann::json::parse(R"({"rules": [{
            "output": {"area": "I", "byte": 0, "bit": 0},
            "condition": {"type": "INPUT", "area": "I", "byte": 0, "bit": 1}
        }]})");
        REQUIRE_THROWS_AS(LogicProgram::from_json(doc, period), LogicParseError);
    }
    SECTION("bit out of range (8)") {
        auto doc = nlohmann::json::parse(R"({"rules": [{
            "output": {"area": "Q", "byte": 0, "bit": 8},
            "condition": {"type": "INPUT", "area": "I", "byte": 0, "bit": 0}
        }]})");
        REQUIRE_THROWS_AS(LogicProgram::from_json(doc, period), LogicParseError);
    }
    SECTION("bit negative") {
        auto doc = nlohmann::json::parse(R"({"rules": [{
            "output": {"area": "Q", "byte": 0, "bit": -1},
            "condition": {"type": "INPUT", "area": "I", "byte": 0, "bit": 0}
        }]})");
        REQUIRE_THROWS_AS(LogicProgram::from_json(doc, period), LogicParseError);
    }
    SECTION("byte out of range for area") {
        auto doc = nlohmann::json::parse(R"({"rules": [{
            "output": {"area": "Q", "byte": 999999, "bit": 0},
            "condition": {"type": "INPUT", "area": "I", "byte": 0, "bit": 0}
        }]})");
        REQUIRE_THROWS_AS(LogicProgram::from_json(doc, period), LogicParseError);
    }
    SECTION("missing required field (condition)") {
        auto doc = nlohmann::json::parse(R"({"rules": [{
            "output": {"area": "Q", "byte": 0, "bit": 0}
        }]})");
        REQUIRE_THROWS_AS(LogicProgram::from_json(doc, period), LogicParseError);
    }
    SECTION("missing required field (output)") {
        auto doc = nlohmann::json::parse(R"({"rules": [{
            "condition": {"type": "INPUT", "area": "I", "byte": 0, "bit": 0}
        }]})");
        REQUIRE_THROWS_AS(LogicProgram::from_json(doc, period), LogicParseError);
    }
    SECTION("well-formed node UID is still rejected (not resolvable in Phase 2)") {
        auto doc = nlohmann::json::parse(R"({"rules": [{
            "output": {"area": "Q", "byte": 0, "bit": 0},
            "condition": {"type": "INPUT", "node": "a1B2", "area": "I", "byte": 0, "bit": 0}
        }]})");
        REQUIRE_THROWS_AS(LogicProgram::from_json(doc, period), LogicParseError);
    }
    SECTION("malformed node UID is rejected") {
        auto doc = nlohmann::json::parse(R"({"rules": [{
            "output": {"area": "Q", "byte": 0, "bit": 0},
            "condition": {"type": "INPUT", "node": "toolongforthis", "area": "I", "byte": 0, "bit": 0}
        }]})");
        REQUIRE_THROWS_AS(LogicProgram::from_json(doc, period), LogicParseError);
    }
}

TEST_CASE("Malformed reload leaves the current program untouched", "[logic][hot-reload]") {
    auto good_doc = nlohmann::json::parse(R"({"rules": [{
        "output": {"area": "Q", "byte": 0, "bit": 0},
        "condition": {"type": "INPUT", "area": "I", "byte": 0, "bit": 0}
    }]})");
    auto program_a = LogicProgram::from_json(good_doc, 20ms);
    LogicRuntime runtime(20ms, program_a);

    auto bad_doc = nlohmann::json::parse(R"({"rules": [{
        "output": {"area": "Q", "byte": 0, "bit": 0},
        "condition": {"type": "BOGUS"}
    }]})");
    REQUIRE_THROWS_AS(runtime.reload_from_json(bad_doc), LogicParseError);

    REQUIRE(runtime.get_current() == program_a);
    REQUIRE(runtime.get_current()->rule_count() == 1);
}

TEST_CASE("LogicRuntime hot-reloads a live running AslcEngine without stopping it",
          "[logic][hot-reload][engine]") {
    auto mem_map = std::make_shared<MemoryMap>();
    AslcEngine engine(mem_map, std::chrono::milliseconds(10));

    auto program_a_doc = nlohmann::json::parse(R"({"rules": [{
        "output": {"area": "Q", "byte": 0, "bit": 0},
        "condition": {"type": "INPUT", "area": "I", "byte": 0, "bit": 0}
    }]})");
    auto program_a = LogicProgram::from_json(program_a_doc, engine.get_period());

    LogicRuntime runtime(engine.get_period(), program_a);
    engine.set_cycle_callback([&](MemoryMap& m, uint64_t c) { runtime.execute_cycle(m, c); });

    engine.start();
    REQUIRE(engine.is_running());

    mem_map->write_bit(MemoryArea::Input, 0, 0, true);
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    REQUIRE(mem_map->read_bit(MemoryArea::Output, 0, 0) == true);

    // Program B inverts the passthrough onto the same output bit.
    auto program_b_doc = nlohmann::json::parse(R"({"rules": [{
        "output": {"area": "Q", "byte": 0, "bit": 0},
        "condition": {
            "type": "NOT",
            "input": {"type": "INPUT", "area": "I", "byte": 0, "bit": 0}
        }
    }]})");
    auto program_b = LogicProgram::from_json(program_b_doc, engine.get_period());
    runtime.reload(program_b);

    REQUIRE(engine.is_running()); // never stopped or restarted for the swap

    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    // %I[0].0 is still true, but program B inverts it -> %Q[0].0 should now be false.
    REQUIRE(mem_map->read_bit(MemoryArea::Output, 0, 0) == false);

    engine.stop();
    REQUIRE_FALSE(engine.is_running());
}
