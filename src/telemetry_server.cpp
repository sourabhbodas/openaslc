#include "openaslc/telemetry_server.hpp"

#include <nlohmann/json.hpp>

#include <array>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace {

#ifdef _WIN32
using socket_t = SOCKET;
constexpr socket_t kInvalidSocket = INVALID_SOCKET;
#else
using socket_t = int;
constexpr socket_t kInvalidSocket = -1;
#endif

void close_socket(socket_t s) {
#ifdef _WIN32
    closesocket(s);
#else
    close(s);
#endif
}

// --- SHA1 (RFC 3174) -- used only for the WebSocket handshake, not general crypto. ---
struct Sha1Digest {
    std::array<uint8_t, 20> bytes;
};

Sha1Digest sha1(const std::string& input) {
    uint32_t h0 = 0x67452301, h1 = 0xEFCDAB89, h2 = 0x98BADCFE, h3 = 0x10325476, h4 = 0xC3D2E1F0;

    std::vector<uint8_t> msg(input.begin(), input.end());
    const uint64_t bit_len = static_cast<uint64_t>(msg.size()) * 8;
    msg.push_back(0x80);
    while (msg.size() % 64 != 56) {
        msg.push_back(0x00);
    }
    for (int i = 7; i >= 0; --i) {
        msg.push_back(static_cast<uint8_t>((bit_len >> (i * 8)) & 0xFF));
    }

    auto left_rotate = [](uint32_t value, int bits) {
        return (value << bits) | (value >> (32 - bits));
    };

    for (std::size_t chunk = 0; chunk < msg.size(); chunk += 64) {
        std::array<uint32_t, 80> w{};
        for (int i = 0; i < 16; ++i) {
            w[i] = (static_cast<uint32_t>(msg[chunk + i * 4]) << 24) |
                   (static_cast<uint32_t>(msg[chunk + i * 4 + 1]) << 16) |
                   (static_cast<uint32_t>(msg[chunk + i * 4 + 2]) << 8) |
                   (static_cast<uint32_t>(msg[chunk + i * 4 + 3]));
        }
        for (int i = 16; i < 80; ++i) {
            w[i] = left_rotate(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        }

        uint32_t a = h0, b = h1, c = h2, d = h3, e = h4;
        for (int i = 0; i < 80; ++i) {
            uint32_t f, k;
            if (i < 20) {
                f = (b & c) | ((~b) & d);
                k = 0x5A827999;
            } else if (i < 40) {
                f = b ^ c ^ d;
                k = 0x6ED9EBA1;
            } else if (i < 60) {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8F1BBCDC;
            } else {
                f = b ^ c ^ d;
                k = 0xCA62C1D6;
            }
            uint32_t temp = left_rotate(a, 5) + f + e + k + w[i];
            e = d;
            d = c;
            c = left_rotate(b, 30);
            b = a;
            a = temp;
        }
        h0 += a;
        h1 += b;
        h2 += c;
        h3 += d;
        h4 += e;
    }

    Sha1Digest digest{};
    uint32_t words[5] = {h0, h1, h2, h3, h4};
    for (int i = 0; i < 5; ++i) {
        digest.bytes[i * 4] = static_cast<uint8_t>((words[i] >> 24) & 0xFF);
        digest.bytes[i * 4 + 1] = static_cast<uint8_t>((words[i] >> 16) & 0xFF);
        digest.bytes[i * 4 + 2] = static_cast<uint8_t>((words[i] >> 8) & 0xFF);
        digest.bytes[i * 4 + 3] = static_cast<uint8_t>(words[i] & 0xFF);
    }
    return digest;
}

std::string base64_encode(const uint8_t* data, std::size_t len) {
    static const char* table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((len + 2) / 3) * 4);
    std::size_t i = 0;
    for (; i + 3 <= len; i += 3) {
        uint32_t n = (static_cast<uint32_t>(data[i]) << 16) | (static_cast<uint32_t>(data[i + 1]) << 8) |
                     data[i + 2];
        out.push_back(table[(n >> 18) & 0x3F]);
        out.push_back(table[(n >> 12) & 0x3F]);
        out.push_back(table[(n >> 6) & 0x3F]);
        out.push_back(table[n & 0x3F]);
    }
    const std::size_t rem = len - i;
    if (rem == 1) {
        uint32_t n = static_cast<uint32_t>(data[i]) << 16;
        out.push_back(table[(n >> 18) & 0x3F]);
        out.push_back(table[(n >> 12) & 0x3F]);
        out.push_back('=');
        out.push_back('=');
    } else if (rem == 2) {
        uint32_t n = (static_cast<uint32_t>(data[i]) << 16) | (static_cast<uint32_t>(data[i + 1]) << 8);
        out.push_back(table[(n >> 18) & 0x3F]);
        out.push_back(table[(n >> 12) & 0x3F]);
        out.push_back(table[(n >> 6) & 0x3F]);
        out.push_back('=');
    }
    return out;
}

constexpr const char* kWsMagicGuid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

// Reads the HTTP upgrade request off `sock` and returns the Sec-WebSocket-Key
// header value, or an empty string if the request isn't a valid WS upgrade.
std::string read_websocket_key(socket_t sock) {
    std::string request;
    char buf;
    // Byte-at-a-time read is fine here: the handshake happens once per
    // connection, well before the 10Hz telemetry loop starts.
    while (request.find("\r\n\r\n") == std::string::npos && request.size() < 8192) {
        int n = recv(sock, &buf, 1, 0);
        if (n <= 0) {
            return {};
        }
        request.push_back(buf);
    }

    const std::string marker = "Sec-WebSocket-Key:";
    auto pos = request.find(marker);
    if (pos == std::string::npos) {
        return {};
    }
    pos += marker.size();
    auto end = request.find("\r\n", pos);
    if (end == std::string::npos) {
        return {};
    }
    std::string key = request.substr(pos, end - pos);
    auto first = key.find_first_not_of(" \t");
    auto last = key.find_last_not_of(" \t");
    if (first == std::string::npos) {
        return {};
    }
    return key.substr(first, last - first + 1);
}

bool send_all(socket_t sock, const uint8_t* data, std::size_t len) {
    std::size_t sent = 0;
    while (sent < len) {
        int n = send(sock, reinterpret_cast<const char*>(data + sent), static_cast<int>(len - sent), 0);
        if (n <= 0) {
            return false;
        }
        sent += static_cast<std::size_t>(n);
    }
    return true;
}

bool send_text_frame(socket_t sock, const std::string& payload) {
    std::vector<uint8_t> frame;
    frame.push_back(0x81); // FIN=1, opcode=0x1 (text)

    const std::size_t len = payload.size();
    if (len < 126) {
        frame.push_back(static_cast<uint8_t>(len)); // MASK=0 -- server frames are never masked
    } else if (len <= 0xFFFF) {
        frame.push_back(126);
        frame.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
        frame.push_back(static_cast<uint8_t>(len & 0xFF));
    } else {
        frame.push_back(127);
        for (int i = 7; i >= 0; --i) {
            frame.push_back(static_cast<uint8_t>((static_cast<uint64_t>(len) >> (i * 8)) & 0xFF));
        }
    }
    frame.insert(frame.end(), payload.begin(), payload.end());
    return send_all(sock, frame.data(), frame.size());
}

void send_control_frame(socket_t sock, uint8_t opcode, const std::vector<uint8_t>& payload) {
    std::vector<uint8_t> frame;
    frame.push_back(static_cast<uint8_t>(0x80 | opcode));
    frame.push_back(static_cast<uint8_t>(payload.size() & 0x7F));
    frame.insert(frame.end(), payload.begin(), payload.end());
    send_all(sock, frame.data(), frame.size());
}

enum class PollResult { kNoData, kClosed, kHandled };

// Non-blocking check for a single incoming WebSocket frame from the client.
// Only used to answer Ping/Close per RFC 6455 -- telemetry is server->client
// only, so any data frame the client sends is simply discarded.
PollResult poll_client_frame(socket_t sock) {
    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(sock, &read_fds);
    timeval timeout{0, 0};
    int ready = select(static_cast<int>(sock) + 1, &read_fds, nullptr, nullptr, &timeout);
    if (ready <= 0) {
        return PollResult::kNoData;
    }

    uint8_t header[2];
    if (recv(sock, reinterpret_cast<char*>(header), 2, 0) <= 0) {
        return PollResult::kClosed;
    }

    const uint8_t opcode = header[0] & 0x0F;
    const bool masked = (header[1] & 0x80) != 0;
    uint64_t payload_len = header[1] & 0x7F;

    if (payload_len == 126) {
        uint8_t ext[2];
        if (recv(sock, reinterpret_cast<char*>(ext), 2, 0) <= 0) {
            return PollResult::kClosed;
        }
        payload_len = (static_cast<uint16_t>(ext[0]) << 8) | ext[1];
    } else if (payload_len == 127) {
        uint8_t ext[8];
        if (recv(sock, reinterpret_cast<char*>(ext), 8, 0) <= 0) {
            return PollResult::kClosed;
        }
        payload_len = 0;
        for (int i = 0; i < 8; ++i) {
            payload_len = (payload_len << 8) | ext[i];
        }
    }

    uint8_t mask_key[4] = {0, 0, 0, 0};
    if (masked) {
        if (recv(sock, reinterpret_cast<char*>(mask_key), 4, 0) <= 0) {
            return PollResult::kClosed;
        }
    }

    std::vector<uint8_t> payload(static_cast<std::size_t>(payload_len));
    std::size_t received = 0;
    while (received < payload.size()) {
        int r = recv(sock, reinterpret_cast<char*>(payload.data() + received),
                      static_cast<int>(payload.size() - received), 0);
        if (r <= 0) {
            return PollResult::kClosed;
        }
        received += static_cast<std::size_t>(r);
    }
    if (masked) {
        for (std::size_t i = 0; i < payload.size(); ++i) {
            payload[i] ^= mask_key[i % 4];
        }
    }

    if (opcode == 0x8) { // close
        send_control_frame(sock, 0x8, {});
        return PollResult::kClosed;
    }
    if (opcode == 0x9) { // ping
        send_control_frame(sock, 0xA, payload);
        return PollResult::kHandled;
    }
    return PollResult::kHandled; // text/binary/pong/continuation -- ignored
}

} // namespace

namespace openaslc {

TelemetryServer::TelemetryServer(std::shared_ptr<MemoryMap> memory_map, int port,
                                  std::chrono::milliseconds broadcast_period)
    : memory_map_(std::move(memory_map)),
      port_(port),
      broadcast_period_(broadcast_period),
      listen_socket_(static_cast<std::uintptr_t>(kInvalidSocket)) {}

TelemetryServer::~TelemetryServer() {
    stop();
}

void TelemetryServer::start() {
    if (is_running_.load()) {
        return;
    }

#ifdef _WIN32
    WSADATA wsa_data;
    WSAStartup(MAKEWORD(2, 2), &wsa_data);
#endif

    socket_t listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listen_sock == kInvalidSocket) {
        std::cerr << "[Error] TelemetryServer: failed to create socket" << std::endl;
        return;
    }

    int reuse = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(static_cast<uint16_t>(port_));

    if (bind(listen_sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        std::cerr << "[Error] TelemetryServer: bind failed on port " << port_ << std::endl;
        close_socket(listen_sock);
        return;
    }
    if (listen(listen_sock, 16) != 0) {
        std::cerr << "[Error] TelemetryServer: listen failed" << std::endl;
        close_socket(listen_sock);
        return;
    }

    listen_socket_ = static_cast<std::uintptr_t>(listen_sock);
    is_running_.store(true);
    accept_thread_ = std::jthread([this](std::stop_token) { accept_loop(); });
}

void TelemetryServer::stop() {
    if (!is_running_.load()) {
        return;
    }
    is_running_.store(false);

    // Closing the listen socket unblocks the accept() call in accept_loop().
    close_socket(static_cast<socket_t>(listen_socket_));
    listen_socket_ = static_cast<std::uintptr_t>(kInvalidSocket);

    if (accept_thread_.joinable()) {
        accept_thread_.join();
    }

    {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        // is_running_ is already false, so every client_loop notices within
        // one broadcast_period_ and returns; clearing the vector joins them.
        client_threads_.clear();
    }

#ifdef _WIN32
    WSACleanup();
#endif
}

void TelemetryServer::accept_loop() {
    // A plain blocking accept() here would rely on stop()'s close_socket() to
    // unblock it from another thread -- reliable on Windows (Winsock closesocket
    // is documented to do this), but unspecified/unreliable on POSIX: on Linux
    // the blocked accept() call just never returns, hanging stop() forever. This
    // was only ever exercised on Windows until CI ran it on Linux for the first
    // time. Polling with select() (same non-blocking-check idiom as
    // poll_client_frame) bounds shutdown latency to one timeout interval on every
    // platform instead of depending on that behavior at all.
    while (is_running_.load()) {
        socket_t listen_sock = static_cast<socket_t>(listen_socket_);

        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(listen_sock, &read_fds);
        timeval timeout{0, 200000}; // 200ms
        int ready = select(static_cast<int>(listen_sock) + 1, &read_fds, nullptr, nullptr, &timeout);
        if (ready <= 0) {
            continue;
        }

        sockaddr_in client_addr{};
#ifdef _WIN32
        int addr_len = sizeof(client_addr);
#else
        socklen_t addr_len = sizeof(client_addr);
#endif
        socket_t client_sock = accept(listen_sock, reinterpret_cast<sockaddr*>(&client_addr), &addr_len);
        if (!is_running_.load()) {
            if (client_sock != kInvalidSocket) {
                close_socket(client_sock);
            }
            break;
        }
        if (client_sock == kInvalidSocket) {
            continue;
        }

        std::string key = read_websocket_key(client_sock);
        if (key.empty()) {
            close_socket(client_sock);
            continue;
        }

        Sha1Digest digest = sha1(key + kWsMagicGuid);
        std::string accept_key = base64_encode(digest.bytes.data(), digest.bytes.size());

        std::ostringstream response;
        response << "HTTP/1.1 101 Switching Protocols\r\n"
                    "Upgrade: websocket\r\n"
                    "Connection: Upgrade\r\n"
                    "Sec-WebSocket-Accept: "
                 << accept_key << "\r\n\r\n";
        const std::string response_str = response.str();
        if (!send_all(client_sock, reinterpret_cast<const uint8_t*>(response_str.data()), response_str.size())) {
            close_socket(client_sock);
            continue;
        }

        std::lock_guard<std::mutex> lock(clients_mutex_);
        client_threads_.push_back(std::jthread([this, client_sock](std::stop_token) {
            client_loop(static_cast<std::uintptr_t>(client_sock));
        }));
    }
}

void TelemetryServer::client_loop(std::uintptr_t client_socket_handle) {
    const socket_t sock = static_cast<socket_t>(client_socket_handle);
    uint64_t sequence = 0;

    while (is_running_.load()) {
        if (poll_client_frame(sock) == PollResult::kClosed) {
            break;
        }

        nlohmann::json msg;
        msg["seq"] = sequence++;
        msg["I"] = memory_map_->get_area_snapshot(MemoryArea::Input);
        msg["Q"] = memory_map_->get_area_snapshot(MemoryArea::Output);

        if (!send_text_frame(sock, msg.dump())) {
            break;
        }

        std::this_thread::sleep_for(broadcast_period_);
    }

    close_socket(sock);
}

} // namespace openaslc
