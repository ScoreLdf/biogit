cmake_minimum_required(VERSION 3.16)
project(biogit2)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

find_package(ZLIB REQUIRED)
find_package(Boost REQUIRED COMPONENTS system filesystem)
find_package(Protobuf REQUIRED CONFIG)
find_package(absl REQUIRED CONFIG)
find_package(jsoncpp REQUIRED)


add_executable(biogit2
        main.cpp
        src/sha1.cpp
        src/repository.cpp
        src/utils.cpp
        include/utils.h
        src/object.cpp
        include/object.h
        src/index.cpp
        include/index.h
        src/IoServicePool.cpp
        include/IoServicePool.h
        include/Singleton.h
        include/protocol.h
        src/msg_node.cpp
        include/msg_node.h
        src/csession.cpp
        include/csession.h
        src/LogicSystem.cpp
        include/LogicSystem.h
        src/RemoteClient.cpp
        include/RemoteClient.h
        src/UserManager.cpp
        include/UserManager.h
        src/AsyncLogger.cpp
        include/AsyncLogger.h
)

target_include_directories(biogit2 PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${ZLIB_INCLUDE_DIRS}
        ${Boost_INCLUDE_DIRS}
        ${Protobuf_INCLUDE_DIRS}
)


target_link_libraries(biogit2 PRIVATE
        z
        Boost::system          # Boost 导入目标
        Boost::filesystem      # Boost 导入目标
        protobuf::libprotobuf  # Protobuf 导入目标

        absl::log
        absl::raw_logging_internal # absl::log 的常见依赖
        absl::synchronization      # Protobuf 经常需要
        JsonCpp::JsonCpp
)