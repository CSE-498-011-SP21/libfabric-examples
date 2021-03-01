project(echo_msg)

add_library(Fabric_msg INTERFACE)
target_include_directories(Fabric_msg INTERFACE include)
target_link_libraries(Fabric_msg INTERFACE fabric)

add_executable(echo ./Echo.cpp)
target_link_libraries(echo PRIVATE Fabric_msg)