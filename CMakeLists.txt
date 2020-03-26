cmake_minimum_required(VERSION 3.0)
project(osmium)

add_library(osmium INTERFACE)
target_include_directories(osmium SYSTEM INTERFACE include)
target_compile_features(osmium INTERFACE cxx_std_17)
