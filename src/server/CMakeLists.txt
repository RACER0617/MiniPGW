add_executable(pgw_server server.cpp)
target_link_libraries(pgw_server PRIVATE common nlohmann_json::nlohmann_json httplib::httplib spdlog::spdlog)