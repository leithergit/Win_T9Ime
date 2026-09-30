add_executable(t9diag t9diag.cpp)
target_link_libraries(t9diag PRIVATE t9common)
t9_win7_subsystem(t9diag CONSOLE)
