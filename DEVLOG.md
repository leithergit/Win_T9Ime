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
