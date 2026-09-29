add_executable(t9_unit_tests unit/main.cpp unit/t9_tests.cpp)
target_link_libraries(t9_unit_tests PRIVATE t9core)
target_include_directories(t9_unit_tests PRIVATE "${CMAKE_SOURCE_DIR}/third_party/doctest")
target_compile_definitions(t9_unit_tests PRIVATE DOCTEST_CONFIG_USE_STD_HEADERS)
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
          "$<TARGET_FILE:t9repl>" "$<TARGET_FILE:t9_unit_tests>" "$<TARGET_FILE:rime>")
