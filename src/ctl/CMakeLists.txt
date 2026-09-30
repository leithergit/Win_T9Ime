# Control API: T9Ctl.dll (C exports, undecorated names via the .def file) and
# t9ctl.exe (command line). Target names differ from the output names because
# T9Ctl / t9ctl collide on a case-insensitive file system (.pdb, .lib).
add_library(T9Ctl SHARED t9ctl.h t9ctl.cpp T9Ctl.def)
target_include_directories(T9Ctl PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(T9Ctl PRIVATE t9common user32 ole32)
t9_win7_subsystem(T9Ctl WINDOWS)

add_executable(t9ctl_cli t9ctl_cli.cpp)
set_target_properties(t9ctl_cli PROPERTIES OUTPUT_NAME t9ctl PDB_NAME t9ctl_cli)
target_link_libraries(t9ctl_cli PRIVATE T9Ctl user32)
t9_win7_subsystem(t9ctl_cli CONSOLE)
