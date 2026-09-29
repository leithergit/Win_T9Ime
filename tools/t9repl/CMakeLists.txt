add_executable(t9repl t9repl.cpp)
target_link_libraries(t9repl PRIVATE t9engine)
add_custom_command(TARGET t9repl POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:rime>" "$<TARGET_FILE_DIR:t9repl>"
  VERBATIM)
add_dependencies(t9repl rime_data)
