# Windows 平台调研：uiAccess、不激活触屏窗口、跨进程切换 TIP、TabTip 共存、AppContainer、TSF 推送、Host 拉起

> 适用：T9Ime（SPEC.md §3/§4/§6/§7）。调研日期 2026-09。
> 标记约定：**[文档]** = 微软官方文档明确；**[源码]** = 读 Weasel 源码得到；**[本机实测]** = 在 Win11 Pro 10.0.26200 上跑过；**[社区]** = 非官方资料；**需实测** = 未验证，必须写最小复现程序确认。
> 已定前提：Host 使用 uiAccess=true 并签名；用户接受修改 TabTip 自动弹出设置；面板被点击而本 IME 未激活时，自动把焦点应用切到本 IME。
> **后续决策（2026-09-29）**：用户决定**彻底不用 uiAccess**，面板并入 T9Host，物理键盘候选窗在 TIP 内绘制。下文 §1、§8 中关于 T9Panel/uiAccess 拆分的建议及 §9 的 D1/D2 已被取代，以 `Docs/ARCHITECTURE.md` 为准。

---

## 0. 结论速览（需要决策的放在最后 §9）

| # | 问题 | 结论 / 推荐 |
|---|---|---|
| 1 | uiAccess | 可行，但**对普通管理员账户（UAC 拆分令牌），uiAccess 进程会以 High IL 运行**。因此**不要让整个 Host（librime、设置、文件对话框、子进程）跑在 uiAccess 里**。推荐拆成：`T9Host.exe`（Medium、asInvoker，持有 librime/IPC/托盘/设置）+ `T9Panel.exe`（uiAccess、签名、只负责面板/候选窗绘制和指针输入）。 |
| 2 | 不激活触屏窗口 | `WS_EX_NOACTIVATE\|WS_EX_TOPMOST\|WS_EX_TOOLWINDOW` + `WM_MOUSEACTIVATE→MA_NOACTIVATE` + `WM_POINTERACTIVATE→PA_NOACTIVATE`；处理 WM_POINTER 且不交给 DefWindowProc；用 `SetWindowFeedbackSetting` 关掉系统长按/点击视觉反馈；不要 `RegisterTouchWindow`；**不调用** `EnableMouseInPointer`（或只在 Panel 进程里调用一次）。 |
| 3 | 跨进程切到本 TIP | 没有官方“把别的进程切到某 TIP”的 API。推荐顺序：① Host/Panel 调 `ActivateProfile(..., TF_IPPMF_FORSESSION)`（默认“输入法跟随用户”模式下社区工具证实可用，**需实测**）；② 兜底：`ActivateProfile(FORSESSION\|DONTCARECURRENTINPUTLANGUAGE)` 预设后，向焦点窗口 `PostMessage(WM_INPUTLANGCHANGEREQUEST, 0, zh-CN HKL)`（uiAccess 可越过 UIPI）。`T9_Activate(HWND)`：在 HWND 所属 UI 线程内调 `ActivateProfile(TF_IPPMF_FORPROCESS)`。 |
| 4 | TabTip 共存 | 安装/首启时（征得用户同意并备份原值）把 `HKCU\Software\Microsoft\TabletTip\1.7\TouchKeyboardTapInvoke=0`（Win11）与 `EnableDesktopModeAutoInvoke=0`（Win10）关掉；运行时用 `IFrameworkInputPane::Location` 检测系统键盘是否在显示，若在则我方面板让位（或经用户设置用未公开的 `ITipInvocation::Toggle` 关掉它）。**没有**让第三方 TIP 声明“我自带触摸 UI”的官方机制。 |
| 5 | 触摸导致的焦点 | TIP 在 `ITfThreadMgrEventSink::OnSetFocus` 里调 `GetCurrentInputMessageSource`，得到 `IMDT_UNAVAILABLE` 时再调 `GetCIMSSM`；再兜底用“最近一次指针按下的类型+时间”（线程内 WH_MOUSE/WH_GETMESSAGE **线程级** hook 读 `GetMessageExtraInfo` 的 0xFF515700 签名，非全局 hook）。各类应用行为**需实测**。 |
| 6 | AppContainer/LPAC | Program Files 默认 ACL 已对 S-1-15-2-1 与 S-1-15-2-2 授予 RX **[本机实测]**，不要破坏继承。管道 SDDL 推荐：`S:(ML;;NW;;;LW)D:(A;;GA;;;SY)(A;;GRGW;;;<用户SID>)(A;;GRGW;;;AC)(A;;GRGW;;;S-1-15-2-2)`。本机没发现“会承载文本输入的 LPAC 进程”**[本机实测]**。提权进程可以连接 Medium 用户的管道。 |
| 7 | TSF 推送 | 后台线程收包 → `PostMessage` 到 TIP 在**线程管理器线程**上创建的 message-only 窗口 → UI 线程 `RequestEditSession(TF_ES_ASYNCDONTCARE\|TF_ES_READWRITE)`。编辑会话对象必须自带全部数据（可能异步执行）；无焦点/只读/断开的 context 要丢弃并通知 Host。 |
| 8 | Host 拉起 | AppContainer 里 CreateProcess 起来的子进程仍是 AppContainer，拉起 Host 没意义；uiAccess exe 不能被 CreateProcess 直接启动（740）。推荐：Run 键 + 登录计划任务（LeastPrivilege）启动 **非 uiAccess 的 T9Host**；T9Host 用 `ShellExecuteEx` 拉 T9Panel；T9Host 与 T9Panel 互为看门狗；普通 Medium 进程中的 TIP 可以 `ShellExecuteEx` 拉 Host，提权进程中的 TIP 改为“运行计划任务”；AppContainer 中的 TIP 只透传按键，等看门狗恢复（可选：COM LocalServer 代理，**需实测**）。 |

---

## 1. uiAccess

### 1.1 使用条件
- 三个条件 **[文档]**：① 清单 `<requestedExecutionLevel level="asInvoker" uiAccess="true"/>`（MSVC 链接选项 `/MANIFESTUAC:"level='asInvoker' uiAccess='true'"`）；② Authenticode 签名，证书链到**本机** Trusted Root；③ 安装在“需要 UAC 才能写”的安全位置（`Program Files`、`Program Files (x86)`、`Windows\System32`）。
  - https://learn.microsoft.com/windows/win32/winauto/uiauto-securityoverview
  - https://learn.microsoft.com/cpp/build/reference/manifestuac-embeds-uac-information-in-manifest
  - 安全位置策略：https://learn.microsoft.com/windows/client-management/mdm/policy-csp-localpoliciessecurityoptions （“Windows enforces a PKI signature check … regardless of the state of this security setting”，即关掉“仅安全位置”策略也仍然要签名。）
- 官方措辞是“uiAccess 仅供辅助技术使用”。我们是屏幕键盘，属于同类场景（osk.exe 就是这么做的），但它**不是**给“只想盖在别人上面”的应用用的。分发签名时要考虑这一点，风险可以接受。
- 开发期测试签名：用户态 uiAccess **与 `bcdedit /set testsigning` 无关**。做法是：自签一张代码签名证书，导入 `LocalMachine\Root`（以及 `TrustedPublisher`）→ signtool 签名 → 复制到 `C:\Program Files\T9Ime\` 后运行。AutoHotkey 的 EnableUIAccess 就是这样做的 **[社区]**：https://github.com/Milly/AutoHotKey-scripts/blob/master/EnableUIAccess/EnableUIAccess.ahk 。建议 CMake 加一个 `dev-sign-install` 目标。调试时用 VS“附加到进程”，不要用 F5（F5 用 CreateProcess 启动，会得到 740，见 1.4）。

### 1.2 完整性级别（关键，影响架构）
- AppInfo 服务创建 uiAccess 进程时：**UAC 拆分令牌的管理员（最常见的家用配置）→ IL 被设为 High**；普通标准用户 → 调用者 IL + 16（Medium Plus，0x2010）**[社区，Project Zero / tiraniddo]**：
  - https://projectzero.google/2026/02/windows-administrator-protection.html
  - https://www.tiraniddo.dev/2019/02/accessing-access-tokens-for-uiaccess.html
  - 官方文档的说法是“admin 用户启动 → high IL”：https://learn.microsoft.com/windows/win32/winauto/uiauto-securityoverview
- Win11 “Administrator Protection”模式下，uiAccess 进程用受限用户令牌，但 IL 仍是 High（Project Zero 已报告为漏洞，微软可能修改行为）→ **需实测当前版本**。
- 后果：
  1. uiAccess 进程启动的子进程都是 High IL（设置、部署器、“打开网页”都会被“半提权”）。
  2. Medium 进程无法向它的窗口 SendMessage/PostMessage（UIPI）。T9Ctl 与它通信必须走管道，或用 `ChangeWindowMessageFilterEx` 显式放行。
  3. 它创建的内核对象默认带什么标签需要确认（High IL 进程创建的对象默认 Medium 标签，**需实测**）。管道的低完整性 SACL 要显式设置。
  4. 被攻破时影响面大。librime + leveldb + yaml 解析的攻击面不小。
- ⇒ **推荐拆分**：`T9Panel.exe`（uiAccess，只有 D2D/DWrite 绘制、指针处理、跟 Host 的管道客户端，不加载 librime，不弹文件对话框，不启动子进程，只有一个例外：见 §8 的“运行计划任务”）+ `T9Host.exe`（Medium，asInvoker）。Panel 可以正常加载系统 DLL、用 D2D；把 librime 放进去技术上也能跑，但不推荐。

### 1.3 z-order band
- Windows 8+ 的窗口分 band，从低到高大致是 **[社区，ADeltaX 逆向]**：`ZBID_DESKTOP`（普通窗口，含 UWP 应用窗口）< `IMMERSIVE_APPCHROME`（任务视图）< `IMMERSIVE_MOGO`（开始菜单、搜索）< `IMMERSIVE_INACTIVEMOBODY`（画中画）< `IMMERSIVE_NOTIFICATION`（操作中心、通知、音量/网络浮层）< `IMMERSIVE_EDGY` < `SYSTEM_TOOLS`（置顶的任务管理器、Alt-Tab）< `LOCK`（锁屏）< `ABOVELOCK_UX` < `IMMERSIVE_IHM` < `GENUINE_WINDOWS` < **`ZBID_UIACCESS`（osk、放大镜）**。
  - https://blog.adeltax.com/window-z-order-in-windows-10/
  - 每个 band 内部再分 topmost / 非 topmost。普通进程的 `WS_EX_TOPMOST` 盖不住开始菜单、搜索、操作中心。
- uiAccess 进程的 topmost 窗口可以“在任何时候处于 z 序最顶”**[文档]**（uiauto-securityoverview）。实际用哪个 band（是否需要未公开的 `CreateWindowInBand(..., ZBID_UIACCESS)`）**需实测**：先用普通 `CreateWindowEx(WS_EX_TOPMOST)`，验证能否盖过开始菜单、搜索、操作中心、Alt-Tab。不行再考虑 `CreateWindowInBand`（未公开 API，GetProcAddress 取，只有 uiAccess 进程能用 ZBID_UIACCESS，否则返回 ERROR_ACCESS_DENIED **[社区]**）。
- **锁屏**：ZBID_UIACCESS 在 ZBID_LOCK 之上，面板可能盖在锁屏上。必须 `WTSRegisterSessionNotification`，在 `WTS_SESSION_LOCK` 时隐藏、`UNLOCK` 时恢复。凭据输入在 Winlogon 安全桌面上（LogonUI），我们的窗口本来就不在那张桌面上。UAC 提示框同理。
- **全屏应用**：DWM 合成的全屏/无边框窗口（含 flip model 的“全屏优化”）上，uiAccess topmost 应该可见；真正的独占全屏（FSE）会绕过合成，覆盖层可能导致模式切换或不可见 → **需实测**。§4 的 UIElement(UILess) 路径就是为这类程序准备的。
- **注意**：候选窗如果由其他进程绘制（SPEC 设计是 Host 绘制），**在开始菜单搜索（SearchHost，IMMERSIVE band）里，普通进程的 topmost 窗口会被盖住**。所以候选窗要么放在 uiAccess 的 T9Panel 里，要么像 Weasel 那样在 TIP 进程内绘制（见 §9 决策 D2）。

### 1.4 UIPI 绕过与启动陷阱
- uiAccess 进程可以向更高 IL 的窗口 `SendInput`/`PostMessage`/`SendMessage`（“bypass user interface protection levels and drive input to higher-permission windows”）**[文档]**。uiAccess 进程用 SendInput 注入的输入，`GetCurrentInputMessageSource` 看到的是 `IMO_HARDWARE`（与真实硬件相同）**[文档]** https://learn.microsoft.com/windows/win32/api/winuser/ne-winuser-input_message_origin_id 。这对 SendInput 降级路径有利；对“判定触摸来源”有影响，见 §5。
  - 标准用户（Medium Plus）的 uiAccess 仍然**不能**驱动 High IL（“以管理员身份运行”）窗口 **[文档]**。对拆分令牌管理员来说，uiAccess 本身就是 High，可以驱动。
- 启动陷阱：
  - **不带 uiAccess 的父进程用 `CreateProcess` 启动 uiAccess exe → 失败，`ERROR_ELEVATION_REQUIRED (740)`**。必须用 `ShellExecuteEx`（内部走 AppInfo 的 `RAiLaunchAdminProcess`）。父进程本身有 uiAccess 时 CreateProcess 可以（子进程继承）**[社区 + 常识，需实测]**。
  - **计划任务直接启动 uiAccess exe → 0x800702E4（即 740）**，因为任务计划用 CreateProcess **[社区]**：https://windows-hexerror.linestarve.com/q/so35407949-run-c-wpf-application-with-uiaccess-true-in-manifest-at-startup 。⇒ 计划任务只启动非 uiAccess 的 T9Host，由它 ShellExecuteEx 启动 T9Panel。
  - HKCU/HKLM `Run` 键由 Explorer 经 ShellExecute 启动，**应该**能启动 uiAccess exe（**需实测**；有人报告“启动”文件夹快捷方式失败，原因不明）。
  - `DcomLaunch`/服务用 `CreateProcessAsUser` 启动 → 同样会 740。
- 可以正常持有 librime 吗？可以。uiAccess 不限制文件和网络访问，只是 IL 更高（见 1.2），所以不推荐这样做。

---

## 2. 不激活的触屏窗口（T9Panel）

- 样式：`WS_EX_NOACTIVATE | WS_EX_TOPMOST | WS_EX_TOOLWINDOW`（加 `WS_POPUP`）。显示用 `ShowWindow(SW_SHOWNOACTIVATE)`/`SetWindowPos(..., SWP_NOACTIVATE)`。`WS_EX_NOACTIVATE`：“用户点击时不成为前台窗口”，也不出现在任务栏。
- `WM_MOUSEACTIVATE → MA_NOACTIVATE`（鼠标）；`WM_POINTERACTIVATE → PA_NOACTIVATE`（触摸/笔）**[文档]** https://learn.microsoft.com/windows/win32/inputmsg/wm-pointeractivate 。注意：多指同时按下时，只有第一个指针有这次“激活机会”，其余指针的输入照样会到达。两个都处理，互为冗余。
- 输入：
  - 处理 `WM_POINTERDOWN/UPDATE/UP/CAPTURECHANGED`，处理完**不要**交给 DefWindowProc，否则会被提升为鼠标消息，还会触发长按右键模拟。
  - `EnableMouseInPointer` 在整个进程生命周期内只能调用一次，影响进程内所有窗口 **[文档]** https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-enablemouseinpointer 。如果 Panel 是独立进程，可以在启动时 `EnableMouseInPointer(TRUE)` 统一成指针模型；不调用也行，鼠标就走 WM_LBUTTON*。
  - **不要** `RegisterTouchWindow`：它切到 WM_TOUCH 模型，和 WM_GESTURE 互斥，旧且容易出错。WM_POINTER 默认就会送达。
  - `SetWindowFeedbackSetting(hwnd, FEEDBACK_TOUCH_CONTACTVISUALIZATION / FEEDBACK_TOUCH_TAP / FEEDBACK_TOUCH_PRESSANDHOLD / FEEDBACK_TOUCH_RIGHTTAP / FEEDBACK_GESTURE_PRESSANDTAP, 0, sizeof(BOOL), &FALSE)` 关掉系统的圈圈反馈和长按反馈，按下态由自己绘制 **[文档]** https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-setwindowfeedbacksetting 。“删除键长按连删”自己用计时器实现。
  - 旧的平板长按→右键：再用 `SetProp(hwnd, L"MicrosoftTabletPenServiceProperty", TABLET_DISABLE_PRESSANDHOLD…)`，或处理 `WM_TABLET_QUERYSYSTEMGESTURESTATUS` 兜底（**需实测**，走 WM_POINTER 时通常不需要）。
  - 不要调用 `RegisterPointerInputTarget`：它会把全桌面同类指针输入重定向给自己，只有 uiAccess 能调用 **[文档]**，不是我们要的。
- 拖动：自己在 WM_POINTERUPDATE 里移动窗口。不要用 `WM_NCLBUTTONDOWN/HTCAPTION`，它进入系统移动循环，可能产生激活副作用（**需实测**）。
- 边缘手势：Win11 从屏幕下/左/右边缘滑入会触发任务栏、小组件、通知中心；Win10 平板模式同理。停靠在屏幕底部时，按键区不要贴到最底几个像素。可选：`SHAppBarMessage(ABM_NEW)` 做成 AppBar，预留工作区（类似 TabTip 停靠模式），让最大化窗口自动让位。
- DPI：进程清单写 `<dpiAwareness>PerMonitorV2</dpiAwareness>`，处理 `WM_DPICHANGED`（用建议矩形），D2D 的 render target DPI 跟随 `GetDpiForWindow`。候选窗按目标光标所在显示器的 DPI 布局；从 TIP 拿到的 `GetTextExt` 坐标是物理像素还是逻辑像素，取决于宿主进程的 DPI 感知，要在 TIP 内用 `LogicalToPhysicalPointForPerMonitorDPI` 规整后再发给 Host（**需实测**，各应用不一致）。
- 其他需实测的副作用：点击不激活的外部窗口，是否会关掉目标应用的弹出层（浏览器地址栏下拉、菜单模态循环、组合框下拉）。菜单模态循环期间目标应用捕获了鼠标，点我方面板会先让菜单关闭。
- 暗色跟随：读 `HKCU\Software\Microsoft\Windows\CurrentVersion\Themes\Personalize\AppsUseLightTheme`，监听 `WM_SETTINGCHANGE("ImmersiveColorSet")`。

---

## 3. 跨进程切换到本 TIP

### 3.1 API 语义
- `ITfInputProcessorProfileMgr::ActivateProfile(dwProfileType, langid, clsid, guidProfile, hkl, dwFlags)` **[文档]** https://learn.microsoft.com/windows/win32/api/msctf/nf-msctf-itfinputprocessorprofilemgr-activateprofile
  - 不带标志：只作用于**调用线程**。
  - `TF_IPPMF_FORPROCESS`：作用于调用进程的所有线程。
  - `TF_IPPMF_FORSESSION`：“Activate this profile for all threads in the current desktop”。
  - `TF_IPPMF_ENABLEPROFILE`：顺便把该 profile 写入“本用户已启用”列表。
  - `TF_IPPMF_DONTCARECURRENTINPUTLANGUAGE`：当前输入语言不匹配时，把此 profile **标记**为“切换到该语言时要激活的那个”；不带这个标志时语言不匹配会直接失败。
- `ActivateKeyboardLayout(hkl, KLF_SETFORPROCESS)` 只作用于调用线程/进程，而且只认 HKL。对 TIP 来说，HKL 是语言级别的（zh-CN 的 0x08040804），只能切到“该语言当前/默认 profile”，**无法区分同一语言下的微软拼音和我们** **[文档 + 社区]**。
  - https://learn.microsoft.com/windows/win32/inputdev/about-keyboard-input
  - imebind 作者说明：“每个现有工具……都用 WM_INPUTLANGCHANGEREQUEST + HKL，无法区分共用一个 HKL 的两个 TSF 输入法”：https://github.com/zql70/imebind-csharp
- `WM_INPUTLANGCHANGEREQUEST`：由系统 Post 给焦点窗口；应用把它交给 DefWindowProc 就是接受 **[文档]** https://learn.microsoft.com/windows/win32/winmsg/wm-inputlangchangerequest 。外部 Post 它等于“请求切换语言”，lParam 是 HKL，同样只能到语言级别。有些应用会拒绝（不交给 DefWindowProc）。
- Win8 起默认“输入法按用户全局”，Win8.1 起转换模式（中/英）按输入上下文保存。“允许我为每个应用窗口设置不同的输入法”打开后，输入法选择才按窗口/线程保存 **[文档]** https://learn.microsoft.com/windows/compatibility/ime-mode-model-changed-from-per-user-to-per-thread 。（设置入口：设置 > 时间和语言 > 输入 > 高级键盘设置。对应注册表值**需实测**。）
- 语言栏/Win+Space：由系统输入切换器（Win11 在 TextInputHost/Explorer）触发，走会话级激活。内部机制未公开。在默认全局模式下，切换结果对所有应用生效。

### 3.2 社区证据
- imebind（C#/Rust）在**自己的进程**里调用 `ActivateProfile(TF_PROFILETYPE_INPUTPROCESSOR, …, TF_IPPMF_FORPROCESS|TF_IPPMF_FORSESSION)`，实现“按前台程序切换 TSF 输入法”**[社区]** https://github.com/zql70/imebind-csharp 。
- 另一个项目用 `ActivateProfile(TF_IPPMF_FORSESSION)` 在注入语音热键前把 WeType 设为会话级激活，耗时约 1ms、幂等 **[社区]** https://github.com/GetSayAll/remote-mic-app-windows/pull/17 。
- 反面证据：im-control 认为“TSF 要求调用方与前台窗口同线程”，于是用 SetWindowsHookEx 注入前台线程再调 ActivateProfile **[社区]** https://github.com/brglng/im-control 。SPEC §13 禁止 DLL 注入，这条路不能走。

### 3.3 推荐
**Host/Panel 侧（面板被点而本 IME 未激活）：**
1. Panel（或 Host）线程 `CoInitialize` → `CoCreateInstance(CLSID_TF_InputProcessorProfiles)` → QI `ITfInputProcessorProfileMgr` → `ActivateProfile(TF_PROFILETYPE_INPUTPROCESSOR, 0x0804, CLSID_T9Tip, GUID_T9Profile, NULL, TF_IPPMF_FORSESSION | TF_IPPMF_DONTCARECURRENTINPUTLANGUAGE)`。
2. 等焦点线程里的 TIP 激活后连上管道（`ActivateEx` 时 TIP 上报 {pid, tid, hwnd}），超时 300 ms 仍未激活，则：
3. 兜底：`PostMessage(GetGUIThreadInfo(前台线程).hwndFocus, WM_INPUTLANGCHANGEREQUEST, INPUTLANGCHANGE_SYSCHARSET, (LPARAM)zhCN_HKL)`。第 1 步带了 DONTCARECURRENTINPUTLANGUAGE，这一步切到 zh-CN 时应该落到我们的 profile 上。uiAccess 保证能 Post 到提权窗口。
4. 仍失败：在面板上提示“请按 Win+Space 切换到 T9Ime”，并记录应用类型。
5. 把被点的按键**暂存**，TIP 连上后再补发；超时则丢弃。
- 全部**需实测**的矩阵：默认全局模式 / 按窗口模式 × {记事本, Win32 Edit, WPF, UWP, Edge, 管理员记事本, SearchHost}。重点看 FORSESSION 是否影响**其他进程的当前线程**（而不只是“新获得焦点的线程”）。
- 不推荐：SendInput 模拟 Win+Space（结果不确定）；为 profile 设置热键（HKCU\Control Panel\Input Method\Hot Keys 对 TIP 的支持不明，**需实测**）。

**T9Ctl `T9_Activate(HWND)`（运行在应用进程内）：**
- 如果 `GetWindowThreadProcessId(hwnd) == GetCurrentThreadId()`：`ActivateProfile(..., TF_IPPMF_FORPROCESS | TF_IPPMF_DONTCARECURRENTINPUTLANGUAGE)`；失败再用 `ActivateKeyboardLayout(zhCN_HKL, KLF_SETFORPROCESS)`。
- 如果是本进程的另一个线程：`SendMessage(hwnd, WM_INPUTLANGCHANGEREQUEST, 0, zhCN_HKL)` 让目标线程自己切（前面先做一次 FORPROCESS 标记）；或者要求调用方在 UI 线程上调用（文档写明）。
- 如果 hwnd 属于别的进程：走管道让 Host/Panel 按上面 1–3 执行。
- 首次安装时用 `InstallLayoutOrTip(L"0804:{CLSID}{ProfileGUID}", 0)` 把 profile 加入用户已启用列表 **[文档]** https://learn.microsoft.com/windows/apps/develop/input/input-method-editor-requirements ；或调用时带 `TF_IPPMF_ENABLEPROFILE`。微软不建议直接写注册表设置默认输入法。

---

## 4. 与系统触摸键盘（TabTip / InputPane）共存

### 4.1 注册表（HKCU\Software\Microsoft\TabletTip\1.7，DWORD）
| 值 | 系统 | 含义 | 来源 |
|---|---|---|---|
| `TouchKeyboardTapInvoke` | Win11 | 设置 > 时间和语言 > 输入 > 触摸键盘 >“显示触摸键盘”：0=从不，1=未连接键盘时，2=始终 | [社区] https://www.elevenforum.com/t/turn-on-or-off-show-touch-keyboard-when-no-keyboard-attached-in-windows-11.3173/ ，https://learn.microsoft.com/en-us/answers/questions/2029240/touch-keyboard-not-opening-up-in-single-app-edge-k |
| `EnableDesktopModeAutoInvoke` | Win10（Win11 部分场景仍读） | 非平板模式、未连接键盘时自动显示触摸键盘：0/1 | [社区] https://www.tenforums.com/tutorials/83312-turn-off-automatically-show-touch-keyboard-windows-10-a.html |
| `TipbandDesiredVisibility` | Win10/11 | 任务栏“触摸键盘”按钮是否显示（0/1） | [社区]，Win11 语义**需实测** |
- 另有无人值守设置 `Microsoft-Windows-TabletPC-Platform-Input-Core / TouchKeyboardAutoInvokeEnabled`（镜像定制用）**[文档]** https://learn.microsoft.com/windows-hardware/customize/desktop/unattend/microsoft-windows-tabletpc-platform-input-core-touchkeyboardautoinvokeenabled
- 生效时机：通过“设置”UI 修改立即生效；直接写注册表后是否需要重启 `TextInputHost.exe`/`TabTip.exe` 或注销 → **需实测**。写完后可以广播 `WM_SETTINGCHANGE` 试一下，不行就结束 `TabTip.exe`（它会按需重新启动）。
- Win10 平板模式下的自动弹出能否被上述值完全关掉 → **需实测**。

### 4.2 检测系统键盘是否在显示
- `IFrameworkInputPane`（CLSID_FrameworkInputPane）：`Location()` 返回屏幕矩形，不可见时为空矩形；也可以 `AdviseWithHWND` 接收 Showing/Hiding **[文档]** https://learn.microsoft.com/windows/win32/api/shobjidl_core/nn-shobjidl_core-iframeworkinputpane 。微软 IME 指南也建议 IME 用它避免候选窗被触摸键盘盖住 **[文档]** https://learn.microsoft.com/windows/apps/develop/input/input-method-editor-requirements 。
  - `AdviseWithHWND` 是否会为“别的进程的窗口”触发事件 → **需实测**。稳妥做法：在 TIP 报告焦点变化后，由 Host/Panel 轮询 `Location()`（焦点变化后的 0–500 ms 内查几次）。
- `ITipInvocation::Toggle(HWND)`（CLSID_UIHostNoLaunch `{4ce576fa-83dc-4f88-951c-9d0782b4e376}`，IID `{37c994e7-432b-4834-a2f7-dce1f13b834b}`）：**未公开**，只能切换（toggle），不能明确 show/hide **[社区]**。必须先用 Location 判断可见，再 Toggle 关掉，否则可能反而把它打开。
- `IInputPaneInterop::GetForWindow(hwnd)` → `InputPane.TryHide()`：大概只对调用进程自己的窗口有效，跨进程**需实测**。

### 4.3 第三方 TIP 能否声明“自带触摸 UI”
- 没有官方机制。TIP 只能通过 `ITfFnGetPreferredTouchKeyboardLayout` 选择系统触摸键盘用哪种布局（经典/简中触摸优化），也就是让系统键盘配合我们，不能替换它 **[文档]** https://learn.microsoft.com/windows/apps/develop/input/input-method-editor-requirements 。
- 反过来：系统触摸键盘的经典布局会像硬件键盘一样把按键送给当前 TIP，所以用户要是手动打开系统键盘，按键仍然会进到我们的输入法。简中触摸优化布局会用 SendInput 发 VK_PACKET 私用区字符 0xF003/0xF004 表示翻页，TIP 要能识别（同一页有代码）。

### 4.4 推荐策略
1. 安装程序 / 首次运行向导中加一个复选框（默认勾选）：“由 T9Ime 接管触摸自动弹出”。勾选后备份并设置 `TouchKeyboardTapInvoke=0`（Win11）、`EnableDesktopModeAutoInvoke=0`；卸载时还原。托盘设置里可以随时切换。
2. 运行时：我方面板准备自动弹出时先查 `IFrameworkInputPane::Location`。系统键盘可见（用户手动点了任务栏按钮，或者设置被改回去了）→ **我方不弹**，把用户的显式选择放在前面。用户点我方的“显示键盘”→ 若系统键盘可见，用 Toggle 关掉它（设置项可控）。
3. 面板和候选窗避开 `Location()` 矩形。
4. 只有在本 TIP 为当前输入法时，才让 TIP 实现 `ITfFnGetPreferredTouchKeyboardLayout` 返回简中触摸布局，这样用户用系统键盘也能正常输入。

---

## 5. 判定“焦点是由触摸/笔带来的”

- `GetCurrentInputMessageSource`：返回调用线程**当前正在处理的输入消息**的来源。`deviceType` 取 `IMDT_UNAVAILABLE/KEYBOARD/MOUSE/TOUCH/PEN/TOUCHPAD`（注意是 IMDT_TOUCH，不是“IMO_TOUCH”；IMO_* 表示来源是硬件/注入/系统）。通过 SendMessage 注入的消息在 SendMessage 返回前都是 `IMDT_UNAVAILABLE`，这时用 `GetCIMSSM` 取 **[文档]**：
  - https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-getcurrentinputmessagesource
  - https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-getcimssm
- 在 TIP 里的语义：`ITfThreadMgrEventSink::OnSetFocus` 如果是在处理 WM_LBUTTONDOWN/WM_POINTERDOWN 的同一调用栈里同步触发（经典 Win32：点击 → SetFocus → WM_SETFOCUS → CUAS/TSF 关联焦点），能拿到 `IMDT_TOUCH/PEN`。以下情况会拿到 `IMDT_UNAVAILABLE`：焦点变化是异步的（XAML/UWP、Chromium 在 browser 进程里设置 TSF 焦点、WPF 的分派器、WebView）；或者变化发生在后续 posted 的任务里。**需实测**，逐类应用建表。
- uiAccess 进程 SendInput 注入的输入被报告为 `IMO_HARDWARE` **[文档]**，所以 SendInput 降级路径不会被误判为“注入”。
- 替代 / 兜底：
  1. **线程级**消息钩子：TIP 在线程管理器线程上 `SetWindowsHookEx(WH_GETMESSAGE 或 WH_MOUSE, …, NULL/hInst, GetCurrentThreadId())`（只钩本线程，不是全局钩子，也不是注入）。对鼠标消息读 `GetMessageExtraInfo()`：`(extra & 0xFFFFFF00) == 0xFF515700` 表示来自触摸/笔的提升，`extra & 0x80` 表示触摸 **[文档]** https://learn.microsoft.com/windows/win32/tablet/system-events-and-mouse-messages 。记录“最近一次指针按下的类型+时间”，OnSetFocus 时如果 500 ms 内有触摸按下，就判定为触摸。这种钩子是否符合 SPEC §13 的精神，请在 §9 确认。
  2. Host 侧 UIA：`IUIAutomation::AddFocusChangedEventHandler`，TabTip 就是这么做的。只能知道焦点变了，不知道输入来源；成本高，而且 UIA 客户端对某些应用会拖慢速度。只建议用来辅助“应该隐藏”的判定。
  3. Raw Input（`RIDEV_INPUTSINK`，Usage Page 0x0D / Usage 0x04 触摸屏）在 Host 后台接收触摸屏 HID 报文，得到“全局最近一次触摸时间”：能否收到触摸屏的 raw input → **需实测**。
- **推荐**：TIP 端依次用 `GetCurrentInputMessageSource` → `GetCIMSSM` → 线程级钩子记录的最近指针类型（1 秒窗口），判定结果随焦点事件发给 Host；Host 结合“面板是否刚被使用过”（面板刚被点过则保持显示）做最终判断。

---

## 6. AppContainer / LPAC

### 6.1 DLL 加载
- `C:\Program Files` 和 `Common Files` 默认 ACL 已包含 `ALL APPLICATION PACKAGES (S-1-15-2-1):(RX)` 和 `ALL RESTRICTED APPLICATION PACKAGES (S-1-15-2-2):(RX)`，可继承（本机 `icacls` **[本机实测]**）。⇒ 安装到 Program Files\T9Ime 并**保留继承**即可，不需要额外设置 ACL。只有做成非继承的保护 ACL 时，才需要手工加这两项。便携版如果放在其他目录，注册脚本里要 `icacls <dir> /grant *S-1-15-2-1:(OI)(CI)RX *S-1-15-2-2:(OI)(CI)RX`。
- 微软 IME 指南：AppContainer 中的 IME 受同等限制，字典文件放在 Program Files/Windows 下即可读取；不要尝试绕过容器，否则可能被当成恶意软件 **[文档]** https://learn.microsoft.com/windows/apps/develop/input/input-method-editor-requirements
- TIP 在 AppContainer 里**读不到** `HKCU\Software\T9Ime` 和 `%APPDATA%`：配置要么通过管道从 Host 获取，要么放在 HKLM 或对 AC 授权的位置。

### 6.2 哪些进程是 AppContainer / LPAC（本机 26200 实测，token 查询 **[本机实测]**）
| 进程 | AppContainer | IL | LPAC(WIN://NOALLAPPPKG) |
|---|---|---|---|
| SearchHost.exe（开始菜单搜索） | 是 | Low | 否 |
| ShellExperienceHost.exe | 是 | Low | 否 |
| LockApp.exe | 是 | Low | 否 |
| StartMenuExperienceHost.exe | **否** | Medium | 否 |
| TextInputHost.exe | **否** | Medium | 否 |
| SystemSettings.exe | 否 | Medium | 否 |
| msedge renderer | 是（Edge 已启用 renderer AppContainer） | Untrusted | 否 |
| chrome renderer | 否 | Untrusted | 否 |
| msedgewebview2（某打包应用） | 是 | Low/Untrusted | 否 |
- 浏览器的文本输入由 **browser 进程**（Medium）里的 TSF 处理，renderer 不承载 TSF 焦点，所以 TIP 实际在 msedge/chrome 主进程里运行（Chromium ui/base/ime/win 的 TSF bridge，**需实测**确认 renderer 中不会加载 TIP）。
- 本机没发现承载文本输入的 LPAC 进程。LPAC 主要用于 Edge/Chromium 的部分工具进程、Windows Defender Application Guard 等。**为保险仍授权 S-1-15-2-2**，代价为零。
- 检测方法：`GetTokenInformation(TokenIsAppContainer)`；LPAC 用 `TokenSecurityAttributes` 中是否存在 `WIN://NOALLAPPPKG` 判断。LPAC 令牌里看不到 S-1-15-2-1 组，AC 令牌也看不到，这个 SID 是访问检查时特殊匹配的（本机脚本已验证）。TIP 可以据此在日志里标注运行环境，并决定能否拉起 Host（§8）。

### 6.3 命名管道
- Weasel 现有做法 **[源码]**（WeaselIPCServer/SecurityAttribute.cpp）：`S:(ML;;NW;;;LW)D:(A;;FA;;;SY)(A;;FA;;;WD)(A;;FA;;;AC)`，管道名 `\\.\pipe\<用户名>\WeaselNamedPipe`（include/WeaselIPC.h）。问题：授予 Everyone 完全控制，没有 LPAC，也没有防管道抢注。
- 推荐 SDDL（Host 创建管道时）：
  ```
  S:(ML;;NW;;;LW)
  D:P(A;;GA;;;SY)(A;;GRGW;;;<当前用户SID>)(A;;GRGW;;;AC)(A;;GRGW;;;S-1-15-2-2)
  ```
  - `AC` 是 S-1-15-2-1 的 SDDL 别名；S-1-15-2-2 没有公认的别名，直接写 SID 字符串。
  - AppContainer 访问要**同时**通过用户部分和 AppContainer 部分的检查 **[文档]** https://learn.microsoft.com/windows/win32/secauthz/implementing-an-appcontainer 。所以用户 SID 和 AC 两项都要有。
  - 低完整性 SACL 让 Low/Untrusted 客户端能写。Edge renderer 是 Untrusted（0x0），不过 renderer 不需要连。
  - 创建时带 `FILE_FLAG_FIRST_PIPE_INSTANCE`（防抢注）和 `PIPE_REJECT_REMOTE_CLIENTS`。
  - 客户端 `CreateFile` 带 `SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION`，防止恶意同名服务端冒充提权客户端。连接后用 `GetNamedPipeServerProcessId` 做校验（AppContainer 中可能无法打开服务端进程，**需实测**）。
- 管道名：SPEC 要求带会话 ID + 用户 SID，如 `\\.\pipe\T9Ime.<SessionId>.<UserSid>`。AppContainer 可以打开全局 `\\.\pipe\` 名字空间下 DACL 允许的管道（Weasel 在 UWP 中可用即为旁证）。

### 6.4 提权进程能否连接 Medium 用户的管道
- 能。完整性机制只有 No-Write-Up（低 → 高写），高 IL 写低标签对象不受限制；DACL 按用户 SID 授权，提权令牌仍含同一用户 SID。
- 例外：**“以其他管理员身份”提权（OTS，标准用户输入管理员凭据）**时，进程用户是另一个账户 → 按用户 SID 命名的管道对不上，DACL 也不允许。可选做法：管道名只按会话 ID；DACL 额外允许 `BA`（Administrators），或连接时由 TIP 以会话交互用户的 SID 拼名。**需决策**，默认可以不支持 OTS 场景（提示“Host 不可用”并透传）。
- 提权进程连接时必须带 SQOS（见上），防止被普通权限的伪服务端模拟。

---

## 7. TSF 推送路径

- 线程：TSF 对象属于 STA，`ITfContext`、`ITfThreadMgr` 只能在创建它们的线程（即 TIP `ActivateEx` 所在的线程管理器线程）上调用。后台管道线程**绝不能**直接调 `RequestEditSession`，否则会得到 RPC_E_WRONG_THREAD 或出现未定义行为。所以：后台线程 → `PostMessage(hwndMsg, WM_T9_PUSH, seq, 0)`（数据放进带锁队列）→ UI 线程处理。`hwndMsg` 在 `ActivateEx` 里创建（`HWND_MESSAGE` 父窗口），`Deactivate` 时销毁，窗口类名每个线程唯一或共享均可。
- `RequestEditSession(tid, pes, TF_ES_ASYNCDONTCARE | TF_ES_READWRITE, &hr)` **[文档]** https://learn.microsoft.com/windows/win32/tsf/tf-es--constants ，https://learn.microsoft.com/windows/win32/api/msctf/nf-msctf-itfcontext-requesteditsession
  - 管理器尽量同步执行；做不到时返回 `hr=TF_S_ASYNC`，稍后（应用授予锁时）再调 `DoEditSession`。**编辑会话对象要自带要提交的文本/preedit/光标快照**，不能在 DoEditSession 里再去读“当前状态”（Weasel 的 `_async_edit` 标志就是为此，**[源码]** Composition.cpp）。
  - 在另一个 TIP 的编辑会话里调用 → `TF_E_LOCKED`；context 已出栈 → `TF_E_DISCONNECTED`；只读文档 → `hr=TS_E_READONLY`。
  - 在 `OnEndEdit / OnLayoutChange / OnStatusChange` 回调中不能请求同步读写，要用异步。
  - context 被销毁或 TIP 在授锁前被停用时，`DoEditSession` 不会被调用，清理工作放在析构函数里 **[文档]** https://learn.microsoft.com/windows/win32/api/msctf/nn-msctf-itfeditsession
- 目标 context：`ITfThreadMgr::GetFocus()` → `ITfDocumentMgr::GetTop()`。取不到（无焦点文档）、`GetStatus` 显示 `TS_SD_READONLY`、或 InputScope 为密码 → **不提交**，回包告诉 Host“推送未消费”，由 Host 决定是否降级（SendInput 降级只能在 Host/Panel 中做）。`TS_SS_TRANSITORY`（CUAS 包装的 IMM32 应用）可以正常提交，但不要依赖读回文本。
- Host 必须只推送给“当前拥有焦点的那个 TIP 实例”：TIP 在 `OnSetThreadFocus`/`OnSetFocus` 时上报 {pid, tid, 连接 id, focus 序号}；推送带上序号，TIP 发现序号过期就丢弃。
- UIElement **[文档 + 源码]**：`ITfUIElementMgr::BeginUIElement(this, &show, &id)` 返回 `show=FALSE` → 通知 Host 隐藏候选窗；TIP 实现 `ITfCandidateListUIElement`（最好再实现 `ITfCandidateListUIElementBehavior`，让游戏可以选词），候选变化时 `UpdateUIElement(id)`，结束时 `EndUIElement(id)`。所有调用都在线程管理器线程上。面板推送导致候选变化时，也要走同样的 Update。Weasel 的 CCandidateList 可以直接参考（WeaselTSF/CandidateList.cpp）。
- 另外要照做的：候选窗出现/隐藏/移动时发送 `EVENT_OBJECT_IME_SHOW/HIDE/CHANGE`（`NotifyWinEvent`）以参与 light-dismiss；候选列表的 UIA AutomationId 设为 `IME_Candidate_Window` **[文档]** https://learn.microsoft.com/windows/apps/develop/input/input-method-editor-requirements

---

## 8. Host 的拉起与恢复

### 8.1 约束
- AppContainer 进程里 CreateProcess 出来的子进程继承 AppContainer 令牌 **[文档]** https://learn.microsoft.com/windows/win32/secauthz/appcontainer-for-legacy-applications- ，这样启动的 Host 读不了用户词库，也建不了能被别人访问的管道，**等于没用**。Low IL（IE 保护模式类）同理。
- 从提权进程 ShellExecute Host → Host 也是提权的，之后 Medium 客户端会连不上它或出现权限错乱。**禁止**。
- uiAccess exe 只能经 ShellExecuteEx（AppInfo）启动（§1.4）。
- Weasel 的做法 **[源码]**（WeaselTSF.cpp `_EnsureServerConnected`）：Echo 失败 6 次后，没找到 WeaselServer.exe 进程就 `ShellExecuteW(start_service.bat)`。它不区分 AppContainer 和提权，存在上面这些问题。

### 8.2 推荐方案
1. **常驻启动**：安装时写 `HKCU\...\Run\T9Host`（按用户安装；多用户可写 HKLM Run），并注册计划任务 `\T9Ime\Host`：触发器为“用户登录”，`RunLevel=LeastPrivilege`，`MultipleInstancesPolicy=IgnoreNew`，允许按需运行；动作是启动 **T9Host.exe（非 uiAccess）**。两者都存在时靠 Host 单实例互斥去重。
2. **T9Host 启动 T9Panel**：`ShellExecuteEx(T9Panel.exe)`，Panel 退出就重启（指数退避，最多 N 次）。
3. **Panel 守护 Host**：Panel 发现管道断开且 Host 进程不在 → 用 `ITaskService` 运行 `\T9Ime\Host` 任务。原因是 Panel 可能是 High IL，直接启动会把 Host 变成提权进程；任务按 LeastPrivilege 运行，得到 Medium Host（**需实测**：High IL 的 uiAccess 进程运行任务、任务以受限令牌启动）。
4. **TIP 侧**（连接失败时，限频，比如每 5 秒最多一次）：
   - 当前进程是 AppContainer 或 Low IL → **什么都不做**，透传按键，等看门狗恢复。
   - 提权（TokenElevationTypeFull）→ 通过 `ITaskService` 运行 `\T9Ime\Host`（任务以 LeastPrivilege 运行），**不**直接 ShellExecute。
   - 普通 Medium → `ShellExecuteEx(T9Host.exe)` 或运行同一个任务（推荐统一走任务，行为一致）。
   - 在单独线程里异步做，不阻塞宿主（`ITaskService::Connect` 可能要几百 ms）。
5. **可选：COM LocalServer 代理**（覆盖 “AppContainer 中且 Host+Panel 都挂了”）：注册 `T9Launcher.exe` 为 LocalServer32，AppID 设 `RunAs="Interactive User"`，`LaunchPermission/AccessPermission` 的 SDDL 包含用户与 `AC`（RPCSS 默认不会自动加 ALL_APPLICATION_PACKAGES **[文档]** https://learn.microsoft.com/windows/win32/com/donotaddallapplicationpackagestorestrictions ）。TIP 在 AppContainer 中 `CoCreateInstance(CLSCTX_LOCAL_SERVER)` → Launcher（Medium）运行计划任务后退出。**需实测**：AppContainer 能否激活、Interactive User 身份、是否满足商店对 IME“不要绕过容器”的要求。优先级低，M7 再评估。
6. 卸载顺序（SPEC §10）之外，还要删除计划任务和 COM 注册。

---

## 9. 需要你决策的问题

- **D1 进程拆分**：是否同意拆成 `T9Host.exe`（Medium，librime/IPC/托盘/设置）+ `T9Panel.exe`（uiAccess，面板+候选窗绘制）？理由见 §1.2：管理员账户下 uiAccess 是 High IL，并且只有 ShellExecute 能启动它。代价是多一个进程和一条管道。
- **D2 候选窗放在哪个进程**：
  (a) 放在 T9Panel（uiAccess，z 序最高，可盖住开始菜单搜索）；
  (b) 像当前 Weasel 那样在 TIP 进程内绘制（WeaselUI 链接进 WeaselTSF，窗口 owner 是应用窗口 **[源码]** WeaselTSF/xmake.lua、CandidateList.cpp `_MakeUIWindow`），符合微软“owned window”指南，天然跟随应用的 z-band 和 DPI，但会让 TIP 引入 D2D 绘制和更多代码；
  (c) 普通 Medium Host 绘制（SPEC 原设计）：在 SearchHost/开始菜单中会被盖住，**不推荐**。
  建议选 (a)，(b) 作为后续选项。
- **D3 线程级消息钩子**（§5 兜底触摸判定）：`SetWindowsHookEx` 只钩 TIP 所在线程，不是全局钩子，也不是注入。是否允许？
- **D4 OTS 提权（其他管理员账户）** 场景是否支持？支持的话，管道名改为按会话 ID，DACL 加 Administrators。
- **D5 TabTip 设置**：默认是否勾选“由 T9Ime 接管触摸自动弹出”？是否允许在用户点“显示键盘”时用未公开的 `ITipInvocation::Toggle` 关掉系统键盘？
- **D6 COM LocalServer 代理**（§8.2-5）要不要做？

## 10. 需实测清单（建议在 M2/M3 前写最小复现）
1. uiAccess 窗口（普通 CreateWindowEx+TOPMOST）能否盖住开始菜单、搜索、操作中心、Alt-Tab；锁屏时是否可见。
2. 自签根证书 + Program Files 下 uiAccess 启动；Run 键启动 uiAccess exe；Win11 Administrator Protection 下的 IL。
3. `ActivateProfile(FORSESSION[|DONTCARE…])` 从外部进程切换前台应用的 TIP：两种“按窗口/全局”模式 × 应用矩阵。
4. `WM_INPUTLANGCHANGEREQUEST` + DONTCARE 标记后，是否落到我们的 profile。
5. 直接写 TabletTip 注册表后多久生效；Win10 平板模式；`IFrameworkInputPane` 跨进程事件。
6. OnSetFocus 时 `GetCurrentInputMessageSource/GetCIMSSM` 在各类应用中的返回值；Raw Input 能否收到触摸屏数据。
7. High IL 的 Panel / 提权 TIP 通过 ITaskService 运行 LeastPrivilege 任务得到 Medium Host。
8. Edge/Chrome renderer 中不加载 TIP；AppContainer 客户端调用 `GetNamedPipeServerProcessId` + OpenProcess 是否可行。
9. `GetTextExt` 坐标在不同 DPI 感知应用中的单位。
10. 点击不激活面板对目标应用弹出层/菜单的影响；独占全屏游戏中面板的可见性。

## 参考来源汇总
- uiAccess/安全：https://learn.microsoft.com/windows/win32/winauto/uiauto-securityoverview ，https://learn.microsoft.com/windows/win32/sbscs/application-manifests ，https://learn.microsoft.com/windows/client-management/mdm/policy-csp-localpoliciessecurityoptions ，https://projectzero.google/2026/02/windows-administrator-protection.html ，https://www.tiraniddo.dev/2019/02/accessing-access-tokens-for-uiaccess.html
- z-band：https://blog.adeltax.com/window-z-order-in-windows-10/ ，https://github.com/arcanine300/CreateWindowInBand
- 指针/触摸：https://learn.microsoft.com/windows/win32/inputmsg/wm-pointeractivate ，https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-enablemouseinpointer ，https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-setwindowfeedbacksetting ，https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-registerpointerinputtarget ，https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-getcurrentinputmessagesource ，https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-getcimssm ，https://learn.microsoft.com/windows/win32/api/winuser/ne-winuser-input_message_origin_id ，https://learn.microsoft.com/windows/win32/tablet/system-events-and-mouse-messages
- TSF：https://learn.microsoft.com/windows/win32/api/msctf/nf-msctf-itfinputprocessorprofilemgr-activateprofile ，https://learn.microsoft.com/windows/win32/api/msctf/nf-msctf-itfcontext-requesteditsession ，https://learn.microsoft.com/windows/win32/tsf/tf-es--constants ，https://learn.microsoft.com/windows/win32/winmsg/wm-inputlangchangerequest ，https://learn.microsoft.com/windows/compatibility/ime-mode-model-changed-from-per-user-to-per-thread ，https://learn.microsoft.com/windows/apps/develop/input/input-method-editor-requirements ，https://learn.microsoft.com/windows/win32/w8cookbook/third-party-input-method-editors ，SampleIME：https://github.com/microsoft/Windows-classic-samples/tree/main/Samples/IME
- 跨进程切换（社区）：https://github.com/zql70/imebind-csharp ，https://github.com/GetSayAll/remote-mic-app-windows/pull/17 ，https://github.com/brglng/im-control
- TabTip：https://learn.microsoft.com/windows/win32/api/shobjidl_core/nn-shobjidl_core-iframeworkinputpane ，https://learn.microsoft.com/windows/win32/api/inputpaneinterop/nn-inputpaneinterop-iinputpaneinterop ，https://www.elevenforum.com/t/turn-on-or-off-show-touch-keyboard-when-no-keyboard-attached-in-windows-11.3173/ ，https://www.tenforums.com/tutorials/83312-turn-off-automatically-show-touch-keyboard-windows-10-a.html
- AppContainer：https://learn.microsoft.com/windows/win32/secauthz/implementing-an-appcontainer ，https://learn.microsoft.com/windows/win32/secauthz/appcontainer-for-legacy-applications- ，https://devblogs.microsoft.com/oldnewthing/20220502-00/?p=106550 ，https://learn.microsoft.com/windows/win32/com/donotaddallapplicationpackagestorestrictions
- 计划任务与 uiAccess：https://windows-hexerror.linestarve.com/q/so35407949-run-c-wpf-application-with-uiaccess-true-in-manifest-at-startup
- Weasel 源码（本地克隆）：WeaselIPCServer/SecurityAttribute.cpp，include/WeaselIPC.h，WeaselTSF/WeaselTSF.cpp，WeaselTSF/Composition.cpp，WeaselTSF/CandidateList.cpp，WeaselTSF/xmake.lua
