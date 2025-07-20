include(GoogleTest)

message(STATUS "Configuring tests")

add_executable(test_utils test_utils.cpp)
target_link_libraries(test_utils PRIVATE gtest_main common)
message(STATUS "Added test_utils")

add_executable(test_config test_config.cpp)
target_link_libraries(test_config PRIVATE gtest_main common)
message(STATUS "Added test_config")

add_executable(test_client_integration test_client_integration.cpp)
target_link_libraries(test_client_integration PRIVATE gtest_main common)
message(STATUS "Added test_client_integration")

add_executable(test_server_integration test_server_integration.cpp)
target_link_libraries(test_server_integration PRIVATE gtest_main common)
message(STATUS "Added test_server_integration")

add_test(NAME test_utils COMMAND test_utils)
add_test(NAME test_config COMMAND test_config)
add_test(NAME test_client_integration COMMAND test_client_integration)
add_test(NAME test_server_integration COMMAND test_server_integration)
message(STATUS "Registered tests for ctest")