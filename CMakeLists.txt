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
target_link_libraries(ga PRIVATE glfw GLEW::GLEW OpenGL::GL assimp::assimp)
target_compile_options(ga PRIVATE -Wall -Wno-unused-function -Wno-missing-field-initializers)
