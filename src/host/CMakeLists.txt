add_library(t9panel STATIC
  panel/panel_layout.h panel/panel_layout.cpp
  panel/auto_show.h panel/auto_show.cpp
  panel/panel_renderer.h panel/panel_renderer.cpp
  panel/panel_window.h panel/panel_window.cpp
  app/engine_thread.h app/engine_thread.cpp
  app/text_output.h app/text_output.cpp
  app/system_input.h app/system_input.cpp
  ipc/pipe_server.h ipc/pipe_server.cpp
  ipc/focus_registry.h ipc/focus_registry.cpp
  ipc/request_server.h ipc/request_server.cpp)
target_include_directories(t9panel PUBLIC panel app ipc)
target_link_libraries(t9panel PUBLIC t9engine t9common d2d1 dwrite)

add_executable(T9Host WIN32 app/main.cpp app/T9Host.manifest)
target_link_libraries(T9Host PRIVATE t9panel shell32 ole32)
t9_win7_subsystem(T9Host WINDOWS)
add_dependencies(T9Host rime_data copy_rime_dll)
# A registered development TIP relaunches T9Host at any time; move the running
# executable aside instead of failing to link.
add_custom_command(TARGET T9Host PRE_LINK
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/move_locked.py" "$<TARGET_FILE:T9Host>"
  VERBATIM)
