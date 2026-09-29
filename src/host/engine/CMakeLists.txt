# t9core: nine-key front end, no librime dependency (unit tested in isolation).
set(T9_SYLLABLE_TABLE "${CMAKE_BINARY_DIR}/gen/syllable_table.gen.inc")
add_custom_command(
  OUTPUT "${T9_SYLLABLE_TABLE}"
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/gen_syllables/gen_syllables.py"
          --dict "${T9_RIME_ICE_DIR}/cn_dicts/8105.dict.yaml" --out "${T9_SYLLABLE_TABLE}"
  DEPENDS "${CMAKE_SOURCE_DIR}/tools/gen_syllables/gen_syllables.py"
          "${T9_RIME_ICE_DIR}/cn_dicts/8105.dict.yaml"
  COMMENT "Generating nine-key syllable table"
  VERBATIM)

add_library(t9core STATIC
  t9/t9_keys.h
  t9/syllable_index.h t9/syllable_index.cpp
  t9/pinyin_bar.h t9/pinyin_bar.cpp
  t9/t9_composer.h t9/t9_composer.cpp
  t9/preedit_formatter.h t9/preedit_formatter.cpp
  "${T9_SYLLABLE_TABLE}")
target_include_directories(t9core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}"
                                  PRIVATE "${CMAKE_BINARY_DIR}/gen")

# t9engine: librime wrapper + sessions.
add_library(t9engine STATIC rime_engine.h rime_engine.cpp)
target_link_libraries(t9engine PUBLIC t9core rime)
target_include_directories(t9engine PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
