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
#include "../common/config.h"
#include "../common/utils.h"
#include <iomanip>
#include <ctime>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <spdlog/sinks/stdout_color_sinks.h>

struct Session {
    std::chrono::steady_clock::time_point creation_time;
};

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <config.json>" << std::endl;
        return 1;
    }
    std::string config_file = argv[1];
    ServerConfig config;
    try {
        config = loadServerConfig(config_file);
    } catch (const std::exception& e) {
        std::cerr << "Error loading config: " << e.what() << std::endl;
        return 1;
    }

    auto logger = spdlog::basic_logger_mt("pgw_logger", config.log_file);
    spdlog::level::level_enum log_level = spdlog::level::from_str(config.log_level);
    logger->set_level(log_level);

    std::ofstream cdr_stream(config.cdr_file, std::ios::app);
    if (!cdr_stream.is_open()) {
        logger->error("Cannot open CDR file: {}", config.cdr_file);
        return 1;
    }

    std::map<std::string, Session> sessions;
    std::set<std::string> blacklist(config.blacklist.begin(), config.blacklist.end());
    bool shutting_down = false;
    bool shutdown_complete = false;
    std::mutex mutex;
    std::condition_variable cv;
    std::mutex cdr_mutex;

    auto udp_function = [&]() {
        int sock = socket(AF_INET, SOCK_DGRAM, 0);
        if (sock < 0) {
            logger->error("Cannot create UDP socket");
            return;
        }
        sockaddr_in serv_addr;
        memset(&serv_addr, 0, sizeof(serv_addr));
        serv_addr.sin_family = AF_INET;
        serv_addr.sin_addr.s_addr = inet_addr(config.udp_ip.c_str());
        serv_addr.sin_port = htons(config.udp_port);
        if (bind(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
            logger->error("Bind failed");
            close(sock);
            return;
        }
        while (true) {
            {
                std::lock_guard<std::mutex> lock(mutex);
                if (shutting_down) break;
            }
            char buffer[8];
            sockaddr_in cli_addr;
            socklen_t len = sizeof(cli_addr);
            int n = recvfrom(sock, buffer, 8, 0, (struct sockaddr*)&cli_addr, &len);
            if (n < 0) {
                logger->error("Recvfrom failed");
                continue;
            }
            if (n != 8) {
                logger->warn("Received packet of size {} != 8", n);
                continue;
            }
            std::vector<uint8_t> bcd(buffer, buffer + 8);
            std::string imsi;
            try {
                imsi = bcdToImsiString(bcd);
            } catch (const std::exception& e) {
                logger->warn("Invalid BCD: {}", e.what());
                continue;
            }
            {
                std::lock_guard<std::mutex> lock(mutex);
                if (blacklist.find(imsi) != blacklist.end() || sessions.find(imsi) != sessions.end()) {
                    sendto(sock, "rejected", 8, 0, (struct sockaddr*)&cli_addr, len);
                } else {
                    Session new_session{std::chrono::steady_clock::now()};
                    sessions[imsi] = new_session;
                    {
                        std::lock_guard<std::mutex> lock(cdr_mutex);
                        auto now = std::chrono::system_clock::now();
                        auto now_c = std::chrono::system_clock::to_time_t(now);
                        cdr_stream << std::put_time(std::localtime(&now_c), "%Y-%m-%d %H:%M:%S") << "," << imsi << ",create" << std::endl;
                        cdr_stream.flush();
                    }
                    sendto(sock, "created", 7, 0, (struct sockaddr*)&cli_addr, len);
                }
            }
        }
        close(sock);
    };

    auto http_function = [&]() {
        httplib::Server svr;
        svr.Get("/check_subscriber", [&](const httplib::Request& req, httplib::Response& res) {
            auto params = req.params;
            auto it = params.find("imsi");
            if (it == params.end()) {
                res.status = 400;
                res.body = "Missing imsi parameter";
                return;
            }
            std::string imsi = it->second;
            std::lock_guard<std::mutex> lock(mutex);
            if (sessions.find(imsi) != sessions.end()) {
                res.body = "active";
            } else {
                res.body = "not active";
            }
        });
        svr.Get("/stop", [&](const httplib::Request& req, httplib::Response& res) {
            {
                std::lock_guard<std::mutex> lock(mutex);
                shutting_down = true;
            }
            svr.stop();
            res.status = 200;
            res.body = "Shutdown initiated";
        });
        svr.listen("0.0.0.0", config.http_port);
    };

    auto cleanup_function = [&]() {
        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            {
                std::lock_guard<std::mutex> lock(mutex);
                auto now = std::chrono::steady_clock::now();
                if (shutting_down) {
                    int removed = 0;
                    while (removed < config.graceful_shutdown_rate && !sessions.empty()) {
                        auto it = sessions.begin();
                        {
                            std::lock_guard<std::mutex> cdr_lock(cdr_mutex);
                            auto now_cdr = std::chrono::system_clock::now();
                            auto now_c = std::chrono::system_clock::to_time_t(now_cdr);
                            cdr_stream << std::put_time(std::localtime(&now_c), "%Y-%m-%d %H:%M:%S") << "," << it->first << ",delete" << std::endl;
                            cdr_stream.flush();
                        }
                        sessions.erase(it);
                        ++removed;
                    }
                    if (sessions.empty()) {
                        shutdown_complete = true;
                        cv.notify_one();
                        break;
                    }
                } else {
                    for (auto it = sessions.begin(); it != sessions.end(); ) {
                        if (now - it->second.creation_time > std::chrono::seconds(config.session_timeout_sec)) {
                            {
                                std::lock_guard<std::mutex> cdr_lock(cdr_mutex);
                                auto now_cdr = std::chrono::system_clock::now();
                                auto now_c = std::chrono::system_clock::to_time_t(now_cdr);
                                cdr_stream << std::put_time(std::localtime(&now_c), "%Y-%m-%d %H:%M:%S") << "," << it->first << ",delete" << std::endl;
                                cdr_stream.flush();
                            }
                            it = sessions.erase(it);
                        } else {
                            ++it;
                        }
                    }
                }
            }
        }
    };

    std::thread udp_thread(udp_function);
    std::thread http_thread(http_function);
    std::thread cleanup_thread(cleanup_function);

    {
        std::unique_lock<std::mutex> lock(mutex);
        cv.wait(lock, [&]{ return shutting_down && shutdown_complete; });
    }

    udp_thread.join();
    http_thread.join();
    cleanup_thread.join();

    cdr_stream.close();

    return 0;
}