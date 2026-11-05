cmake_minimum_required(VERSION 3.20)

project(AtmPlusSe LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(AtmPlusSe 
    main/main.cpp

    modules/access_control/atm_state.cpp
    modules/access_control/atm_controller.cpp
)

target_include_directories(AtmPlusSe PRIVATE include)

if (MSVC)
    add_compile_options(/Wall /utf-8)
else()
    add_compile_options(-Wall -Wextra -Wshadow -Wnon-virtual-dtor -pedantic)
endif()