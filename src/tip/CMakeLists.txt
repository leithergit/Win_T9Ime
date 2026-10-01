# T9Tip.dll: the TSF text service. Loaded into every application that uses the
# IME, so it depends on system DLLs only (static CRT, no third-party code).
add_library(T9Tip SHARED
  T9Tip.def
  globals.h globals.cpp
  dll_main.cpp
  register.h register.cpp
  key_event.h key_event.cpp
  host_client.h host_client.cpp
  event_listener.h event_listener.cpp
  touch_tracker.h touch_tracker.cpp
  edit_session.h
  display_attribute.h display_attribute.cpp
  candidate_ui.h candidate_ui.cpp
  candidate_window.h candidate_window.cpp
  lang_bar.h lang_bar.cpp
  text_service.h text_service.cpp)
target_link_libraries(T9Tip PRIVATE t9common d2d1 dwrite ole32 oleaut32 uuid user32 gdi32 advapi32)
t9_win7_subsystem(T9Tip WINDOWS)
t9_version_resource(T9Tip "T9Ime 九宫格输入法 - 文本服务 (TSF)")
# The registered development DLL is usually loaded by running applications.
add_custom_command(TARGET T9Tip PRE_LINK
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/move_locked.py" "$<TARGET_FILE:T9Tip>"
  VERBATIM)
