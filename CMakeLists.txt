cmake_minimum_required(VERSION 3.16)
project(scrcpy_cam CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# --- Dependencies ----------------------------------------------------------
# Windows: third_party/ffmpeg and third_party/sdl3, fetched by
#          tools/fetch_deps.ps1
# Linux:   distribution packages (libavcodec-dev libswscale-dev libavutil-dev
#          libsdl3-dev)
find_path(FFMPEG_INCLUDE_DIR libavcodec/avcodec.h
  PATHS ${CMAKE_CURRENT_SOURCE_DIR}/third_party/ffmpeg/include
        ${FFMPEG_ROOT}/include /usr/include /usr/include/x86_64-linux-gnu)
find_library(AVCODEC_LIBRARY avcodec
  PATHS ${CMAKE_CURRENT_SOURCE_DIR}/third_party/ffmpeg/lib ${FFMPEG_ROOT}/lib
        /usr/lib /usr/lib/x86_64-linux-gnu)
find_library(SWSCALE_LIBRARY swscale
  PATHS ${CMAKE_CURRENT_SOURCE_DIR}/third_party/ffmpeg/lib ${FFMPEG_ROOT}/lib
        /usr/lib /usr/lib/x86_64-linux-gnu)
find_library(AVUTIL_LIBRARY avutil
  PATHS ${CMAKE_CURRENT_SOURCE_DIR}/third_party/ffmpeg/lib ${FFMPEG_ROOT}/lib
        /usr/lib /usr/lib/x86_64-linux-gnu)

if(NOT FFMPEG_INCLUDE_DIR OR NOT AVCODEC_LIBRARY OR NOT SWSCALE_LIBRARY OR NOT AVUTIL_LIBRARY)
  message(FATAL_ERROR
    "FFmpeg not found.\n"
    "  Windows: powershell -ExecutionPolicy Bypass -File tools/fetch_deps.ps1\n"
    "  Linux:   sudo apt install libavcodec-dev libswscale-dev libavutil-dev\n"
    "  or pass -DFFMPEG_ROOT=<dir>")
endif()

find_path(SDL3_INCLUDE_DIR SDL3/SDL.h
  PATHS ${CMAKE_CURRENT_SOURCE_DIR}/third_party/sdl3/include
        ${SDL3_ROOT}/include /usr/include)
find_library(SDL3_LIBRARY NAMES SDL3 SDL3-static
  PATHS ${CMAKE_CURRENT_SOURCE_DIR}/third_party/sdl3/lib ${SDL3_ROOT}/lib
        /usr/lib /usr/lib/x86_64-linux-gnu)
if(NOT SDL3_INCLUDE_DIR OR NOT SDL3_LIBRARY)
  message(FATAL_ERROR
    "SDL3 not found.\n"
    "  Windows: powershell -ExecutionPolicy Bypass -File tools/fetch_deps.ps1\n"
    "  Linux:   sudo apt install libsdl3-dev\n"
    "  or pass -DSDL3_ROOT=<dir>")
endif()

# --- Dear ImGui (vendored source) ------------------------------------------
set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui)
add_library(imgui STATIC
  ${IMGUI_DIR}/imgui.cpp
  ${IMGUI_DIR}/imgui_draw.cpp
  ${IMGUI_DIR}/imgui_tables.cpp
  ${IMGUI_DIR}/imgui_widgets.cpp
  ${IMGUI_DIR}/imgui_demo.cpp
  ${IMGUI_DIR}/backends/imgui_impl_sdl3.cpp
  ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp)
target_include_directories(imgui PUBLIC ${IMGUI_DIR} ${IMGUI_DIR}/backends ${SDL3_INCLUDE_DIR})
target_link_libraries(imgui PUBLIC ${SDL3_LIBRARY})

# --- scrcpy server ----------------------------------------------------------
# Our modified build, carrying the manual camera parameter messages. Pinned in
# the repository so the client always ships a matching server.
set(DEFAULT_SERVER_APK "${CMAKE_CURRENT_SOURCE_DIR}/vendor/scrcpy-server")

# --- Client -----------------------------------------------------------------
add_executable(scrcpy-cam
  src/main.cpp
  src/gui.cpp
  src/session.cpp
  src/cameras.cpp
  src/net.cpp
  src/adb.cpp
  src/video.cpp)

target_include_directories(scrcpy-cam PRIVATE src ${FFMPEG_INCLUDE_DIR} ${SDL3_INCLUDE_DIR})
target_compile_definitions(scrcpy-cam PRIVATE
  DEFAULT_SERVER_APK="${DEFAULT_SERVER_APK}")
target_link_libraries(scrcpy-cam PRIVATE
  imgui ${AVCODEC_LIBRARY} ${SWSCALE_LIBRARY} ${AVUTIL_LIBRARY} ${SDL3_LIBRARY})

# Virtual camera output: the e2eSoft SDK on Windows, v4l2loopback on Linux.
if(WIN32)
  target_sources(scrcpy-cam PRIVATE src/vcam.cpp)
  target_compile_definitions(scrcpy-cam PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
  target_link_libraries(scrcpy-cam PRIVATE ole32 oleaut32 ws2_32 opengl32)
else()
  find_package(OpenGL REQUIRED)
  find_package(Threads REQUIRED)
  target_sources(scrcpy-cam PRIVATE src/v4l2_sink.cpp)
  target_link_libraries(scrcpy-cam PRIVATE ${CMAKE_DL_LIBS} OpenGL::GL Threads::Threads)
endif()

# --- Copy dependencies next to the executable ------------------------------
# FFmpeg ships its DLLs in bin/ and SDL3 keeps them next to the import library.
set(_dll_dirs
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/ffmpeg/bin
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/ffmpeg/lib
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/sdl3/lib
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/sdl3/bin)
foreach(dll avcodec avutil swscale swresample avformat SDL3 zlib libx264)
  foreach(dir ${_dll_dirs})
    file(GLOB _found "${dir}/${dll}-*.dll" "${dir}/${dll}.dll")
    if(_found)
      add_custom_command(TARGET scrcpy-cam POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${_found} $<TARGET_FILE_DIR:scrcpy-cam>)
    endif()
  endforeach()
endforeach()

add_custom_target(vendor_server
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          ${DEFAULT_SERVER_APK} $<TARGET_FILE_DIR:scrcpy-cam>
  COMMENT "Copying vendor/scrcpy-server")