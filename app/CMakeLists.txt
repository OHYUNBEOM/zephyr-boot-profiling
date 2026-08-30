cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED)
project(boot_profiling)

target_sources(app PRIVATE src/main.c)
