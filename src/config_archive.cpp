#include "openaslc/config_archive.hpp"

#include <sqlite3.h>

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdint>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <vector>

namespace openaslc {

namespace {

// Minimal hand-rolled SHA-256 (FIPS 180-4), used only to derive commit ids for the
// config archive. Same precedent as the hand-rolled SHA-1 in telemetry_server.cpp
// for the WebSocket handshake -- keeps this MIT-clean without adding a crypto
// dependency for what's otherwise a single hash call.
constexpr uint32_t kSha256K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

uint32_t rotr(uint32_t x, uint32_t n) {
    return (x >> n) | (x << (32 - n));
}

void sha256_process_block(uint32_t h[8], const uint8_t block[64]) {
    uint32_t w[64];
    for (int i = 0; i < 16; ++i) {
        w[i] = (uint32_t(block[i * 4]) << 24) | (uint32_t(block[i * 4 + 1]) << 16) |
               (uint32_t(block[i * 4 + 2]) << 8) | uint32_t(block[i * 4 + 3]);
    }
    for (int i = 16; i < 64; ++i) {
        uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
    for (int i = 0; i < 64; ++i) {
        uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        uint32_t ch = (e & f) ^ (~e & g);
        uint32_t temp1 = hh + s1 + ch + kSha256K[i] + w[i];
        uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temp2 = s0 + maj;
        hh = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }

    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
    h[4] += e;
    h[5] += f;
    h[6] += g;
    h[7] += hh;
}

std::string sha256_hex(const std::string& input) {
    uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                      0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};

    std::vector<uint8_t> data(input.begin(), input.end());
    const uint64_t bit_len = static_cast<uint64_t>(data.size()) * 8;
    data.push_back(0x80);
    while (data.size() % 64 != 56) {
        data.push_back(0);
    }
    for (int i = 7; i >= 0; --i) {
        data.push_back(static_cast<uint8_t>((bit_len >> (i * 8)) & 0xFF));
    }
    for (std::size_t i = 0; i < data.size(); i += 64) {
        sha256_process_block(h, &data[i]);
    }

    std::ostringstream oss;
    for (unsigned int i : h) {
        oss << std::hex << std::setfill('0') << std::setw(8) << i;
    }
    return oss.str();
}

std::string current_timestamp() {
    auto now = std::chrono::system_clock::now();
    auto tt = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    std::tm tm_buf{};
#ifdef _WIN32
    gmtime_s(&tm_buf, &tt);
#else
    gmtime_r(&tt, &tm_buf);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm_buf, "%Y-%m-%dT%H:%M:%S") << '.' << std::setfill('0') << std::setw(3)
        << ms.count() << 'Z';
    return oss.str();
}

} // namespace

ConfigArchive::ConfigArchive(std::filesystem::path db_path) : db_(nullptr) {
    if (db_path.has_parent_path()) {
        std::filesystem::create_directories(db_path.parent_path());
    }
    if (sqlite3_open(db_path.string().c_str(), &db_) != SQLITE_OK) {
        std::string err = db_ ? sqlite3_errmsg(db_) : "unknown error";
        if (db_) {
            sqlite3_close(db_);
            db_ = nullptr;
        }
        throw ConfigArchiveError("failed to open config archive database: " + err);
    }

    execute(R"(CREATE TABLE IF NOT EXISTS commits (
        sha256        TEXT PRIMARY KEY,
        parent_sha256 TEXT,
        author        TEXT NOT NULL,
        message       TEXT NOT NULL,
        created_at    TEXT NOT NULL
    );)");
    execute(R"(CREATE TABLE IF NOT EXISTS snapshots (
        commit_sha256 TEXT PRIMARY KEY REFERENCES commits(sha256),
        payload       TEXT NOT NULL
    );)");
    execute(R"(CREATE TABLE IF NOT EXISTS deployments (
        id            INTEGER PRIMARY KEY AUTOINCREMENT,
        commit_sha256 TEXT NOT NULL REFERENCES commits(sha256),
        deployed_at   TEXT NOT NULL,
        status        TEXT NOT NULL
    );)");
}

ConfigArchive::~ConfigArchive() {
    if (db_) {
        sqlite3_close(db_);
    }
}

void ConfigArchive::execute(const std::string& sql) {
    char* err_msg = nullptr;
    if (sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &err_msg) != SQLITE_OK) {
        std::string err = err_msg ? err_msg : "unknown error";
        sqlite3_free(err_msg);
        throw ConfigArchiveError("sqlite error: " + err);
    }
}

CommitInfo ConfigArchive::commit_config(const std::string& author, const std::string& message,
                                        const nlohmann::json& payload) {
    std::lock_guard<std::mutex> lock(mutex_);

    const std::string payload_str = payload.dump();
    const std::string created_at = current_timestamp();

    // HEAD = whatever commit has the highest rowid right now -- a simple linear
    // chain, no branching (nothing in this codebase needs branches).
    std::optional<std::string> parent;
    {
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, "SELECT sha256 FROM commits ORDER BY rowid DESC LIMIT 1;", -1,
                                &stmt, nullptr) != SQLITE_OK) {
            throw ConfigArchiveError(std::string("failed to prepare HEAD query: ") +
                                      sqlite3_errmsg(db_));
        }
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            parent = std::string(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)));
        }
        sqlite3_finalize(stmt);
    }

    // Timestamp-salted so two deploys with identical payload/author/message never
    // collide on the primary key -- same reason git includes committer time in the
    // commit object.
    const std::string sha256 = sha256_hex(payload_str + "\n" + author + "\n" + message + "\n" + created_at);

    {
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_,
                "INSERT INTO commits (sha256, parent_sha256, author, message, created_at) "
                "VALUES (?,?,?,?,?);",
                -1, &stmt, nullptr) != SQLITE_OK) {
            throw ConfigArchiveError(std::string("failed to prepare commit insert: ") +
                                      sqlite3_errmsg(db_));
        }
        sqlite3_bind_text(stmt, 1, sha256.c_str(), -1, SQLITE_TRANSIENT);
        if (parent) {
            sqlite3_bind_text(stmt, 2, parent->c_str(), -1, SQLITE_TRANSIENT);
        } else {
            sqlite3_bind_null(stmt, 2);
        }
        sqlite3_bind_text(stmt, 3, author.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 4, message.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 5, created_at.c_str(), -1, SQLITE_TRANSIENT);
        const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
        sqlite3_finalize(stmt);
        if (!ok) {
            throw ConfigArchiveError(std::string("failed to insert commit: ") + sqlite3_errmsg(db_));
        }
    }

    {
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, "INSERT INTO snapshots (commit_sha256, payload) VALUES (?,?);",
                                -1, &stmt, nullptr) != SQLITE_OK) {
            throw ConfigArchiveError(std::string("failed to prepare snapshot insert: ") +
                                      sqlite3_errmsg(db_));
        }
        sqlite3_bind_text(stmt, 1, sha256.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, payload_str.c_str(), -1, SQLITE_TRANSIENT);
        const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
        sqlite3_finalize(stmt);
        if (!ok) {
            throw ConfigArchiveError(std::string("failed to insert snapshot: ") + sqlite3_errmsg(db_));
        }
    }

    return CommitInfo{sha256, parent, author, message, created_at};
}

void ConfigArchive::record_deployment(const std::string& commit_sha256) {
    std::lock_guard<std::mutex> lock(mutex_);

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_,
            "INSERT INTO deployments (commit_sha256, deployed_at, status) VALUES (?, ?, 'active');",
            -1, &stmt, nullptr) != SQLITE_OK) {
        throw ConfigArchiveError(std::string("failed to prepare deployment insert: ") +
                                  sqlite3_errmsg(db_));
    }
    const std::string deployed_at = current_timestamp();
    sqlite3_bind_text(stmt, 1, commit_sha256.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, deployed_at.c_str(), -1, SQLITE_TRANSIENT);
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    if (!ok) {
        throw ConfigArchiveError(std::string("failed to insert deployment: ") + sqlite3_errmsg(db_));
    }
}

std::vector<CommitInfo> ConfigArchive::get_commit_history(int limit) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<CommitInfo> result;

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_,
            "SELECT sha256, parent_sha256, author, message, created_at FROM commits "
            "ORDER BY rowid DESC LIMIT ?;",
            -1, &stmt, nullptr) != SQLITE_OK) {
        throw ConfigArchiveError(std::string("failed to prepare history query: ") +
                                  sqlite3_errmsg(db_));
    }
    sqlite3_bind_int(stmt, 1, limit);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        CommitInfo info;
        info.sha256 = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        if (sqlite3_column_type(stmt, 1) != SQLITE_NULL) {
            info.parent_sha256 = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        }
        info.author = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        info.message = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        info.created_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        result.push_back(std::move(info));
    }
    sqlite3_finalize(stmt);
    return result;
}

nlohmann::json ConfigArchive::get_snapshot(const std::string& commit_sha256) const {
    std::lock_guard<std::mutex> lock(mutex_);

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, "SELECT payload FROM snapshots WHERE commit_sha256 = ?;", -1, &stmt,
                            nullptr) != SQLITE_OK) {
        throw ConfigArchiveError(std::string("failed to prepare snapshot query: ") +
                                  sqlite3_errmsg(db_));
    }
    sqlite3_bind_text(stmt, 1, commit_sha256.c_str(), -1, SQLITE_TRANSIENT);

    if (sqlite3_step(stmt) != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        throw ConfigArchiveError("no snapshot found for commit " + commit_sha256);
    }
    nlohmann::json result =
        nlohmann::json::parse(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)));
    sqlite3_finalize(stmt);
    return result;
}

} // namespace openaslc
