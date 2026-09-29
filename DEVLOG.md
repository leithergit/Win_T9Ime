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
