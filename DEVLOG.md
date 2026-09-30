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
