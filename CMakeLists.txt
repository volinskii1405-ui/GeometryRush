cmake_minimum_required(VERSION 3.16)
project(GeometryRush VERSION 1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(GEOMETRY_RUSH_BUILD_TOOLS "Build the level_check tool (headless level solver)" OFF)

# ---------------------------------------------------------------- raylib
include(FetchContent)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES    OFF CACHE BOOL "" FORCE)
set(CUSTOMIZE_BUILD OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG        5.5
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(raylib)
# Предупреждения внутри сторонних исходников raylib (например, jar_mod.h) нам не нужны.
if(MSVC)
    target_compile_options(raylib PRIVATE /w)
else()
    target_compile_options(raylib PRIVATE -w)
endif()
# Заголовки raylib (в том числе raymath.h) подключаем как системные — без их предупреждений.
set_target_properties(raylib PROPERTIES
    INTERFACE_SYSTEM_INCLUDE_DIRECTORIES $<TARGET_PROPERTY:raylib,INTERFACE_INCLUDE_DIRECTORIES>)

# ---------------------------------------------------------------- игра
set(GAME_CORE_SOURCES
    src/Level.cpp
    src/Collision.cpp
    src/Player.cpp
)

add_executable(GeometryRush
    src/main.cpp
    src/Game.cpp
    src/IconRenderer.cpp
    src/Menu.cpp
    src/UI.cpp
    src/Settings.cpp
    src/Editor.cpp
    ${GAME_CORE_SOURCES}
)
target_include_directories(GeometryRush PRIVATE src)
target_link_libraries(GeometryRush PRIVATE raylib)

if(MSVC)
    target_compile_options(GeometryRush PRIVATE /W4 /utf-8)
    # без консольного окна в Release
    set_target_properties(GeometryRush PROPERTIES
        LINK_FLAGS_RELEASE "/SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup")
else()
    target_compile_options(GeometryRush PRIVATE -Wall -Wextra)
endif()

if(APPLE)
    target_link_libraries(GeometryRush PRIVATE "-framework IOKit" "-framework Cocoa" "-framework OpenGL")
endif()

# Копируем уровни рядом с исполняемым файлом.
add_custom_command(TARGET GeometryRush POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/levels
            $<TARGET_FILE_DIR:GeometryRush>/levels
    COMMENT "Copying levels")

# ---------------------------------------------------------------- авиасимулятор
add_executable(FlightSim
    flightsim/main.cpp
    flightsim/Aircraft.cpp
    flightsim/Terrain.cpp
    flightsim/Warnings.cpp
    flightsim/Autopilot.cpp
    flightsim/AircraftType.cpp
    flightsim/World.cpp
    flightsim/Scenery.cpp
    flightsim/Effects.cpp
    flightsim/Missions.cpp
    flightsim/Menu.cpp
    flightsim/Config.cpp
    flightsim/UiText.cpp
    flightsim/Audio.cpp
    flightsim/Scene.cpp
    flightsim/Hud.cpp
)
target_include_directories(FlightSim PRIVATE flightsim)
target_link_libraries(FlightSim PRIVATE raylib)
if(MSVC)
    target_compile_options(FlightSim PRIVATE /W4 /utf-8)
    set_target_properties(FlightSim PROPERTIES
        LINK_FLAGS_RELEASE "/SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup")
else()
    target_compile_options(FlightSim PRIVATE -Wall -Wextra)
endif()
if(APPLE)
    target_link_libraries(FlightSim PRIVATE "-framework IOKit" "-framework Cocoa" "-framework OpenGL")
endif()

# Записанные голосовые сигналы кладём рядом с симулятором.
add_custom_command(TARGET FlightSim POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/sounds
            $<TARGET_FILE_DIR:FlightSim>/sounds
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/fonts
            $<TARGET_FILE_DIR:FlightSim>/fonts
    COMMENT "Copying sounds and fonts")

# ---------------------------------------------------------------- инструменты
if(GEOMETRY_RUSH_BUILD_TOOLS)
    add_executable(level_check tools/level_check.cpp ${GAME_CORE_SOURCES})
    target_include_directories(level_check PRIVATE src)
    target_link_libraries(level_check PRIVATE raylib)
endif()
