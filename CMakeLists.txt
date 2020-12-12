cmake_minimum_required(VERSION 3.16.0)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS YES)

project(SakuraBoid VERSION 0.1.0)

# 作为子目标InSource Build的Case, 引擎会作为一个子目标挂载在项目下
#add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../SakuraEngineV3 ${CMAKE_CURRENT_SOURCE_DIR}/../SakuraEngineV3/Debug-build)
# NO
set(SakuraEngine_DIR ${CMAKE_MODULE_PATH} ${CMAKE_CURRENT_SOURCE_DIR}/../SakuraEngine/SakuraEngine/cmake)
find_package(SakuraEngine REQUIRED 0.0.2)

add_executable(Boids Source/Boids.cpp)

target_link_libraries(Boids SakuraEngine::Runtime::ECS)

