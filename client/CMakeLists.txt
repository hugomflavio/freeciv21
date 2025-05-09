set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)

add_compile_definitions(NOMINMAX)

# Generate files
tolua_generate(
  INPUT luascript/tolua_client.pkg
  HEADER tolua_client_gen.h
  SOURCE tolua_client_gen.cpp
  PACKAGE_NAME client
)
add_compile_definitions(LUA_LIB)

# Generate the packet handling code
add_custom_command(
  OUTPUT
    ${CMAKE_CURRENT_BINARY_DIR}/packhand_gen.h
    ${CMAKE_CURRENT_BINARY_DIR}/packhand_gen.cpp
  COMMAND
    ${Python3_EXECUTABLE}
    ${CMAKE_SOURCE_DIR}/common/generate_packets.py
    ${CMAKE_SOURCE_DIR}/common/networking/packets.def
    --mode client
    ${CMAKE_CURRENT_BINARY_DIR}/packhand_gen.h
    ${CMAKE_CURRENT_BINARY_DIR}/packhand_gen.cpp
  VERBATIM
  DEPENDS
    ${CMAKE_SOURCE_DIR}/common/generate_packets.py
    ${CMAKE_SOURCE_DIR}/common/networking/packets.def
)

add_executable(freeciv21-client ${GUI_TYPE})
target_sources(
  freeciv21-client
  PRIVATE
  attribute.cpp
  canvas.cpp
  chatline.cpp
  chatline_common.cpp
  citybar.cpp
  citydlg.cpp
  citydlg_common.cpp
  client_main.cpp
  climap.cpp
  climisc.cpp
  clinet.cpp
  colors_common.cpp
  connectdlg.cpp
  connectdlg_common.cpp
  control.cpp
  dialogs.cpp
  diplodlg.cpp
  editor.cpp
  endgamereport.cpp
  fc_client.cpp
  fonts.cpp
  global_worklist.cpp
  goto.cpp
  gotodlg.cpp
  governor.cpp
  gui_main.cpp
  helpdlg.cpp
  hudwidget.cpp
  icons.cpp
  luaconsole.cpp
  luaconsole_common.cpp
  luascript/api_client_base.cpp
  luascript/script_client.cpp
  mapctrl.cpp
  mapctrl_common.cpp
  map_updates_handler.cpp
  menu.cpp
  messageoptions.cpp
  messagewin.cpp
  messagewin_common.cpp
  minimap.cpp
  minimap_panel.cpp
  music.cpp
  options.cpp
  optiondlg.cpp
  overview_common.cpp
  packhand.cpp
  page_game.cpp
  page_load.cpp
  page_main.cpp
  page_network.cpp
  page_pregame.cpp
  page_scenario.cpp
  pregameoptions.cpp
  ratesdlg.cpp
  renderer.cpp
  repodlgs_common.cpp
  shortcuts.cpp
  spaceshipdlg.cpp
  text.cpp
  themes.cpp
  themes_common.cpp
  tileset_debugger.cpp
  tileset_options.cpp
  tooltips.cpp
  top_bar.cpp
  tradecalculation.cpp
  turn_done_button.cpp
  unitselect.cpp
  update_queue.cpp
  voteinfo.cpp
  voteinfo_bar.cpp
  unithudselector.cpp

  audio/audio.cpp
  audio/audio_none.cpp
  audio/audio_qt.cpp

  editor/map_editor.cpp
  editor/tool_tile.cpp

  tileset/drawn_sprite.cpp
  tileset/layer_abstract_activities.cpp
  tileset/layer_background.cpp
  tileset/layer_base_flags.cpp
  tileset/layer_city.cpp
  tileset/layer_city_size.cpp
  tileset/layer_darkness.cpp
  tileset/layer_infrawork.cpp
  tileset/layer_editor.cpp
  tileset/layer_fog.cpp
  tileset/layer_goto.cpp
  tileset/layer_grid.cpp
  tileset/layer_overlays.cpp
  tileset/layer_roads.cpp
  tileset/layer_special.cpp
  tileset/layer_terrain.cpp
  tileset/layer_units.cpp
  tileset/layer_water.cpp
  tileset/layer_workertask.cpp
  tileset/layer.cpp
  tileset/sprite.cpp
  tileset/tilespec.cpp

  utils/collated_sort_proxy.cpp
  utils/colorizer.cpp
  utils/improvement_seller.cpp
  utils/unit_quick_menu.cpp
  utils/unit_utils.cpp

  views/view_cities.cpp
  views/view_cities_data.cpp
  views/view_economics.cpp
  views/view_map.cpp
  views/view_map_common.cpp
  views/view_map_geometry.cpp
  views/view_nations.cpp
  views/view_nations_data.cpp
  views/view_research.cpp
  views/view_research_reqtree.cpp
  views/view_units.cpp

  widgets/decorations.cpp
  widgets/city/city_icon_widget.cpp
  widgets/city/governor_widget.cpp
  widgets/city/upkeep_widget.cpp
  widgets/multi_slider.cpp
  widgets/report_widget.cpp

  # Generated
  ${CMAKE_CURRENT_BINARY_DIR}/tolua_client_gen.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/tolua_client_gen.h
  ${CMAKE_CURRENT_BINARY_DIR}/packhand_gen.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/packhand_gen.h
)

target_include_directories(freeciv21-client PRIVATE include)
target_include_directories(freeciv21-client PRIVATE luascript)
target_include_directories(freeciv21-client PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_include_directories(freeciv21-client PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(freeciv21-client PRIVATE common)
target_link_libraries(freeciv21-client PRIVATE cvercmp)
target_link_libraries(freeciv21-client PRIVATE tolua)

target_link_libraries(freeciv21-client PRIVATE Qt6::Multimedia)
target_link_libraries(freeciv21-client PRIVATE Qt6::Widgets)
target_link_libraries(freeciv21-client PRIVATE Qt6::Svg)

# Workaround for QTBUG-93413
set_source_files_properties(
  widgets/city/governor_widget.ui
  PROPERTIES AUTOUIC_OPTIONS "--connections;string")

if(NOT EMSCRIPTEN)
  target_sources(freeciv21-client PRIVATE servers.cpp)
endif()

if (AUDIO_SDL)
  target_sources(freeciv21-client PRIVATE audio/audio_sdl.cpp)
  # SDL comes with the -s option on emscripten
  if (NOT EMSCRIPTEN)
    # See https://github.com/microsoft/vcpkg/blob/2023.12.12/ports/sdl2/usage
    target_link_libraries(freeciv21-client
      PRIVATE $<IF:$<TARGET_EXISTS:SDL2::SDL2>,SDL2::SDL2,SDL2::SDL2-static>)
    # Keep the PRIVATE directive on one long line, error w/ crlf or whitespace
    target_link_libraries(freeciv21-client
      PRIVATE $<IF:$<TARGET_EXISTS:SDL2_mixer::SDL2_mixer>,SDL2_mixer::SDL2_mixer,SDL2_mixer::SDL2_mixer-static>)
  endif()
endif()

if (APPLE)
  qt6_import_plugins(freeciv21-client INCLUDE Qt6::QCocoaIntegrationPlugin Qt6::QSvgPlugin)
endif()

if(EMSCRIPTEN)
  qt6_import_plugins(freeciv21-client INCLUDE Qt6::QWasmIntegrationPlugin)
  target_link_options(freeciv21-client PRIVATE
                      -sASSERTIONS=1
                      -sWEBSOCKET_URL=wss://
                      -sTOTAL_MEMORY=52428800
                      -sALLOW_MEMORY_GROWTH=1
                      "-sEXPORTED_RUNTIME_METHODS=['UTF16ToString','stringToUTF16','callMain','printErr']"
                      --bind
                      -sUSE_SDL_MIXER=2
                      "--preload-file=${CMAKE_SOURCE_DIR}/data/tilesets/flags@data/tilesets/flags"
                      "--preload-file=${CMAKE_SOURCE_DIR}/data/helpdata.txt@data/helpdata.txt"
                      "--preload-file=${CMAKE_SOURCE_DIR}/data/misc@data/misc"
                      "--preload-file=${CMAKE_SOURCE_DIR}/data/tilesets/buildings@data/tilesets/buildings"
                      "--preload-file=${CMAKE_SOURCE_DIR}/data/tilesets/wonders@data/tilesets/wonders"
                      "--preload-file=${CMAKE_SOURCE_DIR}/data/hexemplio.tilespec@data/hexemplio.tilespec"
                      "--preload-file=${CMAKE_SOURCE_DIR}/data/hexemplio@data/hexemplio"
                      "--preload-file=${CMAKE_SOURCE_DIR}/data/amplio2.tilespec@data/amplio2.tilespec"
                      "--preload-file=${CMAKE_SOURCE_DIR}/data/amplio2@data/amplio2"
                      "--preload-file=${CMAKE_SOURCE_DIR}/data/themes/Classic@data/themes/Classic"
                      "--preload-file=${CMAKE_SOURCE_DIR}/data/themes/icons@data/themes/icons"
                      "--preload-file=${CMAKE_SOURCE_DIR}/data/stdmusic.musicspec@data/stdmusic.musicspec"
                      "--preload-file=${CMAKE_SOURCE_DIR}/data/stdmusic@data/stdmusic"
                      "--preload-file=${CMAKE_SOURCE_DIR}/data/stdsounds@data/stdsounds"
                      "--preload-file=${CMAKE_SOURCE_DIR}/data/stdsounds.soundspec@data/stdsounds.soundspec"
                      "--exclude-file=*CMakeLists*"
                      "--exclude-file=*.icns"
                      "--exclude-file=*.serv"
                      "--exclude-file=*.svg"
  )
endif()

add_dependencies(freeciv21-client freeciv_translations)
install(TARGETS freeciv21-client
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        COMPONENT freeciv21)
