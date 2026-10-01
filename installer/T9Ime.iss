; T9Ime installer (Inno Setup 6). Build with tools\make_installer.ps1, which
; passes the version and the build directories:
;   ISCC /DAppVersion=0.6.0.49 /DCommit=abc1234 /DX64Bin=... /DX86Bin=...
;        /DX64Lib=... /DX86Lib=... /DSrcRoot=... /DOutputDir=... installer\T9Ime.iss
;
; Layout of {app}:
;   T9Host.exe T9Tip.dll T9Ctl.dll rime.dll t9ctl.exe t9diag.exe   native (x64 or x86)
;   x86\T9Tip.dll                    64-bit Windows only: the TIP for 32-bit programs
;   data\                            Rime data, pre-deployed (file times kept)
;   sdk\ samples\                    optional development package
;
; One installer for both architectures: 64-bit install mode on x64 Windows.

#ifndef AppVersion
  #define AppVersion "0.0.0.0"
#endif
#ifndef Commit
  #define Commit "unknown"
#endif
#ifndef OutputDir
  #define OutputDir "..\dist\installer"
#endif

[Setup]
AppId={{CAC58F69-EEAD-4022-A373-2A1356D55BDB}
AppName=T9Ime 九宫格输入法
AppVersion={#AppVersion}
AppVerName=T9Ime 九宫格输入法 {#AppVersion}
AppPublisher=T9Ime
AppComments=Windows 触摸九宫格输入法（commit {#Commit}）
VersionInfoVersion={#AppVersion}
VersionInfoProductVersion={#AppVersion}
VersionInfoDescription=T9Ime 九宫格输入法 安装程序
VersionInfoCopyright=Copyright (C) 2026 T9Ime. GPL-3.0
DefaultDirName={autopf}\T9Ime
DefaultGroupName=T9Ime 九宫格输入法
DisableProgramGroupPage=yes
LicenseFile={#SrcRoot}\LICENSE
OutputDir={#OutputDir}
OutputBaseFilename=T9Ime-{#AppVersion}-Setup
Compression=lzma2/ultra64
SolidCompression=yes
LZMAUseSeparateProcess=yes
WizardStyle=modern
PrivilegesRequired=admin
; x86 and x64 Windows; no ARM64 (no ARM64 build of the input method).
ArchitecturesAllowed=x86compatible x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=6.1sp1
UninstallDisplayIcon={app}\T9Host.exe
UninstallDisplayName=T9Ime 九宫格输入法
CloseApplications=no
RestartIfNeededByRun=no
; Code signing (not enabled yet): SignTool=signtool sign /fd sha256 /tr <timestamp> $f

[Languages]
Name: "chinesesimp"; MessagesFile: "compiler:Languages\ChineseSimplified.isl"

[Types]
Name: "full"; Description: "完整安装（含开发包）"
Name: "compact"; Description: "仅输入法"
Name: "custom"; Description: "自定义"; Flags: iscustom

[Components]
Name: "main"; Description: "T9Ime 输入法（必需）"; Types: full compact custom; Flags: fixed
Name: "sdk"; Description: "开发包：T9Ctl 控制接口（头文件、库）与示例程序"; Types: full

[Tasks]
Name: "takeover"; Description: "关闭系统触摸键盘的自动弹出（Windows 7：隐藏输入面板图标）——推荐，卸载时恢复"
Name: "autostart"; Description: "登录 Windows 时启动 T9Ime（推荐）"

[Files]
; ---- 64-bit Windows
Source: "{#X64Bin}\T9Host.exe"; DestDir: "{app}"; Flags: ignoreversion; Check: Is64BitInstallMode
Source: "{#X64Bin}\T9Tip.dll"; DestDir: "{app}"; Flags: ignoreversion regserver restartreplace uninsrestartdelete; Check: Is64BitInstallMode
Source: "{#X64Bin}\T9Ctl.dll"; DestDir: "{app}"; Flags: ignoreversion restartreplace uninsrestartdelete; Check: Is64BitInstallMode
Source: "{#X64Bin}\rime.dll"; DestDir: "{app}"; Flags: ignoreversion restartreplace uninsrestartdelete; Check: Is64BitInstallMode
Source: "{#X64Bin}\t9ctl.exe"; DestDir: "{app}"; Flags: ignoreversion; Check: Is64BitInstallMode
Source: "{#X64Bin}\t9diag.exe"; DestDir: "{app}"; Flags: ignoreversion; Check: Is64BitInstallMode
Source: "{#X86Bin}\T9Tip.dll"; DestDir: "{app}\x86"; Flags: ignoreversion regserver 32bit restartreplace uninsrestartdelete; Check: Is64BitInstallMode
; ---- 32-bit Windows
Source: "{#X86Bin}\T9Host.exe"; DestDir: "{app}"; Flags: ignoreversion; Check: not Is64BitInstallMode
Source: "{#X86Bin}\T9Tip.dll"; DestDir: "{app}"; Flags: ignoreversion regserver restartreplace uninsrestartdelete; Check: not Is64BitInstallMode
Source: "{#X86Bin}\T9Ctl.dll"; DestDir: "{app}"; Flags: ignoreversion restartreplace uninsrestartdelete; Check: not Is64BitInstallMode
Source: "{#X86Bin}\rime.dll"; DestDir: "{app}"; Flags: ignoreversion restartreplace uninsrestartdelete; Check: not Is64BitInstallMode
Source: "{#X86Bin}\t9ctl.exe"; DestDir: "{app}"; Flags: ignoreversion; Check: not Is64BitInstallMode
Source: "{#X86Bin}\t9diag.exe"; DestDir: "{app}"; Flags: ignoreversion; Check: not Is64BitInstallMode
; ---- Rime data (architecture independent; original file times are kept, the
;      pre-deployed build/ must not look older than its sources)
Source: "{#X64Bin}\data\*"; DestDir: "{app}\data"; Excludes: ".t9ime-data-stamp"; Flags: ignoreversion recursesubdirs createallsubdirs
; ---- Documents
Source: "{#SrcRoot}\LICENSE"; DestDir: "{app}"; DestName: "LICENSE.txt"; Flags: ignoreversion
Source: "{#SrcRoot}\README.md"; DestDir: "{app}"; Flags: ignoreversion
; ---- Development package
Source: "{#SrcRoot}\src\ctl\t9ctl.h"; DestDir: "{app}\sdk"; Components: sdk; Flags: ignoreversion
Source: "{#SrcRoot}\samples\TestHost\cs\T9Ctl.cs"; DestDir: "{app}\sdk"; Components: sdk; Flags: ignoreversion
Source: "{#X86Bin}\T9Ctl.dll"; DestDir: "{app}\sdk\x86"; Components: sdk; Flags: ignoreversion
Source: "{#X86Lib}\T9Ctl.lib"; DestDir: "{app}\sdk\x86"; Components: sdk; Flags: ignoreversion
Source: "{#X64Bin}\T9Ctl.dll"; DestDir: "{app}\sdk\x64"; Components: sdk; Flags: ignoreversion
Source: "{#X64Lib}\T9Ctl.lib"; DestDir: "{app}\sdk\x64"; Components: sdk; Flags: ignoreversion
Source: "{#X64Bin}\TestHost.exe"; DestDir: "{app}\samples"; Components: sdk; Flags: ignoreversion; Check: Is64BitInstallMode
Source: "{#X64Bin}\TestHost.CS.exe"; DestDir: "{app}\samples"; Components: sdk; Flags: ignoreversion; Check: Is64BitInstallMode
Source: "{#X64Bin}\TestHost.CS.exe.config"; DestDir: "{app}\samples"; Components: sdk; Flags: ignoreversion; Check: Is64BitInstallMode
Source: "{#X64Bin}\T9Ctl.dll"; DestDir: "{app}\samples"; Components: sdk; Flags: ignoreversion; Check: Is64BitInstallMode
Source: "{#X86Bin}\TestHost.exe"; DestDir: "{app}\samples"; Components: sdk; Flags: ignoreversion; Check: not Is64BitInstallMode
Source: "{#X86Bin}\TestHost.CS.exe"; DestDir: "{app}\samples"; Components: sdk; Flags: ignoreversion; Check: not Is64BitInstallMode
Source: "{#X86Bin}\TestHost.CS.exe.config"; DestDir: "{app}\samples"; Components: sdk; Flags: ignoreversion; Check: not Is64BitInstallMode
Source: "{#X86Bin}\T9Ctl.dll"; DestDir: "{app}\samples"; Components: sdk; Flags: ignoreversion; Check: not Is64BitInstallMode

[Registry]
; Start the host at logon for every user (also started on demand by the input method).
Root: HKLM; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "T9Ime"; ValueData: """{app}\T9Host.exe"" --background"; Flags: uninsdeletevalue; Tasks: autostart

[Icons]
Name: "{group}\T9Ime 设置"; Filename: "{app}\T9Host.exe"; Parameters: "--open-settings"
Name: "{group}\T9Ime 示例程序（TestHost）"; Filename: "{app}\samples\TestHost.exe"; Components: sdk
Name: "{group}\卸载 T9Ime"; Filename: "{uninstallexe}"

[Run]
; As the user who started the setup (not the elevated account): the host and
; the touch keyboard settings (HKCU) belong to that user.
Filename: "{app}\T9Host.exe"; Parameters: "--take-over-touch-keyboard"; Flags: nowait runasoriginaluser; Tasks: takeover
Filename: "{app}\T9Host.exe"; Parameters: "--background"; Flags: nowait runasoriginaluser; Tasks: not takeover

[UninstallRun]
; Put the system touch keyboard settings back, then stop the host.
Filename: "{app}\T9Host.exe"; Parameters: "--restore-touch-keyboard"; Flags: runhidden waituntilterminated; RunOnceId: "RestoreTouchKeyboard"
Filename: "{sys}\taskkill.exe"; Parameters: "/f /im T9Host.exe"; Flags: runhidden waituntilterminated; RunOnceId: "StopHost"

[UninstallDelete]
; DLLs renamed aside by an update while they were loaded (see PrepareToInstall).
Type: files; Name: "{app}\*.old-*"
Type: files; Name: "{app}\x86\*.old-*"
Type: dirifempty; Name: "{app}\x86"
Type: dirifempty; Name: "{app}"

[Code]
const
  KB_PLATFORM_UPDATE = 'KB2670838';
  KB_SHA2 = 'KB4474419';

// Direct2D 1.1 (d2d1.dll 6.2+) arrives with the Windows 7 platform update.
function HasPlatformUpdate(): Boolean;
var
  MS, LS: Cardinal;
begin
  Result := GetVersionNumbers(ExpandConstant('{sys}\d2d1.dll'), MS, LS) and
            (((MS shr 16) > 6) or (((MS shr 16) = 6) and ((MS and $FFFF) >= 2)));
end;

function HasUpdate(KB: String): Boolean;
var
  Names: TArrayOfString;
  I: Integer;
  Root: Integer;
begin
  Result := False;
  if IsWin64 then Root := HKLM64 else Root := HKLM;
  if RegGetSubkeyNames(Root, 'SOFTWARE\Microsoft\Windows\CurrentVersion\Component Based Servicing\Packages', Names) then
    for I := 0 to GetArrayLength(Names) - 1 do
      if Pos(KB, Names[I]) > 0 then begin
        Result := True;
        Exit;
      end;
end;

function InitializeSetup(): Boolean;
var
  V: TWindowsVersion;
begin
  Result := True;
  GetWindowsVersionEx(V);
  if (V.Major = 6) and (V.Minor = 1) then begin
    if not HasPlatformUpdate() then begin
      MsgBox('T9Ime 需要 Windows 7 平台更新 ' + KB_PLATFORM_UPDATE + '（提供 Direct2D / DirectWrite）。' + #13#10 +
             '请先通过 Windows Update 或微软下载中心安装该更新，再运行本安装程序。', mbCriticalError, MB_OK);
      Result := False;
      Exit;
    end;
    if not HasUpdate(KB_SHA2) then
      if MsgBox('建议先安装 ' + KB_SHA2 + '（SHA-2 代码签名支持）。不安装也能使用 T9Ime。' + #13#10#13#10 + '现在继续安装吗？',
                mbConfirmation, MB_YESNO) = IDNO then begin
        Result := False;
        Exit;
      end;
  end;
  if (V.Major = 6) and ((V.Minor = 2) or (V.Minor = 3)) then
    if MsgBox('T9Ime 未在 Windows 8 / 8.1 上测试，可能无法正常工作。' + #13#10#13#10 + '仍要安装吗？',
              mbConfirmation, MB_YESNO or MB_DEFBUTTON2) = IDNO then
      Result := False;
end;

procedure StopHost();
var
  Code: Integer;
begin
  Exec(ExpandConstant('{sys}\taskkill.exe'), '/f /im T9Host.exe', '', SW_HIDE, ewWaitUntilTerminated, Code);
end;

// Leftovers of earlier updates that are no longer loaded.
procedure DeleteOldCopies(Dir: String);
var
  F: TFindRec;
begin
  if FindFirst(Dir + '\*.old-*', F) then
    try
      repeat
        DeleteFile(Dir + '\' + F.Name);
      until not FindNext(F);
    finally
      FindClose(F);
    end;
end;

// An update while programs still use the input method: their loaded DLLs
// cannot be overwritten, but they can be renamed. The new files are installed
// at once (no reboot); the old copies go away later.
procedure MoveAside(Path: String);
begin
  if FileExists(Path) then
    if not DeleteFile(Path) then
      RenameFile(Path, Path + '.old-' + GetDateTimeString('yyyymmddhhnnsszzz', #0, #0));
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  App: String;
begin
  Result := '';
  StopHost();
  App := ExpandConstant('{app}');
  DeleteOldCopies(App);
  DeleteOldCopies(App + '\x86');
  MoveAside(App + '\T9Tip.dll');
  MoveAside(App + '\x86\T9Tip.dll');
  MoveAside(App + '\T9Ctl.dll');
  MoveAside(App + '\rime.dll');
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usPostUninstall then
    MsgBox('T9Ime 已卸载。' + #13#10 + '个人数据（用户词库、设置）保留在 %APPDATA%\T9Ime，如不再需要可手动删除。' + #13#10 +
           '正在使用输入法的程序关闭后（或重启后）剩余文件会被删除。', mbInformation, MB_OK);
end;
