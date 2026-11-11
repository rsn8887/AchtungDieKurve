cmake_minimum_required(VERSION 3.13)
project(AchtungDieKurve VERSION 1.3.0 LANGUAGES CXX)
add_compile_definitions(GAME_VERSION="${PROJECT_VERSION}")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(PLATFORM "DESKTOP" CACHE STRING "DESKTOP (macOS, Linux, Windows) or SWITCH (Android is detected from the NDK toolchain)")
# Optional, DESKTOP only: SDL2 and SDL2_ttf source trees. When both are set, they are compiled from source and linked
# statically, so the executable needs no SDL libraries on the target machine (used by linux/build.sh).
set(SDL2_SOURCE_DIR "" CACHE PATH "SDL2 source tree (Android: required; desktop: optional, links SDL statically)")
set(SDL2_TTF_SOURCE_DIR "" CACHE PATH "SDL2_ttf source tree (Android: required; desktop: optional)")

# the game is a single source file
set(SOURCES achtung.cpp)
# the font: text is drawn with SDL2_ttf from romfs/font.ttf (DejaVu Sans Bold, see romfs/FONT-LICENSE.txt)
set(FONT_FILE ${CMAKE_SOURCE_DIR}/romfs/font.ttf)
set(FONT_LICENSE ${CMAKE_SOURCE_DIR}/romfs/FONT-LICENSE.txt)

if(PLATFORM STREQUAL "SWITCH")
  # Configure with the devkitPro toolchain, e.g.
  #   cmake -B build-switch -DPLATFORM=SWITCH -DCMAKE_TOOLCHAIN_FILE=$DEVKITPRO/cmake/Switch.cmake
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET SDL2_ttf sdl2)
  add_executable(achtung ${SOURCES})
  target_compile_definitions(achtung PRIVATE __SWITCH__)
  target_compile_options(achtung PRIVATE -Wall -O2 -ffunction-sections)
  target_link_libraries(achtung PRIVATE PkgConfig::SDL2)
  # the font goes into the .nro's romfs (read as romfs:/font.ttf)
  file(REMOVE_RECURSE ${CMAKE_BINARY_DIR}/romfs)
  file(COPY ${FONT_FILE} ${FONT_LICENSE} DESTINATION ${CMAKE_BINARY_DIR}/romfs)
  nx_generate_nacp(achtung.nacp NAME "Achtung, die Kurve!" AUTHOR "rsn8887" VERSION ${PROJECT_VERSION})
  nx_create_nro(achtung NACP achtung.nacp ICON ${CMAKE_SOURCE_DIR}/icon/icon_switch.jpg ROMFS ${CMAKE_BINARY_DIR}/romfs)
elseif(ANDROID)
  # Android: configured by Gradle (see android/), which passes the SDL2 and SDL2_ttf source trees. SDL's Java
  # activity loads libSDL2.so and the game, libmain.so; SDL2_ttf (with its bundled FreeType) is linked in statically.
  # romfs/ is packed into the APK by Gradle as its assets (font.ttf), and read through SDL (SDL_RWFromFile reads APK assets).
  if(NOT EXISTS "${SDL2_SOURCE_DIR}/CMakeLists.txt" OR NOT EXISTS "${SDL2_TTF_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "Android: set SDL2_SOURCE_DIR and SDL2_TTF_SOURCE_DIR to the SDL2 and SDL2_ttf source trees")
  endif()
  set(SDL_SHARED ON CACHE BOOL "" FORCE)
  set(SDL_STATIC OFF CACHE BOOL "" FORCE)
  set(SDL_TEST OFF CACHE BOOL "" FORCE)
  add_subdirectory(${SDL2_SOURCE_DIR} ${CMAKE_BINARY_DIR}/SDL2)
  # SDL2_ttf is built static, so it looks for the static SDL2 target: point it at the shared one
  if(NOT TARGET SDL2::SDL2-static)
    add_library(SDL2::SDL2-static ALIAS SDL2)
  endif()
  set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
  set(SDL2TTF_VENDORED ON CACHE BOOL "" FORCE)
  set(SDL2TTF_SAMPLES OFF CACHE BOOL "" FORCE)
  set(SDL2TTF_INSTALL OFF CACHE BOOL "" FORCE)
  add_subdirectory(${SDL2_TTF_SOURCE_DIR} ${CMAKE_BINARY_DIR}/SDL2_ttf EXCLUDE_FROM_ALL)
  add_library(main SHARED ${SOURCES})
  target_compile_options(main PRIVATE -Wall -Wextra -O2)
  target_link_libraries(main PRIVATE SDL2::SDL2 SDL2_ttf)
  find_library(ANDROID_LOG log)
  if(ANDROID_LOG)
    target_link_libraries(main PRIVATE ${ANDROID_LOG})
  endif()
else()
  find_package(Threads REQUIRED)
  set(GAME_LIBS "")
  if(SDL2_SOURCE_DIR AND SDL2_TTF_SOURCE_DIR)
    # SDL2 and SDL2_ttf (with its bundled FreeType) compiled from source, linked statically
    if(NOT EXISTS "${SDL2_SOURCE_DIR}/CMakeLists.txt" OR NOT EXISTS "${SDL2_TTF_SOURCE_DIR}/CMakeLists.txt")
      message(FATAL_ERROR "SDL2_SOURCE_DIR / SDL2_TTF_SOURCE_DIR do not point at the SDL2 / SDL2_ttf source trees")
    endif()
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST OFF CACHE BOOL "" FORCE)
    add_subdirectory(${SDL2_SOURCE_DIR} ${CMAKE_BINARY_DIR}/SDL2 EXCLUDE_FROM_ALL)
    if(NOT TARGET SDL2::SDL2-static)
      add_library(SDL2::SDL2-static ALIAS SDL2-static)
    endif()
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(SDL2TTF_VENDORED ON CACHE BOOL "" FORCE)
    set(SDL2TTF_SAMPLES OFF CACHE BOOL "" FORCE)
    set(SDL2TTF_INSTALL OFF CACHE BOOL "" FORCE)
    add_subdirectory(${SDL2_TTF_SOURCE_DIR} ${CMAKE_BINARY_DIR}/SDL2_ttf EXCLUDE_FROM_ALL)
    list(APPEND GAME_LIBS $<TARGET_NAME_IF_EXISTS:SDL2::SDL2main> SDL2::SDL2-static SDL2_ttf::SDL2_ttf-static)
    if(NOT TARGET SDL2_ttf::SDL2_ttf-static)
      # older SDL2_ttf: the target is called SDL2_ttf
      set(GAME_LIBS ${GAME_LIBS})
      list(REMOVE_ITEM GAME_LIBS SDL2_ttf::SDL2_ttf-static)
      list(APPEND GAME_LIBS SDL2_ttf)
    endif()
  else()
    find_package(SDL2 REQUIRED)
    if(TARGET SDL2::SDL2)
      list(APPEND GAME_LIBS $<TARGET_NAME_IF_EXISTS:SDL2::SDL2main> SDL2::SDL2)
    else()
      list(APPEND GAME_INCLUDES ${SDL2_INCLUDE_DIRS})
      list(APPEND GAME_LIBS ${SDL2_LIBRARIES})
    endif()
    find_package(SDL2_ttf CONFIG QUIET)
    if(TARGET SDL2_ttf::SDL2_ttf)
      list(APPEND GAME_LIBS SDL2_ttf::SDL2_ttf)
    elseif(TARGET SDL2_ttf::SDL2_ttf-static)
      list(APPEND GAME_LIBS SDL2_ttf::SDL2_ttf-static)
    else()
      find_package(PkgConfig REQUIRED)
      pkg_check_modules(SDL2TTF REQUIRED IMPORTED_TARGET SDL2_ttf)
      list(APPEND GAME_LIBS PkgConfig::SDL2TTF)
    endif()
  endif()

  if(APPLE)
    # macOS: the game is an app bundle (AchtungDieKurve.app) with its icon and the font in Contents/Resources
    set(ICNS ${CMAKE_SOURCE_DIR}/icon/Achtung.icns)
    set_source_files_properties(${ICNS} ${FONT_FILE} ${FONT_LICENSE} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    add_executable(achtung MACOSX_BUNDLE ${SOURCES} ${ICNS} ${FONT_FILE} ${FONT_LICENSE})
    set_target_properties(achtung PROPERTIES
      OUTPUT_NAME AchtungDieKurve
      MACOSX_BUNDLE_INFO_PLIST ${CMAKE_SOURCE_DIR}/cmake/Info.plist.in
      MACOSX_BUNDLE_BUNDLE_NAME "Achtung, die Kurve!"
      MACOSX_BUNDLE_GUI_IDENTIFIER com.rsn8887.achtung
      MACOSX_BUNDLE_ICON_FILE Achtung.icns
      MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
      MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
      MACOSX_BUNDLE_COPYRIGHT "rsn8887")
    # room in the header so make_dmg.sh can point the library references into the app
    target_link_options(achtung PRIVATE -Wl,-headerpad_max_install_names)
  else()
    add_executable(achtung ${SOURCES})
    # the game looks for font.ttf next to its executable: copy it there after every build
    add_custom_command(TARGET achtung POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different ${FONT_FILE} ${FONT_LICENSE} $<TARGET_FILE_DIR:achtung>
      COMMENT "Copying font.ttf next to the game")
  endif()
  target_link_libraries(achtung PRIVATE Threads::Threads ${GAME_LIBS})
  if(GAME_INCLUDES)
    target_include_directories(achtung PRIVATE ${GAME_INCLUDES})
  endif()
  if(MSVC)
    target_compile_options(achtung PRIVATE /W3 /D_USE_MATH_DEFINES)
  else()
    target_compile_options(achtung PRIVATE -Wall -Wextra -O2)
  endif()
  if(SDL2_SOURCE_DIR AND SDL2_TTF_SOURCE_DIR AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # portable Linux binary: no dependency on a particular libstdc++ version either
    target_link_options(achtung PRIVATE -static-libstdc++ -static-libgcc)
  endif()
  if(WIN32)
    set_target_properties(achtung PROPERTIES WIN32_EXECUTABLE ON)
  endif()
endif()
