#include "openaslc/web_server.hpp"

#include "openaslc/config_archive.hpp"
#include "openaslc/ring_buffer_logger.hpp"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <iostream>
#include <thread>

namespace openaslc {

namespace {

// A plain `cond ? *opt : nullptr` ternary here is a classic footgun: nullptr
// implicitly converts to `const char*` to unify with the std::string branch,
// so a nullopt parent constructs std::string(nullptr) -- undefined behavior
// (throws in libstdc++) instead of writing a JSON null. if/else sidesteps it.
nlohmann::json commit_to_json(const CommitInfo& commit) {
    nlohmann::json commit_json;
    commit_json["sha256"] = commit.sha256;
    if (commit.parent_sha256) {
        commit_json["parent_sha256"] = *commit.parent_sha256;
    } else {
        commit_json["parent_sha256"] = nullptr;
    }
    commit_json["author"] = commit.author;
    commit_json["message"] = commit.message;
    commit_json["created_at"] = commit.created_at;
    return commit_json;
}

} // namespace

WebServer::WebServer(LogicRuntime& logic_runtime, std::filesystem::path www_dir, int port,
                     SetInputCallback set_input_callback, ConfigArchive* config_archive,
                     RingBufferLogger* logger)
    : logic_runtime_(logic_runtime),
      www_dir_(std::move(www_dir)),
      port_(port),
      set_input_callback_(std::move(set_input_callback)),
      config_archive_(config_archive),
      logger_(logger) {}

WebServer::~WebServer() {
    stop();
}

void WebServer::start() {
    if (server_) {
        return;
    }

    server_ = std::make_unique<httplib::Server>();
    server_->set_mount_point("/", www_dir_.string());

    server_->Get("/", [](const httplib::Request&, httplib::Response& res) {
        res.set_redirect("/index.html");
    });

    server_->Post("/api/deploy", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            nlohmann::json doc = nlohmann::json::parse(req.body);
            auto program = logic_runtime_.reload_from_json(doc);
            nlohmann::json ok;
            ok["rule_count"] = program->rule_count();

            // Archiving happens only after a successful validate+swap -- an invalid
            // program never gets committed, so the history only ever contains
            // configs that actually ran. author/message come from query params
            // (not the JSON body) so the pasted textarea content is never mutated.
            if (config_archive_) {
                std::string author = req.get_param_value("author");
                if (author.empty()) {
                    author = "anonymous";
                }
                const std::string message = req.get_param_value("message");
                CommitInfo commit = config_archive_->commit_config(author, message, doc);
                config_archive_->record_deployment(commit.sha256);

                ok["commit"] = commit_to_json(commit);

                if (logger_) {
                    logger_->log(LogLevel::Info, "deploy: commit " + commit.sha256.substr(0, 8) +
                                                      " by " + author + " (" +
                                                      std::to_string(program->rule_count()) +
                                                      " rules)");
                }
            }

            res.status = 200;
            res.set_content(ok.dump(), "application/json");
        } catch (const LogicParseError& e) {
            nlohmann::json err;
            err["error"] = e.what();
            res.status = 400;
            res.set_content(err.dump(), "application/json");
            if (logger_) {
                logger_->log(LogLevel::Warn, std::string("deploy rejected: ") + e.what());
            }
        } catch (const nlohmann::json::parse_error& e) {
            nlohmann::json err;
            err["error"] = std::string("invalid JSON: ") + e.what();
            res.status = 400;
            res.set_content(err.dump(), "application/json");
            if (logger_) {
                logger_->log(LogLevel::Warn, std::string("deploy rejected: invalid JSON: ") + e.what());
            }
        } catch (const ConfigArchiveError& e) {
            // The logic program itself was already validated and swapped in
            // successfully by this point -- only the archive write failed, so this
            // is a genuine 500 (not the caller's fault) rather than a 400.
            nlohmann::json err;
            err["error"] = std::string("deploy succeeded but archiving failed: ") + e.what();
            res.status = 500;
            res.set_content(err.dump(), "application/json");
            if (logger_) {
                logger_->log(LogLevel::Error, std::string("archive write failed: ") + e.what());
            }
        }
    });

    if (config_archive_) {
        server_->Get("/api/history", [this](const httplib::Request& req, httplib::Response& res) {
            int limit = 50;
            if (req.has_param("limit")) {
                try {
                    limit = std::stoi(req.get_param_value("limit"));
                } catch (const std::exception&) {
                    // Keep the default on a malformed ?limit= value.
                }
            }
            nlohmann::json body;
            nlohmann::json commits = nlohmann::json::array();
            for (const auto& commit : config_archive_->get_commit_history(limit)) {
                commits.push_back(commit_to_json(commit));
            }
            body["commits"] = commits;
            res.status = 200;
            res.set_content(body.dump(), "application/json");
        });
    }

    if (set_input_callback_) {
        server_->Post("/api/input", [this](const httplib::Request& req, httplib::Response& res) {
            try {
                nlohmann::json doc = nlohmann::json::parse(req.body);
                std::size_t byte = doc.at("byte").get<std::size_t>();
                uint8_t bit = doc.at("bit").get<uint8_t>();
                bool value = doc.at("value").get<bool>();
                set_input_callback_(byte, bit, value);
                res.status = 200;
                res.set_content("{\"ok\":true}", "application/json");
            } catch (const nlohmann::json::exception& e) {
                nlohmann::json err;
                err["error"] = std::string("invalid input request: ") + e.what();
                res.status = 400;
                res.set_content(err.dump(), "application/json");
            }
        });
    }

    is_running_.store(true);
    server_thread_ = std::jthread([this](std::stop_token) {
        if (!server_->listen("0.0.0.0", port_)) {
            std::cerr << "[Error] WebServer: failed to bind port " << port_ << std::endl;
        }
    });

    // listen() binds synchronously before signaling ready; wait_until_ready()
    // is cpp-httplib's own helper for this exact background-thread pattern.
    server_->wait_until_ready();
}

void WebServer::stop() {
    if (!server_) {
        return;
    }
    server_->stop();
    if (server_thread_.joinable()) {
        server_thread_.join();
    }
    server_.reset();
    is_running_.store(false);
}

} // namespace openaslc
