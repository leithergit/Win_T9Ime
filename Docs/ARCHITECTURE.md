# T9Ime 架构设计（M0）

> 状态：M0 草案，2026-09-29。依据 `Docs/SPEC.md` 与 `Docs/research/*.md`，并纳入用户已做的决策（见 §0）。
> 标记：**[实测]** 本机已验证；**[文档]** 官方文档；**[需实测]** 进入对应里程碑前必须写最小复现程序验证。

## 0. 已定决策（覆盖 SPEC 的部分）

| # | 决策 | 对 SPEC 的影响 |
|---|---|---|
| U1 | **基于 Weasel fork**（上游 `rime/weasel@d73f629`），产品整体 GPL-3.0 | §2 "从零实现" → 裁剪复用 WeaselTSF / WeaselUI / RimeWithWeasel |
| U2 | **不使用 uiAccess**（用户决定彻底去掉）。单一 T9Host 进程（Medium IL）同时持有引擎和面板；签名恢复为 SPEC 的"预留、暂不启用" | 接受 3 个限制，见 §6.6 |
| U3 | **集成 librime-lua** | 官方 librime 1.17.0 rime.dll 已内置 lua/octagram/predict **[实测]**，保留 rime-ice 全部 lua 组件 |
| U4 | **完整词库** | 运行时数据 7z 压缩 24.3 MB **[实测]**；安装包预计 26–28 MB，**允许略超 25 MB**，报告实际值 |
| U5 | **接管 TabTip 自动弹出**（征得同意、备份原值、卸载还原） | §6 共存策略定为"关闭系统自动弹出 + 运行时让位" |
| U6 | 面板被点击而本 IME 未激活时**自动切换**到本 IME | 新增 Host→系统的 profile 激活流程（§6.4） |
| U7 | 支持 **Windows 7 SP1 – Windows 11** | §1 由 Win10 1809+ 扩大；Win8+ API 全部动态加载 + 降级 |
| U8 | 支持 **32 位操作系统** | x86 也构建完整 Host + rime.dll |
| U9 | Win7 前置补丁：**SP1 + KB2670838（平台更新）强制**，KB4474419（SHA-2）建议 | 安装器检测：缺强制项拒绝安装，缺建议项提示 |
| U10 | 物理键盘候选窗**在 TIP 进程内绘制**（沿用 Weasel）；面板及面板模式候选由 T9Host 绘制 | §3 "Host 画候选窗"改为分离 |
| U11 | Win7 分级：**emoji 禁用**；自动弹出用 `GetMessageExtraInfo` 推断；安装时关闭 Win7 平板输入面板自动弹出 | 见 §9 |
| U12 | **主要目标设备是公司的 Win7 触屏机**；开发机无触屏，真机测试由同事完成 | Win7 + WM_TOUCH 是一等路径，不是降级路径；每个里程碑交付测试包 + 测试清单 |
| U13 | **不支持 Win8/8.1**（暂不考虑）；**不支持 ARM64** | 分级只剩 Win7 与 Win10/11 两档 |
| U14 | 允许 TIP 线程级 `WH_GETMESSAGE` 钩子；不支持 OTS 提权；TabTip 接管默认勾选且不用未公开接口；KB4474419 建议安装（不强制），KB2670838 强制；弃用 WTL 设置界面；启用英文九键；**移除 radical_pinyin 反查** | 见各节 |

## 1. 进程与模块

```
┌─────────────────────────── 应用进程（任意：Win32/WPF/UWP/提权/AppContainer）────────────────┐
│ T9Tip.dll（x86 / x64）  —— 由 WeaselTSF 裁剪                                     │
│  · TSF 接口、按键判定（OnTestKeyDown 执行并缓存）、composition、edit session                    │
│  · 物理键盘候选窗（WeaselUI 精简版：D2D1.0 DC RT + DWrite + GDI，layered）                      │
│  · UIElement / InputScope / compartment 双向同步 / 焦点与触摸来源上报                           │
│  · IPC 客户端：请求管道（同步、带超时）+ 事件管道（Host→TIP 推送，后台线程）                    │
└───────────────────────────────▲───────────────────────────────▲───────────────────────────────┘
                   请求/响应（每 TIP 线程一条）       推送（每进程一条，按 tid 分发）
┌───────────────────────────────┴───────────────────────────────┴──────┐
│ T9Host.exe（每会话单实例，Medium IL，asInvoker；x64 或 x86 按 OS）      │
│  引擎线程：librime（rime.dll，含 lua），rime_ice + t9 两个 schema       │
│            九键前端逻辑（拼音栏/preedit/撤销/回车退格语义）              │
│  UI 线程： 九宫格面板 + 面板候选栏 + 拼音栏（D2D HwndRT + DWrite），     │
│            不激活窗口；托盘；设置窗口                                    │
│  IPC：     焦点注册表（当前焦点 TIP = {pid,tid,conn,seq}）、推送路由、  │
│            T9Ctl 服务端                                                  │
│  其他：    部署、词库导入导出、TabTip 设置管理、自动切换本 IME           │
└──────────────────────────────────────────────────────────────────────┘
┌──────────────────────────────┐   ┌───────────────────────────────┐
│ T9Ctl.dll（x86/x64，C 导出）   │   │ t9ctl.exe（命令行，调用 T9Ctl） │
│  应用进程内：本进程 ActivateProfile；其他操作经 Host 控制管道        │
└──────────────────────────────┘   └───────────────────────────────┘
```

- **单进程 Host**：librime 调用集中在一个"引擎线程"串行执行（librime 非线程安全）；面板 UI 在独立 UI 线程，两者以消息队列通信，面板绘制不被引擎阻塞。代码上面板模块（`src/host/panel/`）只依赖一个窄接口，保留将来拆出独立进程的可能，但不做为目标。
- **为什么物理键盘候选窗留在 TIP（U10）**：沿用 Weasel 现状，窗口 owner 是应用窗口，天然跟随应用的 z-band（包括开始菜单搜索 SearchHost 这类沉浸式 AppContainer）与 DPI；不经跨进程绘制。所以**去掉 uiAccess 不影响物理键盘在任何地方的候选显示**。面板可见时，TIP 候选窗隐藏，候选改由面板顶栏显示。

## 2. 目录结构

```
src/common/        协议定义、序列化（TLV）、共享类型、OS 版本/动态 API 加载（无外部依赖）
src/tip/           T9Tip.dll（源自 WeaselTSF，保留文件名以便 cherry-pick）
src/tip/ui/        TIP 候选窗（源自 WeaselUI 精简：去 OpenMP、去 boost、动态加载 Shcore）
src/host/engine/   librime 封装（源自 RimeWithWeasel）、t9 前端（拼音栏/preedit/撤销）
src/host/ipc/      管道服务端（请求、事件、控制三类端点）
src/host/panel/    九宫格面板（窗口、输入抽象 WM_POINTER/WM_TOUCH、布局、绘制）
src/host/app/      托盘、设置窗口、部署、TabTip 设置管理、IME 切换
src/ctl/           T9Ctl.dll、t9ctl.exe
tools/t9repl/      引擎命令行（M1）
tools/gen_syllables/  音节→数字码表 + 音节频率生成脚本（Python，构建期运行）
tools/check_imports/  PE 导入表检查（禁止 Win7 不存在的静态导入）
data/rime-ice/     rime-ice 固定 commit（下载脚本 + 哈希）
data/custom/       t9.custom.yaml、rime_ice.custom.yaml、default.custom.yaml、t9ime.yaml
tests/             doctest 单元测试、引擎回归（t9repl 脚本）
samples/TestHost/  C++ 与 C# 示例
installer/         Inno Setup 脚本（6.3+ 最低支持 Win7 SP1 [文档]）
third_party/       librime 预编译包（脚本下载，SHA-256 校验）、weasel 上游跟踪说明
Docs/              SPEC、ARCHITECTURE、PLAN、research/
```

构建：CMake + Ninja + CMakePresets，MSVC v143，C++20，`/MT`。预设 `x64-Release`、`x86-Release`（两者都构建全部组件，因为 U8）。全局 `_WIN32_WINNT=0x0601`、`WINVER=0x0601`。

## 3. IPC 设计（重写 Weasel IPC，去 boost）

### 3.1 管道端点
| 端点 | 名称 | 客户端 | 模式 |
|---|---|---|---|
| 请求 | `\\.\pipe\T9Ime.<SessionId>.<UserSid>.req` | TIP（每线程一条连接） | 请求-响应，同步 + 超时 |
| 事件 | `\\.\pipe\T9Ime.<SessionId>.<UserSid>.evt` | TIP（**每进程一条**，后台线程） | Host→TIP 单向推送，TIP 回 ack |
| 控制 | `\\.\pipe\T9Ime.<SessionId>.<UserSid>.ctl` | T9Ctl / t9ctl.exe | 请求-响应 + 可见性订阅 |

- 安全描述符（Win8+）：`S:(ML;;NW;;;LW)D:P(A;;GA;;;SY)(A;;GRGW;;;<UserSid>)(A;;GRGW;;;AC)(A;;GRGW;;;S-1-15-2-2)`。Win7 无 AppContainer，去掉 AC 与 S-1-15-2-2（SDDL 别名 `AC` 在 Win7 上是否识别 **[需实测]**，按 OS 版本分别构造），保留 Low 标签。
- 服务端 `FILE_FLAG_FIRST_PIPE_INSTANCE | PIPE_REJECT_REMOTE_CLIENTS`；客户端 `SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION`。
- `SessionId` 由 `ProcessIdToSessionId` 获得；`UserSid` 从进程令牌取（AppContainer 进程令牌的用户 SID 仍是登录用户 **[文档]**）。提权进程（同一用户）可连接 **[文档]**；"以其他管理员身份运行"（OTS）默认不支持（透传）。

### 3.2 编码
定长头 `{u32 magic, u16 version, u16 type, u32 seq, u32 length}` + TLV 体（UTF-16 文本、i32、RECT 全 32 位、DPI）。编解码在 `src/common/protocol.*`，TIP 与 Host 共用，doctest 覆盖。取消 Weasel 的 RECT 32 位压缩与 boost text_archive。

### 3.3 超时与失效
- TIP 请求使用 OVERLAPPED + `WaitForSingleObject`：按键类 **150 ms**，其他 500 ms。超时 → 断开该连接、本次按键透传、进入退避（1s→5s→30s）。**任何情况下 TIP 不阻塞宿主 UI 线程超过超时值。**
- 连接失败时按 §8 策略尝试恢复 Host（限频，异步线程）。
- 管道 I/O 由线程池完成，请求投递到引擎线程；不持引擎锁调用 Shell_NotifyIcon（Weasel `d73f629` 的死锁教训）。

### 3.4 消息集（初版）
TIP→Host：`Hello{pid,tid,exe,isAppContainer,isElevated,osBuild}`、`FocusIn{seq,hwnd,inputScopes[],readOnly,touchOrigin}`、`FocusOut`、`Key{vk,scan,flags,ibusKeycode,mask,isTest}`、`CaretRect{rect,dpi}`、`SelectCandidate{index}`、`ChangePage`、`CompartmentChanged{open,conversion}`、`UIElementShown{bool}`、`PushAck{seq,consumed}`。
Host→TIP（响应或推送）：`Result{eaten, commit, preedit, cursor, candidates[], status{ascii,composing,schema}, styleRev}`、`Style{…}`（仅样式版本变化时）。
Ctl：见 §7。

## 4. TIP（T9Tip.dll）

基于 WeaselTSF 修改，保持文件名便于 cherry-pick。必须的改动（详见 research/weasel.md §5.2）：

1. 去 boost：`thread_specific_ptr`→`thread_local`；IPC 全部替换为 §3。去 OpenMP。
2. **Win7 兼容**：`Shcore.dll`（GetDpiForMonitor 等）、`GetDpiForWindow`、`GetCurrentInputMessageSource`、`SetThreadDpiAwarenessContext` 等 Win8+ API 一律 `GetProcAddress` 动态加载；构建后由 `tools/check_imports` 检查导入表（参照 Win7 SP1 的导出集）。
3. 删除 `DllMain` 中的 `SetUnhandledExceptionFilter`；删除宿主进程内的 MessageBox/ShellExecute；所有 COM 入口 `noexcept` + try/catch。
4. `Deactivate`：先结束/终止 composition，再注销全部 sink（含 ThreadFocusSink），修正重复 Uninit；补上 `ITfActiveLanguageProfileNotifySink` 的 Advise 与 QI。
5. 按键：保留"OnTestKeyDown 即执行并缓存结果、OnKeyDown 返回缓存"，但缓存改为**每线程**、以 (wParam, lParam, 时间戳) 校验，不一致则重新处理；进程级 static 改为线程局部。
6. compartment 双向同步：OPENCLOSE 按真实开关；CONVERSION 的 NATIVE 位 → Host `set_option(ascii_mode)`；回写加 guard 防回环。
7. InputScope：`OnSetFocus` 与 `OnEndEdit` 中读 `GUID_PROP_INPUTSCOPE`（`ITfInputScope::GetInputScopes`），随 FocusIn 上报；密码/私密 → 不组字、强制英文；数字/电话 → 面板默认数字键盘；URL/Email → 英文；只读/无上下文 → 不弹面板。
8. 触摸来源判定：`GetCurrentInputMessageSource` → `GetCIMSSM` →（Win7 及兜底）**线程级** `WH_GETMESSAGE` 钩子记录最近鼠标消息的 `GetMessageExtraInfo`（`(x & 0xFFFFFF00)==0xFF515700` 表示触摸/笔提升）。钩子只挂 TIP 所在线程，不是全局钩子（用户已确认允许，U14）。
9. 推送：`ActivateEx` 时在线程管理器线程创建 `HWND_MESSAGE` 窗口；进程级事件线程收到推送后按 tid 找窗口 `PostMessage`；UI 线程校验焦点 seq 后 `RequestEditSession(TF_ES_ASYNCDONTCARE|TF_ES_READWRITE)`，edit session 对象自带全部数据；无焦点/只读/断开 → ack `consumed=false`。取消 Weasel 的 `SendInput(VK_SELECT)` 选词方式，鼠标选词也走 edit session。
10. 候选窗（TIP 内）：WeaselUI 精简，D2D 1.0（`ID2D1DCRenderTarget`，Win7 SP1+PU 可用）+ DWrite + GDI。样式由 Host 下发（styleRev 变化才传）。面板可见时隐藏。`BeginUIElement` 返回 show=FALSE 时不创建窗口，只提供 `ITfCandidateListUIElement(Behavior)`。发出 `EVENT_OBJECT_IME_SHOW/HIDE/CHANGE`。
11. 注册：仅 zh-CN(0x0804) 一个 profile；类别仅 SPEC §4 所列（去掉 Weasel 的 SECUREMODE、COMLESS 等多余类别）；DLL 装在 Program Files 而非 System32；`DllRegisterServer/Unregister` 幂等；修正 `Register.cpp` 的 "Microsft" 笔误。
12. 配置：TIP 不读 HKCU/%APPDATA%（AppContainer 里读不到），一切配置经 Host 下发。
13. 实现 `ITfFnGetPreferredTouchKeyboardLayout`（Win8+），返回简中触摸布局，使系统触摸键盘在用户手动打开时仍可配合（也是 §6.6 开始菜单搜索场景的替代方案）。

## 5. Host 引擎层

### 5.1 librime
- 官方 `librime 1.17.0`（`33e7814`）msvc-x64 / msvc-x86 预编译包，脚本下载 + SHA-256 校验。rime.dll 仅导入 KERNEL32/USER32/dbghelp **[实测，x64]**，Win7 可加载性 **[需实测]**（x86 包同样检查）。
- traits：`shared_data_dir=<安装目录>\data`，`prebuilt_data_dir=<安装目录>\data\build`，`user_data_dir=%APPDATA%\T9Ime\Rime`，`staging_dir=<user>\build`，`log_dir`：Release 为空（不记日志，SPEC §11 隐私），Debug 可选。
- **启动不跑 maintenance**（M1 实测）：librime 以 mtime 判断重建，zip 解压（2 秒精度、本地时区）后会重建约 7 s；跳过 maintenance 后直接用 `prebuilt_data_dir` 的数据，启动 <20 ms，学习正常 **[实测]**。用户配置变更（模糊音、简繁默认等）由 Host 显式重新部署；版本升级时清理 `<user>uild`，避免旧 staging 遮蔽新的预部署数据。
- opencc：从 rime-deps 包补 `s2t.json + STCharacters.ocd2 + STPhrases.ocd2`（简繁切换需要）**[实测]**。

### 5.2 session 策略
- 每个 TIP 线程一个"物理键盘 session"（rime_ice）；Host 另持**一个面板 session**（t9），面板输入总是作用于当前焦点目标。两者共享 `rime_ice.userdb`，学习互通 **[实测]**。
- 面板开始输入时，若焦点线程的物理键盘 session 正在组字，先清空它（同步清 TIP composition），避免两个 composition 并存。
- 中英状态 `ascii_mode` 在两个 schema 间同步。

### 5.3 schema 补丁（data/custom/）
- `t9.custom.yaml`：从 `engine/processors` 去掉 `t9_processor`（官方 librime 中不存在，缺失只报错不影响功能 **[实测]**）；保留 lua_translator（U3）；Win7 上通过 `set_option("emoji", false)` 关闭 emoji（U11），不改 schema。
- `rime_ice.custom.yaml`：保留全部 lua；**移除 radical_pinyin 部件拆字反查**（U14，省约 1.4 MB 压缩后）：去掉 `reverse_lookup_filter@radical_reverse_lookup`、`affix_segmentor@radical_lookup`、`lua_filter@*search@radical_pinyin` 及其依赖。
- 编码为**数字码**（`derive/[abc]/2/`…），SPEC §5 的"大写字母码"已过时 **[实测]**。
- **英文九键 = 独立方案 `t9_eng`**（`data/custom/t9_eng.schema.yaml`）：主翻译器为 melt_eng 词库，speller 用 xlit 转数字码，单独的 `t9_eng.prism.bin`（0.8 MB），开启 completion。不挂进 t9：非主翻译器的 prism 不会被部署 **[实测]**，且 derive 会使英文 prism 膨胀。`default.custom.yaml` 的 schema_list 为 rime_ice、t9、t9_eng。

### 5.4 九键前端（`src/host/engine/t9/`，纯 C++，可单测）
依据 research/t9-frontends-and-wanxiang.md 与 librime-rime-ice.md：
- **音节表**：`tools/gen_syllables` 从 rime-ice 词典提取全部音节 → `syllable→digits` 与音节频率（按词频累加），生成 C++ 头文件（构建期产物，不手写）。
- **拼音栏**：对第一个未确认数字段做前缀枚举，按匹配长度降序、再按音节频率降序；单键时补充该键的字母。
- **点选**：`input = get_input(); 替换 [segStart, segStart+len) 为 "syl'"; set_input(input)`；压栈 `(segStart, 原数字串, 替换串)`。`zhong'4486` 可用 **[实测]**。
- **撤销**：退格时若撤销栈非空且光标位于最后确认段末尾 → 恢复该段数字码并 `set_input`；否则普通 BackSpace。
- **回车**：替代 `t9_processor` 的语义——上屏"由 comment 还原的拼音串"（纯字母），而不是原始数字（官方 librime 默认上屏原始数字 **[实测]**）。
- **preedit**：取高亮候选的 comment（空则向后找非空者），按音节逐个转数字码消费未确认段，余下数字映射为首字母；已确认段原样显示。`composition.cursor_pos` 等是 preedit 的 **UTF-8 字节偏移** **[实测]**，转换时注意。
- **1 键**：有候选时为分词 `'`，无输入时为符号序列（schema 行为，保持）。
- **候选**：`candidate_list_begin/next` 迭代全量（`94664486` 共 467 个，迭代 6.5 ms **[实测]**），按需分页送面板（首屏 + 懒加载）。

## 6. 面板（T9Host 内）

### 6.1 窗口
- `WS_POPUP`，`WS_EX_NOACTIVATE|WS_EX_TOPMOST|WS_EX_TOOLWINDOW`；`WM_MOUSEACTIVATE→MA_NOACTIVATE`，`WM_POINTERACTIVATE→PA_NOACTIVATE`（Win8+）；`SW_SHOWNOACTIVATE`。
- 输入：Win8+ 处理 `WM_POINTER*` 且不交给 DefWindowProc，`SetWindowFeedbackSetting` 关系统反馈；**Win7** 用 `RegisterTouchWindow`（WM_TOUCH）+ 鼠标消息，`MicrosoftTabletPenServiceProperty` 关长按右键。两套输入统一抽象为 `PanelPointerEvent`。
- 绘制：D2D `ID2D1HwndRenderTarget` + DWrite（Win7 SP1+PU 可用），按下态、长按连删、左滑清空自绘。
- DPI：manifest `dpiAware=true/pm` + `dpiAwareness=PerMonitorV2,PerMonitor`；Win10 1703+ PMv2，Win7 System DPI。
- 深浅色：Win10+ 读 `AppsUseLightTheme` 并监听 `WM_SETTINGCHANGE`；Win7 用浅色或设置中手选。
- 停靠/拖动：自己在指针移动中移动窗口；可选 AppBar 停靠底部；避开屏幕边缘手势区。位置尺寸按显示器保存到 %APPDATA%。

- 输入模式（M2 实现）：`pointer`（Win8+ 默认：WM_POINTER 处理触摸/笔，鼠标走 WM_LBUTTON*）、`touch`（Win7 默认：RegisterTouchWindow + WM_TOUCH，忽略由触摸提升的鼠标消息）、`mouse`（兜底：只用鼠标消息，触摸靠系统提升）。T9Host `--input` 可强制，供真机对比。
- 顶部横条：拖动移动面板；右端角标拖动调整大小（底边固定）。尺寸以 DIP 存 `%APPDATA%\T9Ime\panel.ini`。
- 线程：UI 线程（面板、托盘）+ 引擎线程（librime）。UI 把命令投递给引擎线程；引擎每处理一条命令发布一个快照（状态、候选前 120 个、上屏文字、Passthrough 输出），UI 按顺序输出，保证"上屏"与"透传按键/符号"不乱序。

### 6.2 布局
中文九键（候选栏可展开网格、左侧拼音栏/无输入时快捷标点、3×3 主键、右侧功能列 ⌫/重输/空格0/回车、底栏 符号/123/中英/隐藏）、数字、分类符号（中文/英文/数学/特殊）、英文九键（`t9_eng` 方案；1 键先上屏当前单词再出标点，空格上屏单词并补空格）。长按：⌫ 连删、数字键与空格键输入数字；⌫ 左滑清空。26 键为后期可选。

### 6.3 上屏路径
面板按键 → 引擎线程处理 → 推送给**当前焦点 TIP**（事件管道，带 focus seq）→ TIP edit session 上屏/更新 composition → ack。
降级：焦点窗口没有我们的 TIP 连接（非 TSF 应用，或切换 IME 失败）→ `SendInput(KEYEVENTF_UNICODE)` 上屏最终文本并记录（Debug 日志，不含内容）。对管理员进程受 UIPI 限制无效（§6.6）。

### 6.4 自动切换到本 IME（U6）
1. 面板被点击，Host 发现焦点线程没有已激活的本 TIP 实例；
2. `ActivateProfile(TF_PROFILETYPE_INPUTPROCESSOR, 0x0804, CLSID_T9Tip, GUID_Profile, NULL, TF_IPPMF_FORSESSION|TF_IPPMF_DONTCARECURRENTINPUTLANGUAGE)`；
3. 300 ms 内 TIP 未上报激活 → `PostMessage(hwndFocus, WM_INPUTLANGCHANGEREQUEST, INPUTLANGCHANGE_SYSCHARSET, zh-CN HKL)`（对管理员进程会被 UIPI 拦截）；
4. 仍失败 → 面板提示"请按 Win+Space 切换"；被点按键暂存，TIP 连上后补发，超时丢弃。
全部 **[需实测]**（全局/按窗口输入法模式 × 应用矩阵；Win7 默认按线程模式）。

### 6.5 自动显示/隐藏与 TabTip 共存
- 显示条件：TIP 上报 FocusIn 且可编辑、非只读、InputScope 允许，且 `touchOrigin=true`；设置中可关自动弹出。
- 隐藏条件：FocusOut 且 300 ms 内无新 FocusIn、无编辑上下文、应用/用户请求。面板刚被点过（1 s 内）时不因焦点抖动隐藏。
- TabTip：安装/首次运行（勾选同意后）备份并设置 `HKCU\Software\Microsoft\TabletTip\1.7\TouchKeyboardTapInvoke=0`（Win11）、`EnableDesktopModeAutoInvoke=0`（Win10）；Win7 平板输入面板的自动弹出设置 **[需实测，注册表项待查]**。运行时（Win8+）自动弹出前查 `IFrameworkInputPane::Location`，系统键盘可见则我方让位；面板避开其矩形。卸载时还原。

### 6.6 不使用 uiAccess 的已知限制（U2，已接受）
1. **Win8+ 系统浮层**：开始菜单搜索、Win11 搜索框、操作中心/通知中心等处于更高 z-band，普通 TOPMOST 面板会被盖住。对策：TIP 上报的 exe 为 `SearchHost.exe`/`ShellExperienceHost.exe`/`StartMenuExperienceHost.exe` 等时，**不自动弹出面板**（避免"弹了但看不见"）；物理键盘及其候选窗不受影响（候选在 TIP 内绘制）；用户可手动打开系统触摸键盘，其按键仍进入本 IME（§4 第 13 条）。
2. **管理员进程 + 非 TSF 程序**：SendInput 降级被 UIPI 拦截，无法上屏。TSF 程序（绝大多数）走推送路径，不受影响。
3. **管理员进程中自动切换本 IME**：兜底的 `WM_INPUTLANGCHANGEREQUEST` 被拦截，只剩 FORSESSION 路径；失败则提示 Win+Space。
4. 独占全屏程序中面板可能不可见；这类程序按 SPEC 走 UIElement 路径。

## 7. 对外控制 API

- 标准 API：`ActivateProfile`（进程内 FORPROCESS）、`ImmSetOpenStatus` / OPENCLOSE compartment、`ImmSetConversionStatus(IME_CMODE_NATIVE)`——依赖 §4 第 6 条双向同步，M5 验证。
- `T9Ctl.dll`（C、`__stdcall`、x86/x64）：`T9_IsInstalled`、`T9_Activate(HWND)`（同线程：FORPROCESS ActivateProfile；同进程他线程：SendMessage WM_INPUTLANGCHANGEREQUEST；他进程：经 ctl 管道交 Host 执行 §6.4）、`T9_Deactivate`、`T9_ShowKeyboard(layout)`、`T9_HideKeyboard`、`T9_IsKeyboardVisible`、`T9_SetMode`、`T9_SetDock`/`T9_SetPosition`、`T9_RegisterVisibilityNotify(HWND)`（Host 对注册的 HWND `PostMessage(RegisterWindowMessage("T9Ime.Visibility"))`，wParam=可见；占用矩形通过 `T9_GetKeyboardRect` 查询。管理员进程注册的 HWND 需调用方 `ChangeWindowMessageFilterEx` 放行该消息，T9Ctl 内部代为调用）。
- `t9ctl.exe show|hide|toggle|mode <m>`。

## 8. Host 启动与恢复

- 安装：`HKCU\...\Run\T9Host`（按用户）+ 计划任务 `\T9Ime\Host`（登录触发、LeastPrivilege、IgnoreNew）；Host 单实例（`Local\T9Ime.Host.<SID>` 互斥体）；`RegisterApplicationRestart` 崩溃自动重启。
- TIP 连不上 Host（限频、异步线程）：AppContainer/Low IL → 只透传，等 Host 自恢复；提权进程 → 经 `ITaskService` 运行 `\T9Ime\Host`（避免拉起提权 Host）；普通 Medium → 同样运行计划任务（统一路径）。
- Win7：计划任务 2.0 API 可用；无 AppContainer 分支。

## 9. Windows 版本分级

Win8/8.1 不在支持范围（U13），代码上按"< Win10 走 Win7 路径"处理。

| 能力 | Win10 1809+/Win11 | Win7 SP1(+KB2670838，建议 KB4474419)——**主要目标** |
|---|---|---|
| 物理键盘全拼、TIP 候选窗 | ✓ | ✓ |
| 面板触摸输入 | WM_POINTER | WM_TOUCH + 鼠标 |
| 自动弹出判定 | GetCurrentInputMessageSource + 线程级钩子兜底 | 线程级钩子 + GetMessageExtraInfo（偶有误判） |
| TabTip 共存 | 关闭自动弹出 + InputPane 让位 | 关闭平板输入面板自动弹出 |
| DPI | Per-Monitor V2 | System DPI |
| 深浅色跟随 | ✓ | 手选 |
| emoji 候选 | ✓ | **禁用** |
| 面板在开始菜单搜索中 | 不弹出（§6.6） | 可用（Win7 开始菜单是普通窗口）**[需实测]** |
| AppContainer 应用 | ✓ | 不适用 |
| 自动切换本 IME | FORSESSION + 兜底 | 兜底 WM_INPUTLANGCHANGEREQUEST 为主（按线程模式）**[需实测]** |

工具链风险：MSVC v143 静态 CRT 生成的二进制在 Win7 上可运行性 **[需实测]**：构建期 `check_imports` 拦截 Win7 不存在的静态导入，每个里程碑的测试包交同事在 Win7 触屏真机冒烟。安装器 Inno Setup 6.3+ 最低支持 Win7 SP1 **[文档]**。KB2670838（平台更新，D2D/DWrite）强制；KB4474419（SHA-2）仅建议，未来启用签名时再改为强制。

## 10. 数据、体积、许可证

- 运行时数据（完整词库）：build 目录 76.2 MB，rime.dll+数据 7z 压缩 24.3 MB **[实测，x64]**；安装包同时带 x86 与 x64 二进制（数据共用），估计 26–28 MB。M7 报告实际大小与构成。
- 许可证：产品 GPL-3.0（Weasel fork）；librime BSD-3；rime-ice GPL-3.0-only；Lua MIT；OpenCC Apache-2.0；随 rime.dll 的 leveldb/yaml-cpp/marisa 等许可证在 LICENSES/ 列明；WTL MS-PL（若保留）。
- 隐私：Release 无日志；Debug 日志默认关闭且不含输入内容。

## 11. 与 SPEC 的差异汇总
1. §5 大写字母码 → 数字码；`t9_processor` 移除，回车/退格语义由 Host 前端实现。
2. §3 候选窗：物理键盘候选在 TIP 内绘制；面板在 T9Host 内；不使用 uiAccess。
3. §1 OS 范围：Win7 SP1–Win11，x86 完整支持。
4. §10 体积目标 25 MB → 允许略超，报告实际值。
5. §5 精简：不移除 lua（保留全部 lua 组件）。
6. §2 构建：Weasel 原 msbuild/xmake → CMake + Ninja（重写构建脚本）；TIP 与 Host 均去 boost。
