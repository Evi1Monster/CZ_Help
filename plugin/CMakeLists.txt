add_library(cz_help_mm SHARED plugin.cpp exports.def)
add_library(cz_render STATIC render_hook.cpp)
target_include_directories(cz_render PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(cz_render PUBLIC cz_core opengl32 gdi32 user32)
target_link_libraries(cz_help_mm PRIVATE cz_render)
function(cz_plugin_options target)
  target_include_directories(${target} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../shared
    ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/halflife/common
    ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/halflife/engine
    ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/halflife/dlls
    ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/halflife/public
    ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/halflife/pm_shared
    ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/metamod)
  target_compile_features(${target} PRIVATE cxx_std_17)
  target_compile_definitions(${target} PRIVATE WIN32 _WINDOWS NOMINMAX _CRT_SECURE_NO_WARNINGS)
  set_property(TARGET ${target} PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endfunction()
add_library(cz_bhop STATIC bhop.cpp kz_movement.cpp)
cz_plugin_options(cz_bhop)
target_link_libraries(cz_help_mm PRIVATE cz_bhop)
cz_plugin_options(cz_help_mm)
set_target_properties(cz_help_mm PROPERTIES PREFIX "" OUTPUT_NAME "cz_help_mm")
add_executable(plugin_boundary_tests boundary_tests.cpp)
cz_plugin_options(plugin_boundary_tests)
target_link_libraries(plugin_boundary_tests PRIVATE cz_render cz_bhop)
add_test(NAME plugin_boundaries COMMAND plugin_boundary_tests)
add_executable(render_tests render_tests.cpp)
target_link_libraries(render_tests PRIVATE cz_render)
add_test(NAME opengl_renderer COMMAND render_tests)
add_executable(bhop_tests bhop_tests.cpp)
cz_plugin_options(bhop_tests)
target_link_libraries(bhop_tests PRIVATE cz_bhop)
add_test(NAME bhop_movement COMMAND bhop_tests)
add_executable(kz_tests kz_tests.cpp)
cz_plugin_options(kz_tests)
target_link_libraries(kz_tests PRIVATE cz_bhop)
add_test(NAME kz_movement COMMAND kz_tests)
