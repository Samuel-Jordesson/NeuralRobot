cmake_minimum_required(VERSION 3.16)
project(GaGame CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(OpenGL REQUIRED)
find_package(glfw3 REQUIRED)
find_package(GLEW REQUIRED)
find_package(assimp REQUIRED)

# Caminho raiz do projeto (shaders e assets), gerado num header para lidar com espaços no caminho
configure_file(src/config.h.in ${CMAKE_BINARY_DIR}/generated/config.h)

# GA_EDITOR=ON: jogo com o editor de armas e o botão de compilar (desenvolvimento).
# GA_EDITOR=OFF: versão final; os ajustes de weapons.ini vão embutidos no executável.
option(GA_EDITOR "Editor de armas e botao de compilar" ON)
if(NOT GA_EDITOR)
  set(GA_WEAPONS_INI "")
  if(EXISTS ${CMAKE_SOURCE_DIR}/weapons.ini)
    file(READ ${CMAKE_SOURCE_DIR}/weapons.ini GA_WEAPONS_INI)
  endif()
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/weapons.ini)
  configure_file(src/weapons_baked.h.in ${CMAKE_BINARY_DIR}/generated/weapons_baked.h @ONLY)
  # cenário oficial montado no editor livre (levels/oficial.txt) vai embutido também
  set(GA_OFFICIAL_LEVEL "")
  if(EXISTS ${CMAKE_SOURCE_DIR}/levels/oficial.txt)
    file(READ ${CMAKE_SOURCE_DIR}/levels/oficial.txt GA_OFFICIAL_LEVEL)
  endif()
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/levels/oficial.txt)
  configure_file(src/level_baked.h.in ${CMAKE_BINARY_DIR}/generated/level_baked.h @ONLY)
endif()
find_package(Threads REQUIRED)

add_executable(ga
  src/main.cpp
  src/gl_util.cpp
  src/skeleton.cpp
  src/animation.cpp
  src/mocap.cpp
  src/foot_ik.cpp
  src/aim_ik.cpp
  src/weapon.cpp
  src/table.cpp
  src/guns.cpp
  src/gun_tuning.cpp
  src/arena.cpp
  src/cover.cpp
  src/ragdoll.cpp
  src/level.cpp
  src/free_editor.cpp
  src/terrain.cpp
  src/soldiers.cpp
  src/builder.cpp
  src/loadout.cpp
  src/gun_handling.cpp
  src/character_model.cpp
  src/physics.cpp
  src/renderer.cpp
  src/graphics_settings.cpp
  src/menu.cpp
  third_party/imgui/imgui.cpp
  third_party/imgui/imgui_draw.cpp
  third_party/imgui/imgui_tables.cpp
  third_party/imgui/imgui_widgets.cpp
  third_party/imgui/backends/imgui_impl_glfw.cpp
  third_party/imgui/backends/imgui_impl_opengl3.cpp
)

target_include_directories(ga PRIVATE src third_party third_party/imgui ${CMAKE_BINARY_DIR}/generated)
target_link_libraries(ga PRIVATE glfw GLEW::GLEW OpenGL::GL assimp::assimp Threads::Threads)
target_compile_definitions(ga PRIVATE GA_EDITOR=$<BOOL:${GA_EDITOR}>)
target_compile_options(ga PRIVATE -Wall -Wno-unused-function -Wno-missing-field-initializers)
