add_executable(pgw_client client.cpp)
target_link_libraries(pgw_client PRIVATE common nlohmann_json::nlohmann_json spdlog::spdlog)