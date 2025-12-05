cmake_minimum_required(VERSION 3.10)
project(VideoTranscode)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Include FFmpeg 头文件 + 你自己的头文件
include_directories(${PROJECT_SOURCE_DIR}/include)

# 链接 FFmpeg 动态库的位置
link_directories(${PROJECT_SOURCE_DIR}/lib)

# 源文件
set(SRC_FILES
    src/testday5.cpp
    src/demuxer.cpp
    src/videodecoder.cpp
    src/audiodecoder.cpp
    src/videofilter.cpp
    src/audiofilter.cpp
    src/videoencoder.cpp
    src/audioencoder.cpp
)

# 可执行文件
add_executable(transcode ${SRC_FILES})

# 链接 FFmpeg 动态库（你只有 libffmpeg-za.so）
target_link_libraries(transcode
    ffmpeg-za
    pthread
)

# 调试信息
set_target_properties(transcode PROPERTIES
    COMPILE_FLAGS "-Wall -g"
)
