add_library(
  networking
  STATIC
  connection.cpp
  dataio_raw.cpp
  packets.cpp
)

target_include_directories(networking PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(networking PUBLIC Qt6::Core)
target_link_libraries(networking PUBLIC common)
if (NOT EMSCRIPTEN)
  target_link_libraries(networking PUBLIC ZLIB::ZLIB)
endif()
