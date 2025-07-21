#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <map>
#include <set>
#include <chrono>
#include <fstream>
#include <httplib.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include "../common/config.h"
#include "../common/utils.h"
#include <iomanip>
#include <ctime>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

struct Session {
    std::chrono::steady_clock::time_point creation_time;
};

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <config.json>" << std::endl;
        return 1;
    }
    std::string config_file = argv[1];

    // Загрузка конфига
    ServerConfig config;
    try {
        config = loadServerConfig(config_file);
    } catch (const std::exception& e) {
        std::cerr << "Error loading config: " << e.what() << std::endl;
        return 1;
    }

    // Настройка логгера
    bool enable_debug = (config.log_level == "DEBUG");
    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    // В консоль: debug только если enable_debug, иначе info+
    console_sink->set_level(enable_debug ? spdlog::level::debug : spdlog::level::info);
    auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(config.log_file, true);
    // В файл: всегда логируется всё
    file_sink->set_level(spdlog::level::debug);

    auto logger = std::make_shared<spdlog::logger>(
        "pgw_logger",
        spdlog::sinks_init_list{console_sink, file_sink}
    );
    logger->set_level(spdlog::level::debug);
    logger->flush_on(spdlog::level::info);
    spdlog::set_default_logger(logger);

    logger->info("Server starting: UDP {}:{}  HTTP port {}  CDR file {}  log_level={}",
                 config.udp_ip, config.udp_port, config.http_port, config.cdr_file, config.log_level);
    logger->debug("Config: timeout={}s, graceful_rate={} sess/sec",
                  config.session_timeout_sec, config.graceful_shutdown_rate);

    std::ofstream cdr_stream(config.cdr_file, std::ios::app);
    if (!cdr_stream.is_open()) {
        logger->critical("Cannot open CDR file: {}", config.cdr_file);
        return 1;
    }

    std::map<std::string, Session> sessions;
    std::set<std::string> blacklist(config.blacklist.begin(), config.blacklist.end());
    bool shutting_down = false;
    bool shutdown_complete = false;
    std::mutex mutex;
    std::condition_variable cv;
    std::mutex cdr_mutex;

    // UDP функция
    auto udp_function = [&]() {
        logger->debug("Starting UDP thread");
        int sock = socket(AF_INET, SOCK_DGRAM, 0);
        if (sock < 0) {
            logger->critical("Cannot create UDP socket: {}", strerror(errno));
            return;
        }

        timeval tv{1, 0};
        if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
            logger->warn("Failed to set SO_RCVTIMEO: {}", strerror(errno));
        }

        sockaddr_in serv_addr{};
        serv_addr.sin_family = AF_INET;
        serv_addr.sin_addr.s_addr = inet_addr(config.udp_ip.c_str());
        serv_addr.sin_port = htons(config.udp_port);
        if (bind(sock, (sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
            logger->critical("Bind failed on {}:{} – {}", config.udp_ip, config.udp_port, strerror(errno));
            close(sock);
            return;
        }
        logger->info("UDP listening on {}:{}", config.udp_ip, config.udp_port);

        while (true) {
            {
                std::lock_guard<std::mutex> lock(mutex);
                if (shutting_down) {
                    logger->debug("UDP thread stopping");
                    break;
                }
            }

            char buffer[8];
            sockaddr_in cli_addr{};
            socklen_t len = sizeof(cli_addr);
            int n = recvfrom(sock, buffer, sizeof(buffer), 0, (sockaddr*)&cli_addr, &len);
            if (n < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) continue;
                logger->error("recvfrom error: {}", strerror(errno));
                continue;
            }
            logger->debug("Received {} bytes from {}:{}", n,
                          inet_ntoa(cli_addr.sin_addr), ntohs(cli_addr.sin_port));
            if (n != 8) {
                logger->warn("Packet size {} != 8", n);
                continue;
            }

            std::vector<uint8_t> bcd(buffer, buffer+8);
            std::string imsi;
            try {
                imsi = bcdToImsiString(bcd);
            } catch (const std::exception& e) {
                logger->warn("BCD decode error: {}", e.what());
                continue;
            }
            logger->debug("Decoded IMSI {}", imsi);

            {
                std::lock_guard<std::mutex> lock(mutex);
                if (blacklist.count(imsi)) {
                    sendto(sock, "rejected", 8, 0, (sockaddr*)&cli_addr, len);
                    logger->info("Subscriber {} rejected (blacklist)", imsi);
                } else if (sessions.count(imsi)) {
                    // обновляем время существующей сессии
                    sessions[imsi].creation_time = std::chrono::steady_clock::now();
                    {
                        std::lock_guard<std::mutex> cdr_lock(cdr_mutex);
                        auto now_c = std::time(nullptr);
                        cdr_stream << std::put_time(std::localtime(&now_c), "%Y-%m-%d %H:%M:%S")
                                   << "," << imsi << ",renew\n";
                    }
                    logger->info("Session refreshed for IMSI {}", imsi);
                    sendto(sock, "refreshed", 7, 0, (sockaddr*)&cli_addr, len);
                } else {
                    // создание новой сессии...
                    sessions[imsi] = Session{std::chrono::steady_clock::now()};
                    {
                        std::lock_guard<std::mutex> cdr_lock(cdr_mutex);
                        auto now_c = std::time(nullptr);
                        cdr_stream << std::put_time(std::localtime(&now_c), "%Y-%m-%d %H:%M:%S")
                                   << "," << imsi << ",create\n";
                    }
                    logger->info("Session created for IMSI {}", imsi);
                    sendto(sock, "created", 7, 0, (sockaddr*)&cli_addr, len);
                }
            }
        }
        close(sock);
    };

    // HTTP функция
    auto http_function = [&]() {
        logger->debug("Starting HTTP thread");
        httplib::Server svr;
        svr.Get("/check_subscriber", [&](auto& req, auto& res) {
            std::string imsi = req.get_param_value("imsi");
            logger->debug("HTTP /check_subscriber imsi={}", imsi);
            std::lock_guard<std::mutex> lock(mutex);
            res.body = sessions.count(imsi) ? "active" : "not active";
        });
        svr.Get("/stop", [&](auto&, auto& res) {
            logger->info("HTTP /stop called");
            {
                std::lock_guard<std::mutex> lock(mutex);
                shutting_down = true;
            }
            svr.stop();
            res.status = 200;
            res.body = "Shutdown initiated";
        });
        if (!svr.listen("0.0.0.0", config.http_port)) {
            logger->error("HTTP listen failed on port {}", config.http_port);
        }
    };

    // Cleanup-функция
    auto cleanup_function = [&]() {
        logger->debug("Starting cleanup thread");
        // Удаление сессий после окончания времени обслуживания
        while (true) {
            {
                std::lock_guard<std::mutex> lock(mutex);
                if (shutting_down) break;
            }
            std::this_thread::sleep_for(std::chrono::seconds(1));
            std::vector<std::string> to_delete;
            auto now = std::chrono::steady_clock::now();
            {
                std::lock_guard<std::mutex> lock(mutex);
                for (auto& [imsi, s] : sessions) {
                    if (now - s.creation_time > std::chrono::seconds(config.session_timeout_sec))
                        to_delete.push_back(imsi);
                }
                if (to_delete.empty())
                    logger->debug("No timed‑out sessions this cycle");
            }
            for (auto& imsi : to_delete) {
                {
                    std::lock_guard<std::mutex> cdr_lock(cdr_mutex);
                    auto now_c = std::time(nullptr);
                    cdr_stream << std::put_time(std::localtime(&now_c), "%Y-%m-%d %H:%M:%S")
                               << "," << imsi << ",delete\n";
                }
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    sessions.erase(imsi);
                }
                logger->info("Session deleted for IMSI {}", imsi);
            }
        }
        // graceful shutdown
        logger->info("Graceful shutdown: {} sess/sec", config.graceful_shutdown_rate);
        while (true) {
            std::vector<std::string> to_shutdown;
            {
                std::lock_guard<std::mutex> lock(mutex);
                if (sessions.empty()) break;
                int c = 0;
                for (auto& [imsi, s] : sessions) {
                    if (c++ >= config.graceful_shutdown_rate) break;
                    to_shutdown.push_back(imsi);
                }
            }
            if (to_shutdown.empty()) logger->debug("No sessions to shutdown");
            {
                std::lock_guard<std::mutex> cdr_lock(cdr_mutex);
                auto now_c = std::time(nullptr);
                for (auto& imsi : to_shutdown) {
                    cdr_stream << std::put_time(std::localtime(&now_c), "%Y-%m-%d %H:%M:%S")
                               << "," << imsi << ",shutdown\n";
                    logger->info("Gracefully removed {}", imsi);
                    std::lock_guard<std::mutex> lock(mutex);
                    sessions.erase(imsi);
                }
            }
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        {
            std::lock_guard<std::mutex> lock(mutex);
            shutdown_complete = true;
            cv.notify_one();
        }
        logger->info("Graceful shutdown complete");
    };

    std::thread t1(udp_function), t2(http_function), t3(cleanup_function);
    {
        std::unique_lock<std::mutex> lock(mutex);
        cv.wait(lock, [&]{ return shutdown_complete; });
    }
    t1.join(); t2.join(); t3.join();

    logger->info("All done, exiting");
    cdr_stream.close();
    spdlog::shutdown();
    return 0;
}
