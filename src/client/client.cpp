#include <iostream>
#include <string>
#include <vector>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/ostream_sink.h>
#include "../common/config.h"
#include "../common/utils.h"

int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::cerr << "Usage: " << argv[0] << " <config.json> <IMSI>" << std::endl;
        return 1;
    }
    std::string config_file = argv[1];
    std::string imsi_str    = argv[2];

    // Загрузка конфига
    ClientConfig config;
    try {
        config = loadClientConfig(config_file);
    } catch (const std::exception& e) {
        std::cerr << "Error loading config: " << e.what() << std::endl;
        return 1;
    }

    // Определение debug из log_level
    bool enable_debug = (config.log_level == "DEBUG" || config.log_level == "debug");

    // Логгер
    auto console_sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(std::cerr);
    console_sink->set_level(enable_debug ? spdlog::level::debug : spdlog::level::info);
    auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(config.log_file, true);
    file_sink->set_level(spdlog::level::debug);

    auto logger = std::make_shared<spdlog::logger>(
        "client_logger",
        spdlog::sinks_init_list{console_sink, file_sink}
    );
    logger->set_level(spdlog::level::debug);
    logger->flush_on(spdlog::level::info);
    spdlog::set_default_logger(logger);

    logger->info("Client starting: IMSI={}  server={}:{}  log_level={}",
                 imsi_str, config.server_ip, config.server_port, config.log_level);
    logger->debug("Loaded config: {}", config_file);

    // BCD
    std::vector<uint8_t> bcd;
    try {
        bcd = imsiStringToBcd(imsi_str);
    } catch (const std::exception& e) {
        logger->error("Invalid IMSI '{}': {}", imsi_str, e.what());
        return 1;
    }
    logger->debug("BCD bytes: [{}]", fmt::join(bcd, ","));

    // UDP socket
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        logger->critical("Cannot create UDP socket: {}", strerror(errno));
        return 1;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(config.server_port);
    if (inet_pton(AF_INET, config.server_ip.c_str(), &addr.sin_addr) <= 0) {
        logger->critical("Invalid server IP: {}", config.server_ip);
        close(sock);
        return 1;
    }

    // Отправление
    ssize_t sent = sendto(sock, bcd.data(), bcd.size(), 0,
                          reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    if (sent < 0) {
        logger->error("sendto failed: {}", strerror(errno));
        close(sock);
        return 1;
    }
    logger->info("Sent {} bytes to {}:{}", sent, config.server_ip, config.server_port);

    // Ответ
    char buf[64] = {};
    socklen_t len = sizeof(addr);
    ssize_t n = recvfrom(sock, buf, sizeof(buf)-1, 0,
                         reinterpret_cast<sockaddr*>(&addr), &len);
    if (n < 0) {
        logger->error("recvfrom failed: {}", strerror(errno));
        close(sock);
        return 1;
    }
    std::string response(buf, buf + n);
    logger->info("Received response: '{}'", response);

    // Вывод ответа и выход
    std::cout << response << std::endl;
    close(sock);
    spdlog::shutdown();
    return 0;
}
