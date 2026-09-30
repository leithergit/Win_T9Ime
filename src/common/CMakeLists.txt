# Shared code without external dependencies (linked into the TIP and the host).
add_library(t9common STATIC
  win_compat.h win_compat.cpp
  protocol.h protocol.cpp
  pipe.h pipe.cpp)
target_include_directories(t9common PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(t9common PUBLIC advapi32)
