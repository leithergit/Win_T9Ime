# M5 测试清单：控制 API（T9Ctl）与 IMM32 兼容

测试包：`dist/M5/T9Ime-M5-test.zip`（解压后与 M4 相同的方式注册：`register.bat`）。
新增文件（`x86\`、`x64\` 各一份）：

| 文件 | 用途 |
|---|---|
| `T9Ctl.dll` | 控制 API（C 导出，`__stdcall`），头文件在 `sdk\t9ctl.h`，C# 声明在 `sdk\T9Ctl.cs` |
| `t9ctl.exe` | 命令行：`show [chinese|english|number|symbol]`、`hide`、`toggle`、`mode <m>`、`pos x y`、`dock`、`status`、`activate [hwnd]`、`deactivate [hwnd]`、`installed`、`watch <秒>` |
| `TestHost.exe` | C++ 示例程序 |
| `TestHost.CS.exe` | C# 示例程序（Win7 自带 .NET 3.5 即可运行） |

64 位系统用 `x64\` 目录下的程序，32 位系统用 `x86\`。

## A. 示例程序（触屏设备）

先运行 `register.bat`，再打开 `x64\TestHost.exe`（32 位系统用 `x86\`）。

| # | 操作 | 预期 | 结果 | 备注 |
|---|---|---|---|---|
| A1 | 点"显示键盘" | 九宫格面板出现；窗口底部状态行显示"键盘：显示" | | |
| A2 | 依次点"中文""英文""数字""符号" | 面板切到对应布局，状态行的"布局"随之变化 | | |
| A3 | 点"隐藏键盘"，再点两次"切换" | 隐藏 → 显示 → 隐藏，状态行同步 | | |
| A4 | 显示键盘后，拖动面板顶部把它移到窗口上面，再点"停靠底部" | 状态行位置随拖动变化；停靠后回到屏幕底部中间 | | |
| A5 | 键盘显示时把 TestHost 窗口拖到屏幕下方、与键盘重叠 | 文本框自动变矮，不被键盘挡住 | | |
| A6 | 点"切走 T9Ime"，在文本框里用物理键盘打 `ni` | 直接出现 `ni`（不再是 T9Ime） | | |
| A7 | 点"切到 T9Ime"，再打 `nihao` 空格 | 出现"你好" | | |
| A8 | 关闭 TestHost，打开 `TestHost.CS.exe`，重复 A1–A3 | 与 C++ 版相同 | | .NET 程序 |

## B. 命令行

打开命令提示符，进入 `x64\`（或 `x86\`）目录：

| # | 命令 | 预期 | 结果 |
|---|---|---|---|
| B1 | `t9ctl installed` | 输出 `installed=1` | |
| B2 | `t9ctl show number` | 面板以数字布局出现 | |
| B3 | `t9ctl status` | `visible=1 mode=number rect=...` | |
| B4 | `t9ctl pos 100 100` | 面板移到左上角附近 | |
| B5 | 另开一个命令窗口运行 `t9ctl watch 60`，在第一个窗口里 `t9ctl hide`、`t9ctl show` | watch 窗口打印 `notify visible=0 ...`、`notify visible=1 ...` | |
| B6 | `t9ctl dock`，然后 `t9ctl hide` | 面板回到底部，然后隐藏 | |

## C. IMM32 兼容（自动化测试已在 Win11 通过，Win7 由虚拟机自动验证）

应用程序调用经典输入法 API 时 T9Ime 的反应，无需手工测试；如有使用老式 IMM32 程序（如旧版 VB/Delphi 软件）的业务系统，请在其中试：用该程序自带的中英文切换功能，观察 T9Ime 是否跟着切换。

## D. 附加测试（用户追加，2026-09-30 首轮均不符合，已修复）

| # | 操作 | 预期 | 结果 | 备注 |
|---|---|---|---|---|
| D1 | 在 TestHost 里点"显示键盘"，然后点另一个程序的窗口（或任务栏上的其他程序） | 键盘隐藏 | | 在同一个程序里点别的输入框，键盘不隐藏 |
| D2 | 在 TestHost 里点"显示键盘"，再用语言栏（或 Ctrl+Shift）切换到其他输入法 | 键盘立即隐藏 | | |
| D3 | 键盘显示为英文或数字布局时先隐藏它，把 TestHost 切到其他输入法，点文本框，等 2 秒，再切回 T9Ime | 键盘弹出，显示默认的中文九宫格 | | 数字框里切回时仍显示数字布局 |
| D4 | 在 TestHost 里点"显示键盘"，切换到其他程序，再切回 TestHost | 键盘隐藏后，回到 TestHost 时重新显示 | | 用"隐藏"键主动收起的键盘，切回时不再显示 |

## 回传
- 每一项的结果与备注；异常时附截图。
