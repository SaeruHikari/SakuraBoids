cmake_minimum_required(VERSION 3.16.0)

project(SakuraBoid VERSION 0.1.0)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS YES)

INCLUDE(cmake/Network.cmake)
INCLUDE(cmake/Module.cmake)
INCLUDE(cmake/Initialize.cmake)

# 作为子目标InSource Build的Case, 引擎会作为一个子目标挂载在项目下
#add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../SakuraEngineV3 ${CMAKE_CURRENT_SOURCE_DIR}/../SakuraEngineV3/Debug-build)
# NO
set(SakuraEngine_DIR ${CMAKE_MODULE_PATH} ${CMAKE_CURRENT_SOURCE_DIR}/../SakuraEngine/SakuraEngine/cmake)
find_package(SakuraEngine REQUIRED 0.0.2)

file(GLOB_RECURSE 
    MODULE_SRC_T
    Source/*.cc
    Source/*.cpp
    Source/*.c
    Source/*.cxx
    Source/*.h
    Source/*.hpp
    Source/*.hxx
    Source/*.inl

    Source/*.natvis
)

add_executable(Boids ${MODULE_SRC_T})

target_link_libraries(Boids SakuraEngine::Runtime::ECS)

target_compile_definitions(Boids
    PUBLIC 
    ECSAPI=${API_IMPORT_DEF}
    ECSHIDDEN=${API_HIDDEN_DEF}
)
