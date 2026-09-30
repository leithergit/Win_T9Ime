# T9Ime 开发进度与待解决问题（2026-09-30）

新会话 / 新机器接手时先读 `CLAUDE.md`（工作约定、构建）、本文件和 `DEVLOG.md`（逐日记录）。

## 进度

| 里程碑 | 内容 | 状态 |
|---|---|---|
| M0 | 调研、方案、Weasel fork 骨架 | 完成 |
| M1 | 引擎：librime 1.17 + rime-ice，t9 数字码方案，t9repl 回归 | 完成 |
| M2 | 触摸面板（D2D，九宫格/数字/符号，候选栏） | 完成 |
| M3 | TIP（TSF）物理键盘拼音、TIP 内候选窗、Host 管道 | 完成 |
| M4 | 面板经 TIP 推送上屏、InputScope、自动弹出/隐藏、自动切换本 IME、系统触摸键盘共存、密码框英文全键盘、讯飞风格布局 | 完成，Win7 触屏实测通过 |
| M5 | T9Ctl.dll / t9ctl.exe / TestHost（C++、C#）、控制管道、可见性通知、IMM32 兼容 | 功能完成；**触屏下有下列问题** |
| M6 | 托盘、设置窗口、词库 | 未开始 |
| M7 | 安装包与兼容矩阵 | 未开始 |

自动化测试（开发机 Win11，鼠标 + 注入）：单元、回归、Win7 导入检查、7 项端到端（panel、tip、push、autoshow、switch、ctl、imm）全部通过；Win7 虚拟机（vmrun）上 7 项端到端也全部通过。**这些测试都用鼠标或注入的键盘，没有覆盖真实触摸下的 T9Ctl/TestHost 场景。**

## 待解决问题（2026-09-30 用户在 Win7 触屏虚拟机与 Win11 触屏机上测试，两者表现基本相同）

用鼠标操作全部正常，只有手指触摸出问题：

1. **英文全键盘长按字母不输入小字、不显示预览**（长按 q 应输入 1 并在按键上方显示）。
2. **TestHost 点"显示键盘"能显示，但点"隐藏键盘"不生效。**
3. **从 TestHost(C++) 切换到 TestHost(C#)，键盘不消失**（焦点确实到了 C# 窗口）。
4. **"切换"只能显示不能隐藏；"数字""符号"切换不了布局。**

### 分析（问题 2–4，高度可疑的共同原因，尚未在代码中修复）

TIP 把"最近一次按下来自触摸/笔"之后发生的焦点变化当作"用户触摸了输入框"，据此自动弹出面板（`src/tip/touch_tracker.*`、`TextService::ReportFocus` → Host `DecideOnFocus`，`touch=1 → kShow`）。

手指点 TestHost 的按钮时：按钮获得焦点（TSF 报告焦点离开），按钮处理函数随后 `SetFocus` 回文本框（TSF 报告焦点进入，且带触摸标志）→ Host 认为用户触摸了文本框 → **自动弹出面板、并按输入框类型/文本模式重设布局**。于是：
- "隐藏键盘"：T9Ctl 隐藏后立刻又被自动弹出（问题 2）；
- "切换"：隐藏后又弹出（问题 4）；
- "数字""符号"：T9Ctl 设的布局被随后的自动弹出改回中文（问题 4）；
- 切到 C# 窗口：前台切换先隐藏（D1），随后触摸引起的焦点进入又弹出（问题 3）。

鼠标点击不带触摸标志，所以一切正常。

**修复方向**：只有当触摸按下的位置就在获得焦点的输入框（或其子窗口）上时，才算"触摸聚焦输入框"。TouchTracker 需要记录最近一次按下的窗口/坐标，`ReportFocus` 时与焦点窗口比较；程序自己 `SetFocus` 造成的焦点变化不算触摸。可用 `InjectTouchInput`（Win8+，见 `tests/e2e/autoshow_e2e.py` 的 `touch_tap`）在开发机上复现与回归：注入触摸点 TestHost 的"隐藏键盘"按钮，应保持隐藏。

### 分析（问题 1，原因未明）

- Win7 上怀疑是系统"按住 = 右键"手势接管了按住的手指：已让面板响应 `WM_TABLET_QUERYSYSTEMGESTURESTATUS` 并修正 `MicrosoftTabletPenServiceProperty` 标志（commit 7f91ae6），但 Win11（WM_POINTER 路径）上同样失败，说明另有原因。
- 已加诊断：面板记录最近 40 个指针事件（按下 / 滑出 / 滚动 / 长按触发或跳过 / 抬起 / 取消），`t9diag` 输出里 `recent pointer events` 一节可见。复现一次长按后运行 `t9diag`，看是 `long-press` 未出现、`slid-off`、`cancel` 还是 `long-skipped`。
- 可用 `InjectTouchInput` 注入"按下—保持—抬起"在开发机复现（`POINTER_FLAG_DOWN` 后隔 ~100 ms 发 `POINTER_FLAG_UPDATE|INCONTACT` 保持，再 `UP`）。

## 在 Win11 触屏机上继续开发

1. 安装 VS2022（C++ 桌面）、CMake、Python 3、Git；`pwsh -File third_party/fetch_librime.ps1`、`pwsh -File data/fetch_rime_ice.ps1` 下载依赖（不入库）。
2. `pwsh -File build.ps1 -Preset x64-Release`（另有 x86-Release），产物在 `out/build/<preset>/bin`。
3. 注册开发版 TIP：`pwsh -File tools/dev_register.ps1`（UAC）。
4. 测试：`ctest -LE e2e`；端到端 `ctest -L e2e`（会移动鼠标、需要交互桌面）。
5. 手工触摸测试：`out/build/x64-Release/bin/TestHost.exe`、`TestHost.CS.exe`、`t9ctl.exe`，清单 `Docs/testing/M5-checklist.md`（含 D1–D5）。
6. 诊断：`t9diag.exe`（一次）/ `t9diag.exe --watch 60`（持续记录焦点、触摸判定、指针事件）。
