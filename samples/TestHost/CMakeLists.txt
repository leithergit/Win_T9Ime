# TestHost samples for T9Ctl.dll.
add_executable(TestHost WIN32 cpp/TestHost.cpp)
target_link_libraries(TestHost PRIVATE T9Ctl user32 gdi32)
t9_win7_subsystem(TestHost WINDOWS)

# C# sample: the .NET 3.5 compiler that ships with Windows (runs on stock
# Windows 7 SP1; the .config adds .NET 4.x). AnyCPU: load the T9Ctl.dll of the
# process architecture from the same directory.
set(T9_CSC "$ENV{WINDIR}/Microsoft.NET/Framework/v3.5/csc.exe")
if(EXISTS "${T9_CSC}")
  set(cs_out "${CMAKE_BINARY_DIR}/bin/TestHost.CS.exe")
  # The old compiler takes "D:/x" apart at the slash: native paths only.
  file(TO_NATIVE_PATH "${cs_out}" cs_out_native)
  file(TO_NATIVE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cs/T9Ctl.cs" cs_src1)
  file(TO_NATIVE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cs/TestHost.cs" cs_src2)
  add_custom_command(OUTPUT "${cs_out}" "${CMAKE_BINARY_DIR}/bin/TestHost.CS.exe.config"
    COMMAND "${T9_CSC}" /nologo /target:winexe /platform:anycpu /codepage:65001 "/out:${cs_out_native}" "${cs_src1}" "${cs_src2}"
    COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_CURRENT_SOURCE_DIR}/cs/TestHost.CS.exe.config" "${CMAKE_BINARY_DIR}/bin"
    DEPENDS cs/T9Ctl.cs cs/TestHost.cs cs/TestHost.CS.exe.config
    COMMENT "Building TestHost.CS.exe (C#)"
    VERBATIM)
  add_custom_target(TestHostCS ALL DEPENDS "${cs_out}")
else()
  message(STATUS "C# compiler (.NET 3.5) not found: TestHost.CS.exe is not built")
endif()
