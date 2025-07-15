add_library(common STATIC config.cpp utils.cpp)
target_include_directories(common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})