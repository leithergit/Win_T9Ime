# 项目：T9Ime — 基于 Rime 九键方案的 Windows 触屏九宫格键盘 + 智能拼音输入法

## 0. 工作方式（先读）
- 你是资深 Windows 系统程序员，熟悉 TSF、COM、IMM32 兼容层（CUAS）、Win32、Direct2D/DirectWrite、librime。
- **第一步不要写代码。** 先调研并产出 docs/ARCHITECTURE.md 和 docs/PLAN.md，同时给出：
  (a) 本文中标注"待核实"内容的核实结果；
  (b) 需要我决策的问题清单。
  等我确认后再开始编码。
- 调研对象：librime 的 rime_api.h（及其 Windows release 包）、rime-ice 当前的 t9.schema.yaml / rime_ice.schema.yaml、rime-wanxiang 的九键方案、Weasel（小狼毫）的 TSF 层与 IPC、微软 Windows-classic-samples 中的 SampleIME、仓输入法 Hamster 的九键拼音栏逻辑。大范围阅读源码时用子代理完成，结论写进 docs/research/，不要让主上下文膨胀。
- 维护两个文件：
  - CLAUDE.md：构建命令、目录约定、编码规范、已知陷阱。
  - DEVLOG.md：每个会话结束时追加本次完成内容、做出的决策、遗留问题、下一步。
  新会话开始时先读这两个文件。
- 按里程碑（§12）推进。每个里程碑都要做到：可编译、测试通过、git commit、更新 DEVLOG。不要一次生成全部代码。
- 遇到 API 行为不确定时，先写最小复现程序验证，不要猜。

## 1. 目标
在 Windows 10 1809+ / Windows 11（x64 为主，x86 应用也要支持）上实现一个完整可用的中文输入法：
1. 触屏九宫格虚拟键盘，走中文拼音九键输入，带拼音选择栏和智能组句。
2. 物理键盘下是全拼智能拼音（同一引擎、同一用户词库）。
3. 标准 TSF 文本服务：可被系统和应用启用、切换、开关，支持选词、关闭；兼容通过 IMM32 API 操作输入法的旧应用。
4. 应用可通过 API 激活输入法、显示或隐藏键盘。
5. 对系统依赖最低，安装包尽量小。

## 2. 技术选型（已定，计划阶段可提出异议）
- 语言与构建：C++20，MSVC（VS 2022），CMake + Ninja，CMakePresets。
- TSF DLL（T9Tip.dll）：纯 C++，静态 CRT（/MT）。除系统 DLL 外零依赖，禁止使用 .NET、Qt、C++/WinRT、Boost。它会被加载进所有进程，包括 AppContainer 进程和管理员进程。
- Host 进程 UI：Win32 + Direct2D + DirectWrite，用系统自带组件，零额外运行时。不用 WinUI 3（依赖 Windows App SDK 运行时，难以做不抢焦点的窗口），也不用 Qt5（体积大、已停止维护）。
- 引擎：librime。优先使用官方 release 的 Windows MSVC 预编译包；没有合适版本时，按其构建脚本把依赖静态编译进 rime.dll。
- 输入方案：rime-ice（雾凇拼音）的 rime_ice（全拼）和 t9（九宫格）。另评估 rime-wanxiang 的九键方案，给出对比结论，由我选择。
- 打包：Inno Setup（如需 MSI 改用 WiX v4）。

## 3. 架构（参考 Weasel）
进程与模块：
1. **T9Tip.dll**（x64 + x86，ARM64 可选）：TSF 文本服务，薄客户端。职责只有按键判定与转发、composition 管理、上报光标位置、读取 compartment 和 InputScope、实现 UIElement 接口。**不加载 librime。**
2. **T9Host.exe**（每个用户会话单实例，x64）：持有 librime 及所有 session，绘制候选窗、九宫格面板、托盘图标，负责 IPC。
   原因：librime 的用户词库（leveldb）不能被多进程同时写，必须由单进程持有。
3. **T9Ctl.dll + t9ctl.exe**：供第三方应用使用的控制 API（见 §7）。
4. 数据：共享数据放在安装目录 data\（含预部署好的 build\*.bin）；用户数据放在 %APPDATA%\T9Ime\。

IPC：
- 使用命名管道，名称包含会话 ID 和用户 SID。DACL 必须允许当前用户和 ALL APPLICATION PACKAGES（S-1-15-2-1）访问，SACL 设置 Low 完整性标签。否则 UWP、浏览器沙箱等进程中的 TIP 无法连接。
- 请求与响应：TIP 发送按键事件，Host 返回 {是否消费该键, commit 文本, preedit, 光标位置, 候选列表, 拼音栏内容}。
- 推送（面板按键的上屏路径）：触屏按键不经过目标应用的消息队列。Host 处理完后，把结果推送给当前拥有焦点的 TIP 实例。
  - TIP 每个线程保持一条长连接。后台线程收到推送后，PostMessage 给本进程内的隐藏窗口；UI 线程再调用 ITfContext::RequestEditSession（TF_ES_ASYNCDONTCARE | TF_ES_READWRITE）更新 composition 或上屏。
  - 禁止 Host 跨进程 PostMessage 或 SendInput 到目标应用（受 UIPI 限制，对管理员进程无效）。只有当焦点窗口不支持 TSF 时，才降级用 SendInput（KEYEVENTF_UNICODE），并记录日志。
- Host 未运行或崩溃时：TIP 透传所有按键，绝不影响宿主进程。拉起或恢复 Host 的策略（包括在受限进程中无法 CreateProcess 的情况）由你调研后给出。

## 4. TSF 文本服务要求
- 实现以下接口：ITfTextInputProcessorEx、ITfThreadMgrEventSink、ITfKeyEventSink、ITfCompositionSink、ITfThreadFocusSink、ITfDisplayAttributeProvider、ITfCompartmentEventSink、ITfActiveLanguageProfileNotifySink，以及 ITfLangBarItemButton（托盘和语言栏的"中/英"指示）。
- 注册：通过 ITfInputProcessorProfileMgr::RegisterProfile 注册 zh-CN（0x0804）profile。类别包括 GUID_TFCAT_TIP_KEYBOARD、TIPCAP_IMMERSIVESUPPORT、TIPCAP_SYSTRAYSUPPORT、TIPCAP_UIELEMENTENABLED、TIPCAP_INPUTMODECOMPARTMENT、DISPLAYATTRIBUTEPROVIDER。x86 和 x64 两份 DLL 分别注册，DllRegisterServer 和 DllUnregisterServer 都要幂等。
- IMM32 兼容：与 GUID_COMPARTMENT_KEYBOARD_OPENCLOSE、GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION 双向同步，使 ImmSetOpenStatus、ImmGetOpenStatus、ImmSetConversionStatus 在经典 Win32 应用中生效。
- 候选窗定位：使用 ITfContextView::GetTextExt。返回 TS_E_NOLAYOUT 时使用缓存的位置；支持多显示器和 Per-Monitor V2 DPI。
- UIElement：BeginUIElement 返回 show=FALSE 时（如全屏游戏自绘候选），隐藏 Host 的候选窗，改为通过 ITfCandidateListUIElement 提供数据。
- InputScope（GUID_PROP_INPUTSCOPE）：
  - IS_PASSWORD / IS_PRIVATE：不做中文组字，键盘切到英文。
  - IS_NUMBER / IS_DIGITS / IS_TELEPHONE_*：面板默认显示数字键盘。
  - IS_URL / IS_EMAIL_*：切到英文模式。
  - 无可编辑上下文或只读：不弹出键盘。
- 物理键盘：使用 rime_ice 全拼 session。Shift 单击切换中英；Ctrl 和 Alt 组合键透传；OnTestKeyDown 与 OnKeyDown 的判定必须一致。
- 稳健性：
  - 所有 COM 方法包一层 noexcept，异常不得越过 COM 边界。
  - DllCanUnloadNow 计数正确。
  - Deactivate 时完成或终止 composition，并注销所有 sink。
  - 不在宿主进程里做耗时操作。

## 5. 九键引擎层（Host 内）
t9 schema 的已知事实（待核实，以 rime-ice 当前仓库为准）：
- t9.schema.yaml 通过 `__include: rime_ice.schema.yaml:/` 复用全拼方案，并设置 `translator/prism: t9`。
- 按键编码使用大写字母：2→A、3→D、4→G、5→J、6→M、7→P、8→T、9→W。speller/algebra 用 derive 规则把每个音节穷举成大写码（例：zhong→WGMMG，shi→PGG），同时保留小写原拼写用于精确匹配。另有 abbrev 超级简拼。
- 1 键：punctuator 中 "@" 对应一个符号序列。
- 英文九键：melt_eng.custom.yaml 用 xlit 把字母映射为大写码。
- 分词：发送 `'`（音节分隔符）。

需要实现的部分：
- session 策略：面板输入使用 t9，物理键盘使用 rime_ice。要核实两者的 user_dict 是否共享，确保学习结果互通；如不共享，给出方案。
- **拼音选择栏**（前端实现，是九键体验的核心）：
  - 用生成脚本（不要手写）预计算全部音节到九键码的映射表，可以复用 schema 的 algebra，或从词典中提取。
  - 对当前输入中第一个未确认的段做前缀匹配，列出候选音节，排序规则为先按匹配长度降序，再按音节频率。
  - 用户点选某个音节后，用 set_input 把对应的大写码段替换成小写拼音加分隔符，然后继续处理下一段。
  - 支持撤销已确认的拼音段。
- **preedit 显示**：把大写码转换成可读拼音（如 zhong'guo）。先实测 librime 实际返回的 preedit 和候选 comment，再决定用前端转换还是 lua filter。
- 候选：用 candidate_list 迭代器获取完整列表，以支持横向滑动和展开网格；支持选词和翻页。
- 精简：通过 *.custom.yaml 移除 lua_* 组件（除非决定集成 librime-lua）以及不需要的反查。提供词库裁剪选项，并给出各选项的体积对比。
- 预部署：在构建阶段用 rime_deployer 生成 build\*.bin 并打包，首次运行不需要部署。托盘菜单提供"重新部署"。

## 6. 触屏九宫格面板（T9Host 内）
- 窗口：
  - 扩展样式 WS_EX_NOACTIVATE | WS_EX_TOPMOST | WS_EX_TOOLWINDOW；WM_MOUSEACTIVATE 返回 MA_NOACTIVATE。
  - 用 WM_POINTER 处理触摸和笔输入，有按下态反馈；支持 Per-Monitor V2 DPI。
  - 可拖动或停靠屏幕底部，记住位置和尺寸；深浅色跟随系统。
- 中文九键布局：
  - 顶部：候选栏，可展开为网格。
  - 左侧：拼音选择栏，可滚动。
  - 中间：3×3 主键区，1 键为分词/符号，2 ABC … 9 WXYZ。
  - 右侧功能列：删除（长按连续删除，左滑清空）、重输、空格/0、回车。
  - 底部：符号、中/英、123、隐藏键盘。
- 其他布局：数字键盘、分类符号面板（含中文标点）、英文九键（melt_eng 预测）。26 键全键盘为可选，放到后期里程碑。
- 自动显示与隐藏：
  - 焦点进入可编辑上下文，且最近一次输入来自触摸或笔时（GetCurrentInputMessageSource / 指针类型），自动弹出面板。
  - 焦点离开、无编辑上下文、或应用或用户请求时，隐藏面板。
  - 设置中可以关闭自动弹出。
  - 调研如何与系统触摸键盘（TabTip / InputPane）共存，避免两个键盘同时出现，并给出策略。

## 7. 对外控制 API（应用可调用激活和隐藏）
1. 标准系统 API（必须验证可用）：
   - ActivateKeyboardLayout，或 ITfInputProcessorProfileMgr::ActivateProfile（传入本 TIP 的 CLSID 和 profile GUID），用于切换到本输入法。
   - ImmSetOpenStatus，或设置 OPENCLOSE compartment，用于开关输入法。
   - ImmSetConversionStatus(IME_CMODE_NATIVE)，用于切换中英。
2. T9Ctl.dll：纯 C 导出，__stdcall，附带头文件和 C# P/Invoke 示例。导出函数：
   `T9_IsInstalled, T9_Activate(HWND), T9_Deactivate, T9_ShowKeyboard(layout), T9_HideKeyboard, T9_IsKeyboardVisible, T9_SetMode(chinese|english|number|symbol), T9_SetDock/T9_SetPosition, T9_RegisterVisibilityNotify(HWND)`。
   最后一个通过 RegisterWindowMessage 通知应用键盘的显示、隐藏和占用区域，便于应用调整布局，作用类似 InputPane 的 Showing/Hiding 事件。内部复用 Host 的命名管道。
3. 命令行：`t9ctl.exe show|hide|toggle|mode <m>`。
4. InputScope：应用设置了 InputScope 时，自动获得对应的键盘布局（见 §4）。

## 8. 托盘与设置
- 托盘图标：显示中/英状态，菜单包括显示键盘、设置、重新部署、退出。
- 设置窗口（Win32/D2D 简单实现）：
  - 自动弹出开关、面板尺寸和主题、候选数量。
  - 简繁切换、模糊音（生成对应的 Rime custom.yaml）。
  - 用户词库导入导出、清空学习记录。

## 9. 目录结构（建议，计划阶段可调整）
```
src/tip/        TSF DLL
src/host/       T9Host：engine/ ui/ ipc/
src/ctl/        T9Ctl.dll、t9ctl.exe
src/common/     IPC 协议、共享类型（无外部依赖）
tools/          t9repl（引擎命令行测试）、音节码表生成脚本
data/           rime-ice（固定 commit）、custom.yaml、预部署产物
tests/  samples/TestHost(C++, C#)  installer/  third_party/  docs/
```

## 10. 构建与打包
- 构建配置：x64-Release 构建全部组件；x86-Release 只构建 TIP 和 T9Ctl。
- 第三方依赖：librime 预编译包通过脚本下载并校验哈希；rime-ice 固定到某个 commit，并记录版本号。
- 安装程序：
  - 安装到 Program Files\T9Ime，并给目录设置 ACL，允许 ALL APPLICATION PACKAGES 读取和执行。
  - 分别注册 x86 和 x64 的 TIP；Host 通过 HKCU\...\Run 随登录启动。
  - 卸载顺序：先注销 TIP，再结束 Host，最后删除文件。被占用的 DLL 用 MoveFileEx 延迟到重启时删除。
- 发布产物：安装包，以及便携版 zip（附带需要管理员权限运行的注册和注销脚本）。签名步骤（signtool）先预留，暂不启用。
- 体积：目标是安装包小于 25MB，并报告实际大小和构成明细。
- 许可证：librime 为 BSD；rime-ice 为 GPL-3.0（待核实）。把所有第三方许可证放进 LICENSES/，并单独提醒我评估分发影响。

## 11. 测试
- 单元测试（doctest，header-only）：九键码映射、拼音栏前缀匹配、set_input 替换逻辑、preedit 转换、IPC 编解码。
- 引擎回归：用 t9repl.exe 输入数字串、选拼音、选词，打印 preedit 和候选。回归用例示例：94664486 → 中国；744 → 是。期望值以实测为准。
- 应用兼容矩阵（手工测试，能用 UI Automation 自动化的尽量自动化）：
  - 应用范围：Win11 新版记事本、Win32 Edit、WinForms、WPF、UWP/WinUI（设置搜索框、开始菜单搜索）、Edge/Chrome、Firefox、VS Code、Word/Excel、Windows Terminal、以管理员身份运行的记事本、自绘候选的全屏程序。
  - 每个应用验证：激活、组字、选词、上屏、候选定位、中英切换、面板自动弹出，以及 §7 的全部 API。
- TestHost 示例程序（C++ 和 C# 各一份）：演示 §7 的全部调用。
- 稳定性：
  - 杀掉 Host 后，宿主应用不崩溃，Host 可以恢复。
  - 长时间输入时内存不持续增长。
- 隐私：不记录任何输入内容。调试日志只在 Debug 构建中存在，且默认关闭。

## 12. 里程碑
- M0 调研与计划：只写文档，不写代码。交付 ARCHITECTURE.md、PLAN.md 和问题清单。
- M1 引擎原型：t9repl 跑通 librime + t9 schema，包括拼音栏算法和 preedit 转换。
- M2 Host 与面板：走 SendInput 降级路径，能在记事本中用触屏输入中文。
- M3 TIP 骨架：注册和注销、物理键盘全拼、composition、候选窗定位、IPC。
- M4 面板经 TIP 推送上屏，完成 InputScope、自动弹出和隐藏、UIElement。
- M5 T9Ctl、t9ctl.exe、TestHost，并完成 IMM32 兼容验证。
- M6 托盘、设置、词库管理。
- M7 打包、体积优化、x86 TIP、兼容矩阵报告（ARM64 可选）。

每个里程碑结束时，列出已验证项、未验证项和已知问题。

## 13. 禁止事项
- 不用全局键盘钩子（WH_KEYBOARD_LL），不用 DLL 注入。
- TIP 中不引入任何第三方库；Host 只依赖 librime 和系统库；产品本身不依赖 .NET。
- 不在 TIP 或 Host 中记录用户输入内容。
