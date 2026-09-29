add_executable(t9repl t9repl.cpp)
target_link_libraries(t9repl PRIVATE t9engine)
t9_win7_subsystem(t9repl CONSOLE)
add_dependencies(t9repl rime_data copy_rime_dll)
