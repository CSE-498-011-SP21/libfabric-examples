project(wrappers)

add_library(Fabricxx INTERFACE)
target_include_directories(Fabricxx INTERFACE include)
target_link_libraries(Fabricxx INTERFACE fabric)

add_executable(creation_test src/test.cc)
target_link_libraries(creation_test PRIVATE Fabricxx)