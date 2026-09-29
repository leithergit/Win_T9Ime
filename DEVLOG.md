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
