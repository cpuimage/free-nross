cmake_minimum_required(VERSION 3.9)
project(denoise)

set(CMAKE_CXX_STANDARD 11)
add_executable(denoise src/main.c src/noise_remover.c)