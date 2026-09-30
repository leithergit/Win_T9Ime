# T9Ime — 工作约定

新会话先读本文件和 `DEVLOG.md`；需求见 `Docs/SPEC.md`，设计见 `Docs/ARCHITECTURE.md`，计划见 `Docs/PLAN.md`，调研结论见 `Docs/research/`。

## 当前状态
M3（TIP：物理键盘全拼）已完成，M1–M3 真机测试包待同事回收。下一步：M4 面板经 TIP 推送上屏、InputScope、自动显隐、自动切换本 IME。

## 关键决策（覆盖 SPEC，详见 ARCHITECTURE §0）
- Weasel fork（上游 `rime/weasel@d73f629`），GPL-3.0。
- **不用 uiAccess**；单一 T9Host 进程（引擎 + 面板）；物理键盘候选窗在 TIP 进程内绘制。
- 支持 Win7 SP1 与 Win10/11（**不支持 Win8.x、ARM64**），x86 与 x64 都是完整构建；`_WIN32_WINNT=0x0601`，Win8+ API 一律动态加载。
- **主要目标设备是公司的 Win7 触屏机**；开发机无触屏，触摸行为由同事真机测试——每个里程碑交付测试包 + `Docs/testing/` 清单。鼠标与触摸必须走同一面板代码路径。
- librime 1.17.0 官方包（内置 librime-lua）；rime-ice 完整词库，固定 commit `3aea6d3694fb3d94ec663641f021f788822897ad`。
- t9 方案是**数字码**（不是 SPEC 写的大写字母码）；`t9_processor` 用 t9.custom.yaml 移除。

## 构建
- 一键：`pwsh -File build.ps1 -Preset x64-Release`（进入 VS2022 开发环境 → cmake 配置 → 构建 → ctest）；`-NoTest` 跳过测试，`-Fetch` 重新下载依赖。预设：`x64/x86-Debug/Release`，产物在 `out/build/<preset>/`。
- 从 Bash 调用时用 `pwsh -NoProfile -File build.ps1 ... > log 2>&1` 再 grep，直接在 PowerShell 工具里跑会因 throw 丢输出。
- 依赖：`third_party/fetch_librime.ps1`（librime 1.17.0 x64/x86 + opencc，SHA-256 校验）、`data/fetch_rime_ice.ps1`（rime-ice 固定 commit）。两者产物不入库。
- 产物布局与安装目录一致：`out/build/<preset>/bin/{T9Host.exe,t9repl.exe,rime.dll,data/}`。Rime 数据由构建目标 `rime_data`（`tools/prepare_data.py`）生成到 `bin/data`（首次约 45 s，有 stamp 缓存）。
- 测试：默认 `ctest -LE e2e`；`e2e_panel`（`tests/e2e/panel_e2e.py`，用真实鼠标点面板往 test_target 输入，会移动光标，需要交互桌面）用 `ctest -L e2e` 单独跑。其余：`unit`（doctest）、`regress_*`（`tests/regress/*.t9` 经 t9repl 与 `.expected` 快照比对；改动后用 `run_regress.py --update` 重写并审阅 diff）、`win7_imports`（`tools/check_imports` 检查 Win7 不存在的静态导入）。
- TIP 开发注册：`pwsh -File tools/dev_register.ps1 [-Unregister]`（会弹 UAC；注册 x64 与 x86 的 `out/build/*/bin/T9Tip.dll`）。注册后 DLL 被各应用加载而锁定，构建时 `tools/move_locked.py` 在链接前把旧 DLL 改名为 `.old-*`。
- TIP 端到端：`py -3 tests/e2e/tip_e2e.py --host <x64 bin>/T9Host.exe --target <x64|x86 bin>/test_target.exe`（`ctest -L e2e` 也会跑；未注册时跳过）。构建前先 `taskkill /im T9Host.exe /f`，否则 exe 被占用。
- 真机测试包：`pwsh -File tools/make_test_package.ps1 -Milestone Mx` → `dist/Mx/`，清单写在 `Docs/testing/`。
- T9Host 调试参数：`--data --user --settings --show --input pointer|touch|mouse --dump-layout <json> --no-single-instance`。
- 引擎调试：`out/build/x64-Release/bin/t9repl.exe --data <data> --user <dir> [--fresh] [--script f]`，命令见文件头注释。

## 编码规范
- TIP：除系统 DLL 外零依赖；禁止 boost/.NET/Qt/C++WinRT；所有 COM 方法 `noexcept` 且内部 try/catch；不在宿主进程做耗时操作、不弹 MessageBox、不 ShellExecute。
- Host：只依赖 librime 和系统库；librime 调用只在引擎线程。
- 不记录任何用户输入内容；日志仅 Debug 构建且默认关闭。
- 禁止全局键盘钩子与 DLL 注入；TIP 所在线程的线程级 `WH_GETMESSAGE` 钩子已获允许。
- 遇到 API 行为不确定，先在 `tests/probes/` 写最小复现，不要猜。

## 已知陷阱
- librime 的 maintenance 按文件 mtime 判断是否重建；zip（2 秒精度、本地时区）解压后会触发约 7 s 的重建。因此 **RimeEngine 默认不跑 maintenance**（`Options::maintenance=false`），直接用预部署数据。代价：用户改配置后必须显式重新部署；升级时要清理用户目录 `build/`（staging 会遮蔽新的预部署数据）——M6 处理。
- 英文九键是独立方案 `t9_eng`（`data/custom/t9_eng.schema.yaml`，xlit 数字码，单独 prism）。不要把 melt_eng 挂进 t9：非主翻译器的 prism 不会被部署，而且 derive 会让英文 prism 膨胀。
- `RimeContext.composition.cursor_pos/sel_*` 是 preedit 的 **UTF-8 字节偏移**。
- 官方 librime 无 `t9_processor`：回车会上屏原始数字、退格逐字母删，需前端实现语义。
- rime-ice 不带 opencc s2t 数据，需从 rime-deps 包补。
- Weasel 的 WeaselUI 静态导入 Shcore（Win8.1+），且开启了 OpenMP（vcomp140.dll）——都必须去掉/改动态加载。
- Windows 大小写不敏感：文档目录是 `Docs/`。
- **Win8+ 输入法默认按用户全局生效**：在前台进程里用 `TF_IPPMF_FORPROCESS` 激活 T9Ime，会切换用户在所有程序里的输入法。test_target 激活前记下原 profile、关闭时恢复；写新测试时照做。控制台进程里的 `GetActiveProfile` 不代表用户当前的输入法。
- TIP 必须放行 `VK_PACKET`（SendInput 的 KEYEVENTF_UNICODE 文本），否则面板降级路径和屏幕键盘会被当成按键组字。
- 在按键回调之外（窗口消息里）应用结果时，先请求 `TF_ES_SYNC` 编辑会话，失败再异步；否则 CUAS 文档里的异步会话可能拖到下一次按键才执行。
- 测试脚本要先 `SetProcessDpiAwarenessContext(-4)` 再取窗口坐标，否则非 DPI 感知的脚本拿到逻辑坐标，点击会偏。用点击而不是 `SetForegroundWindow` 让测试窗口获得焦点（后者受前台锁限制）。
- 面板鼠标输入：先 `PointerUp` 再 `ReleaseCapture`——`ReleaseCapture` 触发的 `WM_CAPTURECHANGED` 会取消按下。
- `Session::State()` 会取走待上屏文字，引擎线程命令里不要调用它（用 `Input()/HasInput()`）。
- 面板的所有输出（上屏、退格/回车透传、直接符号）都经引擎线程的 Passthrough 排队，保证顺序；不要在 UI 线程直接 SendInput。
- `_WIN32_WINNT=0x0601` 下 SDK 不声明 WM_POINTER/WM_DPICHANGED 等，常量在 `src/common/win_compat.h`。
- 命令行输出中文用 `py -3 C:\Users\leith\.claude\tools\enc.py`，避免 cp936 乱码。
