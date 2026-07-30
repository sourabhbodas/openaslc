#ifndef OPENASLC_CONFIG_ARCHIVE_HPP
#define OPENASLC_CONFIG_ARCHIVE_HPP

#include <nlohmann/json_fwd.hpp>

#include <filesystem>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

// Forward-declared at global scope, same trick already used for httplib::Server in
// web_server.hpp, so <sqlite3.h> never has to be included from this header.
struct sqlite3;

namespace openaslc {

class ConfigArchiveError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct CommitInfo {
    std::string sha256;
    std::optional<std::string> parent_sha256;
    std::string author;
    std::string message;
    std::string created_at; // ISO 8601 UTC
};

// SQLite-backed version history for deployed logic programs (Concepts/what-is-it.md
// section 3's "commits"/"snapshots"/"deployments" tables). commit_config() hashes
// the payload + author + message + timestamp into a SHA-256 commit id (hand-rolled,
// same precedent as telemetry_server.cpp's hand-rolled SHA-1 for the WS handshake --
// keeps this MIT-clean with no extra crypto dependency) and chains it to whatever
// commit is currently HEAD, forming a simple linear history (no branching, nothing
// in this codebase needs branches yet).
class ConfigArchive {
public:
    explicit ConfigArchive(std::filesystem::path db_path);
    ~ConfigArchive();

    ConfigArchive(const ConfigArchive&) = delete;
    ConfigArchive& operator=(const ConfigArchive&) = delete;

    // Inserts a commits row (parented to the current HEAD) and a matching snapshots
    // row holding the full JSON payload. Throws ConfigArchiveError on any SQLite
    // failure.
    CommitInfo commit_config(const std::string& author, const std::string& message,
                              const nlohmann::json& payload);

    // Records that `commit_sha256` is now the actively running program.
    void record_deployment(const std::string& commit_sha256);

    [[nodiscard]] std::vector<CommitInfo> get_commit_history(int limit = 50) const;
    [[nodiscard]] nlohmann::json get_snapshot(const std::string& commit_sha256) const;

private:
    void execute(const std::string& sql);

    ::sqlite3* db_;
    mutable std::mutex mutex_; // serializes the read-HEAD-then-insert sequence in
                               // commit_config, not just individual sqlite3 calls
};

} // namespace openaslc

#endif // OPENASLC_CONFIG_ARCHIVE_HPP
