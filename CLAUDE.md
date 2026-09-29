# T9Ime — 工作约定

新会话先读本文件和 `DEVLOG.md`；需求见 `Docs/SPEC.md`，设计见 `Docs/ARCHITECTURE.md`，计划见 `Docs/PLAN.md`，调研结论见 `Docs/research/`。

## 当前状态
M0 已完成并经用户确认（PLAN.md "决策结果"）。当前：M1 引擎原型。

## 关键决策（覆盖 SPEC，详见 ARCHITECTURE §0）
- Weasel fork（上游 `rime/weasel@d73f629`），GPL-3.0。
- **不用 uiAccess**；单一 T9Host 进程（引擎 + 面板）；物理键盘候选窗在 TIP 进程内绘制。
- 支持 Win7 SP1 与 Win10/11（**不支持 Win8.x、ARM64**），x86 与 x64 都是完整构建；`_WIN32_WINNT=0x0601`，Win8+ API 一律动态加载。
- **主要目标设备是公司的 Win7 触屏机**；开发机无触屏，触摸行为由同事真机测试——每个里程碑交付测试包 + `Docs/testing/` 清单。鼠标与触摸必须走同一面板代码路径。
- librime 1.17.0 官方包（内置 librime-lua）；rime-ice 完整词库，固定 commit `3aea6d3694fb3d94ec663641f021f788822897ad`。
- t9 方案是**数字码**（不是 SPEC 写的大写字母码）；`t9_processor` 用 t9.custom.yaml 移除。

## 构建（M1 起生效，待补充）
- CMake + Ninja + CMakePresets，MSVC v143，C++20，`/MT`。预设：`x64-Release`、`x86-Release`（均为全部组件）。
- 第三方：`third_party/fetch_librime.ps1`、`data/fetch_rime_ice.ps1`（SHA-256 校验）。

## 编码规范
- TIP：除系统 DLL 外零依赖；禁止 boost/.NET/Qt/C++WinRT；所有 COM 方法 `noexcept` 且内部 try/catch；不在宿主进程做耗时操作、不弹 MessageBox、不 ShellExecute。
- Host：只依赖 librime 和系统库；librime 调用只在引擎线程。
- 不记录任何用户输入内容；日志仅 Debug 构建且默认关闭。
- 禁止全局键盘钩子与 DLL 注入；TIP 所在线程的线程级 `WH_GETMESSAGE` 钩子已获允许。
- 遇到 API 行为不确定，先在 `tests/probes/` 写最小复现，不要猜。

## 已知陷阱
- 预部署的 `build/*.bin` 与 yaml 必须**保留 mtime** 复制，否则首次启动重建 prism（约 9 s）。
- `RimeContext.composition.cursor_pos/sel_*` 是 preedit 的 **UTF-8 字节偏移**。
- 官方 librime 无 `t9_processor`：回车会上屏原始数字、退格逐字母删，需前端实现语义。
- rime-ice 不带 opencc s2t 数据，需从 rime-deps 包补。
- Weasel 的 WeaselUI 静态导入 Shcore（Win8.1+），且开启了 OpenMP（vcomp140.dll）——都必须去掉/改动态加载。
- Windows 大小写不敏感：文档目录是 `Docs/`。
- 命令行输出中文用 `py -3 C:\Users\leith\.claude\tools\enc.py`，避免 cp936 乱码。
