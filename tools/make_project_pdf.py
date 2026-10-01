"""Generate Docs/Project.pdf: modules, architecture, call relationships and the
annotated file tree of T9Ime.

    py -3 tools/make_project_pdf.py            (needs: pip install reportlab)

Every tracked file (git ls-files) must have a description in FILES below; the
script fails otherwise, so the document stays complete when files are added.
Fonts: Microsoft YaHei and Consolas from C:\\Windows\\Fonts.
"""
import datetime
import subprocess
import sys
from pathlib import Path

from reportlab.graphics.shapes import Drawing, Line, Polygon, Rect, String
from reportlab.lib import colors
from reportlab.lib.enums import TA_CENTER
from reportlab.lib.pagesizes import A4
from reportlab.lib.styles import ParagraphStyle
from reportlab.lib.units import mm
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.platypus import (KeepTogether, PageBreak, Paragraph, SimpleDocTemplate, Spacer, Table,
                                TableStyle)

ROOT = Path(__file__).resolve().parent.parent
FONTS = Path('C:/Windows/Fonts')
pdfmetrics.registerFont(TTFont('YaHei', str(FONTS / 'msyh.ttc'), subfontIndex=0))
pdfmetrics.registerFont(TTFont('YaHeiBold', str(FONTS / 'msyhbd.ttc'), subfontIndex=0))
pdfmetrics.registerFont(TTFont('Mono', str(FONTS / 'consola.ttf')))
pdfmetrics.registerFontFamily('YaHei', normal='YaHei', bold='YaHeiBold', italic='YaHei', boldItalic='YaHeiBold')

INK = colors.HexColor('#1F2328')
MUTED = colors.HexColor('#57606A')
ACCENT = colors.HexColor('#1E6FD9')
LINE = colors.HexColor('#D0D7DE')
FILL_APP = colors.HexColor('#EEF4FC')
FILL_HOST = colors.HexColor('#F3F7EE')
FILL_EXT = colors.HexColor('#F6F6F6')
FILL_BOX = colors.white

S = {
    'title': ParagraphStyle('title', fontName='YaHeiBold', fontSize=24, leading=32, textColor=INK, alignment=TA_CENTER),
    'subtitle': ParagraphStyle('subtitle', fontName='YaHei', fontSize=12, leading=18, textColor=MUTED,
                               alignment=TA_CENTER),
    'h1': ParagraphStyle('h1', fontName='YaHeiBold', fontSize=16, leading=22, textColor=INK, spaceBefore=6,
                         spaceAfter=8),
    'h2': ParagraphStyle('h2', fontName='YaHeiBold', fontSize=12.5, leading=18, textColor=ACCENT, spaceBefore=10,
                         spaceAfter=4),
    'body': ParagraphStyle('body', fontName='YaHei', fontSize=9.5, leading=15.5, textColor=INK, spaceAfter=4,
                           wordWrap='CJK'),
    'bullet': ParagraphStyle('bullet', fontName='YaHei', fontSize=9.5, leading=15, textColor=INK, leftIndent=12,
                             bulletIndent=2, spaceAfter=1.5, wordWrap='CJK'),
    'cell': ParagraphStyle('cell', fontName='YaHei', fontSize=8.3, leading=11.5, textColor=INK, wordWrap='CJK'),
    'cellb': ParagraphStyle('cellb', fontName='YaHeiBold', fontSize=8.3, leading=11.5, textColor=INK,
                            wordWrap='CJK'),
    'mono': ParagraphStyle('mono', fontName='Mono', fontSize=7.6, leading=11.5, textColor=INK),
    'monodir': ParagraphStyle('monodir', fontName='Mono', fontSize=7.6, leading=11.5, textColor=ACCENT),
    'caption': ParagraphStyle('caption', fontName='YaHei', fontSize=8.5, leading=12, textColor=MUTED,
                              alignment=TA_CENTER, spaceBefore=3, spaceAfter=8),
}


def P(text, style='body'):
    return Paragraph(text, S[style])


def bullets(items):
    return [Paragraph(t, S['bullet'], bulletText='•') for t in items]


def table(rows, widths, header=True):
    data = [[c if not isinstance(c, str) else P(c, 'cellb' if header and i == 0 else 'cell') for c in row]
            for i, row in enumerate(rows)]
    t = Table(data, colWidths=widths, repeatRows=1 if header else 0)
    style = [('GRID', (0, 0), (-1, -1), 0.4, LINE), ('VALIGN', (0, 0), (-1, -1), 'TOP'),
             ('LEFTPADDING', (0, 0), (-1, -1), 4), ('RIGHTPADDING', (0, 0), (-1, -1), 4),
             ('TOPPADDING', (0, 0), (-1, -1), 2.5), ('BOTTOMPADDING', (0, 0), (-1, -1), 2.5)]
    if header:
        style.append(('BACKGROUND', (0, 0), (-1, 0), colors.HexColor('#EAF1FB')))
    t.setStyle(TableStyle(style))
    return t


# ---------------------------------------------------------------- diagrams

def box(d, x, y, w, h, title, lines=(), fill=FILL_BOX, stroke=INK, title_size=8.5, size=7.2):
    d.add(Rect(x, y, w, h, rx=4, ry=4, fillColor=fill, strokeColor=stroke, strokeWidth=0.8))
    d.add(String(x + 6, y + h - 12, title, fontName='YaHeiBold', fontSize=title_size, fillColor=INK))
    for i, line in enumerate(lines):
        d.add(String(x + 8, y + h - 24 - i * 10.5, line, fontName='YaHei', fontSize=size, fillColor=MUTED))


def frame(d, x, y, w, h, title, fill):
    d.add(Rect(x, y, w, h, rx=6, ry=6, fillColor=fill, strokeColor=MUTED, strokeWidth=0.6, strokeDashArray=[3, 2]))
    d.add(String(x + 8, y + h - 14, title, fontName='YaHeiBold', fontSize=9.5, fillColor=INK))


def arrow(d, x1, y1, x2, y2, label='', color=ACCENT, both=False, dx=0, dy=4):
    d.add(Line(x1, y1, x2, y2, strokeColor=color, strokeWidth=1.1))

    def head(xa, ya, xb, yb):
        import math
        a = math.atan2(yb - ya, xb - xa)
        L, W = 6, 3
        p1 = (xb - L * math.cos(a) + W * math.sin(a), yb - L * math.sin(a) - W * math.cos(a))
        p2 = (xb - L * math.cos(a) - W * math.sin(a), yb - L * math.sin(a) + W * math.cos(a))
        d.add(Polygon([xb, yb, p1[0], p1[1], p2[0], p2[1]], fillColor=color, strokeColor=color))

    head(x1, y1, x2, y2)
    if both:
        head(x2, y2, x1, y1)
    if label:
        d.add(String((x1 + x2) / 2 + dx, (y1 + y2) / 2 + dy, label, fontName='YaHei', fontSize=6.8,
                     fillColor=color, textAnchor='middle'))


def label(d, x, y, text, color=ACCENT, anchor='middle'):
    d.add(String(x, y, text, fontName='YaHei', fontSize=6.8, fillColor=color, textAnchor=anchor))


def architecture_diagram():
    d = Drawing(500, 420)
    # Application process
    frame(d, 0, 205, 165, 210, '应用程序进程（任意程序）', FILL_APP)
    box(d, 10, 215, 145, 180, 'T9Tip.dll（TSF 文本服务）', [
        'TextService：按键 / 焦点 / 组字',
        'HostClient：请求管道客户端',
        'EventListener：事件管道（推送）',
        'TouchTracker：触摸判定',
        'CandidateWindow：物理键盘候选窗',
        'CandidateUI：UIElement 候选接口',
        'LangBar：中/英按钮',
        'DisplayAttribute / KeyEvent',
        'Register / DllMain：COM 注册',
        '依赖：t9common（协议、管道）'])
    # Business application
    frame(d, 0, 20, 165, 170, '业务程序 / 工具', FILL_APP)
    box(d, 10, 105, 145, 65, 'T9Ctl.dll（控制接口）', [
        'T9_ShowKeyboard / Hide / Toggle',
        'T9_SetMode / SetPosition / Dock',
        'T9_Activate / RegisterVisibility…'])
    box(d, 10, 30, 145, 65, 't9ctl.exe / TestHost', [
        '命令行（show / hide / watch …）',
        'TestHost.exe（C++）',
        'TestHost.CS.exe（C#, .NET 3.5）'])
    # Host
    frame(d, 245, 20, 255, 395, 'T9Host.exe（每个用户会话一个）', FILL_HOST)
    box(d, 255, 300, 110, 95, 'IPC 线程', [
        'RequestServer（.req）',
        'EventServer（.evt）',
        'CtlServer（.ctl）',
        'FocusRegistry：焦点登记',
        'PipeServer：每连接一线程'])
    box(d, 375, 300, 115, 95, '引擎线程', [
        'EngineThread：命令队列',
        'RimeEngine / Session',
        't9 前端：拼音栏、预编辑',
        'Maintain：部署 / 词库',
        '面板会话 + 每 TIP 一会话'])
    box(d, 255, 120, 235, 165, 'UI 线程', [
        'HostApp：启动、托盘、控制请求、可见性通知',
        'PanelWindow：面板窗口（指针 / 触摸 / 鼠标）',
        '    布局 PanelLayout · 渲染 PanelRenderer',
        '    弹出判定 AutoShow',
        'SettingsWindow：设置窗口（键盘/输入/词库）',
        'InputSettings：简繁、候选数、模糊音',
        'ImeSwitcher / foreground：切换输入法、前台',
        'touch_keyboard：接管系统触摸键盘',
        'TextOutput：SendInput 降级上屏'])
    box(d, 255, 30, 235, 75, 'rime.dll（librime 1.17）+ data/', [
        'rime-ice 方案与词库（预部署 build/）',
        't9（九宫格数字码）· rime_ice（全拼）· t9_eng',
        '用户目录 %APPDATA%\\T9Ime\\Rime：',
        '    用户词库、定制、编译数据 build/'], fill=FILL_EXT)
    # Pipes, labelled in the gap between the two sides.
    arrow(d, 155, 368, 255, 368)
    label(d, 205, 382, '请求管道 .req')
    label(d, 205, 373, '按键·焦点·候选')
    arrow(d, 255, 330, 155, 330)
    label(d, 205, 320, '事件管道 .evt')
    label(d, 205, 311, '面板上屏推送')
    arrow(d, 155, 140, 255, 305)
    label(d, 198, 228, '控制管道 .ctl', anchor='end')
    # Inside the host.
    arrow(d, 365, 347, 375, 347)
    arrow(d, 432, 300, 432, 285, '', both=True)
    label(d, 438, 290, '快照 / 命令', anchor='start')
    arrow(d, 300, 120, 300, 105)
    label(d, 306, 109, 'librime C API（仅引擎线程）', anchor='start')
    return d


def dependency_diagram():
    d = Drawing(500, 300)
    W, H = 110, 44
    nodes = {
        'tip': (10, 230, 'src/tip', 'T9Tip.dll'),
        'ctl': (10, 130, 'src/ctl', 'T9Ctl.dll · t9ctl.exe'),
        'samples': (10, 30, 'samples/TestHost', 'C++ / C# 示例'),
        'common': (195, 130, 'src/common', '协议 · 管道 · Win7 兼容'),
        'app': (380, 230, 'src/host/app', 'T9Host 进程与 UI 线程'),
        'ipc': (380, 160, 'src/host/ipc', '管道服务 · 焦点登记'),
        'panel': (380, 90, 'src/host/panel', '面板 · 布局 · 渲染 · 弹出'),
        'engine': (380, 20, 'src/host/engine', 'librime 封装 · t9 前端'),
        'tools': (195, 30, 'tools / tests', 't9repl · t9diag · 测试'),
    }
    for key, (x, y, title, sub) in nodes.items():
        box(d, x, y, W, H, title, [sub], fill=FILL_HOST if key in ('app', 'ipc', 'panel', 'engine') else FILL_APP,
            title_size=8, size=6.6)

    def c(key, side):
        x, y, *_ = nodes[key]
        return {'l': (x, y + H / 2), 'r': (x + W, y + H / 2), 't': (x + W / 2, y + H), 'b': (x + W / 2, y)}[side]

    def link(a, sa, b, sb, label=''):
        (x1, y1), (x2, y2) = c(a, sa), c(b, sb)
        arrow(d, x1, y1, x2, y2, label, color=MUTED, dy=3)

    link('tip', 'r', 'common', 'l')
    link('ctl', 'r', 'common', 'l')
    link('samples', 't', 'ctl', 'b', '')
    link('app', 'l', 'common', 't')
    link('ipc', 'l', 'common', 'r')
    link('app', 'b', 'ipc', 't')
    link('ipc', 'b', 'panel', 't')
    link('panel', 'b', 'engine', 't')
    link('tools', 'r', 'engine', 'l')
    link('tools', 't', 'common', 'b')
    d.add(String(250, 285, '箭头：A → B 表示 A 调用 / 链接 B', fontName='YaHei', fontSize=7, fillColor=MUTED,
                 textAnchor='middle'))
    return d


# ---------------------------------------------------------------- file descriptions

DIRS = {
    '': 'T9Ime 仓库根目录',
    'Docs': '需求、设计、计划、调研、测试清单与本文档',
    'Docs/research': '技术调研结论（M0）',
    'Docs/testing': '每个里程碑的真机测试清单 / 用例',
    'cmake': 'CMake 模板',
    'data': 'Rime 数据：本项目对 rime-ice 的定制',
    'data/custom': '随包发布的 Rime 补丁与自定义方案',
    'installer': 'Inno Setup 安装程序脚本',
    'res': '图标等资源',
    'samples': '控制接口示例程序',
    'samples/TestHost': 'TestHost：演示 T9Ctl 的全部调用',
    'samples/TestHost/cpp': 'C++（Win32）版',
    'samples/TestHost/cs': 'C#（WinForms，.NET 3.5+）版',
    'src': '产品源代码',
    'src/common': 't9common 静态库：TIP、Host、T9Ctl 共用，无外部依赖',
    'src/ctl': '控制接口：T9Ctl.dll 与 t9ctl.exe',
    'src/host': 'T9Host.exe：引擎、面板、托盘、设置、IPC 服务',
    'src/host/app': '进程入口、引擎线程、设置、系统输入交互',
    'src/host/engine': 't9engine 静态库：librime 封装',
    'src/host/engine/t9': '九宫格前端：数字码 ↔ 拼音、拼音栏、预编辑',
    'src/host/ipc': '管道服务端与焦点登记',
    'src/host/panel': '触摸面板：窗口、布局、渲染、自动弹出判定',
    'src/tip': 'T9Tip.dll：TSF 文本服务（加载进每个使用输入法的程序）',
    'tests': '测试：单元、引擎回归、端到端、探针',
    'tests/e2e': '端到端测试（Python，真实桌面）',
    'tests/probes': '测试辅助程序与 API 行为探针',
    'tests/regress': '引擎回归：t9repl 脚本与快照',
    'tests/unit': '单元测试（doctest）',
    'third_party': '第三方：下载脚本与随仓库的少量代码',
    'third_party/doctest': 'doctest 单元测试框架',
    'third_party/doctest/doctest': '',
    'third_party/weasel': 'Weasel 上游追踪说明',
    'tools': '构建、打包、诊断、虚拟机自动化工具',
    'tools/check_imports': 'Windows 7 导入表检查',
    'tools/gen_syllables': '音节表生成',
    'tools/t9diag': '诊断工具 t9diag.exe',
    'tools/t9repl': '引擎命令行 t9repl.exe',
    'tools/touch_test': '真机触屏测试一键脚本',
}

FILES = {
    '.gitignore': '忽略构建产物、下载的依赖、测试包与虚拟机密码文件',
    'CLAUDE.md': '开发约定：当前状态、关键决策、构建与测试命令、编码规范、已知陷阱',
    'CMakeLists.txt': '顶层 CMake：MSVC/静态 CRT、Win7 目标、版本号（project VERSION + 提交数）、版本资源函数、librime 与预部署数据目标、子目录',
    'CMakePresets.json': 'x64/x86 × Debug/Release 构建预设（Ninja）',
    'DEVLOG.md': '开发日志：按日期记录每次改动的原因、做法与验证',
    'LICENSE': 'GPL-3.0 协议全文',
    'README.md': '项目介绍、功能、构建、开源协议',
    'build.ps1': '一键构建：进入 VS2022 开发环境 → CMake 配置 → 构建 → ctest',
    'Docs/ARCHITECTURE.md': '架构设计：进程与模块划分、IPC、TSF 细节、自动弹出、版本分级',
    'Docs/PLAN.md': '里程碑计划 M0–M7 与决策记录',
    'Docs/SPEC.md': '原始需求说明',
    'Docs/STATUS.md': '开发进度与待解决问题（新会话 / 新机器接手先读）',
    'Docs/Project.pdf': '本文档（由 tools/make_project_pdf.py 生成）',
    'Docs/T9.jpg': '布局参考：讯飞 iOS 九宫格（外观参照）',
    'Docs/T9_2.png': '用户定稿的中文九宫格布局',
    'Docs/T9_ABC.jpg': '布局参考：讯飞 iOS 英文全键盘',
    'Docs/research/librime-rime-ice.md': '调研：librime 1.17 + rime-ice 的 t9 / rime_ice 方案实测',
    'Docs/research/t9-frontends-and-wanxiang.md': '调研：九键前端（Hamster、万象拼音等）的拼音栏与组字实现',
    'Docs/research/weasel.md': '调研：Weasel（小狼毫）源码，作为 TIP 的 fork 基线',
    'Docs/research/windows-platform.md': '调研：uiAccess、不激活窗口、跨进程切换输入法、TabTip、AppContainer、TSF 推送',
    'Docs/testing/M1-checklist.md': 'M1 真机测试清单（引擎）',
    'Docs/testing/M2-checklist.md': 'M2 真机测试清单（触摸面板）',
    'Docs/testing/M3-checklist.md': 'M3 真机测试清单（TSF 物理键盘）',
    'Docs/testing/M4-checklist.md': 'M4 真机测试清单（推送上屏、自动弹出、切换）',
    'Docs/testing/M5-checklist.md': 'M5 真机测试清单（T9Ctl、TestHost、D1–D5）',
    'Docs/testing/M6-checklist.md': 'M6 测试清单（托盘、设置、词库）',
    'Docs/testing/M6-test-cases.md': 'M6 详细测试用例（逐步操作与预期）',
    'cmake/version.rc.in': 'Windows 版本资源模板（文件版本、产品版本、描述、版权）',
    'data/fetch_rime_ice.ps1': '按固定 commit 下载 rime-ice 到 data/rime-ice',
    'data/custom/default.custom.yaml': 'default.yaml 补丁：方案列表等全局设置',
    'data/custom/rime_ice.custom.yaml': 'rime_ice（全拼）补丁：去掉部件拆字反查以减小体积',
    'data/custom/t9.custom.yaml': 't9（九宫格）补丁：去掉 t9_processor（由前端实现回车/退格语义）',
    'data/custom/t9_eng.schema.yaml': '英文九键方案（xlit 数字码，独立 prism；面板已改用全键盘，保留供回归）',
    'samples/TestHost/CMakeLists.txt': '构建 TestHost.exe，并用 .NET 3.5 csc 构建 TestHost.CS.exe',
    'samples/TestHost/cpp/TestHost.cpp': 'C++ 示例：每个 T9Ctl 调用一个按钮，键盘显示时收缩文本框避免遮挡',
    'samples/TestHost/cs/T9Ctl.cs': 'T9Ctl.dll 的 C# P/Invoke 声明',
    'samples/TestHost/cs/TestHost.CS.exe.config': '让 .NET 3.5 编译的程序也能在 .NET 4.x 上运行',
    'samples/TestHost/cs/TestHost.cs': 'C# WinForms 示例，与 C++ 版功能相同',
    'src/common/CMakeLists.txt': 't9common 静态库',
    'src/common/ime_profile.cpp': 'T9Ime 的 CLSID/Profile、查找其他输入法、ActivateProfile 封装',
    'src/common/ime_profile.h': '同上（声明）',
    'src/common/pipe.cpp': '命名管道：重叠 I/O、超时、带序号的请求/应答、DACL（含 AppContainer、低完整性）',
    'src/common/pipe.h': 'PipeClient、ReadMessage/WriteMessage、管道命名（会话 + 用户 SID + 端点）',
    'src/common/protocol.cpp': '消息编解码（TLV 字段）与 Result 结构的序列化',
    'src/common/protocol.h': 'IPC 协议：消息类型（TIP 请求、推送、控制请求）、字段标签、标志位',
    'src/common/win_compat.cpp': 'Win7 基线兼容：动态加载 Win8+ API、DPI、系统版本、触摸反馈关闭',
    'src/common/win_compat.h': '同上（声明与 Win8+ 常量）',
    'src/ctl/CMakeLists.txt': 'T9Ctl.dll（.def 导出无修饰名）与 t9ctl.exe',
    'src/ctl/T9Ctl.def': 'T9Ctl.dll 导出表',
    'src/ctl/t9ctl.cpp': '控制接口实现：经控制管道请求 T9Host，按需拉起 Host，调用线程内切换输入法',
    'src/ctl/t9ctl.h': '公开 C 头文件（__stdcall），附调用说明',
    'src/ctl/t9ctl_cli.cpp': 't9ctl.exe：show / hide / toggle / mode / pos / dock / status / activate / watch',
    'src/host/CMakeLists.txt': 't9panel 静态库与 T9Host.exe',
    'src/host/app/T9Host.manifest': 'Win7–11 兼容声明、PerMonitorV2 DPI、通用控件 6',
    'src/host/app/engine_thread.cpp': '引擎线程主循环：预热、命令/任务/维护队列、快照发布、全局选项',
    'src/host/app/engine_thread.h': 'EngineThread、EngineContext（面板会话、每 TIP 会话、选项）',
    'src/host/app/input_settings.cpp': '输入设置读写；生成用户 *.custom.yaml（模糊音插入数字映射之前）；部署戳记',
    'src/host/app/input_settings.h': 'InputSettings、模糊音目录、PrepareUserData / MarkDeployed',
    'src/host/app/main.cpp': 'T9Host 入口：参数、单实例、HostApp（托盘、控制请求、设置宿主、维护任务、可见性通知）',
    'src/host/app/settings_window.cpp': '设置窗口：三页控件、DPI 缩放、应用/确定/取消、文件对话框、状态行',
    'src/host/app/settings_window.h': 'SettingsWindow 与 SettingsHost 接口',
    'src/host/app/system_input.cpp': 'ImeSwitcher（会话级 profile + WM_INPUTLANGCHANGEREQUEST）、前台监视钩子、系统触摸键盘接管',
    'src/host/app/system_input.h': '同上（声明）',
    'src/host/app/text_output.cpp': 'SendInput 上屏（KEYEVENTF_UNICODE）、UTF-8/UTF-16 转换',
    'src/host/app/text_output.h': '同上（声明）',
    'src/host/engine/CMakeLists.txt': 't9core（纯 C++ 九宫格前端）与 t9engine（librime）静态库',
    'src/host/engine/rime_engine.cpp': 'librime 初始化/结束、会话、按键、候选、九宫格回车/退格语义、部署、用户词库',
    'src/host/engine/rime_engine.h': 'RimeEngine、Session、EngineState、Candidate',
    'src/host/engine/t9/pinyin_bar.cpp': '拼音栏：为第一个未确认的数字段排序候选音节',
    'src/host/engine/t9/pinyin_bar.h': '同上（声明）',
    'src/host/engine/t9/preedit_formatter.cpp': '把数字输入显示为可读拼音（zhong\'guo）',
    'src/host/engine/t9/preedit_formatter.h': '同上（声明）',
    'src/host/engine/t9/syllable_index.cpp': '内置音节表：数字码 → 音节（由 gen_syllables 生成的数据）',
    'src/host/engine/t9/syllable_index.h': '同上（声明）',
    'src/host/engine/t9/t9_composer.cpp': '组字状态：拼音栏确认的段、输入改写与撤销',
    'src/host/engine/t9/t9_composer.h': '同上（声明）',
    'src/host/engine/t9/t9_keys.h': '九宫格字母 ↔ 数字映射',
    'src/host/ipc/ctl_server.cpp': '控制管道服务：读请求、交给处理函数、写应答',
    'src/host/ipc/ctl_server.h': '同上（声明）',
    'src/host/ipc/focus_registry.cpp': '焦点登记：哪个 TIP 有焦点、输入框类型、触摸/激活/停用标志、推送上屏',
    'src/host/ipc/focus_registry.h': 'FocusInfo、FocusRegistry',
    'src/host/ipc/pipe_server.cpp': '管道监听：先建下一个实例再交接连接，每连接一线程，停止时取消 I/O',
    'src/host/ipc/pipe_server.h': '同上（声明）',
    'src/host/ipc/request_server.cpp': '请求管道：Hello/焦点在本线程应答，按键与候选经引擎线程；事件管道服务',
    'src/host/ipc/request_server.h': 'RequestServer、EventServer',
    'src/host/panel/auto_show.cpp': '自动弹出/隐藏判定：触摸、总是弹出、切换到 T9Ime、停用、系统浮层、InputScope 选布局',
    'src/host/panel/auto_show.h': 'AutoShowSettings、FocusEvent、AutoDecision',
    'src/host/panel/panel_layout.cpp': '布局：中文九宫格、英文全键盘、数字、符号、候选栏/展开网格、命中测试',
    'src/host/panel/panel_layout.h': '布局数据结构（DIP 坐标）与元素/动作定义',
    'src/host/panel/panel_renderer.cpp': 'D2D/DirectWrite 渲染：渐变键、阴影、浅/深主题、按键放大预览',
    'src/host/panel/panel_renderer.h': 'PanelRenderer、Theme',
    'src/host/panel/panel_window.cpp': '面板窗口：指针/触摸/鼠标手势、长按、执行动作、引擎快照、焦点事件、前台归属、主题/大小、位置保存',
    'src/host/panel/panel_window.h': 'PanelWindow、PanelOptions',
    'src/tip/CMakeLists.txt': 'T9Tip.dll（仅系统库，静态 CRT，版本资源）',
    'src/tip/T9Tip.def': 'COM 导出：DllGetClassObject、DllRegisterServer 等',
    'src/tip/candidate_ui.cpp': 'ITfCandidateListUIElement：供自绘候选的程序读取/操作候选',
    'src/tip/candidate_ui.h': '同上（声明）',
    'src/tip/candidate_window.cpp': '物理键盘候选窗（程序进程内 D2D 弹窗，不激活）',
    'src/tip/candidate_window.h': '同上（声明）',
    'src/tip/display_attribute.cpp': '组字文字的显示属性（虚线下划线）',
    'src/tip/display_attribute.h': '同上（声明）',
    'src/tip/dll_main.cpp': 'DLL 入口与类工厂',
    'src/tip/edit_session.h': 'ITfEditSession 的可调用对象封装',
    'src/tip/event_listener.cpp': '事件管道后台线程：接收面板推送，投递给 UI 线程',
    'src/tip/event_listener.h': '同上（声明）',
    'src/tip/globals.cpp': '模块句柄、引用计数、CLSID/Profile/GUID',
    'src/tip/globals.h': '同上（声明）',
    'src/tip/host_client.cpp': '请求管道客户端：连接/重连、Host 忙时快速失败、必要时拉起 T9Host',
    'src/tip/host_client.h': '同上（声明）',
    'src/tip/key_event.cpp': 'Windows 虚拟键 → X11 keysym 与修饰键（librime 需要）',
    'src/tip/key_event.h': '同上（声明）',
    'src/tip/lang_bar.cpp': '语言栏 / 任务栏输入模式按钮（中/英）',
    'src/tip/lang_bar.h': '同上（声明）',
    'src/tip/register.cpp': 'COM、TSF profile 与类别注册/注销（幂等）',
    'src/tip/register.h': '同上（声明）',
    'src/tip/text_service.cpp': '文本服务主体：激活/停用、按键、组字与上屏、焦点上报（含 InputScope、触摸、激活/停用标志）、推送、兼容区（IMM32）',
    'src/tip/text_service.h': 'TextService（实现全部 TSF 接口）',
    'src/tip/touch_tracker.cpp': '触摸判定：消息来源、鼠标钩子签名、按下窗口必须是焦点输入框',
    'src/tip/touch_tracker.h': '同上（声明）',
    'tests/CMakeLists.txt': '单元、回归、Win7 导入检查、端到端测试的注册',
    'tests/e2e/apps_e2e.py': '兼容性探测：记事本等真实程序里的物理键盘输入',
    'tests/e2e/autoshow_e2e.py': '自动弹出/隐藏、InputScope 布局、密码键盘、切换到 T9Ime（D2/D3）',
    'tests/e2e/ctl_e2e.py': '控制接口：显示/隐藏/布局/位置/通知、切换程序（D1/D4）、TestHost（D2/D5）',
    'tests/e2e/imm_e2e.py': 'IMM32 兼容：ImmSetOpenStatus / ImmSetConversionStatus',
    'tests/e2e/panel_e2e.py': '面板端到端（真实鼠标）及公共工具（启动 Host、读布局、点击）',
    'tests/e2e/probe_candclick.py': '调试探针：点击 TIP 候选窗时发生什么',
    'tests/e2e/push_e2e.py': '面板上屏经 TIP 推送（事件管道 + 编辑会话）',
    'tests/e2e/settings_e2e.py': '设置窗口（不移动鼠标）：模糊音部署、繁体、主题/大小保存',
    'tests/e2e/switch_e2e.py': '点面板自动把程序切换到 T9Ime',
    'tests/e2e/tip_e2e.py': 'TIP 端到端：物理键盘全拼、候选窗、Shift 中英',
    'tests/e2e/touch_e2e.py': '注入触摸：长按、退格连发、按钮触摸不误弹（触屏问题 1–4 回归）',
    'tests/probes/test_target.cpp': '测试窗口：文本框、数字框、密码框；可切换输入法、调用 IMM32',
    'tests/probes/tip_activate.cpp': '把整个会话切换到 T9Ime / 恢复（测试辅助）',
    'tests/regress/basic.expected': '回归快照：基本九宫格输入',
    'tests/regress/basic.t9': '回归脚本：基本九宫格输入',
    'tests/regress/run_regress.py': '运行 t9repl 脚本并与快照比对（--update 重写快照）',
    'tests/regress/schemas.expected': '回归快照：方案切换',
    'tests/regress/schemas.t9': '回归脚本：t9 / t9_eng / rime_ice 方案',
    'tests/unit/auto_show_tests.cpp': '自动弹出判定',
    'tests/unit/engine_tests.cpp': '用户词库导出 / 清空 / 导入（真实数据）',
    'tests/unit/ipc_tests.cpp': '管道、协议、请求服务、超时与迟到应答',
    'tests/unit/main.cpp': 'doctest 主程序',
    'tests/unit/panel_layout_tests.cpp': '面板布局：元素位置、九宫格、全键盘、数字/符号',
    'tests/unit/settings_tests.cpp': '输入设置：定制生成、读写、真实部署后的模糊音效果',
    'tests/unit/t9_tests.cpp': '九宫格前端：映射、音节表、拼音栏、组字、预编辑',
    'third_party/doctest/doctest/doctest.h': 'doctest 单头文件（MIT）',
    'third_party/fetch_librime.ps1': '下载固定版本 librime 官方包并校验 SHA-256',
    'third_party/weasel/UPSTREAM.md': '与 Weasel 上游的关系与追踪说明',
    'tools/check_imports/check_imports.py': '检查 PE 文件能否在 Win7 SP1 加载（子系统版本、导入表）',
    'tools/dev_register.ps1': '注册/注销开发版 T9Tip.dll（x64 与 x86）',
    'tools/gen_syllables/gen_syllables.py': '从 rime-ice 单字词典生成九宫格音节表',
    'tools/make_project_pdf.py': '生成本文档 Docs/Project.pdf',
    'installer/T9Ime.iss': '安装程序：x86/x64 一个安装包、TIP 注册、数据、开发包、开机启动、接管系统触摸键盘、Win7 补丁检测、更新时移开被占用的 DLL、卸载恢复',
    'res/T9Ime.ico': 'T9Ime 图标（16–256 px）：输入法列表、T9Host、安装程序',
    'res/T9Ime.png': '图标预览（256 px，README 使用）',
    'tools/make_icon.py': '绘制图标：蓝色圆角底、3×3 九宫格键、"T9"；小尺寸只保留"T9"',
    'tools/make_installer.ps1': '生成安装程序：版本号（project VERSION + 提交数）、检查构建与版本一致、调用 ISCC、体积报告',
    'tools/make_test_package.ps1': '打真机测试包（x86+x64、数据、脚本、SDK、VERSION.txt）',
    'tools/move_locked.py': '链接前把被占用的 DLL/EXE 改名，便于重新构建',
    'tools/prepare_data.py': '组装共享 Rime 数据目录并用 rime_deployer 预部署',
    'tools/t9diag/CMakeLists.txt': 't9diag.exe',
    'tools/t9diag/t9diag.cpp': '诊断：系统、权限、注册、Host 进程、焦点/触摸/指针事件记录（--watch）',
    'tools/t9repl/CMakeLists.txt': 't9repl.exe',
    'tools/t9repl/t9repl.cpp': '引擎命令行：按键、拼音栏、选词、方案切换、脚本（回归测试用）',
    'tools/touch_test/start_m5_test.bat': '真机一键测试 M5：注册、启动 Host、打开 TestHost、录制诊断',
    'tools/touch_test/start_m6_test.bat': '真机一键测试 M6：注册、启动 Host、打开测试窗口与设置、录制诊断',
    'tools/touch_test/start_touch_test.bat': '真机一键测试 M4：触摸弹出、布局、密码键盘',
    'tools/vm.py': 'vmrun 驱动 Win7 测试虚拟机（运行、拷贝、截图、进程）',
    'tools/vm_deploy.py': '把测试包部署到虚拟机并注册',
    'tools/vm_e2e.py': '在虚拟机里运行端到端测试',
}


def file_tree():
    files = subprocess.run(['git', 'ls-files'], cwd=ROOT, capture_output=True, text=True).stdout.split()
    files.append('Docs/Project.pdf')
    files = sorted(set(files))
    missing = [f for f in files if f not in FILES]
    if missing:
        sys.exit('no description for: ' + ', '.join(missing))
    # Build a nested dict.
    tree = {}
    for f in files:
        node = tree
        parts = f.split('/')
        for p in parts[:-1]:
            node = node.setdefault(p + '/', {})
        node[parts[-1]] = None
    return tree, len(files)


def tree_rows(node, prefix='', path=''):
    rows = []
    entries = sorted(node.items(), key=lambda kv: (kv[1] is None, kv[0].lower()))
    for i, (name, child) in enumerate(entries):
        last = i == len(entries) - 1
        branch = prefix + ('└─ ' if last else '├─ ')
        if child is not None:
            dpath = (path + name).rstrip('/')
            rows.append((branch + name, DIRS.get(dpath, ''), True))
            rows += tree_rows(child, prefix + ('   ' if last else '│  '), path + name)
        else:
            rows.append((branch + name, FILES[path + name], False))
    return rows


# ---------------------------------------------------------------- document

def build(out):
    commit = subprocess.run(['git', 'rev-parse', '--short', 'HEAD'], cwd=ROOT, capture_output=True,
                            text=True).stdout.strip()
    count = subprocess.run(['git', 'rev-list', '--count', 'HEAD'], cwd=ROOT, capture_output=True,
                           text=True).stdout.strip()
    version = '0.0.0'
    for line in (ROOT / 'CMakeLists.txt').read_text(encoding='utf-8').splitlines():
        if line.startswith('project(T9Ime VERSION'):
            version = line.split()[2]
    today = datetime.date.today().isoformat()
    full_width = A4[0] - 36 * mm

    def on_page(canvas, doc):
        canvas.saveState()
        canvas.setFont('YaHei', 7.5)
        canvas.setFillColor(MUTED)
        canvas.drawString(18 * mm, 10 * mm, f'T9Ime 项目文档 · {version}.{count} ({commit})')
        canvas.drawRightString(A4[0] - 18 * mm, 10 * mm, f'第 {doc.page} 页')
        canvas.setStrokeColor(LINE)
        canvas.line(18 * mm, 13 * mm, A4[0] - 18 * mm, 13 * mm)
        canvas.restoreState()

    doc = SimpleDocTemplate(str(out), pagesize=A4, leftMargin=18 * mm, rightMargin=18 * mm, topMargin=16 * mm,
                            bottomMargin=18 * mm, title='T9Ime 项目文档', author='T9Ime',
                            subject='模块、架构、调用关系与文件结构')
    st = []

    # Cover
    st += [Spacer(1, 60 * mm), P('T9Ime', 'title'), Spacer(1, 4), P('Windows 触摸九宫格输入法', 'subtitle'),
           Spacer(1, 10), P('项目文档：模块、架构、调用关系与文件结构', 'subtitle'), Spacer(1, 30 * mm),
           P(f'版本 {version}.{count}（commit {commit}）· {today}', 'subtitle'),
           P('开源协议：GPL-3.0', 'subtitle'), PageBreak()]

    # Contents
    st.append(P('目录', 'h1'))
    for line in ['1  项目概述', '2  总体架构', '3  模块说明', '4  模块之间的调用关系', '5  主要流程（调用链）',
                 '6  进程间通信', '7  线程模型', '8  文件结构与说明（树形）', '9  构建、测试与工具',
                 '10 第三方组件与开源协议']:
        st.append(P(line))
    st.append(PageBreak())

    # 1 Overview
    st.append(P('1  项目概述', 'h1'))
    st.append(P('T9Ime 是面向 Windows 触屏设备的中文输入法：屏幕上提供手机风格的<b>九宫格拼音键盘</b>和<b>英文全键盘</b>，'
                '同时支持物理键盘全拼。它以 TSF（Text Services Framework）文本服务的形式工作，输入引擎为 librime（中州韵），'
                '词库为雾凇拼音 rime-ice。'))
    st.append(P('目标', 'h2'))
    st += bullets([
        '主要目标设备：Windows 7 SP1 触屏一体机 / 平板；同时支持 Windows 10 / 11；x86 与 x64 完整支持（不支持 Win8.x、ARM64）。',
        '只有触摸屏的设备也能高效输入中文：点输入框自动弹出键盘，按输入框类型（数字、密码等）自动换布局，离开后收起。',
        '业务程序可通过 C 接口 T9Ctl.dll（C/C++/C#）控制键盘显示、布局、位置，并在键盘显示/隐藏时得到通知。',
        '安全与稳定：不用全局键盘钩子、不注入 DLL、不需要 uiAccess 签名；输入法 DLL 只依赖系统库；不记录用户输入。'])
    st.append(P('里程碑', 'h2'))
    st.append(table([
        ['里程碑', '内容', '状态'],
        ['M0', '调研、方案、Weasel fork 骨架', '完成'],
        ['M1', '引擎：librime + rime-ice，t9 数字码方案，t9repl 回归', '完成'],
        ['M2', '触摸面板（D2D，九宫格/数字/符号，候选栏）', '完成'],
        ['M3', 'TSF 文本服务：物理键盘拼音、进程内候选窗、Host 管道', '完成'],
        ['M4', '面板经 TIP 推送上屏、InputScope、自动弹出/隐藏、自动切换、系统键盘共存、密码键盘、讯飞风格布局', '完成，Win7 触屏实测通过'],
        ['M5', 'T9Ctl.dll / t9ctl.exe / TestHost、控制管道、可见性通知、IMM32 兼容', '完成，Win7/Win11 触屏实测通过'],
        ['M6', '托盘、设置窗口、简繁、模糊音、候选个数、词库导入导出清空、重新部署', '完成，Win7 触屏实测通过'],
        ['M7', '安装包、兼容矩阵', '未开始'],
    ], [16 * mm, 110 * mm, full_width - 126 * mm]))
    st.append(P('关键决策', 'h2'))
    st += bullets([
        '文本服务结构 fork 自 Weasel（小狼毫，GPL-3.0），整体以 GPL-3.0 发布。',
        '不用 uiAccess；单一 T9Host 进程承载引擎与面板；物理键盘候选窗在应用程序进程内绘制。',
        'Win7 为基线（_WIN32_WINNT=0x0601），Win8+ API 全部运行时动态加载；静态 CRT，无运行库依赖。',
        'librime 1.17.0 官方包（内置 librime-lua）；rime-ice 完整词库，固定 commit；数据预部署，运行时默认不做 maintenance。',
        't9 方案为数字码；回车、退格、拼音栏等九宫格语义由前端（src/host/engine/t9）实现。'])
    st.append(PageBreak())

    # 2 Architecture
    st.append(P('2  总体架构', 'h1'))
    st.append(P('T9Ime 由三类进程组成：每个使用输入法的<b>应用程序进程</b>里加载的 T9Tip.dll（TSF 文本服务，薄客户端）；'
                '每个用户会话一个的 <b>T9Host.exe</b>（引擎、触摸面板、托盘、设置）；以及调用 <b>T9Ctl.dll</b> 的业务程序。'
                '它们之间通过三条命名管道通信。librime 只在 T9Host 的引擎线程里调用。'))
    st.append(architecture_diagram())
    st.append(P('图 1  进程、模块与通信', 'caption'))
    st.append(P('设计要点', 'h2'))
    st += bullets([
        '<b>薄客户端 TIP</b>：T9Tip.dll 被加载进每个程序，只做 TSF 交互（按键、组字、焦点、候选窗绘制），所有输入逻辑在 T9Host；'
        'Host 不可达时请求快速失败，按键直接交给程序，不会卡住程序。',
        '<b>面板不抢焦点</b>：面板窗口 WS_EX_NOACTIVATE，点按键不改变前台程序；面板输出优先经 TIP 的编辑会话写入（推送），'
        '没有 TIP 连接时退回 SendInput。',
        '<b>焦点登记</b>：TIP 上报焦点（输入框类型、是否触摸引起、激活/停用），Host 据此决定弹出/隐藏与布局，并知道推送给谁。',
        '<b>引擎单线程</b>：librime 非线程安全，所有调用在引擎线程；面板命令异步投递，TIP 请求同步等待（带超时），'
        '维护任务（部署、词库）在关闭全部会话后执行。',
        '<b>不用 uiAccess 的取舍</b>：Win8+ 系统浮层（开始菜单搜索等）之上不自动弹出；其余场景用 TOPMOST 普通窗口。'])
    st.append(PageBreak())

    # 3 Modules
    st.append(P('3  模块说明', 'h1'))
    modules = [
        ('src/tip — T9Tip.dll（TSF 文本服务）',
         '加载进每个使用输入法的程序。TextService 实现 ITfTextInputProcessorEx、按键/焦点/组字/兼容区等全部接口；'
         'HostClient 经请求管道把按键、焦点交给 T9Host，结果以 TSF 组字写回；EventListener 在后台线程接收面板推送；'
         'TouchTracker 判断焦点变化是否由触摸点中该输入框引起；CandidateWindow 在程序进程内绘制物理键盘候选；'
         'LangBar 显示中/英；Register 负责 COM 与 TSF 注册。依赖：t9common、系统库。'),
        ('src/host/engine — t9core / t9engine',
         'RimeEngine 封装 librime 的初始化、会话、按键、候选、部署与用户词库；Session 在 librime 之上实现九宫格语义'
         '（回车上屏拼音、退格按段撤销、拼音栏选音节）。t9/ 子目录是纯 C++ 的九宫格前端（字母↔数字、音节表、拼音栏排序、组字状态、可读预编辑），'
         '可单独测试。'),
        ('src/host/app — T9Host 进程',
         'main.cpp：参数、单实例、HostApp（创建面板、启动引擎线程与三个管道服务、托盘、控制请求在 UI 线程执行、可见性通知、设置宿主、维护任务）。'
         'EngineThread：引擎线程与命令/任务/维护队列。InputSettings：简繁、候选数、模糊音及其生成的 Rime 定制文件。'
         'SettingsWindow：设置窗口。system_input：输入法切换（ImeSwitcher）、前台监视、系统触摸键盘接管。TextOutput：SendInput 上屏。'),
        ('src/host/ipc — 管道服务',
         'PipeServer：通用监听（每连接一线程）。RequestServer：TIP 请求（Hello/焦点直接应答，按键/候选经引擎线程）；'
         'EventServer：给 TIP 推送面板上屏。CtlServer：T9Ctl 控制请求。FocusRegistry：哪个 TIP 有焦点、输入框信息，并负责推送。'),
        ('src/host/panel — 触摸面板',
         'PanelWindow：不激活的 TOPMOST 窗口，统一处理 WM_POINTER（Win8+）、WM_TOUCH（Win7）与鼠标；手势（点击、长按、滑动、拖动、缩放）；'
         '把动作交给引擎线程，接收快照重排重绘；处理焦点事件（自动弹出）、前台归属、主题与大小。PanelLayout：纯几何布局与命中测试。'
         'PanelRenderer：Direct2D 1.0 渲染。AutoShow：弹出/隐藏判定（纯函数，可单测）。'),
        ('src/common — t9common',
         'protocol：IPC 消息格式；pipe：命名管道客户端与读写（重叠 I/O、超时、序号）；win_compat：Win7 基线与 Win8+ API 动态加载；'
         'ime_profile：T9Ime 的 TSF 身份与切换。TIP、Host、T9Ctl 共用，无外部依赖。'),
        ('src/ctl — T9Ctl.dll / t9ctl.exe',
         '公开 C 接口（t9ctl.h）。每次调用新建一条控制管道连接；需要 Host 时按需拉起；调用者自己的窗口在调用线程内切换输入法（Win7 同语言输入法之间只能这样切），'
         '其他窗口交给 Host。t9ctl.exe 是命令行封装（含 watch 打印可见性通知）。'),
        ('tools / tests / samples', 't9repl（引擎命令行与回归）、t9diag（现场诊断）、打包与虚拟机自动化脚本；'
         '单元测试（doctest）、引擎回归、Win7 导入检查、端到端测试；TestHost 示例（C++/C#）。'),
    ]
    for title, text in modules:
        st.append(KeepTogether([P(title, 'h2'), P(text)]))
    st.append(PageBreak())

    # 4 Dependencies
    st.append(P('4  模块之间的调用关系', 'h1'))
    st.append(dependency_diagram())
    st.append(P('图 2  模块依赖（编译与调用）', 'caption'))
    st.append(table([
        ['调用方', '被调用方', '方式', '内容'],
        ['T9Tip（TextService）', 'T9Host（RequestServer）', '请求管道 .req，同步，150–500 ms 超时',
         'Hello、按键、焦点进入/离开、选候选、翻页、清空、中英、状态查询'],
        ['T9Host（FocusRegistry）', 'T9Tip（EventListener）', '事件管道 .evt，Host 主动写', '面板上屏文字（PushCommit）'],
        ['T9Ctl.dll / t9ctl.exe', 'T9Host（CtlServer → HostApp）', '控制管道 .ctl，同步', '显示/隐藏/布局/位置/查询/切换输入法/可见性订阅'],
        ['T9Host（HostApp）', '业务程序窗口', 'PostMessage("T9Ime.Visibility")', '键盘显示、隐藏、移动通知'],
        ['RequestServer / PanelWindow', 'EngineThread', 'Invoke（同步）/ Post（异步）/ Maintain', '按键与面板命令、部署、词库'],
        ['EngineThread', 'PanelWindow', '快照 + 窗口消息', '组字、候选、上屏文字、透传按键'],
        ['FocusRegistry', 'PanelWindow', 'PostFocusEvent + 窗口消息', '焦点事件 → 自动弹出/隐藏'],
        ['HostApp / SettingsWindow', 'EngineThread、PanelWindow、InputSettings', '直接调用（UI 线程）', '应用设置、部署、词库、主题、大小'],
        ['EngineThread', 'rime.dll', 'librime C API', '会话、按键、候选、部署、levers（用户词库）'],
        ['T9Tip（HostClient）/ T9Ctl', 'T9Host.exe', 'CreateProcess（--background）', 'Host 未运行时按需拉起'],
    ], [33 * mm, 40 * mm, 45 * mm, full_width - 118 * mm]))
    st.append(PageBreak())

    # 5 Flows
    st.append(P('5  主要流程（调用链）', 'h1'))
    flows = [
        ('5.1 物理键盘输入', [
            '程序收到按键 → TSF 调用 TextService::OnTestKeyDown / OnKeyDown。',
            'HostClient::Call(kKey) 经请求管道发到 RequestServer → EngineThread::Invoke → 该 TIP 的 rime_ice Session::ProcessKey。',
            '应答 Result（组字、候选、上屏文字、中英）回到 TIP → 编辑会话更新组字 / 上屏，CandidateWindow 绘制候选。',
            'Host 忙（启动预热、部署）超时：本键直接交给程序，1 秒内只发焦点上报不发按键（避免堆积超时）。']),
        ('5.2 触摸面板输入与上屏', [
            '手指按键 → PanelWindow::PointerDown/Up（WM_POINTER / WM_TOUCH / 鼠标统一）→ Execute → EngineThread::Post 面板命令。',
            '引擎线程处理后发布快照并发窗口消息 → PanelWindow::OnEngineSnapshots 重排重绘；有上屏文字则 Output。',
            'Output → FocusRegistry::PushCommit：前台线程有带焦点的 TIP → 经事件管道推送 → EventListener → TIP 编辑会话写入；'
            '否则 TextOutput::SendText（SendInput）。所有输出经引擎线程排队，保证顺序。',
            '若前台程序还没在用 T9Ime：OnInteraction → ImeSwitcher 会话级切换 + WM_INPUTLANGCHANGEREQUEST（后台线程执行，避免卡 UI）。']),
        ('5.3 焦点与自动弹出 / 隐藏', [
            'TIP 的 OnSetFocus / OnPushContext / OnSetThreadFocus → ReportFocus：读 InputScope（GetAppProperty），TouchTracker 判断是否触摸点中该输入框，'
            'kFocusIn（含激活标志）/ kFocusOut（含停用标志）。',
            'RequestServer 在管道线程直接更新 FocusRegistry → 监听器转 FocusEvent（是否用户切换 T9Ime、是否前台停用）→ PanelWindow::PostFocusEvent。',
            'PanelWindow::OnFocusEvents → DecideOnFocus：显示（按 InputScope 选布局）/ 隐藏（立即或延时）/ 不变；显示时绑定前台程序。',
            '前台监视钩子 → OnForegroundChanged：换到其他程序则隐藏并记住；该程序回到前台时恢复显示。']),
        ('5.4 控制接口', [
            '业务程序 T9_ShowKeyboard(mode) → T9Ctl（调用线程拥有前台窗口时先在本线程把程序切到 T9Ime）→ 控制管道 → CtlServer。',
            'CtlServer 把请求以 SendMessageTimeout 交给 UI 线程 → HostApp::HandleCtl → PanelWindow 显示/布局 → 应答带面板状态（可见、布局、矩形）。',
            '面板 WM_WINDOWPOSCHANGED → HostApp::NotifyPlacement → 向已注册窗口 PostMessage("T9Ime.Visibility")。']),
        ('5.5 设置与重新部署、词库', [
            'SettingsWindow「应用」→ HostApp::ApplySettings：面板设置立即生效并保存；简繁 → EngineThread::SetOption（全部会话）；'
            '模糊音 / 候选数变化 → InputSettings::PrepareUserData 写用户 *.custom.yaml → EngineThread::Maintain(Redeploy)。',
            'Maintain：关闭全部会话 → RimeEngine::Redeploy（全量编译到 user/build）→ MarkDeployed 戳记 → 重开面板会话；TIP 会话下次请求时重建。',
            '结果以窗口消息回到 UI 线程 → 设置窗口状态行 / 托盘气泡。导出 / 导入 / 清空词库同样经 Maintain（levers API；清空时重启 librime）。',
            '启动时 PrepareUserData 比对戳记：升级后编译数据过期则重新部署，无定制则删除 user/build。']),
        ('5.6 启动与恢复', [
            '登录或首次需要时：TIP（HostClient）或 T9Ctl 以 --background 拉起 T9Host；单实例互斥体保证每用户会话一个。',
            'T9Host：创建面板 → 引擎线程初始化 librime、按需部署、预热词典 → 启动事件/请求/控制管道服务 → 托盘。',
            'TIP 与 Host 断开：定时重连并补报焦点；管道请求带序号，超时不断开，迟到的应答被跳过。']),
    ]
    for title, steps in flows:
        st.append(KeepTogether([P(title, 'h2')] + [Paragraph(s, S['bullet'], bulletText=f'{i}.')
                                                 for i, s in enumerate(steps, 1)]))
    st.append(PageBreak())

    # 6 IPC
    st.append(P('6  进程间通信', 'h1'))
    st.append(P('管道名：<font name="Mono">\\\\.\\pipe\\T9Ime.&lt;会话 ID&gt;.&lt;用户 SID&gt;.&lt;端点&gt;</font>。'
                '安全描述符允许 SYSTEM、当前用户与 AppContainer 读写，低完整性进程可写。消息头：magic "T9IM"、版本、类型、序号、长度；'
                '正文为 TLV 字段（标签 + 长度 + 值）。'))
    st.append(table([
        ['端点', '方向', '主要消息', '服务端'],
        ['.req 请求', 'TIP → Host（请求/应答）', 'Hello、Key、FocusIn（InputScope、触摸、只读、激活标志）、FocusOut（停用标志）、'
         'SelectCandidate、ChangePage、ClearComposition、SetAsciiMode、QueryState、Diagnostics', 'RequestServer'],
        ['.evt 事件', 'Host → TIP（推送）', 'EventHello（TIP 登记）、PushCommit（面板上屏文字）', 'EventServer'],
        ['.ctl 控制', 'T9Ctl → Host（请求/应答）', 'Query、Show、Hide、Toggle、SetMode、Dock、SetPosition、Activate、Deactivate、'
         'Register/UnregisterNotify；应答带可见、布局、矩形', 'CtlServer'],
    ], [22 * mm, 35 * mm, 85 * mm, full_width - 142 * mm]))
    st.append(P('可靠性措施', 'h2'))
    st += bullets([
        '服务端先建下一个管道实例再交接连接，避免连续连接时"找不到管道"；客户端在超时内短暂重试。',
        '请求带序号：超时不断开连接，下一次调用跳过迟到的应答；连续 8 次超时才断开，断开后 TIP 定时重连并补报焦点。',
        'Hello / FocusIn / FocusOut 不经引擎线程，直接在管道线程应答，保证 Host 始终知道焦点。'])

    # 7 Threads
    st.append(P('7  线程模型', 'h1'))
    st.append(table([
        ['进程', '线程', '职责'],
        ['T9Host', 'UI 线程', '面板窗口、托盘、设置窗口、控制请求（经 SendMessageTimeout 交来）、焦点事件、前台监视回调'],
        ['T9Host', '引擎线程', 'librime 全部调用；面板命令、TIP 请求（Invoke）、维护任务（部署、词库）'],
        ['T9Host', '管道线程（每连接一个）', '读写管道；焦点登记；需要引擎时 Invoke 等待'],
        ['T9Host', '后台切换线程', '会话级输入法切换（ActivateProfile FORSESSION 可能阻塞数秒）'],
        ['应用程序', 'UI 线程（TSF）', 'TextService 全部 TSF 回调、编辑会话、候选窗'],
        ['应用程序', '事件监听线程', '阻塞读事件管道，把推送投递给 UI 线程的消息窗口'],
    ], [22 * mm, 40 * mm, full_width - 62 * mm]))
    st.append(PageBreak())

    # 8 File tree
    tree, n = file_tree()
    st.append(P('8  文件结构与说明（树形）', 'h1'))
    st.append(P(f'仓库共 {n} 个文件（不含构建时下载的 librime、rime-ice 与构建产物）。目录用蓝色表示，右列为目录用途或文件实现的功能。'))
    rows = [('T9Ime/', DIRS[''], True)] + tree_rows(tree)
    data = [[Paragraph(name.replace(' ', '&nbsp;'), S['monodir' if is_dir else 'mono']),
             Paragraph(('<b>%s</b>' % desc) if is_dir else desc, S['cell'])] for name, desc, is_dir in rows]
    t = Table(data, colWidths=[78 * mm, full_width - 78 * mm], repeatRows=0)
    t.setStyle(TableStyle([('VALIGN', (0, 0), (-1, -1), 'TOP'), ('LEFTPADDING', (0, 0), (-1, -1), 2),
                           ('RIGHTPADDING', (0, 0), (-1, -1), 3), ('TOPPADDING', (0, 0), (-1, -1), 0.6),
                           ('BOTTOMPADDING', (0, 0), (-1, -1), 0.6),
                           ('LINEBELOW', (0, 0), (-1, -1), 0.2, colors.HexColor('#EEF0F2'))]))
    st.append(t)
    st.append(PageBreak())

    # 9 Build / test
    st.append(P('9  构建、测试与工具', 'h1'))
    st += bullets([
        '依赖：Visual Studio 2022（C++ 桌面）、CMake ≥ 3.25、Python 3、Git、PowerShell 7。librime 与 rime-ice 由脚本下载（不入库）。',
        '构建：<font name="Mono">pwsh -File build.ps1 -Preset x64-Release</font>（另有 x86-Release、*-Debug）；产物在 out/build/&lt;preset&gt;/bin，'
        '与安装目录布局一致（T9Host.exe、T9Tip.dll、T9Ctl.dll、rime.dll、data/）。',
        '版本：CMakeLists.txt 的 project(VERSION) 为唯一来源，第四位为 git 提交数；T9Tip.dll、T9Ctl.dll 带 Windows 版本资源。',
        '测试：ctest -LE e2e（单元、引擎回归、Win7 导入检查）；ctest -L e2e（端到端：panel、tip、push、autoshow、ctl、imm、switch、touch、settings）。',
        '真机：tools/make_test_package.ps1 打测试包，tools/touch_test/*.bat 一键测试，Docs/testing/ 为清单与用例；tools/vm*.py 在 Win7 虚拟机自动化。',
        '诊断：t9diag.exe（一次）/ t9diag.exe --watch N（持续记录焦点、触摸判定、指针事件）。'])

    # 10 Licenses
    st.append(P('10  第三方组件与开源协议', 'h1'))
    st.append(P('T9Ime 以 <b>GNU 通用公共许可证第 3 版（GPL-3.0）</b>发布（仓库 LICENSE）。文本服务结构源自 Weasel（GPL-3.0）。'
                '分发二进制须同时提供对应源代码并保留协议声明。'))
    st.append(table([
        ['组件', '用途', '协议'],
        ['librime 1.17.0（含 librime-lua 等插件）', '输入引擎 rime.dll（构建时下载）', 'BSD-3-Clause'],
        ['rime-ice 雾凇拼音', '方案与词库（构建时下载，固定 commit）', 'GPL-3.0'],
        ['OpenCC', '简繁转换数据', 'Apache-2.0'],
        ['Weasel 小狼毫', 'TSF 文本服务结构参考', 'GPL-3.0'],
        ['doctest', '单元测试框架（仅测试）', 'MIT'],
    ], [60 * mm, 75 * mm, full_width - 135 * mm]))

    doc.build(st, onFirstPage=lambda c, d: None, onLaterPages=on_page)
    return out


if __name__ == '__main__':
    out = build(ROOT / 'Docs' / 'Project.pdf')
    print(f'wrote {out}')
