cmake_minimum_required(VERSION 3.16)
project(MiniPGW)

include(FetchContent)

# nlohmann_json через FetchContent
FetchContent_Declare(
        nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG v3.11.3  # или актуальная версия
)
FetchContent_MakeAvailable(nlohmann_json)

# Остальные зависимости через FetchContent
FetchContent_Declare(
        httplib
        GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
        GIT_TAG v0.12.0
)
FetchContent_MakeAvailable(httplib)

FetchContent_Declare(
        spdlog
        GIT_REPOSITORY https://github.com/gabime/spdlog.git
        GIT_TAG v1.12.0
)
FetchContent_MakeAvailable(spdlog)

FetchContent_Declare(
        gtest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG release-1.12.1
)
FetchContent_MakeAvailable(gtest)

add_subdirectory(src/common)
add_subdirectory(src/server)
add_subdirectory(src/client)
add_subdirectory(tests)