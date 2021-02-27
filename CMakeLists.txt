cmake_minimum_required(VERSION 3.0)

project(Libfabric-examples)

enable_testing()

add_subdirectory(wrappers)

add_subdirectory(echo_rma)
