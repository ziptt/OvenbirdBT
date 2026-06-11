cmake_minimum_required(VERSION 3.24)

project(ovenbirdbt VERSION 1.0 LANGUAGES CXX)

option(OVENBIRDBT_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(ftxui CONFIG QUIET)
if(NOT ftxui_FOUND)
  find_package(FTXUI CONFIG QUIET)
endif()

if(NOT TARGET ftxui::component OR NOT TARGET ftxui::dom OR NOT TARGET ftxui::screen)
  message(FATAL_ERROR "Could not find FTXUI CMake targets. Install FTXUI development files or use vcpkg.")
endif()

find_package(LibtorrentRasterbar CONFIG QUIET)
if(NOT LibtorrentRasterbar_FOUND)
  find_package(libtorrent-rasterbar CONFIG QUIET)
endif()

if(TARGET LibtorrentRasterbar::torrent-rasterbar)
  set(OVENBIRDBT_LIBTORRENT_TARGET LibtorrentRasterbar::torrent-rasterbar)
elseif(TARGET libtorrent-rasterbar::torrent-rasterbar)
  set(OVENBIRDBT_LIBTORRENT_TARGET libtorrent-rasterbar::torrent-rasterbar)
elseif(TARGET torrent-rasterbar)
  set(OVENBIRDBT_LIBTORRENT_TARGET torrent-rasterbar)
else()
  message(FATAL_ERROR "Could not find a libtorrent-rasterbar CMake target. Install libtorrent-rasterbar development files or use vcpkg.")
endif()

add_executable(ovenbirdbt
  src/Config.cpp
  src/Format.cpp
  src/PathUtil.cpp
  src/TorrentClient.cpp
  src/TuiApp.cpp
  src/main.cpp
)

target_include_directories(ovenbirdbt PRIVATE src)

target_compile_definitions(ovenbirdbt PRIVATE OVENBIRDBT_VERSION="${PROJECT_VERSION}")

target_link_libraries(ovenbirdbt PRIVATE
  ${OVENBIRDBT_LIBTORRENT_TARGET}
  ftxui::component
  ftxui::dom
  ftxui::screen
)

if(WIN32)
  target_link_libraries(ovenbirdbt PRIVATE user32)
endif()

if(MSVC)
  target_compile_options(ovenbirdbt PRIVATE /W4 /permissive- /Zc:__cplusplus)
  target_compile_definitions(ovenbirdbt PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
  if(OVENBIRDBT_WARNINGS_AS_ERRORS)
    target_compile_options(ovenbirdbt PRIVATE /WX)
  endif()
else()
  target_compile_options(ovenbirdbt PRIVATE -Wall -Wextra -Wpedantic)
  if(OVENBIRDBT_WARNINGS_AS_ERRORS)
    target_compile_options(ovenbirdbt PRIVATE -Werror)
  endif()
endif()

install(TARGETS ovenbirdbt RUNTIME DESTINATION bin)


