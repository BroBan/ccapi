set(NAME market_data_generic)
project(${NAME})
add_compile_definitions(CCAPI_ENABLE_SERVICE_MARKET_DATA)
add_executable(${NAME} ${SOURCE_LOGGER} test.cpp)
target_link_libraries(${NAME})
gtest_discover_tests(${NAME})
