#include <catch2/catch_test_macros.hpp>
#include "openaslc/config_archive.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>

using namespace openaslc;

namespace {

std::filesystem::path fresh_db_path(const std::string& name) {
    auto dir = std::filesystem::temp_directory_path() / "openaslc_test_archive";
    std::filesystem::create_directories(dir);
    auto path = dir / name;
    std::filesystem::remove(path);
    return path;
}

} // namespace

TEST_CASE("ConfigArchive chains commits and records snapshots", "[archive]") {
    ConfigArchive archive(fresh_db_path("chain.db"));

    auto payload_a = nlohmann::json::parse(R"({"version":1,"rules":[]})");
    CommitInfo first = archive.commit_config("alice", "initial version", payload_a);
    REQUIRE_FALSE(first.sha256.empty());
    REQUIRE_FALSE(first.parent_sha256.has_value());

    auto payload_b = nlohmann::json::parse(R"({"version":1,"rules":[{"id":"r1"}]})");
    CommitInfo second = archive.commit_config("bob", "added rule", payload_b);
    REQUIRE_FALSE(second.sha256.empty());
    REQUIRE(second.sha256 != first.sha256);
    REQUIRE(second.parent_sha256.has_value());
    REQUIRE(*second.parent_sha256 == first.sha256);

    auto history = archive.get_commit_history();
    REQUIRE(history.size() == 2);
    REQUIRE(history[0].sha256 == second.sha256); // newest first
    REQUIRE(history[1].sha256 == first.sha256);

    REQUIRE(archive.get_snapshot(first.sha256) == payload_a);
    REQUIRE(archive.get_snapshot(second.sha256) == payload_b);
}

TEST_CASE("ConfigArchive gives identical payloads distinct commit ids", "[archive]") {
    ConfigArchive archive(fresh_db_path("distinct.db"));

    auto payload = nlohmann::json::parse(R"({"version":1,"rules":[]})");
    CommitInfo first = archive.commit_config("alice", "same message", payload);
    CommitInfo second = archive.commit_config("alice", "same message", payload);

    REQUIRE(first.sha256 != second.sha256);
    REQUIRE(archive.get_commit_history().size() == 2);
}

TEST_CASE("ConfigArchive record_deployment does not throw for a known commit", "[archive]") {
    ConfigArchive archive(fresh_db_path("deploy.db"));

    auto payload = nlohmann::json::parse(R"({"version":1,"rules":[]})");
    CommitInfo commit = archive.commit_config("alice", "msg", payload);

    REQUIRE_NOTHROW(archive.record_deployment(commit.sha256));
}

TEST_CASE("ConfigArchive get_snapshot throws for an unknown commit", "[archive]") {
    ConfigArchive archive(fresh_db_path("missing.db"));
    REQUIRE_THROWS_AS(archive.get_snapshot("does-not-exist"), ConfigArchiveError);
}
