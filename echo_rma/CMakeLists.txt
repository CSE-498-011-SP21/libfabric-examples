project(echo_rma)

add_library(Fabric_rma INTERFACE)
target_include_directories(Fabric_rma INTERFACE include)
target_link_libraries(Fabric_rma INTERFACE fabric)

add_executable(echo src/echo_rma.cc)
target_link_libraries(echo PRIVATE Fabric_rma)