set(CMAKE_AUTOMOC true)

# Generate the packet handling code
add_custom_command(
  OUTPUT
    ${CMAKE_CURRENT_BINARY_DIR}/hand_gen.h
    ${CMAKE_CURRENT_BINARY_DIR}/hand_gen.cpp
  COMMAND
    ${Python3_EXECUTABLE}
    ${CMAKE_SOURCE_DIR}/common/generate_packets.py
    ${CMAKE_SOURCE_DIR}/common/networking/packets.def
    --mode server
    ${CMAKE_CURRENT_BINARY_DIR}/hand_gen.h
    ${CMAKE_CURRENT_BINARY_DIR}/hand_gen.cpp
  VERBATIM
  DEPENDS
    ${CMAKE_SOURCE_DIR}/common/generate_packets.py
    ${CMAKE_SOURCE_DIR}/common/networking/packets.def
)

add_library(
  server_gen
  STATIC
  # The generated code calls these directly
  cityhand.cpp
  handchat.cpp
  # Generated
  ${CMAKE_CURRENT_BINARY_DIR}/hand_gen.cpp
)

target_include_directories(server_gen PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(server_gen PUBLIC ${CMAKE_CURRENT_BINARY_DIR})

target_link_libraries(server_gen PUBLIC common)

add_library(
  server
  STATIC
  actiontools.cpp
  aiiface.cpp
  animals.cpp
  auth.cpp
  barbarian.cpp
  citizenshand.cpp
  citytools.cpp
  cityturn.cpp
  civserver.cpp
  commands.cpp
  connecthand.cpp
  console.cpp
  demographic.cpp
  diplhand.cpp
  diplomats.cpp
  edithand.cpp
  fcdb.cpp
  gamehand.cpp
  maphand.cpp
  meta.cpp
  mood.cpp
  notify.cpp
  plrhand.cpp
  report.cpp
  rscompat.cpp
  rssanity.cpp
  ruleset.cpp
  sanitycheck.cpp
  score.cpp
  sernet.cpp
  server.cpp
  settings.cpp
  spacerace.cpp
  srv_log.cpp
  srv_main.cpp
  stdinhand.cpp
  techtools.cpp
  unithand.cpp
  unittools.cpp
  voting.cpp
)

target_include_directories(server PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# Required for interface (the corresponding libraries are PRIVATE dependencies)
target_include_directories(server PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/advisors)
target_include_directories(server PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/generator)
target_include_directories(server PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/savegame)
target_include_directories(server PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/scripting)
target_include_directories(server PUBLIC ${CMAKE_SOURCE_DIR}/ai)
target_include_directories(server PUBLIC ${CMAKE_SOURCE_DIR}/ai/default)
target_include_directories(server PUBLIC ${CMAKE_SOURCE_DIR}/ai/classic)

target_link_libraries(server PUBLIC Qt6::Core Qt6::Network server_gen)
target_link_libraries(server PRIVATE Readline::readline)

add_subdirectory(advisors)
add_subdirectory(generator)
add_subdirectory(savegame)
add_subdirectory(scripting)
add_subdirectory(${CMAKE_SOURCE_DIR}/ai ${CMAKE_CURRENT_BINARY_DIR}/ai)

# Keep them private because they use INTERFACE_SOURCES
target_link_libraries(server PRIVATE advisors)
target_link_libraries(server PRIVATE generator)
target_link_libraries(server PRIVATE savegame)
target_link_libraries(server PRIVATE scripting)
target_link_libraries(server PRIVATE ai)
target_link_libraries(server PRIVATE ai_classic)

# Create an empty file to build the server from. srv_main.cpp is needed by ruledit
# so it has to be in the freeciv_server library.
if(NOT EXISTS ${CMAKE_CURRENT_BINARY_DIR}/null.cpp)
  file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/null.cpp "")
endif()
add_executable(freeciv21-server null.cpp)
target_link_libraries(freeciv21-server server)
add_dependencies(freeciv21-server freeciv_translations)
install(TARGETS freeciv21-server
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        COMPONENT freeciv21)

# Tests
if (BUILD_TESTING AND FREECIV_ENABLE_SERVER)
  add_subdirectory(tests)
endif()
