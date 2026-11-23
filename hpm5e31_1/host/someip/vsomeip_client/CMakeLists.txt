cmake_minimum_required(VERSION 3.13)
project(someip_vsomeip_client CXX)

# 上位机：vsomeip 3.7.6 客户端（板端 app_someip 的最小实现 vs. 工业级中间件对照）
#
# 依赖（Ubuntu）：
#   sudo apt install -y g++ cmake ninja-build libboost-system-dev libboost-thread-dev
#   libboost-filesystem-dev libboost-log-dev
#   vsomeip 源码构建后 cmake --install build --prefix <PREFIX>
#
# 构建：
#   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
#         -DCMAKE_PREFIX_PATH=$HOME/tmp/tp-someip/vsomeip-install
#   cmake --build build -j

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)
find_package(vsomeip3 REQUIRED)

add_executable(someip_client src/main.cpp)
# libvsomeip3-sd / libvsomeip3-cfg 由 libvsomeip3 在运行时 dlopen，不需要显式链接
# （显式 -l 反而会因为只拿到 .so 绝对路径、缺少 -L 而报 cannot find -lvsomeip3-sd）
target_link_libraries(someip_client PRIVATE vsomeip3 Threads::Threads)
target_compile_options(someip_client PRIVATE -O2 -Wall)

install(TARGETS someip_client RUNTIME DESTINATION bin)
