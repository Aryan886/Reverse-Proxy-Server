#include "reverse_proxy.h"
#include <charconv>
#include <csignal>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace {
volatile std::sig_atomic_t stopRequested = 0;
void requestStop(int) { stopRequested = 1; }

int parseInteger(std::string_view text, const std::string& name, int maximum) {
    int number = 0;
    auto result = std::from_chars(text.data(), text.data() + text.size(), number);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size() || number < 1 || number > maximum) {
        throw std::invalid_argument(name + " must be between 1 and " + std::to_string(maximum));
    }
    return number;
}

BackendEndpoint parseEndpoint(std::string_view text) {
    auto separator = text.rfind(':');
    if (separator == std::string_view::npos || separator == 0) {
        throw std::invalid_argument("Backend endpoint must be HOST:PORT or [IPv6]:PORT");
    }
    std::string host(text.substr(0, separator));
    if (host.front() == '[' && host.back() == ']') host = host.substr(1, host.size() - 2);
    else if (host.find(':') != std::string::npos) throw std::invalid_argument("IPv6 backend hosts must use brackets");
    if (host.empty()) throw std::invalid_argument("Backend host must not be empty");
    return {host, parseInteger(text.substr(separator + 1), "Backend port", 65535)};
}

void printHelp() {
    std::cout << "Usage: reverse_proxy [options]\n"
        "  --port PORT                    Listen port (default 8080)\n"
        "  --api HOST:PORT                API backend; repeat for replicas\n"
        "  --static HOST:PORT             Static backend; repeat for replicas\n"
        "  --policy round-robin|least-connections\n"
        "  --io-timeout-ms N               Total deadline per read/write stage (default 5000)\n"
        "  --connect-timeout-ms N          Upstream connect deadline (default 1000)\n"
        "  --help\n"
        "Defaults: /api -> 127.0.0.1:9001,9002; /static -> 127.0.0.1:9003\n";
}
}

int main(int argumentCount, char* arguments[]) {
    try {
        ProxyConfig config;
        std::vector<BackendEndpoint> apiBackends{{"127.0.0.1", 9001}, {"127.0.0.1", 9002}};
        std::vector<BackendEndpoint> staticBackends{{"127.0.0.1", 9003}};
        bool apiConfigured = false, staticConfigured = false, useLeastConnections = false;
        for (int index = 1; index < argumentCount; ++index) {
            std::string option = arguments[index];
            if (option == "--help") {
                printHelp();
                return 0;
            }
            if (index + 1 >= argumentCount) throw std::invalid_argument("Missing value for " + option);
            std::string_view value = arguments[++index];
            if (option == "--port") config.listenPort = parseInteger(value, "Listen port", 65535);
            else if (option == "--io-timeout-ms") config.ioTimeout = std::chrono::milliseconds(parseInteger(value, "I/O timeout", 60000));
            else if (option == "--connect-timeout-ms") config.connectTimeout = std::chrono::milliseconds(parseInteger(value, "Connect timeout", 60000));
            else if (option == "--api") {
                if (!apiConfigured) apiBackends.clear();
                apiConfigured = true;
                apiBackends.push_back(parseEndpoint(value));
            } else if (option == "--static") {
                if (!staticConfigured) staticBackends.clear();
                staticConfigured = true;
                staticBackends.push_back(parseEndpoint(value));
            } else if (option == "--policy") {
                if (value == "least-connections") useLeastConnections = true;
                else if (value == "round-robin") useLeastConnections = false;
                else throw std::invalid_argument("Policy must be round-robin or least-connections");
            } else {
                throw std::invalid_argument("Unknown option: " + option);
            }
        }
        config.routes.push_back({"/api", std::move(apiBackends), useLeastConnections});
        config.routes.push_back({"/static", std::move(staticBackends), useLeastConnections});
        std::signal(SIGINT, requestStop);
        std::signal(SIGTERM, requestStop);
        ReverseProxy proxy(std::move(config));
        proxy.run(stopRequested);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Proxy startup/run failed: " << error.what() << '\n';
        return 1;
    }
}
