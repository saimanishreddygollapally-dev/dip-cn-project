cmake_minimum_required(VERSION 3.16)
project(adaptive_image_streaming LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Prefer MSYS2 UCRT64 OpenCV if the user did not pass -DOpenCV_DIR
if(NOT OpenCV_DIR AND EXISTS "C:/msys64/ucrt64/lib/cmake/opencv5")
    set(OpenCV_DIR "C:/msys64/ucrt64/lib/cmake/opencv5")
endif()

find_package(OpenCV REQUIRED)

add_library(dip STATIC
    dip/image_processing.cpp
    dip/adaptive.cpp
)
target_include_directories(dip PUBLIC
    ${CMAKE_SOURCE_DIR}/dip
    ${OpenCV_INCLUDE_DIRS}
)
target_link_libraries(dip PUBLIC ${OpenCV_LIBS})

add_library(net STATIC
    network/tcp_socket.cpp
    network/udp_socket.cpp
    network/packet.cpp
    network/impair.cpp
    network/reliable_transfer.cpp
)
target_include_directories(net PUBLIC
    ${CMAKE_SOURCE_DIR}/network
)
if(WIN32)
    target_link_libraries(net PUBLIC ws2_32)
endif()

add_executable(client
    client/client.cpp
)
target_include_directories(client PRIVATE
    ${CMAKE_SOURCE_DIR}/dip
    ${CMAKE_SOURCE_DIR}/network
)
target_link_libraries(client PRIVATE dip net ${OpenCV_LIBS})

add_executable(server
    server/server.cpp
)
target_include_directories(server PRIVATE
    ${CMAKE_SOURCE_DIR}/dip
    ${CMAKE_SOURCE_DIR}/network
)
target_link_libraries(server PRIVATE dip net ${OpenCV_LIBS})

add_executable(client_udp
    client/client_udp.cpp
)
target_include_directories(client_udp PRIVATE
    ${CMAKE_SOURCE_DIR}/dip
    ${CMAKE_SOURCE_DIR}/network
)
target_link_libraries(client_udp PRIVATE dip net ${OpenCV_LIBS})

add_executable(server_udp
    server/server_udp.cpp
)
target_include_directories(server_udp PRIVATE
    ${CMAKE_SOURCE_DIR}/dip
    ${CMAKE_SOURCE_DIR}/network
)
target_link_libraries(server_udp PRIVATE dip net ${OpenCV_LIBS})

# Tiny helper used only to create a sample image for localhost testing.
add_executable(make_sample
    tools/make_sample.cpp
)
target_link_libraries(make_sample PRIVATE ${OpenCV_LIBS})

add_executable(client_reliable
    client/client_reliable.cpp
    network/stream_session.cpp
)
target_include_directories(client_reliable PRIVATE
    ${CMAKE_SOURCE_DIR}/dip
    ${CMAKE_SOURCE_DIR}/network
)
target_link_libraries(client_reliable PRIVATE dip net ${OpenCV_LIBS})

add_executable(server_reliable
    server/server_reliable.cpp
)
target_include_directories(server_reliable PRIVATE
    ${CMAKE_SOURCE_DIR}/dip
    ${CMAKE_SOURCE_DIR}/network
)
target_link_libraries(server_reliable PRIVATE dip net ${OpenCV_LIBS})

add_executable(benchmark
    tools/benchmark.cpp
    network/stream_session.cpp
)
target_include_directories(benchmark PRIVATE
    ${CMAKE_SOURCE_DIR}/dip
    ${CMAKE_SOURCE_DIR}/network
)
target_link_libraries(benchmark PRIVATE dip net ${OpenCV_LIBS})

# Sends malformed UDP packets for checksum/header rejection tests.
add_executable(send_bad_udp
    tools/send_bad_udp.cpp
)
target_include_directories(send_bad_udp PRIVATE
    ${CMAKE_SOURCE_DIR}/network
)
target_link_libraries(send_bad_udp PRIVATE net)

add_executable(ssim_check
    tools/ssim_check.cpp
)
target_include_directories(ssim_check PRIVATE
    ${CMAKE_SOURCE_DIR}/dip
)
target_link_libraries(ssim_check PRIVATE dip ${OpenCV_LIBS})

message(STATUS "OpenCV version: ${OpenCV_VERSION}")
message(STATUS "OpenCV include: ${OpenCV_INCLUDE_DIRS}")
