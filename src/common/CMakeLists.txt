# Shared helpers without external dependencies (also used by the TIP later).
add_library(t9common STATIC win_compat.h win_compat.cpp)
target_include_directories(t9common PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
