#include <catch2/catch_test_macros.hpp>
#include "openaslc/telemetry_server.hpp"
#include "openaslc/memory_map.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

using namespace openaslc;
using namespace std::chrono_literals;

// A minimal RFC 6455 client used only to exercise TelemetryServer end-to-end.
// TelemetryServer's own handshake/framing helpers are file-local statics in
// telemetry_server.cpp, so this is deliberately an independent, from-scratch
// implementation -- it doubles as a cross-check that the server's SHA1 +
// base64 handshake math actually matches RFC 6455 (see the well-known test
// vector used below).
namespace {

#ifdef _WIN32
using socket_t = SOCKET;
constexpr socket_t kInvalidSocket = INVALID_SOCKET;
struct WinsockGuard {
    WinsockGuard() {
        WSADATA wsa_data;
        WSAStartup(MAKEWORD(2, 2), &wsa_data);
    }
    ~WinsockGuard() { WSACleanup(); }
};
void close_socket(socket_t s) { closesocket(s); }
#else
using socket_t = int;
constexpr socket_t kInvalidSocket = -1;
struct WinsockGuard {};
void close_socket(socket_t s) { close(s); }
#endif

constexpr int kTestPort = 18081;
// The RFC 6455 spec's own worked handshake example (section 1.3).
constexpr const char* kClientKey = "dGhlIHNhbXBsZSBub25jZQ==";
constexpr const char* kExpectedAccept = "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=";

socket_t connect_to_server() {
    socket_t sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(kTestPort));
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    if (connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        close_socket(sock);
        return kInvalidSocket;
    }
    return sock;
}

std::string read_until(socket_t sock, const std::string& delimiter) {
    std::string data;
    char buf;
    while (data.find(delimiter) == std::string::npos && data.size() < 16384) {
        int n = recv(sock, &buf, 1, 0);
        if (n <= 0) {
            break;
        }
        data.push_back(buf);
    }
    return data;
}

bool recv_exact(socket_t sock, uint8_t* out, std::size_t len) {
    std::size_t received = 0;
    while (received < len) {
        int n = recv(sock, reinterpret_cast<char*>(out + received), static_cast<int>(len - received), 0);
        if (n <= 0) {
            return false;
        }
        received += static_cast<std::size_t>(n);
    }
    return true;
}

// Reads one unmasked server->client WebSocket text frame and returns its payload.
std::string read_text_frame(socket_t sock) {
    uint8_t header[2];
    if (!recv_exact(sock, header, 2)) {
        return {};
    }
    REQUIRE((header[0] & 0x0F) == 0x1); // text opcode
    REQUIRE((header[1] & 0x80) == 0);   // server frames must not be masked

    uint64_t len = header[1] & 0x7F;
    if (len == 126) {
        uint8_t ext[2];
        if (!recv_exact(sock, ext, 2)) return {};
        len = (static_cast<uint16_t>(ext[0]) << 8) | ext[1];
    } else if (len == 127) {
        uint8_t ext[8];
        if (!recv_exact(sock, ext, 8)) return {};
        len = 0;
        for (int i = 0; i < 8; ++i) len = (len << 8) | ext[i];
    }

    std::vector<uint8_t> payload(static_cast<std::size_t>(len));
    if (!payload.empty() && !recv_exact(sock, payload.data(), payload.size())) {
        return {};
    }
    return std::string(payload.begin(), payload.end());
}

} // namespace

TEST_CASE("TelemetryServer performs the WS handshake and streams %I/%Q", "[telemetry]") {
    WinsockGuard winsock_guard;

    auto mem = std::make_shared<MemoryMap>();
    mem->write_bit(MemoryArea::Input, 0, 0, true); // %I[0].0 = 1, so I[0] should read back as 1

    TelemetryServer server(mem, kTestPort, 50ms);
    server.start();
    REQUIRE(server.is_running());

    // Give the accept loop a moment to be ready to accept connections.
    std::this_thread::sleep_for(50ms);

    socket_t sock = connect_to_server();
    REQUIRE(sock != kInvalidSocket);

    std::string request = std::string("GET /ws/telemetry HTTP/1.1\r\n") + "Host: localhost\r\n" +
                           "Upgrade: websocket\r\n" + "Connection: Upgrade\r\n" +
                           "Sec-WebSocket-Key: " + kClientKey + "\r\n" +
                           "Sec-WebSocket-Version: 13\r\n\r\n";
    REQUIRE(send(sock, request.data(), static_cast<int>(request.size()), 0) > 0);

    std::string response = read_until(sock, "\r\n\r\n");
    REQUIRE(response.find("101") != std::string::npos);
    REQUIRE(response.find(std::string("Sec-WebSocket-Accept: ") + kExpectedAccept) != std::string::npos);

    std::string frame_payload = read_text_frame(sock);
    REQUIRE_FALSE(frame_payload.empty());

    auto msg = nlohmann::json::parse(frame_payload);
    REQUIRE(msg["I"].is_array());
    REQUIRE(msg["I"].size() == MemoryMap::INPUT_SIZE);
    REQUIRE(msg["Q"].is_array());
    REQUIRE(msg["Q"].size() == MemoryMap::OUTPUT_SIZE);
    REQUIRE(msg["I"][0] == 1);

    close_socket(sock);
    server.stop();
    REQUIRE_FALSE(server.is_running());
}
