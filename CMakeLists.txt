cmake_minimum_required(VERSION 3.16)
project(adaptive_image_streaming_day1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Prefer MSYS2 UCRT64 OpenCV if the user did not pass -DOpenCV_DIR
if(NOT OpenCV_DIR AND EXISTS "C:/msys64/ucrt64/lib/cmake/opencv5")
    set(OpenCV_DIR "C:/msys64/ucrt64/lib/cmake/opencv5")
endif()

find_package(OpenCV REQUIRED)

add_library(dip STATIC
    dip/image_processing.cpp
)
target_include_directories(dip PUBLIC
    ${CMAKE_SOURCE_DIR}/dip
    ${OpenCV_INCLUDE_DIRS}
)
target_link_libraries(dip PUBLIC ${OpenCV_LIBS})

add_library(net STATIC
    network/tcp_socket.cpp
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

# Tiny helper used only to create a sample image for localhost testing.
add_executable(make_sample
    tools/make_sample.cpp
)
target_link_libraries(make_sample PRIVATE ${OpenCV_LIBS})

message(STATUS "OpenCV version: ${OpenCV_VERSION}")
message(STATUS "OpenCV include: ${OpenCV_INCLUDE_DIRS}")
