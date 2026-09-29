# Weasel（小狼毫）源码调研 —— 作为 T9Ime fork 基线

- 调研对象：`https://github.com/rime/weasel`，master HEAD `d73f629`（2026-08-17，"fix(WeaselServer): avoid tray refresh blocking IPC pipe"）
- 本地克隆：`%TEMP%\claude\...\scratchpad\weasel`（--depth 50，未拉子模块）
- 下文 `文件:行` 均指该提交。标 **[实测]** 的是读代码确认的事实，标 **[推断]** 的尚未编译验证。

---

## 1. 仓库结构、版本、构建、依赖

### 1.1 模块

| 目录 | 产物 | 作用 |
|---|---|---|
| `WeaselTSF/` | `weasel.dll`(x86) / `weaselx64.dll` / `weaselARM.dll` / `weaselARM64.dll`（`WeaselTSF/xmake.lua`） | TSF TIP，进程内加载 |
| `WeaselIPC/` | 静态库 | 客户端 IPC（`WeaselClientImpl`、`PipeChannel`、`ResponseParser` 和各类 Deserializer） |
| `WeaselIPCServer/` | 静态库 | 服务端管道监听（`WeaselServerImpl`、`SecurityAttribute`） |
| `WeaselUI/` | 静态库 | **候选窗绘制**，同时被 TIP 和 Server 链接 |
| `RimeWithWeasel/` | 静态库 | librime 封装，`RimeWithWeaselHandler : RequestHandler` |
| `WeaselServer/` | `WeaselServer.exe` | 每用户一个的服务进程，包含托盘、WinSparkle 自动更新 |
| `WeaselDeployer/` | `WeaselDeployer.exe` | 部署和设置对话框（WTL），负责方案选择、配色、词典管理、同步 |
| `WeaselSetup/` | `WeaselSetup.exe` | 注册 TIP：把 dll 拷进 System32/SysWOW64，再调用 regsvr32 |
| `arm64x_wrapper/` | `weaselARM64X.dll` | ARM64X 转发 DLL（`build.bat`：`link /machine:arm64x`） |
| `output/install.nsi` | NSIS 安装包 | |
| `include/` | 公共头文件 | 含 **WTL 源码** `include/wtl`，以及 `WeaselIPC.h`、`WeaselIPCData.h`、`PipeChannel.h` |
| `librime/`、`plum/` | 子模块 | 默认不从源码构建 librime，用 `get-rime.ps1` 下载官方预编译包 |

IMM 版 `WeaselIME` 已在 `d636e0a` 中删除（"refactor: drop WeaselIME"）。现在只有 TSF 路径，`imesetup.cpp:296` 也注明 "IMM/.ime support removed — TSF-only build"。

### 1.2 版本与许可证
- 最新正式版是 **0.17.4**（2025-06-04，`build.bat:13-15`）。此后没有新的版本号，只有滚动的 `latest` nightly 标签（2026-08-18）。
- 许可证是 **GPL-3.0**（`LICENSE.txt`）。

### 1.3 构建系统
- 主路径：`build.bat`，先用 `render.js` 生成 `weasel.props`，再执行 `msbuild weasel.sln`，依次构建 x64、Win32，可选 ARM/ARM64（`build.bat:202-212`），最后运行 NSIS（`build.bat:224-231`）。
- 备选：`xmake.lua`（CI 的 matrix 里同时跑 msbuild 和 xmake 两种变体）。**没有 CMake。**
- 工具集：`env.vs2022.bat` 设置 `PLATFORM_TOOLSET=v143`、`BJAM_TOOLSET=msvc-14.3`。模板默认值仍是 v142。CI 使用 windows-2022 和 SDK 10.0.19041。
- C++17（`WeaselTSF.vcxproj:176`，`xmake.lua` 用 `set_languages("c++17")`）。
- 静态 CRT：TIP 的 Release 配置是 `RuntimeLibrary=MultiThreaded`（`WeaselTSF.vcxproj:227`），xmake 全局设置 `set_runtimes("MT")`。

### 1.4 第三方依赖
- **Boost 1.84**（CI）：`install_boost.bat` 和 `build.bat boost` 以 `link=static runtime-link=static` 编译 filesystem/json/locale/regex/serialization/system/thread（`build.bat:236-250`）。Weasel 自身代码只用到 boost 的几小块：
  - `boost::interprocess::wbufferstream`（header-only）：`include/PipeChannel.h:5,13`，`WeaselIPC/ResponseParser.cpp:22`
  - `boost::thread` / `thread_specific_ptr`：`PipeChannel.h:6-7,60,63`，`WeaselServerImpl.cpp:191,425-428`，`RimeWithWeasel.cpp:80`
  - `boost::archive::text_w{i,o}archive` 和 serialization：`include/WeaselIPCData.h:5-6,427-536`（UIStyle、CandidateInfo 的序列化），`WeaselIPC/Styler.cpp:18`，`RimeWithWeasel.cpp:~889,909`
- **WTL/ATL**：WTL 源码放在 `include/wtl`，ATL 来自 VS。
- **librime**：通过 `get-rime.ps1 -use dev` 从 GitHub releases 下载 `rime-<hash>-Windows-msvc-x64.7z`（以及 deps 包），解压到 `lib/`、`lib64/`、`output/`。子模块仅在执行 `build.bat rime` 时使用。发布包附带的 rime.dll 是否内置 librime-lua 等插件 **[待核实]**，需要打开 release 包确认。
- **WinSparkle**（自动更新，打了 patch，`lib/WinSparkle.lib`）：只有 Server 使用。
- 数据：`build.bat data` 通过 plum 拉取默认方案，另外生成 opencc 数据。

### 1.5 TIP DLL 实际带入的依赖 [实测，来自源码和工程文件]
- 链接进 TIP 的库：`WeaselIPC`（boost serialization + thread + interprocess）和 **`WeaselUI`**（GDI+、Direct2D、DirectWrite、Shcore、`usp10`、DbgHelp）。见 `WeaselTSF.vcxproj:505-511`、`WeaselTSF.vcxproj:185`、`WeaselUI/WeaselPanel.h:8-9`、`WeaselUI/GdiplusBlur.h:3`。
- **Boost 以静态库形式链入 TIP**（通过 auto-link 引入 libboost_serialization、wserialization、thread），因此不产生额外的 DLL 依赖，但违反 SPEC §2 的"禁止 Boost"。
- **OpenMP**：`WeaselUI.vcxproj:162` 等处开启了 `<OpenMP>GenerateParallelCode</OpenMP>`，`GdiplusBlur.cpp:5` 的 `#pragma omp` 在 `cbe3aab`（2026-02）加入。MSVC 的 OpenMP 只能动态链接 `VCOMP140.DLL`，而安装包 `install.nsi` 并不分发它 **[推断：TIP 会导入 vcomp140.dll，在没有安装 VC++ 运行库的机器上可能加载失败；需要 dumpbin /dependents 核实]**。这是上游的新回归风险，fork 时必须去掉。

---

## 2. IPC

### 2.1 传输与命名
- 使用**命名管道**，消息模式，双工：`PIPE_TYPE_MESSAGE|PIPE_READMODE_MESSAGE|PIPE_WAIT`，实例数 `PIPE_UNLIMITED_INSTANCES`，缓冲 64KB（`WeaselIPC/PipeChannel.cpp:104-113`，`PipeChannel.h:89`）。
- 名称为 `\\.\pipe\<GetUserName()>\WeaselNamedPipe`（`include/WeaselIPC.h:178-185`）。**名字里只有用户名，没有 SID，也没有会话 ID。** 命名管道是全局命名空间，同一用户同时登录控制台和 RDP 时会冲突；用户名还可能含有特殊字符或发生重名。
- 单实例互斥体 `(WEASEL)Furandōru-Sukāretto-<username>`（`WeaselServerImpl.cpp:155-169`），没有 `Local\` 前缀，默认落在会话内命名空间，所以"每会话一实例"和"全局唯一管道名"两者不一致。

### 2.2 安全描述符（`WeaselIPCServer/SecurityAttribute.cpp:61-73`）
SDDL 为 `S:(ML;;NW;;;LW)D:(A;;FA;;;SY)(A;;FA;;;WD)(A;;FA;;;AC)`：
- SACL 带 Low 完整性标签（NW），所以 Low IL 和 AppContainer 进程可以写入。
- DACL 授予 SYSTEM、**Everyone** 和 ALL APPLICATION PACKAGES `FILE_ALL_ACCESS`。
- 问题：Everyone 的授权过宽，其他用户或会话也能连进来操控你的 Rime 会话，包括读取候选、注入按键。SPEC §3 要求改为"当前用户 SID + AC(S-1-15-2-1) + Low IL"，建议再加上 `S-1-15-2-2`（ALL RESTRICTED APP PACKAGES，Edge/Chromium 的 LPAC 渲染进程需要）**[待核实 LPAC 场景]**。

### 2.3 协议
- **请求**：固定头 `PipeMessage{WEASEL_IPC_COMMAND Msg; DWORD wParam; DWORD lParam;}`（`WeaselIPC.h:43-47`），后面可以跟 UTF-16 文本体（`PipeChannel.h:151-175`）。lParam 通常是 session_id。按键打包进 wParam（`KeyEvent` 32 位，包含 ibus keycode 和 mask）。
- 命令集见 `WeaselIPC.h:18-36`：ECHO / START_SESSION / END_SESSION / PROCESS_KEY_EVENT / SHUTDOWN / FOCUS_IN / FOCUS_OUT / UPDATE_INPUT_POS / START|END_MAINTENANCE / COMMIT|CLEAR_COMPOSITION / TRAY_COMMAND / SELECT|HIGHLIGHT_CANDIDATE / CHANGE_PAGE。
- **响应**：先回一个 DWORD 返回值（`_ReceiveResponse`），若消息更长则整条消息读入缓冲，靠 `ERROR_MORE_DATA` 分支处理（`PipeChannel.cpp:88-102`）。消息体是行式文本 `key=value\n`，以 `.` 结尾，由 `ResponseParser` 按首段 key 分派（`ResponseParser.cpp:21-69`）：
  - `action=commit,status,ctx,config,style`
  - `commit=...`
  - `status.ascii_mode=…`、`status.composing=…`、`status.schema_id=…`
  - `ctx.preedit=…`、`ctx.preedit.cursor=…`、`ctx.aux…`
  - `ctx.cand=<boost text_woarchive 序列化的 CandidateInfo>`
  - `config.inline_preedit=…`
  - `style=<boost text_woarchive 序列化的整个 UIStyle>`，只在 `__synced==false` 时发送（`RimeWithWeasel.cpp:741-930`，其中 909 行）
- UPDATE_INPUT_POS 把 RECT **压缩进 32 位**：12 位 left、12 位 top、7 位 height，外加 1 位 hi-res 移位标志（`WeaselClientImpl.cpp:106-130`，`WeaselServerImpl.cpp:266-306`）。坐标超过 ±4096 就会被截断，多显示器或 8K 环境下会出错；服务端还额外调用了 `PhysicalToLogicalPointForPerMonitorDPI`。

### 2.4 同步性、线程、超时、重连
- **完全同步阻塞**：客户端依次 `WriteFile`、`FlushFileBuffers`、`ReadFile`，都没有 OVERLAPPED，也**没有任何超时**（`PipeChannel.cpp:71-102`）。只要 Server 卡住，比如 `d73f629` 修复的托盘死锁，或者部署维护期间，**宿主应用的 UI 线程就会被挂住**。
- 连接按线程保存：`boost::thread_specific_ptr<HANDLE>`，每个 TIP 线程各有一条管道（`PipeChannel.h:60-63`）。
- `_Connect` 在管道 busy 时会 `while (...) WaitNamedPipe(500)` **无限循环**（`PipeChannel.cpp:41-50`）。服务端不存在时 CreateFile 抛出 `ERROR_FILE_NOT_FOUND`，`_Ensure` 返回 false。
- 服务端为每个连接开一个 `boost::thread`（`WeaselServerImpl.cpp:421-451`），所有请求共用一把全局 `g_api_mutex` 串行执行（`:178,187-190`）。
- 重连逻辑：每次按键调用 `_EnsureServerConnected()`（`WeaselTSF.cpp:239-282`）。它先 Echo，失败就 `_Reconnect()`（Disconnect、Connect、StartSession）。连续失败 6 次、确认 Deployer 互斥体不存在、并用 ToolHelp 快照确认没有 `WeaselServer.exe` 进程后，**用 `ShellExecuteW` 执行 `start_service.bat`**（`:263-271`）。
- **Server 不在时**：`_ProcessKeyEvent` 发现连接不可用就返回 `pfEaten=FALSE`，按键透传（`KeyEventSink.cpp:19-23`）。每次按键都会尝试 CreateFile，开销不大；但一旦连接建立后 Server 卡死，就会无限阻塞。
- **在 AppContainer 中拉起 Server**：ShellExecute 执行 .bat 会失败，因为 AppContainer 无法创建普通进程。Weasel 实际依赖 HKLM Run 键开机自启（`install.nsi:338`）和 `RegisterApplicationRestart`（`WeaselServer.cpp:119`）。没有 COM LocalServer 或计划任务这类可以从受限进程唤起的机制。

### 2.5 Server→Client 推送
- **没有推送通道。** 所有数据都只在客户端请求时随响应返回。Server 虽有一个隐藏窗口 `WeaselIPCWindow_1.0`（`WeaselIPC.h:9`，`WeaselServerImpl.h:14-32`），但只处理托盘和 `WM_COMMAND`，TIP 从不使用它。
- 鼠标点选候选时，候选窗在 TIP 进程内，回调中先发 `SelectCandidateOnCurrentPage` IPC，然后对**本进程** `SendInput(VK_SELECT)`，借助一次伪按键进入 key 路径拿到 edit session（`CandidateList.cpp:389-400`，注释写着 "fix me"）。SPEC 的面板上屏路径不能沿用这种做法。

---

## 3. TSF 层（`WeaselTSF/`）

### 3.1 实现的接口（`WeaselTSF.h:11-20`，QI 见 `WeaselTSF.cpp:44-77`）
- 已实现：ITfTextInputProcessorEx、ITfThreadMgrEventSink、ITfTextEditSink、ITfTextLayoutSink、ITfKeyEventSink、ITfCompositionSink、ITfThreadFocusSink、ITfEditSession、ITfDisplayAttributeProvider。
- `ITfActiveLanguageProfileNotifySink`：**声明了，也实现了 `OnActivated`（`WeaselTSF.cpp:209-224`），但既没有 AdviseSink，也不在 QI 里，是死代码。**
- ITfCompartmentEventSink 由独立类 `CCompartmentEventSink` 实现（`Compartment.h:5`）。
- 候选 UIElement：`CCandidateList` 实现 ITfCandidateListUIElementBehavior 和 ITfIntegratableCandidateListUIElement（`CandidateList.cpp:18-41`）。
- 语言栏：`CLangBarItemButton`，使用 `GUID_LBI_INPUTMODE`，样式为 BTN_BUTTON|BTN_MENU|SHOWNINTRAY（`LanguageBar.cpp:105,374`）。

### 3.2 按键流（`KeyEventSink.cpp`）
- `OnTestKeyDown` **直接把按键发给 Server 处理**，这会改变 Rime 状态。若判定为 eaten，就置 `_fTestKeyDownPending=TRUE`；随后的 `OnKeyDown` 看到该标志就直接返回 eaten，不再发送（`:86-115`）。注释（`:76-84`）说明这是为了兼容 QQ 只发 OnKeyDown、Word 发多次 OnTestKeyDown 的情况。
- 这种做法下"Test 与 KeyDown 的判定一致"是靠"Test 阶段即执行"实现的。风险是 Test 之后 KeyDown 如果没到，状态已经被改了，而残留的 pending 标志会让下一次 Test 直接返回 eaten，不做任何处理。
- KeyUp 也会发给 Rime（带 RELEASE_MASK），Rime 的 `ascii_composer` 靠它实现 Shift 单击切换。
- `prevKeyEvent`、`prevfEaten`、`keyCountToSimulate` 是**进程级 static**（`:7-9`），多线程 UI 的应用会互相串扰。CapsLock 的补偿逻辑用 `SendInput(VK_CAPITAL)`（`:40-55`）。
- VK 到 ibus keycode 的转换在 `KeyEvent.cpp:4 ConvertKeyEvent`，可以复用。
- `_IsKeyboardDisabled()` 检查 `GUID_COMPARTMENT_KEYBOARD_DISABLED` 和 `GUID_COMPARTMENT_EMPTYCONTEXT`（`Compartment.cpp:~80-142`）。

### 3.3 Composition 与 edit session
- 所有 edit session 都用 `TF_ES_ASYNCDONTCARE`（`Composition.cpp:70,130,225,316,374,387`）。主会话就是 `WeaselTSF::DoEditSession`（`EditSession.cpp:6-61`）。它**在 edit session 里读取上一次 IPC 响应缓冲**（`GetResponseData` 读的是线程局部缓冲），再决定 Start/Insert/End composition 和 inline preedit，最后调用 `_UpdateUI`。
- 这种"按键时 IPC、稍后在 edit session 里读缓冲"的模型依赖同一线程的缓冲在两者之间不被覆盖。异步 edit session 期间如果又发生别的 IPC，比如 Echo，就会有覆盖风险。
- `OnCompositionTerminated`：Rime 仍在 composing 时只丢弃本地 composition 指针，否则调用 `_AbortComposition`（`Composition.cpp:393-414`）。
- **Deactivate**（`WeaselTSF.cpp:99-122`）：
  - 只 EndSession 并注销部分 sink，**没有主动结束 composition**。
  - `_UninitThreadMgrEventSink()` 被调用两次。
  - **`_UninitThreadFocusSink()` 没有被调用**，sink 泄漏。
- COM 方法里没有 try/catch。boost 或 STL 抛出的异常可能越过 COM 边界，只有 IPC 层的 `_SendMessage` 捕获了 `DWORD` 异常。
- `DllMain` 在 PROCESS_ATTACH 时调用 **`SetUnhandledExceptionFilter`**（`dllmain.cpp:64`），会劫持宿主进程的崩溃处理，属于不良行为，fork 后应删除。
- `DllCanUnloadNow` 的计数从 -1 起（`Globals.cpp:6`，`Server.cpp:97-101`）；`CClassFactory::AddRef/Release` 直接操作全局计数（`Server.cpp:40-48`）。基本可用，但写法不规范。

### 3.4 候选窗定位
- `_UpdateCompositionWindow` 发起一个 TF_ES_READ 的异步 session，里面调用 `ITfContextView::GetTextExt(composition range 起点)`（`Composition.cpp:159-228`）。
- 只有 `S_OK` 且坐标非零时才更新位置。**没有显式处理 TS_E_NOLAYOUT**；失败时就不更新，隐式沿用上次位置。
- 可选的 `enhanced_position` 会借助 `GetForegroundWindow()`/`GetCaretPos()` 修正越界坐标（`:187-206`）。
- `ITfTextLayoutSink::OnLayoutChange(TF_LC_CHANGE)` 会触发重新定位（`TextEditSink.cpp:59-71`）。
- 定位结果既交给本进程候选窗（`_cand->UpdateInputPosition`），也用压缩 RECT 发给 Server（`Composition.cpp:230-245`）。
- 有一个 CUAS workaround：第一次测得 rect 高度为 0 时启用（`:233-239`）。

### 3.5 UIElement
- `CCandidateList::StartUI` 调用 `BeginUIElement(this, &_pbShow, &uiid)`，只有 `_pbShow` 为真时才创建窗口（`CandidateList.cpp:285-316`）。每次更新都会调用 `UpdateUIElement`（`:268-283`）。
- 这部分已经满足 SPEC §4 中"show=FALSE 时隐藏自绘窗、改由 UIElement 提供数据"的要求。

### 3.6 Compartment / IMM32 同步（`Compartment.cpp:215-278`，`LanguageBar.cpp:402-418`）
- 监听了 OPENCLOSE 和 INPUTMODE_CONVERSION 两个 compartment。
- **OPENCLOSE**：默认模式（注册表 `ToggleImeOnOpenClose != "yes"`）下，应用关闭输入法会被解释成"切换 ascii_mode"，然后**强制把 OPENCLOSE 设回 open**（`:256-257`），并不是真正关闭。`ToggleImeOnOpenClose=yes` 时才是真开关。
- **CONVERSION**：只做 TIP→compartment 的单向写入。`_UpdateLanguageBar` 根据 ascii/full_shape 写入 NATIVE/FULLSHAPE 位。应用调用 `ImmSetConversionStatus` 触发变更时，TIP 只是重新读取 Server 状态再写回，**等于忽略了应用的请求**（`:267-276`）。SPEC §4/§7 要求双向同步，这里需要补上。

### 3.7 InputScope
- **完全没有实现。** 全仓库搜索不到 `InputScope`、`GUID_PROP_INPUTSCOPE`、`IS_PASSWORD`。

### 3.8 注册（`Register.cpp`、`imesetup.cpp`）
- 调用 `ITfInputProcessorProfileMgr::RegisterProfile` 注册 zh-CN、zh-TW、zh-HK、zh-MO、zh-SG 五个 profile，通过环境变量 `TEXTSERVICE_PROFILE` 决定启用哪一个（`Register.cpp:44-93`）。
- 类别**注册过多**（`:111-121`）：除了需要的类别外，还登记了 COMLESS、WOW16、SECUREMODE、PROP_AUDIODATA/INKDATA、PROPSTYLE_* 等不相关的类别。SECUREMODE 表示 TIP 可在安全桌面/登录界面加载，需要评估是否保留。
- `UnregisterServer` 有笔误：`"Software\\Microsft\\CTF\\TIP\\"`（`Register.cpp:9`），而且写在 HKCR 下（`:261`），这段清理代码实际不起作用。
- 安装方式：`WeaselSetup` 把 dll **拷进 System32/SysWOW64（ARM 还有 SysArm32）**，再运行 `regsvr32`（`imesetup.cpp:162-240,345-363`），然后调用 `InstallLayoutOrTip` 启用。
- ARM64 平台上 System32 里的 weasel.dll 是 ARM64X 转发 DLL（`Register.cpp:218-232`，`imesetup.cpp:201-228`）。SPEC 要求装在 Program Files，因此需要改动这一流程。

### 3.9 语言栏
`CLangBarItemButton` 的菜单项转成 `TrayCommand` IPC，或直接 `ShellExecute` 打开目录或网页（`LanguageBar.cpp:295-340`）。在宿主进程里做 ShellExecute 和 MessageBox（`WeaselTSF.cpp:13-20`、`LanguageBar.cpp:325`）都不理想。

---

## 4. Server 与 UI

### 4.1 候选窗绘制在哪里（关键发现）
**现在的 Weasel 在 TIP 进程内绘制候选窗**：`CCandidateList` 持有 `std::unique_ptr<weasel::UI> _ui`（`CandidateList.h:80`，`CandidateList.cpp:11-12,201-218,356-359`），窗口的 owner 是焦点 context 的 HWND。

Server 中也有一个 `weasel::UI`，但设置了 `InServer()=true`（`RimeWithWeasel.cpp:45`），只用来显示提示气泡：切换中英、简繁、schema 时的 tips（`WeaselPanel.cpp:148-151`）。所以 Weasel 与 SPEC §3 描述的"Host 画候选窗"并不相同，它是"**TIP 画候选，Server 只画提示**"。

这样做的原因 [推断]：
- 不具备 uiAccess 的跨进程窗口无法显示在沉浸式、开始菜单、搜索框之上。
- 窗口 owner 属于同进程时，在 UWP 中的 Z 序和定位更可靠。

### 4.2 绘制技术
- GDI+ 负责背景、圆角和阴影，阴影做高斯模糊（`GdiplusBlur.cpp`，**使用 OpenMP**）。
- 文本用 **Direct2D `ID2D1DCRenderTarget` + DirectWrite**（`DirectWriteResources.cpp:47`，`WeaselPanel.cpp:1061`）。
- 最终通过 `UpdateLayeredWindow` 输出到 layered 窗口（`WeaselPanel.cpp:1137`）。
- 窗口样式为 `WS_POPUP|WS_DISABLED`，扩展样式 `WS_EX_TOOLWINDOW|WS_EX_TOPMOST|WS_EX_NOACTIVATE|WS_EX_LAYERED(|WS_EX_TRANSPARENT)`（`WeaselPanel.h:13-15`，`WeaselUI.cpp:85-100`）；`WM_MOUSEACTIVATE` 返回 `MA_NOACTIVATE`（`WeaselPanel.cpp:282`）。
- 支持多种布局：Horizontal、Vertical、VHorizontal、FullScreen。

### 4.3 DPI
- Server 调用 `SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE)`（`WeaselServer.cpp:36`），这是 Per-Monitor **V1**，manifest 为 `PerMonitorHighDPIAware`。
- UI 通过 `GetDpiForMonitor(MDT_EFFECTIVE_DPI)` 计算 `DPI_SCALE`（`WeaselPanel.cpp:87,187`）。
- 在 TIP 中，DPI 感知级别取决于宿主进程。

### 4.4 托盘
`WeaselTrayIcon`（WTL `SystemTraySDK`）。`d73f629` 把 `Shell_NotifyIcon` 挪到 server 消息线程，以免管道线程持有 `g_api_mutex` 时与任务栏死锁（`WeaselIPC.h:38-40`，`WeaselServerImpl.cpp:134-145`）。

### 4.5 进程与会话模型
- 每个用户一个 Server。`WeaselServer.cpp:88-107` 在启动时会先让旧实例退出，并跳过 SYSTEM 账户（`:41-46`），同时调用 `ImmDisableIME(-1)`。
- **一个 TIP 实例（即每个 ITfThreadMgr/线程）对应一个 Rime session**：Activate 时 `StartSession`，Deactivate 时 `EndSession`。session 与 document/context 无关。
- `ipc_id` 由 pid 移位后加计数生成（`RimeWithWeasel.cpp:27-31,166-215`）。
- 按 `client_app`（exe 名）加载每应用选项，例如默认 ascii 或 inline_preedit（`RimeWithWeasel.cpp:1422 _LoadAppOptions`）。
- 支持 `global_ascii` 在所有 session 间同步中英状态（`:137-139,174-182`）。

### 4.6 部署
- Server 启动时 `rime_api->initialize` 后接 `start_maintenance(false)`（`RimeWithWeasel.cpp:107-147`）。
- WeaselDeployer 使用 `WeaselDeployerMutex` 互斥，通过 IPC 发 `StartMaintenance`，执行 `deploy()` 和 `deploy_config_file("weasel.yaml")`，再发 `EndMaintenance`（`WeaselDeployer/Configurator.cpp:116-156`）。
- 安装阶段执行 `WeaselDeployer.exe /install` 和 `/deploy`（`install.nsi:324,328`）。
- `prebuilt_data_dir` 等于 shared_data_dir（`RimeWithWeasel.cpp:95`），所以预编译 bin 可以直接放在 `<安装目录>\data\build`。

### 4.7 数据目录
- shared 数据：`<exe目录>\data`（`WeaselUtility.cpp:27-31`）。
- user 数据：取 `HKCU\Software\Rime\Weasel\RimeUserDir`，没有则用 `%AppData%\Rime`（`:6-25`）。
- 日志：`%TEMP%\rime.weasel`（`WeaselUtility.h:37-46`）。
- `distribution_code_name="Weasel"`，`app_name="rime.weasel"`（`RimeWithWeasel.cpp:98-100`）。

### 4.8 安装包（NSIS，`output/install.nsi`）
- 拷贝 weasel*.dll、Server、Deployer、rime.dll、WinSparkle.dll、data、opencc、7z/curl（给 plum 的 rime-install 用）。
- 执行 `WeaselSetup.exe` 注册 TIP；写 HKLM Uninstall 键、`HKLM\Software\Rime\Weasel\WeaselRoot/ServerExecutable`，以及 **HKLM\...\Run\WeaselServer** 开机自启（`:338`）；写 WER LocalDumps 键（`imesetup.cpp:450-459`）。

### 4.9 uiAccess
**没有。** 全仓库搜索不到 `uiAccess`，Server 的 manifest 只设置了 DPI 感知。

---

## 5. 与 SPEC 的差距及改造点

### 5.1 需要新增的功能与挂接位置

| SPEC 需求 | 挂接点与做法 |
|---|---|
| 触屏九宫格面板（§6） | 在 Server（改名 T9Host）里新建 `PanelWindow`，与 `WeaselServerApp::Run()` 的 `m_ui.Create` 并列（`WeaselServerApp.cpp:30`）。**不要复用 WeaselPanel**：它是 WS_DISABLED、为只读候选设计的 layered 窗口。新面板按 SPEC 用 D2D HwndRenderTarget 或 DComp 加 WM_POINTER 实现。exe 需要 uiAccess 签名，才能盖在开始菜单、UWP 之上。 |
| 面板 session（t9 schema） | 在 `RimeWithWeaselHandler` 里给每个 TIP 会话再挂一个 t9 session，或做 schema 切换（`AddSession` `RimeWithWeasel.cpp:166`）。面板按键在 Host 内直接调用 `process_key`/`set_input`，不经过 IPC 请求。 |
| **推送通道**（§3） | 新增 Host→TIP 方向。建议每个 TIP 线程额外建一条"事件管道"：TIP 后台线程阻塞读取，收到消息后 `PostMessage` 给本线程的隐藏 message-only 窗口，再在 UI 线程 `RequestEditSession(TF_ES_ASYNCDONTCARE|READWRITE)`。edit session 的回调可直接复用 `WeaselTSF::DoEditSession`（`EditSession.cpp:6`），只要把数据源从"上次响应缓冲"改成"推送的消息"。Host 侧在 `ServerImpl` 中维护 `session_id → 事件管道` 映射，并记录"当前焦点 session"（`FocusIn`/`FocusOut` 已经有了，`WeaselServerImpl.cpp:248-264`）。**同时取消 `SendInput(VK_SELECT)` 的写法**（`CandidateList.cpp:389-400`），鼠标选词也走这条推送或 edit session 路径。 |
| 拼音选择栏（§5） | 纯 Host 侧：面板 UI 加上 Rime `get_input`/`set_input`。TIP 不需要改。 |
| InputScope（§4） | 在 `ITfThreadMgrEventSink::OnSetFocus`（`ThreadMgrEventSink.cpp:12`）和 `ITfTextEditSink::OnEndEdit` 中，用 TF_ES_READ session 读 `GUID_PROP_INPUTSCOPE`（先 `ITfContext::GetAppProperty`/`TrackProperties`，再取 `ITfInputScope::GetInputScopes`），结果通过 FocusIn 的 wParam 发给 Host。`client_caps` 目前是 `/* TODO */`（`WeaselClientImpl.cpp:133`），可以直接扩展它。 |
| 自动弹出/隐藏（§6） | TIP 在 OnSetFocus 时调用 `GetCurrentInputMessageSource`，并判断是否可编辑（结合 `_IsKeyboardDisabled` 和 EMPTYCONTEXT），然后通过 FocusIn/FocusOut 上报。Host 据此决定面板显隐。 |
| T9Ctl API（§7） | 新增一种 pipe 客户端（不创建 Rime session）。现有 `TRAY_COMMAND` 加上 `WeaselServer.exe /ascii` 的命令行（`WeaselServer.cpp:74-85`）就是雏形。需要新增 SHOW/HIDE_KEYBOARD、SET_MODE 等命令，以及 visibility 广播（Host 对注册的 HWND 执行 `PostMessage(RegisterWindowMessage)`）。 |
| IMM32 双向同步（§4、§7） | 改写 `_HandleCompartment`（`Compartment.cpp:244-278`）：OPENCLOSE 按真实开关处理，默认行为改成 SPEC 语义；CONVERSION 的 NATIVE 位变化映射为 Host 的 `set_option("ascii_mode")`，写回时用 guard 防止回环。 |
| uiAccess | Host 的 manifest 加 `uiAccess="true"`，必须签名，且必须安装在 Program Files。安装路径需要从"拷贝进 System32"改成 Program Files。 |
| ITfActiveLanguageProfileNotifySink | 补上 AdviseSink 和 QI（`WeaselTSF.cpp:209`）。 |
| Host 拉起与恢复 | 把 `ShellExecute(start_service.bat)`（`WeaselTSF.cpp:263-271`，`LanguageBar.cpp:302-306`）换成可靠方案：HKCU Run、TIP 在非 AppContainer 进程中 `CreateProcess` 带参数的 Host（AppContainer 内不尝试）、Host 自身 `RegisterApplicationRestart`；可选方案是计划任务或 COM LocalServer，AppContainer 可激活，**[待验证]**。 |

### 5.2 必须删除或替换的部分（以满足 SPEC 硬约束或安全要求）
1. **TIP 中的 Boost**：改动面很小，而且协议本来就要重写。
   - `thread_specific_ptr` 换成 C++ `thread_local`。
   - `wbufferstream` 换成自写的 span reader/writer。
   - `text_wiarchive`（UIStyle、CandidateInfo）换成自定义的定长头加 TLV 二进制协议，放在 `src/common/`。
   - 建议 Host 侧一并去掉 boost（`WeaselServerImpl` 的 boost::thread 改为 std::thread，`RimeWithWeasel.cpp:80` 同理）。
2. **OpenMP**：从 WeaselUI 中去掉（`WeaselUI.vcxproj:162...`、`xmake.lua` 的 `/openmp`）。
3. **管道名和 DACL**：名称改为 `\\.\pipe\T9Ime.<UserSID>.<SessionId>`；DACL 改为当前用户、SYSTEM、AC（和 RAC），SACL 保持 Low。
4. **IPC 超时**：客户端改为 OVERLAPPED I/O 加 `WaitForSingleObject` 超时（按键建议 ≤100–200ms），超时后断开并透传按键；删除 `_Connect` 中的无限循环。这是 SPEC "Host 挂了不影响宿主"的硬性要求，**Weasel 现有代码达不到**。
5. **RECT 压缩**：改为传完整 32 位 RECT 加 DPI，消除 ±4096 的限制。
6. 删除 `DllMain` 中的 `SetUnhandledExceptionFilter`，删除 TIP 中的 MessageBox/ShellExecute；COM 入口加 `noexcept` 和 try/catch 包装。
7. 修正 Deactivate：先终止 composition，再注销 ThreadFocusSink；去掉重复调用的 Uninit。
8. 修正类别注册：只保留 SPEC 列出的类别。修正 `Register.cpp:9` 的笔误，DllRegisterServer/DllUnregisterServer 做成幂等。
9. 删除 WinSparkle、plum、7z、curl、WeaselDeployer 的 WTL 设置对话框（用我们自己的设置窗口替代），以及繁体、港澳新 profile，只保留 0x0804。
10. 构建：msbuild/xmake 改为 CMake + Ninja，属于必须的重写。工程只有 7 个模块，文件清单清楚，工作量可控。

### 5.3 建议放宽或调整 SPEC 的地方
- **候选窗放在 TIP 里（沿用 Weasel 现状）是合理的**，建议 SPEC 允许"物理键盘候选窗由 TIP 进程内绘制"：
  - D2D、DWrite、GDI+ 都是系统 DLL，不违反"除系统 DLL 外零依赖"。
  - 同进程窗口在 UWP/沉浸式中的定位和 Z 序问题更少，UIElement 与候选窗之间也没有跨进程时序问题。
  - 面板则必须放在 Host（uiAccess），因为它要跨应用持久存在，且不能抢焦点。
  - 代价是 TIP 体积变大（估计增加 300–600KB [推断]），以及候选窗样式需要从 Host 下发（Weasel 已经这样做了，走 `style=` 通道）。
  - 如果坚持"Host 统一画候选"，则必须依赖 uiAccess 才能盖住开始菜单、搜索框，而 uiAccess 只在签名且安装于 Program Files 时生效，开发期调试会很麻烦。
  - **建议分两阶段**：M3 阶段沿用 TIP 内候选窗；面板激活时隐藏 TIP 候选窗，候选改显示在面板顶栏。
- **"OnTestKeyDown 不改状态"**：Weasel 的"Test 阶段即执行并缓存结果"是业界常见做法，小狼毫多年实践下来可用。SPEC 的"判定一致"可以用"Test 阶段执行并缓存，KeyDown 直接返回缓存"来满足，但要修正 static 变量的线程问题和 pending 标志残留问题（KeyDown 时比对 wParam，不一致则重新处理）。
- **"每线程一个 Rime session"** 与 SPEC 一致，可以保留。

---

## 6. 上游健康度与 fork 策略

### 6.1 健康度
- 约 8.1k star、813 fork、267 个 open issue（GitHub API，2026-09）。
- 最近 50 个提交跨越 2025-07 至 2026-08，其中 **44 个出自同一人（fxliang）**，bus factor 约等于 1。
- 正式版停在 0.17.4（2025-06），之后只有 nightly `latest`。
- 最近的提交质量不稳：OpenMP 引入了运行时依赖；托盘死锁、`join_maintenance_thread` 卡死、"multi WeaselServer might be started"等问题都是修了又出。代码风格不统一（混有注释掉的代码和 "fix me"）。

### 6.2 建议：**硬 fork，但保留一个只读的 upstream 跟踪分支，按需 cherry-pick**
- 我们要改的正好是 Weasel 的核心：IPC 协议（去 boost、加推送、加超时）、构建系统（CMake）、注册和安装方式（Program Files、uiAccess）、会话模型（加 t9 session 和面板）、目录和命名（T9Ime）。改完后与上游的差异会覆盖 WeaselIPC、WeaselIPCServer、WeaselServer、WeaselSetup、install 的几乎全部内容，保持可合并的成本高于收益。
- 仍值得跟踪的上游改动集中在 `WeaselTSF/`（composition 与 edit session 的兼容性修复，例如 `8f2561f`、`93eec2d`、`f9203ca`）和 `WeaselUI/`（布局和 DPI 修复）。做法：
  - 保留 `upstream` remote，建一个 `vendor/weasel` 分支记录 fork 点 `d73f629`。
  - 每个里程碑执行一次 `git log vendor/weasel..upstream/master -- WeaselTSF WeaselUI RimeWithWeasel`，人工评估后 cherry-pick。
  - 目录尽量保持 WeaselTSF 与 WeaselUI 的文件名不变，以降低 cherry-pick 冲突。
- 许可证：fork 后整体为 GPL-3.0，必须公开源码并保留版权声明；librime 是 BSD，boost 如仍在 Host 中使用则是 BSL。建议在 LICENSES/ 中记录 fork 点和上游版权。

### 6.3 落地顺序建议（与 SPEC 里程碑对齐）
- **M1**：独立的 t9repl，不依赖 Weasel 代码。
- **M3**：从 Weasel 裁剪出 TIP：
  1. 迁移到 CMake；
  2. 替换 IPC 为无 boost 的新协议，加超时，改管道名和 DACL；
  3. 修复 Deactivate、类别注册、DllMain；
  4. 候选窗暂时沿用 TIP 内的 WeaselUI，并去掉 OpenMP。
- **M4**：加推送管道和 InputScope，Host 面板接入。

---

## 附：待核实清单
1. 用 `dumpbin /dependents weaselx64.dll`（nightly）确认 TIP 是否导入 VCOMP140.DLL。
2. 查看官方 librime Windows release 包附带的插件，确认是否含 librime-lua。
3. 在 Edge/Chrome 渲染进程（LPAC）中测试：Weasel 当前的 AC-only DACL 能否连接；是否需要加 `S-1-15-2-2`。
4. 在 AppContainer 进程中，能否通过 COM LocalServer 或计划任务拉起 Host。
