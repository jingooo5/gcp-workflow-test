#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <optional>
#include <regex>
#include <sstream>
#include <string>
#include <string_view>

namespace {
constexpr int kPort = 8080;
constexpr int kBacklog = 16;
constexpr int kBufferSize = 4096;

std::optional<std::string> extract_query_param(std::string_view target, std::string_view key) {
    auto pos = target.find('?');
    if (pos == std::string_view::npos) {
        return std::nullopt;
    }
    std::string_view query = target.substr(pos + 1);
    std::string_view k = key;

    while (!query.empty()) {
        auto amp = query.find('&');
        std::string_view pair = query.substr(0, amp);
        auto eq = pair.find('=');
        if (eq != std::string_view::npos) {
            auto name = pair.substr(0, eq);
            auto value = pair.substr(eq + 1);
            if (name == k) {
                return std::string(value);
            }
        }
        if (amp == std::string_view::npos) {
            break;
        }
        query = query.substr(amp + 1);
    }
    return std::nullopt;
}

bool is_safe_host(std::string_view host) {
    if (host.empty() || host.size() > 255) {
        return false;
    }
    for (char c : host) {
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '-')) {
            return false;
        }
    }
    return true;
}

std::optional<double> ping_once_ms(const std::string& host) {
    std::string cmd = "ping -c 1 -W 1 " + host + " 2>/dev/null";
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        return std::nullopt;
    }

    char buffer[512];
    std::string output;
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        output += buffer;
    }
    int rc = pclose(pipe);
    if (rc != 0) {
        return std::nullopt;
    }

    std::regex re(R"(time=([0-9]+\.?[0-9]*)\s*ms)");
    std::smatch match;
    if (std::regex_search(output, match, re) && match.size() > 1) {
        return std::stod(match[1].str());
    }
    return std::nullopt;
}

std::string http_response(int status, std::string_view body, std::string_view content_type = "application/json") {
    std::ostringstream oss;
    oss << "HTTP/1.1 " << status << " ";
    switch (status) {
        case 200: oss << "OK"; break;
        case 400: oss << "Bad Request"; break;
        case 404: oss << "Not Found"; break;
        case 500: oss << "Internal Server Error"; break;
        default: oss << "OK"; break;
    }
    oss << "\r\n";
    oss << "Content-Type: " << content_type << "\r\n";
    oss << "Content-Length: " << body.size() << "\r\n";
    oss << "Connection: close\r\n";
    oss << "\r\n";
    oss << body;
    return oss.str();
}

std::string json_error(std::string_view message) {
    std::ostringstream oss;
    oss << "{\"error\":\"" << message << "\"}";
    return oss.str();
}

}  // namespace

int main() {
    std::signal(SIGPIPE, SIG_IGN);

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        std::cerr << "socket error: " << std::strerror(errno) << "\n";
        return 1;
    }

    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        std::cerr << "setsockopt error: " << std::strerror(errno) << "\n";
        close(server_fd);
        return 1;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(kPort);

    if (bind(server_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "bind error: " << std::strerror(errno) << "\n";
        close(server_fd);
        return 1;
    }

    if (listen(server_fd, kBacklog) < 0) {
        std::cerr << "listen error: " << std::strerror(errno) << "\n";
        close(server_fd);
        return 1;
    }

    std::cout << "Ping server listening on 0.0.0.0:" << kPort << "\n";
    std::cout << "Try: curl 'http://127.0.0.1:" << kPort << "/ping?host=8.8.8.8'" << "\n";

    while (true) {
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(server_fd, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
        if (client_fd < 0) {
            std::cerr << "accept error: " << std::strerror(errno) << "\n";
            continue;
        }

        char buffer[kBufferSize];
        ssize_t read_bytes = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
        if (read_bytes <= 0) {
            close(client_fd);
            continue;
        }
        buffer[read_bytes] = '\0';
        std::string request(buffer);

        std::istringstream iss(request);
        std::string method;
        std::string target;
        std::string version;
        iss >> method >> target >> version;

        std::string response;
        if (method != "GET") {
            response = http_response(400, json_error("Only GET supported"));
        } else if (target == "/" || target == "/health" || target == "/healthz") {
            response = http_response(200, "OK\n", "text/plain");
        } else if (target.rfind("/ping", 0) == 0) {
            auto host = extract_query_param(target, "host");
            std::string host_value = host.value_or("8.8.8.8");
            if (!is_safe_host(host_value)) {
                response = http_response(400, json_error("Invalid host"));
            } else {
                auto ms = ping_once_ms(host_value);
                if (!ms.has_value()) {
                    response = http_response(500, json_error("Ping failed"));
                } else {
                    std::ostringstream body;
                    body << "{\"host\":\"" << host_value << "\",\"latency_ms\":"
                         << std::fixed << std::setprecision(2) << ms.value() << "}";
                    response = http_response(200, body.str());
                }
            }
        } else {
            response = http_response(404, json_error("Not found"));
        }

        send(client_fd, response.c_str(), response.size(), 0);
        close(client_fd);
    }

    close(server_fd);
    return 0;
}
