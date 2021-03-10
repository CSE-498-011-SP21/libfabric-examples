project(echo_msg)

add_library(Fabric_msg INTERFACE)
target_include_directories(Fabric_msg INTERFACE include)
target_link_libraries(Fabric_msg INTERFACE fabric)

add_executable(echo_msg ./Echo.cpp)
target_link_libraries(echo_msg PRIVATE Fabric_msg)