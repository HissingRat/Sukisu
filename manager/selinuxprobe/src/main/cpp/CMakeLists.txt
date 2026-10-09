cmake_minimum_required(VERSION 3.18.1)
project(selinuxprobe C)
add_library(selinuxprobe SHARED bridge.c)
target_compile_options(selinuxprobe PRIVATE -Wall -Wextra -Werror)
