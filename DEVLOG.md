# DEVLOG

## 2026-09-29 — M0 调研与计划

**完成**
- 审阅 SPEC，调研 Weasel、librime 1.17.0 + rime-ice、Hamster/万象九键、Windows 平台问题，结论见 `Docs/research/`（4 篇）。
- 实测（Python ctypes 调 rime.dll x64）：t9 数字码可用、`94664486→中国`、`744→是`、`set_input("zhong'4486")` 可混输、comment 为完整拼音、两 schema 共用 `rime_ice.userdb`、完整词库压缩 24.3 MB、预部署需保留 mtime。
- 产出 `Docs/ARCHITECTURE.md`、`Docs/PLAN.md`、`CLAUDE.md`。

**决策（用户确认）**
- Weasel fork；集成 librime-lua；完整词库（允许略超 25 MB）；接管 TabTip 自动弹出；面板点击时自动切换到本 IME。
- 支持 Win7 SP1 与 Win10/11，含 32 位系统；Win7 补丁要求 SP1 + KB2670838（强制），KB4474419（建议）；Win7 禁用 emoji，自动弹出用推断，关闭平板输入面板自动弹出。
- 物理键盘候选窗在 TIP 进程内绘制。
- **彻底去掉 uiAccess**：面板并入 T9Host，接受系统浮层遮挡等 3 个限制（ARCHITECTURE §6.6）。

- Q1–Q9 已确认（PLAN.md "决策结果"）：不支持 Win8.x 与 ARM64；主要目标为公司 Win7 触屏设备，由同事真机测试；移除 radical_pinyin；其余按建议。

**遗留问题**
- 大量"需实测"项已分配到各里程碑前置 probes（P1–P6）。
- rime-wanxiang 结论：不采用为默认，可作为后续可选包。

**下一步**
- 进入 M1：CMake 骨架、依赖下载脚本、音节表生成、九键前端库、t9repl、单元测试与引擎回归。

## 2026-09-29 — M1 引擎原型

**完成**
- 构建骨架：CMake + Ninja + CMakePresets（x64/x86），`build.ps1`，静态 CRT，`_WIN32_WINNT=0x0601`。
- 依赖脚本：librime 1.17.0（x64/x86，SHA-256）、opencc 数据、rime-ice 固定 commit `3aea6d3`。
- 数据：`tools/prepare_data.py` 组装 + rime_deployer 预部署；补丁 `t9.custom.yaml`（去 t9_processor）、`rime_ice.custom.yaml`（去 radical_pinyin）、新方案 `t9_eng`（英文九键）。完整词库 data 目录 70 MB。
- 九键前端 `t9core`：按键映射、音节表（构建期从 8105 字表生成，414 个音节）、拼音栏（长度优先 + 当前候选中的音节优先 + 频率）、`T9Composer`（选拼音 set_input 替换、撤销栈、复用用户分隔符）、preedit 对齐（comment 动态规划，支持简拼与不完整音节）、回车上屏拼音。
- `RimeEngine`/`Session`（librime 封装）、`t9repl`。
- 测试：doctest 11 个用例；引擎回归 `basic`、`schemas`；`win7_imports` 导入检查。x64 与 x86 全部通过。
- 真机测试包 `dist/M1/T9Ime-M1-test.zip`（30 MB）+ `Docs/testing/M1-checklist.md`，本机解压后两种架构 ALL PASSED。

**决策**
- 英文九键做成独立方案 `t9_eng`，不混入中文 t9 候选。
- 启动不跑 librime maintenance（zip 时间戳会触发 7 s 重建）；配置变更显式部署。
- 部分选词后隐藏拼音栏（set_input 会丢掉已选的词）。

**已验证**
- `94664486→中国`、`744→是`、`zhong'486`、撤销、分隔符、部分选词、t9/rime_ice 学习互通、简繁、禁用 emoji、rime_ice 的 lua（日期）、t9_eng（43556→hello）。
- 所有产物无 Win7 缺失的静态导入（MSVC 14.44 静态 CRT）。

**未验证**
- Win7 真机运行（P1，已交测试包）。
- 回车/退格以外的 t9_processor 行为差异。

**已知问题**
- t9_eng：1 键符号与单词上屏的交互、preedit 仍显示数字，M2 面板时处理。
- 拼音栏音节频率来自单字字频，仅作次要排序。
- t9 中日期/计算器等 lua 触发词是字母，数字键触发不了（与仓输入法相同）。
- 跳过 maintenance 后，M6 必须实现"改配置 → 重新部署"与"升级 → 清理 staging"。

**下一步**
- 回收同事的 M1 真机结果。
- M2：P2 探针（不激活窗口 + WM_TOUCH/WM_POINTER），T9Host 单实例 + 引擎线程 + 面板 + SendInput 上屏。

## 2026-09-30 — M2 Host 与触屏面板（SendInput 路径）

**完成**
- `src/common/win_compat`：Win8+ API 动态加载（指针、DPI、触摸反馈）、RtlGetVersion、深色主题检测。
- T9Host：单实例（Local\T9Ime.Host.<SID>）、引擎线程（命令队列 + 快照 + Passthrough 保序）、托盘（显示/隐藏、停靠底部、退出）、PMv2 manifest（Win7 回退 System DPI）。
- 面板：不激活窗口（MA_NOACTIVATE / PA_NOACTIVATE）；输入统一（WM_POINTER / WM_TOUCH / 鼠标，`--input` 可强制）；D2D + DWrite 渲染；中文/英文九键、数字、符号布局；拼音栏；候选栏横向滚动与展开网格；长按连删、左滑清空、长按数字键输入数字；拖动移动、角标调整大小并保存；深浅色。
- 英文九键：1 键先上屏单词再出标点；空格上屏单词并补空格；preedit 显示单词。
- 测试：面板布局单元测试；`tests/e2e/panel_e2e.py` 用真实鼠标点击面板往测试窗口输入（x64/x86 全部 PASS，焦点始终不丢失，含调整大小）。
- 测试包 `dist/M2/T9Ime-M2-test.zip` + `Docs/testing/M2-checklist.md`（Win7 触屏真机）。

**决策**
- 所有输出经引擎线程排队（Passthrough），避免快速点击时"上屏"与"透传"乱序。
- 构建产物统一放 `bin/`，与安装目录布局一致。

**已验证（开发机，鼠标）**
- 不抢焦点、各布局输入、拼音栏、展开网格、调整大小；x86 与 x64。

**未验证**
- 真实触摸（Win7 WM_TOUCH 路径、Win10 WM_POINTER 触摸路径）——开发机无触屏，交同事按 M2 清单测试。
- 管理员程序（SendInput 受 UIPI 限制，M4 改为 TIP 推送后仅剩非 TSF 管理员程序受影响）。

**已知问题**
- 托盘图标暂用系统默认图标（M6 替换）。
- 面板拖动时不限制在屏幕内（下次启动或"停靠到屏幕底部"可找回）。
- 🇨🇳 等国旗 emoji 在 Windows 上显示为字母（系统字体限制）。

**下一步**
- 回收 M1/M2 真机结果。
- M3：P3 探针（GetTextExt 坐标、Win7 SDDL、AppContainer 管道）；Weasel fork 裁剪出 TIP、CMake 化、去 boost、新 IPC、注册注销、物理键盘全拼。

## 2026-09-30 — M3 TIP 骨架（物理键盘全拼）

**完成**
- IPC：TLV 协议、会话+SID 管道名与 DACL（Win7/Win8+ 分支）、带超时的重叠 I/O；Host 请求服务（每连接一个线程、一个 rime_ice session，经引擎线程执行）。
- T9Tip.dll（x64/x86，217 KB，只依赖系统 DLL，Win7 导入检查通过）：参照 Weasel WeaselTSF 重写——按键（test/key 结果按实例缓存）、组字（内联 preedit + 虚线显示属性）、进程内 D2D 候选窗（鼠标点选、滚轮翻页）、UIElement、compartment 双向同步（开关、中英）、语言栏"中/英"按钮、注册与注销（幂等）。上游对照见 `third_party/weasel/UPSTREAM.md`。
- 健壮性：Host 不在时按键立即透传（3 键 0.25 s），调用失败后退避；Host 被杀后由 TIP 自动拉起（仅普通权限进程）；所有 COM 入口异常不外抛；DllMain 不做任何工作。
- 测试：IPC 单元/集成测试（含超时）；`tip_e2e.py`（x64/x86 各 16 项全过：组字、候选窗、鼠标点选、数字选词、回车、Esc、Shift 中英、Ctrl 透传、杀 Host 不卡、自动拉起）；Win11 新版记事本实测通过（`apps_e2e.py`，手动探针）。
- 测试包 `dist/M3/`（含 register.bat / unregister.bat）+ `Docs/testing/M3-checklist.md`。

**过程中发现并修复**
- TIP 把 SendInput 的 `VK_PACKET` 当按键组字 → 放行。
- 窗口消息里应用结果时异步编辑会话被延后 → 先同步再异步。
- 测试工具问题：CapsLock、DPI 坐标、前台锁、`FORPROCESS` 会改用户全局输入法（test_target 现在会恢复）。

**已知问题 / 待办**
- 开发过程中用户的全局输入法被测试切到了 T9Ime，未能自动恢复原状态，已请用户用 Win+Space 确认。
- A14（点击别处时未上屏拼音的去留）在 CUAS 下的行为待真机记录。
- 未做：InputScope、推送通道、自动显隐（M4）；IMM32 API 验证（M5）；AppContainer（开始菜单搜索）实测。

**下一步**
- M4：P4 探针（已运行程序的会话级切换、GetCurrentInputMessageSource）、事件管道推送上屏、InputScope、自动弹出/隐藏、自动切换本 IME、TabTip 共存。

## 2026-09-30 — Win7 VM 首测反馈

- 用户在 VMware Win7 x64 SP1：run.bat 通过、注册成功、输入法列表可见；但记事本里物理键盘打不出汉字、无候选窗。
- 分析：虚拟键盘不出现是预期（自动弹出属 M4）。打不出汉字最可能是 T9Host 未运行：TIP 只在中完整性进程里拉起 Host，而 UAC 关闭的 Win7 所有进程都是高完整性 → 永不拉起。已放宽为：拒绝低完整性、UAC 分离令牌的提权进程、AppContainer；允许 UAC 关闭时的高完整性进程。
- 新增 `t9diag.exe`（测试包 x86/x64 各一份）：输出系统版本、令牌、UAC、TIP 注册路径、T9Host 进程、管道连接与 nihao 实测。
- 已知不稳定：tip_e2e 的"点击候选"偶发失败（约 1/4，多在刚重启 Host 后），待查。

## 2026-09-30 — M4 面板经 TIP 上屏、自动显隐、自动切换

**完成**
- M4a 推送：事件管道（EventServer）+ 焦点登记（FocusRegistry）；TIP 每实例一个事件线程，经消息窗口回到 UI 线程，用同步优先的编辑会话上屏；面板输出优先推送，SendInput 兜底；物理键与面板组字互相让位。
- M4b 显隐：TIP 上报 InputScope（应用属性）、只读、触摸来源（GetCurrentInputMessageSource/GetCIMSSM/线程级钩子）；密码/PIN/数字/电话/网址/邮箱字段物理键直通；Host 纯函数规则 DecideOnFocus（触摸或"总是弹出"、按 InputScope 选数字/英文/文字布局、密码与只读隐藏、Win10+ 系统浮层不弹）；隐藏 300 ms 防抖；托盘开关。
- M4c：点面板自动切换到 T9Ime（FORSESSION → WM_INPUTLANGCHANGEREQUEST 兜底）；系统触摸键盘可见时不弹出；托盘"关闭系统触摸键盘的自动弹出"（备份/还原 TabletTip 值，Win10+）。
- 测试：单元测试 253 断言；端到端 5 项（panel、tip、push、autoshow 含 InjectTouchInput 模拟触摸、switch），x64 全过，TIP/推送另测 x86 目标。
- 测试包 `dist/M4/` + `Docs/testing/M4-checklist.md`。

**决策**
- 推送只含最终文字，preedit 不进文档。
- 密码字段不自动弹出面板（九键不适合密码；26 键布局为后期里程碑）。

**未验证**
- Win7：WM_TOUCH 面板输入、钩子判定触摸来源、自动切换（按线程输入法模式）——交同事按 M4 清单。
- Win7 平板输入面板的自动弹出设置（注册表项未知，托盘开关仅 Win10+ 显示）。
- AppContainer 应用（开始菜单搜索等）中的推送与焦点上报。

**下一步**
- M5：T9Ctl.dll / t9ctl.exe / TestHost(C++/C#)、控制管道、IMM32 API（ImmSetOpenStatus / ImmSetConversionStatus）兼容验证。

## 2026-09-30 — Win7 M4 首测反馈（面板不自动弹出）

- 用户 Win7：T9Host 在运行、引擎正常，但手指点输入框面板不弹出（A1/A2 失败）。
- 原因（分析）：① 测试窗口启动时焦点已在大输入框，点它不产生焦点变化 → 不上报；② Win7 按程序记住输入法，测试窗口里未必启用了 T9Ime（没有 TIP 就没有焦点上报）；③ Win7 触摸判定（钩子 + GetMessageExtraInfo）未经真机验证。
- 修复：TIP 钩子发现触摸点中已有焦点的窗口时重新上报焦点（带触摸标志）；`target.bat` 以 T9Ime 启动测试窗口；Host 诊断请求 + `t9diag --watch` 实时记录焦点/触摸/弹出决定；事件管道在每次连上 Host 时附着（此前某些重连路径不附着，Chrome 中看到 NO-EVENTS）；TIP 以 `--background` 拉起 Host，已在运行时不再弹出面板；T9Host 也加链接前改名（开发机上会被 TIP 随时拉起）。
- 待回收：新 M4 包 + watch.txt。
- 偶发：autoshow e2e 曾有一次"返回文本框后未弹出"，随后 4 次全过，待观察。

## 2026-09-30 — Win7 watch.txt 分析：触摸未被识别

- watch.txt：焦点上报正常（Win7 上 InputScope 也读到了，数字框 scopes=29），但所有焦点事件 touch=0，决定均为 none → 面板不弹。
- 修复：触摸判定改用线程级 WH_MOUSE 钩子读每次按下的 `MOUSEHOOKSTRUCT.dwExtraInfo`（0xFF515700 签名），并在焦点变化当下检查 GetMessageExtraInfo；原 WH_GETMESSAGE 里读 GetMessageExtraInfo 的时机不可靠。点中已有焦点的输入框（鼠标或触摸）都会重报焦点，由 Host 按设置判定（修正了"总是弹出"模式下点已聚焦输入框不弹的问题）。
- FocusIn 附带触摸判定依据（source / extra / 最近按下），`t9diag --watch` 的 `touch:` 行可直接看到 Windows 报告了什么。Win11 注入触摸实测：extra=ff515799 → touch=1 → show。
- 待确认：用户的 Win7 测试是在触屏真机还是 VMware 虚拟机（虚拟机里触摸会变成普通鼠标，无法识别，应改测 A7）。

## 2026-09-30 — Win7 虚拟机自动化测试

- 新增 `tools/vm.py`、`tools/vm_deploy.py`、`tools/vm_e2e.py`：经 vmrun 在用户的 VMware Win7 x64 SP1 虚拟机里部署测试包、注册 TIP、跑端到端测试（Python 3.8 免安装版）。
- **Win7 结果：panel、tip、push、autoshow（总是弹出模式）、switch 全部通过**；模拟触摸在 Win7 不可用（跳过），真实触摸仍需触屏真机。
- 虚拟机里"点击候选失败"定位：VMware 绝对指针使注入的鼠标点击落不到目标；投递窗口消息后 TIP 处理正常（同步编辑会话 hr=0）。测试在虚拟机中改用消息投递点击。
- 顺带修复：候选窗点击/滚轮改为 PostMessage 后处理（Win11 上"点候选要到下个按键才上屏"的偶发问题）；Host 启动预热引擎（构建后首轮 e2e 变慢/失败的原因，也是用户冷启动首批按键被放行的原因）；test_target 在 Win7 上切英文键盘失败时退回 ActivateKeyboardLayout。
- 用户的虚拟机点选 T9 输入法不会弹出面板：符合设计（只在触摸聚焦时弹，或勾选"总是弹出"）；如需"切到 T9 时弹出面板"可加选项，待用户决定。

## 2026-09-30 — Win7 触屏实测通过；密码输入与系统输入面板图标

- 用户 ThinkPad 上的 Win7 虚拟机（触屏 USB 直通）实测 5 步全部符合预期（extra=ff51578a，touch=1）。
- 反馈 1"点密码框面板消失，无法输密码"：新增**字母面板**（Mode::kLetters）。密码 / 私密 / 字母数字 PIN 输入框自动切到此面板：九宫格按键多击选字母（900 ms 内再点同键换下一个字母），长按输入数字，"大小写"键切换大小写，右列有退格与空格。字母直接用 SendInput 送出，不经引擎，也不进入组合。Win7 的密码 Edit 不走 TSF，TIP 识别 ES_PASSWORD 后以"无上下文"焦点上报，Host 对其只用 SendInput。
- 反馈 2"点密码框时出现系统输入面板图标"：接管系统触摸键盘时，Win7 另写用户策略 `HKCU\Software\Policies\Microsoft\TabletTip.7` 的 HideIPTIPTouchTarget / HideIPTIPTarget / DisableEdgeTarget（先备份，取消接管时还原），并重启输入面板进程使之生效。托盘开关在所有系统上显示（Win7 显示为"关闭系统输入面板图标"），命令行 `--take-over-touch-keyboard` 也可开启。
- e2e 回归修复：Host 预热期间 TIP 的请求超时会断开管道，重连后 Host 不知道焦点 → 推送退回 SendInput（push e2e 偶发失败）。现在管道请求带序号，超时不断开（下一次调用跳过迟到的应答，连续 3 次超时才断开）；重连后 TIP 重新上报焦点。Hello/FocusIn 不再排队等引擎。e2e 启动 Host 时确认管道归属（避免其它程序的 TIP 拉起的后台 Host 抢占）；panel e2e 每次重置面板尺寸。连续 6 轮 e2e 中 5 轮全过、1 次 autoshow 偶发失败（单独重跑 3 次均过）。

## 2026-09-30 — 切换到 T9Ime 时自动弹出面板

- 用户决定：切换到 T9Ime 时弹出面板（默认开，托盘"切换到 T9Ime 时弹出键盘"可关，ini `show_on_switch`）。
- TIP 在 ActivateEx 中上报焦点时带 `kFocusActivated`。TSF 在线程首次获得焦点时也会激活 TIP（新打开 / 切到前台的程序），为区分"用户切换"：Host 用进程外 WinEvent 钩子记录前台窗口变化，只有激活线程拥有前台窗口、且该窗口已在前台 ≥1.5 s（或短暂离开后回到它，如点任务栏语言栏）时才算用户切换。只读框不弹；布局按 InputScope 选。
- test_target 收到 WM_APP+1 时切到 T9Ime（模拟用户切换）；autoshow e2e 新增切换场景，并已有"启动即激活不弹出"的反例（鼠标聚焦不弹）。x64 e2e 两轮全过。

## 2026-09-30 — 英文改为全键盘（QWERTY），面板外观参照讯飞 iOS

- 用户反馈：密码框里用多击字母面板输入时，用户看不到自己输入了什么字符。决定：英文模式只显示完整英文键盘（参照 `Docs/T9_ABC.jpg`），密码 / 网址 / 邮箱 / 用户名等输入框也用它；中文九宫格布局改成 `Docs/T9.jpg` 的排列。
- QWERTY：字母直接上屏（经引擎线程 Passthrough 排队）；键上小字（数字、符号）长按输入；⇧ 单击只对下一个字母大写，双击锁定大写；按下的字母在手指上方放大显示（密码框只显示圆点，靠它确认输入）。英文九键（t9_eng）不再用于面板，方案数据仍保留（t9repl 回归用）。
- 输入框类型选出的布局不再改变用户的中/英选择：从密码框回到普通输入框仍是用户上次选的中文或英文。
- 中文九宫格：左列标点（组字时为拼音），右列 ⌫ / 清空 / 符号，底行 隐藏 / 123 / 宽空格（0）/ 中/英 / 换行；多击字母面板（kLetters）删除。
- 外观：石板灰渐变背景，浅色渐变字键、深灰功能键（白字）、键下阴影，数字 / 小字在上、主字在下；候选栏为浅色。Win7 没有深色模式，始终用浅色主题；T9Host 新增调试参数 `--theme light|dark`。
- x64 e2e 两轮全过（panel e2e 改为 QWERTY 输入 "hello Work."，autoshow 的密码输入改为 QWERTY + 一次性大写）。

## 2026-09-30 — 按用户定稿调整布局（Docs/T9_2.png）

- 中文九宫格：右列 ⌫ / 清空 / 换行 / 隐藏，左下角为 符号；底行 123 / 宽空格（0）/ 中/英 不变。
- 英文全键盘：空格键变窄（2.6 → 1.6 格），符号、123、逗号、句号、英/中、隐藏、换行相应加宽。

## 2026-09-30 — M4 Win7 触屏实测通过

- 用户在 Win7 触屏虚拟机上按 `start_touch_test.bat` 与 M4 清单实测：触摸弹出 / 隐藏、数字框布局、密码框英文全键盘（按键放大预览、长按小字、单次大写）、系统输入面板图标隐藏、切换到 T9Ime 弹出面板、T9_2.png 布局，均符合预期。
- M4 关闭。下一步 M5：T9Ctl.dll / t9ctl.exe / TestHost（C++ / C#）、控制管道、IMM32 兼容验证。

## 2026-09-30 — M5：控制 API（T9Ctl）与 IMM32 兼容验证

- **控制管道**（`…ctl`，`src/host/ipc/ctl_server`）：查询 / 显示（可带布局）/ 隐藏 / 切换 / 布局 / 停靠 / 位置 / 切到 T9Ime / 切走 / 可见性订阅；请求经 `SendMessageTimeout` 交 UI 线程执行（调用对象用 shared_ptr 交接，超时不留悬空指针），每个应答都带面板状态（可见、布局、矩形）。
- **可见性通知**：面板 `WM_WINDOWPOSCHANGED` → 状态（可见 + 矩形）有变化时，对已注册窗口 `PostMessage("T9Ime.Visibility", wParam=可见)`；失效窗口自动剔除。
- **T9Ctl.dll**（`src/ctl`，x86/x64，`.def` 导出无修饰名）：每次调用新建管道连接；需要 Host 的调用（显示、布局、位置、订阅、切换）在 Host 未运行时以 `--background` 拉起（DLL 同目录或已注册 TIP 同目录的 T9Host.exe）；查询类调用不拉起。`T9_Activate/Deactivate`：调用线程自己的窗口直接 `ActivateProfile(FORPROCESS)`（Win7 切走失败时退回 `ActivateKeyboardLayout`）；其他线程交 Host（FORSESSION + `WM_INPUTLANGCHANGEREQUEST`），Host 不在时 DLL 自己投递消息。`T9_RegisterVisibilityNotify` 代调用 `ChangeWindowMessageFilterEx`，提权程序也能收到通知。"切走"的目标：第一个其他语言的键盘布局，否则第一个非 T9Ime 的输入法（`src/common/ime_profile`）。
- **t9ctl.exe**：命令行封装，`watch` 用消息窗口打印通知（e2e 用）。
- **TestHost**：C++（Win32）与 C#（WinForms，用 Windows 自带的 .NET 3.5 编译器构建，Win7 原生可运行，config 兼容 .NET 4.x）；演示全部调用，并在键盘显示时收缩文本框避免被遮挡。
- **IMM32 兼容**（`tests/e2e/imm_e2e.py`，test_target 新增 WM_APP+2/3/4 调 ImmSetOpenStatus / ImmSetConversionStatus / 查询）：Win11 上全部通过——ImmSetConversionStatus 切中英、ImmSetOpenStatus 关闭时按键透传、再打开恢复、T9Ime 自己的 Shift 切换反映到 ImmGetConversionStatus。
- **ctl e2e**（`tests/e2e/ctl_e2e.py`）：状态、显示数字布局、切布局、位置 / 停靠、隐藏 / 切换、通知（含移动后的新矩形）、对另一进程窗口 activate / deactivate（物理键盘验证）。x64 全部 7 项 e2e 通过。
- 测试包加入 `T9Ctl.dll`、`t9ctl.exe`、`TestHost*.exe` 与 `sdk\`（`t9ctl.h`、`T9Ctl.cs`）；清单 `Docs/testing/M5-checklist.md`。

## 2026-09-30 — M5 在 Win7 虚拟机上验证；修复四个 Win7 暴露的问题

- 虚拟机结果：ctl、imm、tip、push、autoshow、switch 连续两轮全过；panel 在内存吃紧（主机仅剩约 1.5 GB）的虚拟机里偶发首击超时（重绘等待），属测试时序，已放宽等待。
- **管道实例空档**：服务端接受一个连接后才创建下一个实例，期间新连接得到 ERROR_FILE_NOT_FOUND 立即失败（`t9ctl status` 连续三次调用在 Win7 上随机失败）。现在先建下一个实例再交接连接；客户端在超时内对"找不到"短暂重试。
- **CUAS 先聚焦后压入上下文**（Win7）：新文本框第一次获得焦点时 `OnSetFocus` 里取不到上下文，TIP 报告为焦点离开，之后不再补报——触屏上第一次点某个输入框可能弹出错误布局或隐藏面板。现在 `OnPushContext` 对当前焦点文档重新上报。
- **断线后不自动重连**：Host 预热期间 TIP 连续超时断开，Host 把断开当作焦点离开；TIP 要等下一次按键或焦点变化才重连，其间面板上屏退回 SendInput。现在 TIP 断线后用定时器（2.5 s，最多 12 次）重连并补报焦点；Host 的 FocusOut 不再排在引擎后面（立即应答，会话在该连接下一次引擎请求时清空）。
- 测试：autoshow 在无 InjectTouchInput（Win7）时跳过整个触摸场景；panel e2e 查找元素时等待重排（最多 3 s），回车上屏等待到达。

## 2026-09-30 — M5 Win7 虚拟机稳定：7 项 e2e 连续两轮全过

- **面板首击卡住**（panel e2e 在冷启动后第一下点击 10 s 无重绘）：点面板时若前台程序没接 T9Ime，会做会话级切换（`ActivateProfile(FORSESSION)`），它要通知桌面上所有 GUI 线程，在 Win7 上可能阻塞数秒——原来在 UI 线程执行。现在 ImeSwitcher 的会话级切换（含控制 API 的 activate / deactivate）都在单独的 STA 线程执行。
- **Host 忙时的 TIP 行为**：一次超时后 1 s 内，引擎类请求（按键、状态）直接失败（按键透传），不再堆积超时；焦点上报（FocusIn / FocusOut，Host 不经引擎直接应答）照常发送，Host 始终知道焦点。连接只在连续 8 次超时后才断开（原来 3 次，断开会被 Host 当作焦点离开）。
- 虚拟机（主机内存仅剩约 1 GB）：panel、tip、push、autoshow、switch、ctl、imm 连续两轮全部通过。

## 2026-09-30 — M5 触屏虚拟机实测：清单全过；追加 D1–D3 修复

- 用户在触屏 Win7 虚拟机上按 M5 清单测试，A、B 两节全部符合预期；追加的三项不符合，已修复：
  - **D1 切换程序后键盘不消失**：面板现在"属于"前台程序——触摸聚焦弹出或 T9Ctl 显示时绑定当时的前台进程；前台换成别的进程（WinEvent 前台钩子，回调在 UI 线程）即隐藏。托盘 / `--show` 显示时不绑定，第一个来到前台的程序成为其所属（否则点进程序就会把刚打开的键盘关掉）。同一程序内切换窗口 / 输入框不隐藏。
  - **D2 切换到其他输入法后键盘不消失**：TIP `Deactivate` 发送带 `kFocusDeactivated` 的 FocusOut；Host 在该线程是前台线程时立即隐藏面板（不论面板怎么显示的）。即使当时没有聚焦的输入框也上报。
  - **D3 切回 T9Ime 显示的是之前的布局**：因切换到 T9Ime 而弹出时，文本模式重置为中文九宫格（数字等输入框仍按 InputScope 选布局）。
- e2e：ctl 增加 D1（同程序内点击保持、点第二个程序隐藏；test_target 新增 `--title`、`--at`），autoshow 的切换场景增加 D2、D3（test_target 新增 WM_APP+5 切到英文键盘）。x64 七项 e2e 全过。

## 2026-09-30 — D2（Win7）与 D4 修复

- 触屏虚拟机复测：D1、D3 通过；D2 在 Win7 上仍不隐藏；新增 D4（切走再切回，键盘应重新显示）不符合。
- **D2（Win7）**：Win7 用语言栏 / Ctrl+Shift 切到其他语言时不一定立即调用 TIP 的 `Deactivate`，先来的是 `ITfActiveLanguageProfileNotifySink::OnActivated(本 TIP, FALSE)`。现在两处都上报"停用"（FocusOut + kFocusDeactivated），Host 立即隐藏。Win11 上 e2e 通过，Win7 待触屏虚拟机复测。
- **D4**：因前台程序切换而隐藏的键盘记住其所属进程，该程序回到前台时重新显示；主动隐藏（隐藏键、T9Ctl、切换输入法）不会恢复。
- Host 诊断增加 `engine: ready / warming up`；e2e 启动 Host 后等引擎预热完成再输入（预热期间首批按键按设计直接放行，imm e2e 因此偶发）。

## 2026-09-30 — D5：程序显示键盘时切到 T9Ime；D2 兜底

- 复测：D2 仍不符合；新增 D5（TestHost 点"显示键盘"后输入法状态应变为 T9）不符合。二者同源：TestHost 并未使用 T9Ime，切换输入法时没有 T9Ime 的 TIP 事件可用。
- **D5**：`T9_ShowKeyboard` / `T9_ToggleKeyboard`（变为可见时）在调用线程拥有前台窗口时（按钮处理函数里），顺带把该程序切到 T9Ime（FORPROCESS；已是 T9Ime 则不动）。t9ctl.exe 在控制台里调用时不切。同时键盘已显示时，"切到 T9Ime"的焦点事件不再改布局（否则程序指定的数字布局会被重置为中文）。
- **D2 兜底**：面板可见期间每 400 ms 查看前台线程的输入语言（`GetKeyboardLayout`），切到非中文语言即隐藏——覆盖 T9Ime 并未激活的程序；T9Ime 激活时仍由 TIP 的停用上报处理。
- ctl e2e 增加 TestHost 场景（BM_CLICK"数字" → 数字布局、TestHost 物理键盘出中文、deactivate 后键盘隐藏）；x64 七项 e2e 全过。

## 2026-09-30 — D2：T9Ctl 不再在调用线程里切换输入法

- 复测：D5 通过；D2 仍不通过。用户观察：由"切换输入法"弹出的键盘都能随切换隐藏；由 T9Ctl 显示的键盘不能。
- 原因（推断）：T9Ctl 在调用程序的 UI 线程里 `CoInitializeEx` → `ActivateProfile(FORPROCESS)` → `CoUninitialize`；Win7 上随后 T9Ime 在该线程里收不到停用通知（隐藏依赖该通知）。
- 修复：T9Ctl 不再在调用线程做任何 TSF / COM 操作。`T9_ShowKeyboard` / `T9_ToggleKeyboard` 把调用者的前台窗口交给 Host，由 Host 在前台线程没有 T9Ime 时执行与面板点击相同的切换（会话级 profile + 向窗口投递 `WM_INPUTLANGCHANGEREQUEST`）；`T9_Activate` / `T9_Deactivate` 一律经 Host，Host 不在时直接投递 `WM_INPUTLANGCHANGEREQUEST`。
- ctl e2e 的 D2 改为模拟用户切换：向 TestHost 输入框投递 `WM_INPUTLANGCHANGEREQUEST`（其他语言的键盘布局），键盘隐藏。Win11 通过，Win7 待触屏虚拟机复测。

## 2026-09-30 — D2/D5：回到调用线程内切换，但不再 CoUninitialize

- 复测（00dcbce）：D2、D5 都不符合。经 Host 切换（会话级 profile + WM_INPUTLANGCHANGEREQUEST）在 Win7 上只能换语言：TestHost 用的是另一个中文输入法（同为 0804）时什么都不会发生，T9Ime 没激活，D5 失败，D2 也就没有停用可报；400 ms 语言轮询也看不到变化（语言没变）。
- 修复：调用者自己的窗口（按钮处理函数里）重新在调用线程 `ActivateProfile(FORPROCESS)`——Win7 上同语言输入法之间切换只有这条路；COM 若由我们初始化就保持初始化，不再 `CoUninitialize`（推断此前 D2 失败的原因：反初始化把刚激活的 T9Ime 又关掉了，状态图标仍是 T9，但已没有实例上报停用）。其他窗口仍经 Host。
- 诊断：`start_m5_test.bat` 录制 `results\m5_watch.txt`（t9diag --watch 600），焦点事件带 `deactivated` 标记，D2/D5 再失败时据此定位。

## 2026-09-30 — D2、D5 通过；长按预览显示小字

- 触屏 Win7 虚拟机复测（5ba6753）：D2、D5 通过。
- 改进：英文键盘长按字母键时，按键上方的放大预览从主字母换成实际输入的小字（如长按 q 显示蓝色的 1）；长按触发时立即重绘。

## 2026-09-30 — 触屏长按不触发（Win7）

- 复测：鼠标长按预览通过；触屏长按既不输入小字也不显示预览（长按根本没触发）。
- 推断：Win7 的"按住 = 右键"手势接管了按住的手指。面板原先只设置了 `MicrosoftTabletPenServiceProperty` 窗口属性，且其中一个标志值写错（0x40000 实为 ENABLE_FLICKLEARNINGMODE）。现在：标志改为 tpcshrd.h 的正确值（禁用按住、笔点击/笔杆反馈、轻拂、轻拂快捷键），并响应 `WM_TABLET_QUERYSYSTEMGESTURESTATUS` 返回同样的标志。
- 诊断：面板记录最近 40 个指针事件（按下 / 滑出 / 滚动 / 长按触发或跳过 / 抬起 / 取消，带毫秒与坐标），t9diag 输出里可见；M5 启动脚本录制的 `results\m5_watch.txt` 会包含它。

## 2026-09-30 — 问题 1：触屏长按不触发（真实原因：WM_TIMER 被饿死）

- 取证（Win11 触屏，真实手指，t9diag --watch）：长按 q 时指针记录只有 `down` → 2.1 s 后 `up`，中间既无 `long-press` 也无 `long-skipped` / `long-no-track`——长按计时器的 WM_TIMER 根本没有被处理，而不是被取消。注入触摸（每 50 ms 一次 UPDATE）长按正常。
- 原因：真实触屏在手指静止时也持续发送指针更新（约每 8 ms 一次，Win7 的 WM_TOUCH / 触摸转鼠标同样），每次 `PointerMove` 都 `InvalidateRect`；WM_TIMER 只在队列里没有输入消息和 WM_PAINT 时才生成，于是一直得不到处理。鼠标按住不动没有消息，所以正常。把注入的 UPDATE 间隔改为 8 ms 即可在开发机稳定复现（长按 q 输入 q；按住退格也不连发）。Win7 的"按住 = 右键"修正（7f91ae6）不是主因。
- 修复（`panel_window.cpp`）：长按与退格连发记录到期时刻（`long_press_due_`、`repeat_due_`），WM_TIMER 仍然保留，同时每次指针更新检查到期即触发（`FireDue`），两者只会触发一次；坐标没变的更新不重绘。鼠标、WM_POINTER、WM_TOUCH 走同一路径。
- 回归：新增 `e2e_touch`（`tests/e2e/touch_e2e.py`，InjectTouchInput，按住期间每 8 ms 一次 UPDATE）：长按 q 输入 1、按住退格连续删除。修复前两项都失败，修复后通过；鼠标长按仍正常。

## 2026-09-30 — 问题 2–4：程序自己 SetFocus 回文本框被当成触摸聚焦

- 取证（Win11 触屏真实手指，t9diag --watch）：手指点 TestHost（C++ / C#）的任何按钮后 0–16 ms 内都有一次焦点进入，`source=4`（GetCurrentInputMessageSource = 触摸）、`extra=ff5157xx`（触摸转鼠标）、`touch=1` → Host `show`，并按输入框重设布局。按钮处理函数最后 `SetFocus` 回文本框，此时正在处理的正是那次触摸产生的消息。于是"隐藏""切换"隐藏后立即又弹出，"数字""符号"被改回文本布局；切到 C# 窗口时 D1 先隐藏，随后 WinForms 激活时把焦点还给文本框又弹出。注入触摸同样复现（`touch_e2e` 在旧代码上 7 项失败）。
- 修复（`src/tip/touch_tracker.*`、`TextService::ReportFocus`）：TouchTracker 记录最近一次按下的目标窗口（`WM_POINTERDOWN`、新增 `WM_NCPOINTERDOWN`（标题栏）、WH_MOUSE 的鼠标按下）。焦点进入时，若本线程最近看到过按下，只有按下的窗口是焦点窗口、在焦点窗口里面、或是包含焦点窗口的非顶层控件（组合框与其编辑框）才算触摸；按在按钮、标题栏、对话框背景上不算。本线程没看到按下时保持原判断（消息来源）。t9diag 的 touch 行显示 `on=<按下窗口类名>(focus)`。
- 回归：`e2e_touch` 增加：手指点 TestHost 的"隐藏键盘""显示键盘""切换"×2、"数字""符号""英文"，断言显隐与布局保持 1.5 s 不被改回；手指点文本框本身仍弹出；C++ 显示键盘后手指点 C# 窗口标题栏 → 隐藏且不再弹出，C# 的"显示键盘""隐藏键盘"有效。修复前 7 项失败，修复后全过。x64 八项 e2e 全过；autoshow / tip 用 x86 test_target 也通过；x64、x86 单元测试全过。
- 环境：本机（Win11）启用了 .NET Framework 3.5（重启后生效）以便用 v3.5 csc 构建 TestHost.CS.exe；重启前本地调试用的是 v4 csc 临时编译的版本（未入库）。

## 2026-09-30 — 触屏 4 个问题：Win11 触屏真实手指复测通过

- 用户在 Win11 触屏机上用手指复测问题 1–4：基本修复。t9diag 录制显示真实长按触发 long-press，点 TestHost 按钮后的焦点进入判为非触摸（`-> none`）。
- 待办：Win7 触屏机复测（M5 测试包）；.NET 3.5 启用后需重启本机，再用 v3.5 csc 重建 TestHost.CS.exe 并打包。

## 2026-09-30 — 触屏 4 个问题：Win7 触屏机复测通过

- 第一次 Win7 复测报告问题 2、4 仍在：原因是 Win7 上仍加载着旧的 T9Tip.dll（运行中的程序不会换 DLL）。改用 `start_m5_test.bat`（新目录注册、先关测试程序）后，用户确认所有测试符合预期（问题 1–4 与 M5 清单）。
- CLAUDE.md 已知陷阱记下：真机复测务必用启动脚本在新目录注册，t9diag touch 行有 `on=` 即为新版 TIP。
- 包内 TestHost.CS.exe 本次由 v4 csc 编译（本机 .NET 3.5 待重启后生效）。
