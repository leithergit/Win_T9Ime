<img src="res/T9Ime.png" width="96" align="right" alt="T9Ime">

# T9Ime — Windows 触摸九宫格输入法

T9Ime 是一个面向 Windows 触屏设备的中文输入法：在屏幕上提供手机风格的**九宫格拼音键盘**和**英文全键盘**，同时支持物理键盘全拼输入。它以 TSF（Text Services Framework）文本服务的形式工作，输入引擎是 [librime](https://github.com/rime/librime)（中州韵），词库为 [雾凇拼音 rime-ice](https://github.com/iDvel/rime-ice)。

## 项目目标

- **主要目标设备：Windows 7 SP1 触屏一体机 / 平板**（企业现场设备），同时支持 Windows 10 / 11；x86 与 x64 均完整支持。
- 让只有触摸屏的设备也能高效输入中文：手指点输入框自动弹出键盘，打完自动收起；数字框、密码框自动换成合适的布局。
- 业务程序可以通过简单的 C 接口（`T9Ctl.dll`，C/C++/C# 均可调用）控制键盘的显示、隐藏、布局和位置，并在键盘显示/隐藏时得到通知以调整界面。
- 安全与稳定：不使用全局键盘钩子与 DLL 注入、不需要 uiAccess 签名；输入法 DLL 只依赖系统库；不记录任何用户输入内容。

## 功能

### 触摸键盘面板
- **中文九宫格**（布局参照讯飞 iOS）：数字码拼音输入、左侧拼音选择栏 / 常用标点、候选栏可左右滑动、展开候选网格、清空、换行、123 / 符号 / 中英切换、隐藏。
- **英文全键盘（QWERTY）**：字母直接上屏；长按输入键上的小字符（如长按 q 输入 1）；⇧ 单击大写一个字母、双击锁定大写；按下的键在手指上方放大显示（密码框里也能看清按的是什么）。
- **数字键盘、符号键盘**。
- 可拖动、可调整大小（右上角手柄）、可停靠到屏幕底部；浅色 / 深色主题；按显示器 DPI 缩放。
- 鼠标、触摸、笔走同一套代码；Windows 7 用 WM_TOUCH，Windows 8+ 用 WM_POINTER。

### 自动弹出与收起
- 手指点输入框时自动弹出，离开时收起；用户切换到 T9Ime 时自动弹出；切换到其他程序或其他输入法时收起，切回来再显示。
- 按输入框的 InputScope 选布局：数字 / 电话 → 数字键盘；密码 / 网址 / 邮箱 / 用户名 → 英文全键盘。
- 接管系统触摸键盘：Windows 10/11 关闭系统触摸键盘的自动弹出，Windows 7 隐藏"输入面板"图标与屏幕边缘标签（可在设置中关闭，修改前会备份、关闭时还原）。
- 点面板时若前台程序还没在用 T9Ime，自动把它切换到 T9Ime。

### 物理键盘
- 全拼输入，候选窗在应用程序进程内绘制（跟随程序的 DPI 与窗口层级），Shift 切换中英，支持只读 UI 元素的程序（游戏、全屏程序自绘候选）。
- 兼容 IMM32：老程序调用 `ImmSetOpenStatus` / `ImmSetConversionStatus` 时 T9Ime 随之开关 / 切换中英。

### 托盘与设置
- 托盘图标显示"中 / 英"；菜单：显示/隐藏键盘、停靠、设置…、重新部署、退出。
- 设置窗口（适合手指操作）：自动弹出的三个开关、系统键盘接管、主题、键盘大小；简繁切换（即时生效）、物理键盘候选个数、11 组模糊音（z/zh、c/ch、s/sh、n/l、f/h、r/l、an/ang、en/eng、in/ing、ian/iang、uan/uang）；用户词库导出 / 导入 / 清空学习记录、重新部署。

### 控制接口（T9Ctl）
`T9Ctl.dll`（x86/x64，`__stdcall` C 导出，头文件 `src/ctl/t9ctl.h`，C# 声明 `samples/TestHost/cs/T9Ctl.cs`）：

| 函数 | 作用 |
|---|---|
| `T9_ShowKeyboard(mode)` / `T9_HideKeyboard()` / `T9_ToggleKeyboard()` / `T9_IsKeyboardVisible()` | 显示 / 隐藏键盘（可指定中文、英文、数字、符号布局） |
| `T9_SetMode(mode)` / `T9_GetMode()` | 设置 / 查询布局 |
| `T9_SetDock()` / `T9_SetPosition(x, y)` / `T9_GetKeyboardRect(&rect)` | 停靠、移动、查询键盘位置 |
| `T9_Activate(hwnd)` / `T9_Deactivate(hwnd)` | 把程序切换到 T9Ime / 切走 |
| `T9_RegisterVisibilityNotify(hwnd)` / `T9_GetVisibilityMessage()` | 键盘显示 / 隐藏 / 移动时收到窗口消息 |
| `T9_IsInstalled()` | 是否已安装 |

命令行工具 `t9ctl.exe show|hide|toggle|mode|pos|dock|status|activate|deactivate|watch`；示例程序 `TestHost.exe`（C++）与 `TestHost.CS.exe`（C#，Windows 7 自带 .NET 3.5 即可运行）。

## 架构概览

```
 应用程序进程（任意程序）                          T9Host.exe（每个用户会话一个）
 ┌──────────────────────────┐   请求管道 (.req)   ┌───────────────────────────────────┐
 │ T9Tip.dll（TSF 文本服务）│ ──────────────────► │ 请求服务 ─► 引擎线程（librime）    │
 │  按键 / 焦点 / 候选窗     │ ◄────────────────── │ 事件服务  面板窗口（D2D 九宫格）   │
 └──────────────────────────┘   事件管道 (.evt)   │ 控制服务  托盘 / 设置窗口          │
 ┌──────────────────────────┐   控制管道 (.ctl)   │ 焦点登记  前台监视 / 输入法切换    │
 │ 业务程序 + T9Ctl.dll      │ ──────────────────► │                                   │
 └──────────────────────────┘                     └───────────────────────────────────┘
```

完整的模块划分、调用关系与每个文件的说明见 **[Docs/Project.pdf](Docs/Project.pdf)**；设计细节见 `Docs/ARCHITECTURE.md`，需求见 `Docs/SPEC.md`，开发进度与问题见 `Docs/STATUS.md`，逐日记录见 `DEVLOG.md`。

## 系统要求

- Windows 7 SP1（需 KB2670838 平台更新——安装包已附带，缺少时自动离线安装；建议 KB4474419）、Windows 10、Windows 11；x86 或 x64。不支持 Windows 8/8.1 与 ARM64。
- 运行时无需 VC++ 运行库（静态链接 CRT）。

## 构建

需要 Visual Studio 2022（C++ 桌面开发）、CMake ≥ 3.25、Python 3、Git、PowerShell 7。

```powershell
pwsh -File third_party/fetch_librime.ps1     # librime 1.17.0 官方包（SHA-256 校验）
pwsh -File data/fetch_rime_ice.ps1           # rime-ice 固定 commit
pwsh -File build.ps1 -Preset x64-Release     # 配置 + 构建 + 测试（另有 x86-Release / *-Debug）
pwsh -File tools/dev_register.ps1            # 注册开发版输入法（需管理员）
```

产物在 `out/build/<preset>/bin`（与安装目录布局一致）。安装程序：`pwsh -File tools/make_installer.ps1 -Build`（需要 Inno Setup 6）→ `dist/installer/T9Ime-<版本>-Setup.exe`。测试：`ctest -LE e2e`（单元、引擎回归、Windows 7 导入检查），`ctest -L e2e`（端到端，会移动鼠标，需要交互桌面）。版本号由 `CMakeLists.txt` 的 `project(VERSION)` 与 git 提交数生成，写入 DLL 版本信息。

## 开源协议

T9Ime 以 **GNU 通用公共许可证第 3 版（GPL-3.0）** 发布，全文见 [LICENSE](LICENSE)。输入法文本服务（`src/tip/`）的结构源自 [小狼毫 Weasel](https://github.com/rime/weasel)（GPL-3.0），见 `third_party/weasel/UPSTREAM.md`。

第三方组件：

| 组件 | 用途 | 协议 |
|---|---|---|
| [librime](https://github.com/rime/librime) 1.17.0（含 librime-lua 等插件） | 输入引擎（`rime.dll`，构建时下载） | BSD-3-Clause |
| [rime-ice 雾凇拼音](https://github.com/iDvel/rime-ice) | 方案与词库（构建时下载，固定 commit） | GPL-3.0 |
| [OpenCC](https://github.com/BYVoid/OpenCC) | 简繁转换数据 | Apache-2.0 |
| [Weasel 小狼毫](https://github.com/rime/weasel) | TSF 文本服务结构参考 | GPL-3.0 |
| [doctest](https://github.com/doctest/doctest) | 单元测试框架（仅测试） | MIT |

分发 T9Ime（含二进制）须遵守 GPL-3.0：提供对应源代码，并保留上述协议声明。
