cmake_minimum_required(VERSION 4.1)
project(craftinginterpreters_compiler C)

set(CMAKE_C_STANDARD 11)

add_executable(craftinginterpreters_compiler
        main.c
        chunk.h
        chunk.c
        memory.h
        memory.c)
