cmake_minimum_required(VERSION 3.10)
project(SimpleChat)

set(CMAKE_CXX_STANDARD 11)

find_package(FastDDS REQUIRED)

# --- 关键修改：匹配你 tree 里的文件 ---
# Chat.hpp 是头文件，不需要加入 source list
# 加入 ChatPubSubTypes.cxx 和 ChatTypeObjectSupport.cxx
set(GENERATED_SOURCES 
    ChatPubSubTypes.cxx 
    ChatTypeObjectSupport.cxx
)

add_executable(ChatPublisher ChatPublisher.cpp ${GENERATED_SOURCES})
target_link_libraries(ChatPublisher fastdds)

add_executable(ChatSubscriber ChatSubscriber.cpp ${GENERATED_SOURCES})
target_link_libraries(ChatSubscriber fastdds)

