# T9Ime 实施计划（M0）

> 依据：`Docs/SPEC.md`、`Docs/ARCHITECTURE.md`、`Docs/research/*.md`。2026-09-29。
> 每个里程碑的完成标准：可编译、测试通过、git commit、更新 DEVLOG.md，并列出已验证项、未验证项、已知问题。
> "需实测"项按里程碑前置：进入该里程碑时先写最小复现程序（放 `tests/probes/`），结论写回 research 文档。

## 里程碑总览

| 里程碑 | 目标 | 主要产出 | 前置实测 |
|---|---|---|---|
| M0 | 调研与计划 | ARCHITECTURE.md、PLAN.md、research/*、CLAUDE.md、DEVLOG.md | — |
| M1 | 引擎原型 | CMake 骨架、t9repl、九键前端库、音节表生成、单元测试、引擎回归 | P1 |
| M2 | Host 与面板（SendInput 降级） | T9Host（引擎线程+面板+托盘），记事本中触屏输入中文 | P2 |
| M3 | TIP 骨架 | Weasel fork 裁剪、CMake 化、新 IPC、注册注销、物理键盘全拼、TIP 候选窗 | P3 |
| M4 | 推送上屏与智能显隐 | 事件管道、InputScope、自动弹出/隐藏、UIElement、自动切换本 IME、TabTip 共存 | P4 |
| M5 | 控制 API | T9Ctl.dll、t9ctl.exe、TestHost(C++/C#)、IMM32 兼容验证 | P5 |
| M6 | 托盘、设置、词库 | 设置窗口、简繁/模糊音、词库导入导出清空、重新部署 | — |
| M7 | 打包与兼容 | Inno 安装包、便携 zip、x86 全套、体积报告、Win7/Win10/Win11 兼容矩阵 | P6 |

## 前置实测（probes）

- **P1（M1 中）**：x86 rime.dll 行为与 x64 一致；打包 t9repl 交同事在 Win7 真机加载 rime.dll 并跑 `94664486`；MSVC v143 `/MT` 空程序在 Win7 运行；`tools/check_imports` 能识别 Win8+ 导入。
- **P2（M2 前）**：不激活窗口 + WM_POINTER（Win10/11）与 WM_TOUCH（Win7）点击后焦点不丢失；点面板是否关闭目标应用的下拉/菜单；TOPMOST 面板在各类窗口之上的表现；SendInput(KEYEVENTF_UNICODE) 到记事本/Edit/浏览器。
- **P3（M3 前）**：`GetTextExt` 坐标单位在不同 DPI 感知应用中的差异；Win7 上 SDDL `AC` 别名是否可解析；AppContainer（SearchHost）中 TIP 连接管道；提权进程连接 Medium 管道。
- **P4（M4 前）**：`ActivateProfile(FORSESSION|DONTCARE…)` 从 Host 切换前台应用 TIP（全局/按窗口模式 × 应用矩阵，含 Win7 按线程模式）；`WM_INPUTLANGCHANGEREQUEST` 落点；`GetCurrentInputMessageSource/GetCIMSSM` 在 OnSetFocus 时的返回值（Win32/WPF/UWP/Chromium）；线程级钩子方案；TabletTip 注册表写入的生效时机、Win7 平板输入面板对应设置；`IFrameworkInputPane` 跨进程。
- **P5（M5 前）**：`ImmSetOpenStatus/ImmSetConversionStatus` 经 CUAS 映射到 compartment 的行为；`T9_RegisterVisibilityNotify` 对提权窗口 `ChangeWindowMessageFilterEx`。
- **P6（M7 前）**：Inno Setup lzma2/ultra64 实际压缩比；安装/卸载在 Win7–Win11、x86/x64 的完整流程；被占用 DLL 的 MoveFileEx 延迟删除。

## M1 引擎原型

1. 仓库骨架：CMakeLists、CMakePresets（x64/x86 Release/Debug）、`cmake/` 工具函数、doctest（header-only，放 third_party）、`.gitignore`、CLAUDE.md。
2. `third_party/fetch_librime.ps1`：下载 librime 1.17.0 msvc-x64/x86 + rime-deps（取 opencc 数据），SHA-256 校验，**保留 mtime 解压**。
3. `data/fetch_rime_ice.ps1`：固定 rime-ice commit `3aea6d3694fb3d94ec663641f021f788822897ad`，校验哈希。
4. `data/custom/`：`t9.custom.yaml`（去 t9_processor）、`t9_eng.schema.yaml`（英文九键，独立方案）、`rime_ice.custom.yaml`（去 radical_pinyin 反查）、`default.custom.yaml`（schema_list: rime_ice, t9）。
5. 构建期预部署：CMake 自定义目标调用 `rime_deployer --build`，产物进 `build/data/build`，保留 mtime。
6. `tools/gen_syllables`（Python）：从 rime-ice 词典生成 `syllable_table.gen.h`（音节、数字码、频率）。
7. `src/host/engine/t9/`：`SyllableIndex`（前缀匹配）、`PinyinBar`（候选音节、排序）、`T9Composer`（点选替换、撤销栈、回车/退格语义）、`PreeditFormatter`（comment → 拼音 preedit，UTF-8 字节偏移处理）。不依赖 librime 的部分全部可单测；依赖部分通过 `IRimeSession` 接口注入。
8. `src/host/engine/RimeEngine`：初始化/部署/会话/候选迭代封装（取 RimeWithWeasel 中可复用部分，去 boost）。
9. `tools/t9repl`：交互 + 脚本模式（stdin 读命令：`keys 94664486`、`bar`、`pick 1`、`sel 0`、`undo`、`enter`、`page+`），输出 preedit、拼音栏、候选。
10. 测试：doctest 覆盖九键码映射、前缀匹配、set_input 替换、撤销、preedit 转换；引擎回归脚本 `tests/regress/*.t9` 以 t9repl 执行并比对快照（期望值以实测为准：`94664486→中国`、`744→是`、`zhong'4486`…）。
- 验证：x64 与 x86 均通过；`check_imports` 对 t9repl 无告警。

## M2 Host 与面板（SendInput 降级）

1. T9Host.exe：单实例、引擎线程、消息循环、Per-Monitor 感知 manifest（含 Win7 回退）。
2. 面板：不激活窗口、`PanelPointerEvent` 输入抽象（WM_POINTER / WM_TOUCH+鼠标）、D2D 绘制、中文九键布局（候选栏、拼音栏、主键、功能列、底栏）、按下态、长按连删、左滑清空、拖动/停靠、深浅色。
3. 面板 ↔ 引擎：按键 → t9 session → 状态回填面板。
4. 上屏：SendInput(KEYEVENTF_UNICODE)。
5. 托盘雏形（显示键盘/退出）。
- 验证：开发机（Win11）用鼠标在记事本、Win32 Edit 中输入"中国""是"；同事在 Win7 触屏真机按 M2 清单验证触摸。

## M3 TIP 骨架

1. 建立 `vendor/weasel` 跟踪分支（fork 点 `d73f629`），导入 WeaselTSF/WeaselUI 相关文件到 `src/tip/`，CMake 化。
2. 去 boost、去 OpenMP、Win8+ API 动态加载；`check_imports` 纳入构建。
3. `src/common/protocol` + 请求管道（OVERLAPPED、超时、退避）；Host 侧请求端点与每线程 rime_ice session。
4. TSF 修正清单（ARCHITECTURE §4 第 3–6、10–12 条）；注册/注销幂等，x86/x64 各自注册；profile 0x0804。
5. 物理键盘全拼：composition、TIP 内候选窗、GetTextExt 定位（TS_E_NOLAYOUT 缓存）、Shift 切换、Ctrl/Alt 透传、翻页选词。
6. Host 未运行 → 透传；计划任务恢复（§8）。
- 验证：记事本、Win32 Edit、WPF、Edge、Word、管理员记事本、开始菜单搜索（SearchHost）中物理键盘输入；杀 Host 后宿主不卡不崩。

## M4 推送上屏与智能显隐

1. 事件管道 + TIP 后台线程 + 消息窗口 + edit session 上屏；焦点注册表与 seq。
2. 面板输入改走推送；SendInput 仅作降级。
3. InputScope（密码/数字/URL/只读）。
4. 自动弹出/隐藏（触摸来源判定 + 焦点抖动去抖）；系统浮层进程不弹出（ARCHITECTURE §6.6）。
5. UIElement（show=FALSE 路径）。
6. 自动切换本 IME（§6.4）。
7. TabTip 共存（注册表接管 + InputPane 让位）。
- 验证：兼容矩阵中的推送、定位、显隐项；Win7 上的推断式自动弹出。

## M5 控制 API
T9Ctl.dll（x86/x64）、头文件、C# P/Invoke、t9ctl.exe、TestHost(C++/C#)；compartment 双向同步验证（ImmSetOpenStatus/ImmSetConversionStatus）。

## M6 托盘、设置、词库
托盘中/英状态与菜单；设置窗口（Win32 + D2D，自动弹出、面板尺寸主题、候选数量、简繁、模糊音 → 生成 custom.yaml 并重新部署）；词库导入导出（`rime_levers_api` export/import_user_dict）、清空学习记录；重新部署（引擎线程 maintenance，期间 TIP 透传）。

## M7 打包与兼容
Inno Setup（Program Files、保留 mtime、x86+x64 TIP 注册、Run 键 + 计划任务、TabTip 接管复选框、Win7 补丁检测、卸载顺序与 MoveFileEx）；便携 zip + 注册脚本；签名步骤预留（signtool，不启用）；体积与构成报告；LICENSES/；Win7/10/11 × x86/x64 兼容矩阵报告。

## 决策结果（2026-09-29 已确认）

| # | 问题 | 结论 |
|---|---|---|
| Q1 | TIP 线程级 `WH_GETMESSAGE` 钩子判定触摸来源 | 允许 |
| Q2 | OTS 提权（其他管理员账户） | 不支持，透传 |
| Q3 | TabTip 接管 | 复选框默认勾选；不用未公开 `ITipInvocation`，只让位 |
| Q4 | Win7 补丁 / Win8.x | KB2670838 强制、KB4474419 建议；**Win8/8.1 暂不支持** |
| Q5 | ARM64 | 不支持 |
| Q6 | WTL 设置界面 | 弃用，自写 Win32 + D2D |
| Q7 | 测试环境 | 开发机无触屏；**主要目标是公司的 Win7 触屏设备**，由同事在真机测试 |
| Q8 | 英文九键 | 启用 |
| Q9 | radical_pinyin 反查 | 不保留 |

## 测试协作（Q7）

- 开发机只能用鼠标验证面板逻辑；面板输入抽象 `PanelPointerEvent` 必须让鼠标与触摸走同一代码路径，触摸专有行为（WM_TOUCH、长按、滑动、按下态）由同事在 Win7 触屏真机验证。
- 从 M2 起，每个里程碑额外交付：
  1. `dist/<里程碑>/` 测试包（x86 + x64，便携 zip + 注册/注销脚本，Win7 可直接运行）；
  2. `Docs/testing/<里程碑>-checklist.md`：逐项操作步骤、预期结果、结果填写栏（通过/失败/备注/截图）；
  3. 问题反馈模板，回收后写入 DEVLOG。
- Win7 是一等目标：P1 的 Win7 冒烟（rime.dll、`/MT` 运行时）在 M1 结束时就交同事执行，不等到 M7。
