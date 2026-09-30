add_executable(t9_unit_tests unit/main.cpp unit/t9_tests.cpp unit/panel_layout_tests.cpp unit/ipc_tests.cpp unit/auto_show_tests.cpp)
target_link_libraries(t9_unit_tests PRIVATE t9core t9panel)
t9_win7_subsystem(t9_unit_tests CONSOLE)
target_include_directories(t9_unit_tests PRIVATE "${CMAKE_SOURCE_DIR}/third_party/doctest")
target_compile_definitions(t9_unit_tests PRIVATE DOCTEST_CONFIG_USE_STD_HEADERS T9_TEST_DATA_DIR="${T9_DATA_DIR}")
add_dependencies(t9_unit_tests rime_data copy_rime_dll)
add_test(NAME unit COMMAND t9_unit_tests)

# Engine regression: run each tests/regress/*.t9 script through t9repl and
# compare with the .expected snapshot next to it.
file(GLOB T9_REGRESS_SCRIPTS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/regress/*.t9")
foreach(script IN LISTS T9_REGRESS_SCRIPTS)
  get_filename_component(name "${script}" NAME_WE)
  add_test(NAME regress_${name}
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/regress/run_regress.py"
            --repl "$<TARGET_FILE:t9repl>" --data "${T9_DATA_DIR}"
            --user "${CMAKE_BINARY_DIR}/regress_user/${name}" --script "${script}")
endforeach()

# Windows 7 SP1 must be able to load every shipped binary.
add_test(NAME win7_imports
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/check_imports/check_imports.py"
          "$<TARGET_FILE:t9repl>" "$<TARGET_FILE:t9_unit_tests>" "$<TARGET_FILE:T9Host>"
          "$<TARGET_FILE:test_target>" "$<TARGET_FILE:T9Tip>" "$<TARGET_FILE:t9diag>" "$<TARGET_FILE:rime>"
          "$<TARGET_FILE:T9Ctl>" "$<TARGET_FILE:t9ctl_cli>" "$<TARGET_FILE:TestHost>")

# End-to-end panel test: drives the real mouse cursor, needs an interactive desktop.
add_executable(test_target WIN32 probes/test_target.cpp)
target_link_libraries(test_target PRIVATE ole32 shell32 uuid imm32)
add_executable(tip_activate probes/tip_activate.cpp)
target_link_libraries(tip_activate PRIVATE ole32 uuid)
t9_win7_subsystem(tip_activate CONSOLE)
t9_win7_subsystem(test_target WINDOWS)
add_test(NAME e2e_panel
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/e2e/panel_e2e.py"
          --host "$<TARGET_FILE:T9Host>" --target "$<TARGET_FILE:test_target>"
          --work "${CMAKE_BINARY_DIR}/e2e")
set_tests_properties(e2e_panel PROPERTIES LABELS e2e RUN_SERIAL TRUE)

# End-to-end TIP test: needs T9Tip.dll registered (tools/dev_register.ps1);
# skipped otherwise. Types with the real keyboard into test_target.
add_test(NAME e2e_tip
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/e2e/tip_e2e.py"
          --host "$<TARGET_FILE:T9Host>" --target "$<TARGET_FILE:test_target>")
set_tests_properties(e2e_tip PROPERTIES LABELS e2e RUN_SERIAL TRUE SKIP_RETURN_CODE 77)

# Panel output through the TIP (events pipe + edit session). Needs the TIP registered.
add_test(NAME e2e_push
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/e2e/push_e2e.py"
          --host "$<TARGET_FILE:T9Host>" --target "$<TARGET_FILE:test_target>" --work "${CMAKE_BINARY_DIR}/e2e")
set_tests_properties(e2e_push PROPERTIES LABELS e2e RUN_SERIAL TRUE SKIP_RETURN_CODE 77)

# Automatic show / hide: InputScope layouts, password fields, touch focus.
add_test(NAME e2e_autoshow
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/e2e/autoshow_e2e.py"
          --host "$<TARGET_FILE:T9Host>" --target "$<TARGET_FILE:test_target>" --work "${CMAKE_BINARY_DIR}/e2e")
set_tests_properties(e2e_autoshow PROPERTIES LABELS e2e RUN_SERIAL TRUE SKIP_RETURN_CODE 77)

# Control API (T9Ctl.dll through t9ctl.exe): show / hide / modes / position /
# notifications / switching an application to and from T9Ime.
add_test(NAME e2e_ctl
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/e2e/ctl_e2e.py"
          --host "$<TARGET_FILE:T9Host>" --target "$<TARGET_FILE:test_target>" --ctl "$<TARGET_FILE:t9ctl_cli>"
          --work "${CMAKE_BINARY_DIR}/e2e")
set_tests_properties(e2e_ctl PROPERTIES LABELS e2e RUN_SERIAL TRUE SKIP_RETURN_CODE 77)

# IMM32 compatibility: ImmSetOpenStatus / ImmSetConversionStatus reach T9Ime (CUAS).
add_test(NAME e2e_imm
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/e2e/imm_e2e.py"
          --host "$<TARGET_FILE:T9Host>" --target "$<TARGET_FILE:test_target>" --work "${CMAKE_BINARY_DIR}/e2e")
set_tests_properties(e2e_imm PROPERTIES LABELS e2e RUN_SERIAL TRUE SKIP_RETURN_CODE 77)

# Tapping the panel switches an application from another input method to T9Ime.
add_test(NAME e2e_switch
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/e2e/switch_e2e.py"
          --host "$<TARGET_FILE:T9Host>" --target "$<TARGET_FILE:test_target>" --work "${CMAKE_BINARY_DIR}/e2e")
set_tests_properties(e2e_switch PROPERTIES LABELS e2e RUN_SERIAL TRUE SKIP_RETURN_CODE 77)
