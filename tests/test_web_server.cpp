#include <catch2/catch_test_macros.hpp>
#include "openaslc/web_server.hpp"
#include "openaslc/config_archive.hpp"
#include "openaslc/logic_interpreter.hpp"
#include "openaslc/memory_map.hpp"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>

using namespace openaslc;
using namespace std::chrono_literals;

namespace {

constexpr const char* kProgramA = R"({
    "version": 1,
    "rules": [
        {"id": "r1", "output": {"area": "Q", "byte": 0, "bit": 0},
         "condition": {"type": "INPUT", "area": "I", "byte": 0, "bit": 0}}
    ]
})";

constexpr const char* kProgramB = R"({
    "version": 1,
    "rules": [
        {"id": "r1", "output": {"area": "Q", "byte": 0, "bit": 0},
         "condition": {"type": "INPUT", "area": "I", "byte": 0, "bit": 0}},
        {"id": "r2", "output": {"area": "Q", "byte": 0, "bit": 1},
         "condition": {"type": "INPUT", "area": "I", "byte": 0, "bit": 1}}
    ]
})";

constexpr int kTestPort = 18080;
// Not 18081: that's test_telemetry_server.cpp's kTestPort. Reusing it caused a
// real, platform-dependent bind failure -- Windows happened to tolerate the
// TIME_WAIT timing, Linux didn't.
constexpr int kArchiveTestPort = 18082;

} // namespace

TEST_CASE("WebServer serves static files and handles /api/deploy", "[web]") {
    auto initial = LogicProgram::from_json(nlohmann::json::parse(kProgramA), 20ms);
    LogicRuntime runtime(20ms, initial);

    auto www_dir = std::filesystem::temp_directory_path() / "openaslc_test_www";
    std::filesystem::create_directories(www_dir);
    {
        std::ofstream f(www_dir / "index.html");
        f << "<html>hello openaslc</html>";
    }

    WebServer server(runtime, www_dir, kTestPort);
    server.start();
    REQUIRE(server.is_running());

    // 127.0.0.1, not "localhost": the server only binds IPv4 (0.0.0.0), and on
    // Windows resolving "localhost" tries ::1 first and stalls for ~2s before
    // falling back to IPv4.
    httplib::Client client("127.0.0.1", kTestPort);

    SECTION("serves static files from www_dir") {
        auto res = client.Get("/index.html");
        REQUIRE(res);
        REQUIRE(res->status == 200);
        REQUIRE(res->body.find("hello openaslc") != std::string::npos);
    }

    SECTION("valid /api/deploy hot-reloads the running program") {
        auto res = client.Post("/api/deploy", kProgramB, "application/json");
        REQUIRE(res);
        REQUIRE(res->status == 200);

        auto body = nlohmann::json::parse(res->body);
        REQUIRE(body["rule_count"] == 2);
        REQUIRE(runtime.get_current()->rule_count() == 2);
    }

    SECTION("malformed /api/deploy returns 400 and leaves the program untouched") {
        auto res = client.Post("/api/deploy", "{ this is not valid json", "application/json");
        REQUIRE(res);
        REQUIRE(res->status == 400);
        REQUIRE(runtime.get_current()->rule_count() == 1);
    }

    server.stop();
    REQUIRE_FALSE(server.is_running());
    std::filesystem::remove_all(www_dir);
}

TEST_CASE("WebServer records a commit and exposes it via /api/history when a "
          "ConfigArchive is supplied", "[web]") {
    auto initial = LogicProgram::from_json(nlohmann::json::parse(kProgramA), 20ms);
    LogicRuntime runtime(20ms, initial);

    auto www_dir = std::filesystem::temp_directory_path() / "openaslc_test_www_archive";
    std::filesystem::create_directories(www_dir);
    {
        std::ofstream f(www_dir / "index.html");
        f << "<html>hello openaslc</html>";
    }

    auto db_path = std::filesystem::temp_directory_path() / "openaslc_test_www_archive.db";
    std::filesystem::remove(db_path);
    ConfigArchive archive(db_path);

    WebServer server(runtime, www_dir, kArchiveTestPort, nullptr, &archive);
    server.start();

    httplib::Client client("127.0.0.1", kArchiveTestPort);

    auto res = client.Post("/api/deploy?author=alice&message=first+deploy", kProgramB,
                            "application/json");
    REQUIRE(res);
    REQUIRE(res->status == 200);

    auto body = nlohmann::json::parse(res->body);
    REQUIRE(body["rule_count"] == 2);
    REQUIRE(body.contains("commit"));
    REQUIRE_FALSE(body["commit"]["sha256"].get<std::string>().empty());
    REQUIRE(body["commit"]["author"] == "alice");

    auto history_res = client.Get("/api/history");
    REQUIRE(history_res);
    REQUIRE(history_res->status == 200);
    auto history_body = nlohmann::json::parse(history_res->body);
    REQUIRE(history_body["commits"].size() == 1);
    REQUIRE(history_body["commits"][0]["sha256"] == body["commit"]["sha256"]);

    server.stop();
    std::filesystem::remove_all(www_dir);
    // db_path is intentionally not removed here: `archive` (holding the open
    // sqlite3 handle) is still alive in this scope, and Windows refuses to
    // delete an open file. The next run's std::filesystem::remove(db_path)
    // above cleans up the leftover before reusing the path.
}
