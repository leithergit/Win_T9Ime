# 调研：librime 1.17.0 + rime-ice（t9 / rime_ice）实测

> 日期：2026-09-29 · 环境：Windows 11 x64，Python 3.13 ctypes 直接调用 rime.dll
> 所有数字均为本机实测；测试脚本在会话 scratchpad（`rime/t9test*.py`、`rimect*.py`），未入库。

## 0. 结论速览

| 项目 | 结论 |
|---|---|
| librime 版本 | 1.17.0，包名 `rime-33e7814-Windows-msvc-x64.7z`（7,428,809 B），librime commit `33e7814` |
| 插件 | **librime-lua（Lua 5.4.8，非 LuaJIT）、librime-octagram、librime-predict 已静态编进 rime.dll**，无 plugins 目录，不用另外编译 |
| 运行时依赖 | rime.dll 只导入 `KERNEL32 / USER32 / dbghelp`，静态 CRT，**无需 VC++ 运行库**，满足 §13 |
| t9_processor | **不在官方 librime 里**，是仓输入法（Hamster）闭源 librime 的插件。缺少它时只写一条 ERROR 日志，schema 照常可用 |
| user_dict 共享 | t9 与 rime_ice 都用 `translator/dictionary: rime_ice`，**共用 `rime_ice.userdb`**。实测：在 t9 选的词，rime_ice 下立刻排第一 |
| prism 共存 | `rime_ice.prism.bin`（40 KB）与 `t9.prism.bin`（206 KB）共用同一个 `rime_ice.table.bin` |
| 94664486 | preedit=`"94664 486"`，首选 **中国**〔comment=`zhong guo`〕 |
| 744 | preedit=`"744"`，首选 **是**〔`shi`〕 |
| set_input `zhong'4486` | 可用，数字码和字母拼音可以混输，preedit=`"zhong'4 486"`（不再是纯数字） |
| comment | 就是空格分隔的**完整拼音**（`spelling_hints:100` + `comment_format:[]` 的效果）；emoji 候选的 comment 为空 |
| 体积（完整词库） | build 目录 76.2 MB；rime.dll + 运行时数据用 7z LZMA2-9 压缩后 **24.3 MB**（zip 33.2 MB）。已逼近 §10 的 25 MB 上限，后续要用 Inno 的 lzma2/ultra64 实测 |
| 首次运行 | 预部署数据**必须保留文件 mtime**，否则首次启动会重建 prism（8.6 s，写入用户目录 11.3 MB）；保留 mtime 时 maintenance 只要 0.1 s |

---

## 1. librime 1.17.0 Windows 包

Release 资产（tag 1.17.0）：

| 文件 | 大小 |
|---|---|
| rime-33e7814-Windows-msvc-x64.7z | 7,428,809 |
| rime-33e7814-Windows-msvc-x86.7z | 7,183,925 |
| rime-33e7814-Windows-clang-x64.7z | 1,421,442 |
| rime-deps-33e7814-Windows-msvc-x64.7z | 3,622,532（静态库 + **opencc 数据**） |

msvc-x64 包内容：

```
dist/bin/rime_deployer.exe      459,776
dist/bin/rime_dict_manager.exe  278,016
dist/bin/rime_patch.exe         217,600
dist/include/rime_api.h          20,632   (+ rime_api_deprecated.h, rime_api_stdbool.h, rime_levers_api.h)
dist/lib/rime.dll             3,739,136   (LZMA 压缩后约 1.20 MB)
dist/lib/rime.lib               301,656
dist/lib/rime.pdb            50,098,176
version-info.txt: RIME_PLUGINS=hchunhui/librime-lua(ec52e48) lotem/librime-octagram(dfcc151) rime/librime-predict(920bd41)
```

- rime.dll 里能找到 `lua_translator / lua_filter / lua_processor / octagram / grammar / predictor` 这些字符串，以及 `Lua 5.4.8`；找不到 `t9_processor`。
- 实测：`rq` 触发了 `date_translator.lua`，得到 `2026-09-29`，说明 librime-lua 可用。
- rime_deployer.exe 依赖 rime.dll，需要放在同一目录。用法：`rime_deployer --build <user_dir> <shared_dir> <staging_dir>`。
- `rime_get_api()->data_size = 788`。

### 1.1 rime_api.h 相关 API（RimeApi 函数表，按需用 `RIME_API_AVAILABLE` 判断）

| 用途 | 函数 |
|---|---|
| 初始化 | `setup(traits)`、`set_notification_handler(cb, ctx)`、`initialize(traits)`、`finalize()` |
| 部署 | `start_maintenance(full_check)` + `join_maintenance_thread()`、`is_maintenance_mode`、`deployer_initialize`、`prebuild()`、`deploy()`、`deploy_schema(file)`、`deploy_config_file(file, version_key)`、`run_task(name)` |
| Traits | `shared_data_dir / user_data_dir / prebuilt_data_dir / staging_dir / log_dir / min_log_level / app_name / distribution_*` |
| Session | `create_session / find_session / destroy_session / cleanup_stale_sessions / cleanup_all_sessions` |
| 按键 | `process_key(sid, keycode, mask)`（X11 keysym，例如 BackSpace=0xff08、Return=0xff0d）、`simulate_key_sequence(sid, "…")` |
| 输入串 | `get_input(sid)`、**`set_input(sid, str)`**、`get_caret_pos / set_caret_pos` |
| 输出 | `get_commit/free_commit`、`get_context/free_context`（composition{length, cursor_pos, sel_start, sel_end, preedit} + menu + commit_text_preview + select_labels）、`get_status/free_status` |
| 候选 | **`candidate_list_begin / candidate_list_next / candidate_list_end`**、`candidate_list_from_index(sid, it, index)`、`select_candidate(sid, idx)`（全局下标）、`select_candidate_on_current_page`、`highlight_candidate(_on_current_page)`、`change_page(sid, backward)`、`delete_candidate(_on_current_page)`（删除用户词） |
| 选项 | `set_option / get_option`（ascii_mode、traditionalization、emoji、full_shape、ascii_punct、search_single_char）、`set_property / get_property`、`get_state_label(_abbreviated)` |
| Schema | `get_schema_list / select_schema / get_current_schema / schema_open / config_*` |
| 用户数据 | `sync_user_data()`（导出 `*.userdb.txt` 快照到 sync 目录并合并）；`rime_levers_api.h` 中的 `export_user_dict(dict, file)`、`import_user_dict(dict, file)`、`backup_user_dict`、`restore_user_dict`、`user_dict_iterator_*`；命令行工具 `rime_dict_manager.exe` |
| 通知 | `RimeNotificationHandler(ctx, sid, type, value)`：deploy start/success/failure、schema `t9/中文九键`、option `ascii_mode` / `!ascii_mode` 等 |

**caret 与 preedit 的实测细节**（设计前端时要注意）：
- `composition.cursor_pos / sel_start / sel_end` 是 **preedit 的 UTF-8 字节偏移**，不是字符下标，也不是 input 下标。例如：`"94664486"` 的 preedit 是 `"94664 486"`，其中加入了空格分隔，cursor_pos=9；部分选词"中"之后，preedit 为 `"中486"`，sel=[3,6]，cursor_pos=6。
- `get_caret_pos` 是 **input 串的字节偏移**。`set_caret_pos(5)` 之后，preedit 变回原始串 `"94664486"`（不再分段），候选只对前 5 位计算（中 / 种 / 兄…）。
- preedit 只在 abc 段的音节之间插入**空格**；用户手输的 `'` 原样保留，例如 `"shi'648"`、`"94664 486'"`。
- 1 键：input 非空且有候选时，key_binder 把 `1` 转成 `'`（实测 `94664486`+`1` 得到 input `94664486'`）。input 为空时，1 走 punctuator，候选为 `1 @ . / : _ - #`，comment 为 `〔半角〕`。

## 2. rime-ice

- 仓库：`https://github.com/iDvel/rime-ice`，**commit `3aea6d3694fb3d94ec663641f021f788822897ad`**（2026-09-25，"fix(lua): 修复 LuaJIT 下 pin_cand_filter 模式匹配报错 (#1633)"）
- 许可证：**GPL-3.0**（LICENSE 文件首行为 GNU GENERAL PUBLIC LICENSE Version 3）。
- `t9.schema.yaml` 版本 3.0.0：`__include: rime_ice.schema.yaml:/`，但**自己完整覆盖了 `engine` 的四个列表**，所以 rime_ice 的 lua filter 等组件在 t9 下都不生效。编码用**数字**（`derive/[abc]/2/` …），**SPEC §5 "大写字母码" 的说法已经过时**。
  - `derive` 每步把一组字母全部替换，同时保留原拼写，因此同一音节会同时存在"全字母、部分数字、全数字"几种拼写。这是 `zhong'4486` 能混输的原因。
  - 编译后的 `translator/preedit_format` 里还留着 `xform/(?<=[A-Z])\s(?=[A-Z])//`，是大写方案的遗留，对数字码无影响。
  - SPEC 中"九宫格英文走 melt_eng xlit 大写码"的说法也过时了：实际是 `others/Hamster/melt_eng.custom.yaml` 里 `xlit/abc…/222333…/` 转成数字，t9 默认注释掉了 `table_translator@melt_eng`。
- 另有 `lua/t9_preedit.lua`（给 iRime 用，把 cand.preedit 设成 comment）；t9 schema 没有挂载它。

### 2.1 t9_processor 的来源与替代方案

- 官方 librime 和 1.17.0 的三个内置插件里都没有 `t9_processor`。GitHub 上公开的 imfuxiao/Hamster 最后一次提交是 `65693706`（2025-05-13），也不含它。仓输入法 2.19.0（2026-01-26）"中文九键功能优化"引入了它，2.19.3（2026-04-12）的更新日志写着"fix: rime t9 插件 t9_processor 部分情况下删除键无响应"，所以它是 Hamster 私有 librime 的插件。
- 仓文档的说法："t9_processor 处理九键的回车键、删除键等"，要求它放在 processors 第一位；另要求 `t9/isDisplayOriginalPreedit:false`，preedit 按候选 comment 还原。
- 缺少时实测：每次 `select_schema t9`（以及每建一个 session）日志里出现 `E engine.cc:312] error creating processor: 't9_processor'`，其余功能正常。
- 缺少它的实际影响（实测）：
  - Return：直接上屏原始 input，例如 `"94664486"`，或 `set_input` 之后的 `"zhong'4486"`。
  - BackSpace：逐字节删除；删到已确认的拼音段时，会一个字母一个字母地删（`zhong'` → `zhong`），不会整段撤回成数字。
- **建议：在 Host 前端实现等价逻辑，不写 lua_processor。** 理由：拼音栏状态（已确认段、对应的原始数字、撤销栈）本来就在前端；面板按键先经过前端，前端可以拦截 Return（改为上屏由 comment 还原的拼音或字母）和 BackSpace（有已确认段时整段恢复为数字码，用 `set_input` 实现）。另外，用 `t9.custom.yaml` 把 `t9_processor` 从 processors 中去掉，消除日志噪声：`patch: engine/processors: [ascii_composer, recognizer, key_binder, speller, punctuator, selector, navigator, express_editor]`。

### 2.2 Hamster 前端做法（公开版源码，供 M1 参考）

- `HamsterKit/Sources/T9/T9Constants.swift`（686 行）：`t9ToPinyinMapping` / `pinyinToT9Mapping` 是手写的静态表，加一个 trie。
- 拼音栏 `getPinyinCandidates()`：取 preedit，去掉空格，跳过开头的非数字（即已确认的拼音），对剩下的数字串逐个前缀查表；排序规则为先按长度降序，再按字母序。
- preedit 还原 `t9pinyinToPinyin(comment:)`：把高亮候选的 comment 按空格切成音节，逐个转成数字码，然后替换 preedit 中对应的数字段。
- 选拼音：调用私有 `replaceInputKeys(text, startPos, count)`。我们可以用 `get_input` 加 `set_input` 实现同样的效果（已实测可行）。

## 3. 部署与体积实测

`rime_deployer --build . . ./build`，schema_list 为 [rime_ice, t9]。用时：完整词库 29.5 s（wall）。

| 文件 (build/) | 完整 (full) | 去 tencent | 仅 8105+base+others |
|---|---:|---:|---:|
| rime_ice.table.bin | 60,642,468 | 29,064,184 | 17,655,024 |
| rime_ice.prism.bin | 40,032 | 40,032 | 40,032 |
| rime_ice.reverse.bin | 57,524 | 57,524 | 57,524 |
| t9.prism.bin | 206,232 | 206,232 | 206,232 |
| melt_eng.table.bin | 640,564 | = | = |
| melt_eng.prism.bin | 8,418,208 | = | = |
| melt_eng.reverse.bin | 245,124 | = | = |
| radical_pinyin.table/prism/reverse.bin | 2,319,996 / 2,488,672 / 963,180 | = | = |
| **build 目录合计** | **76,165,768** | 44,587,484 | 33,178,324 |
| 运行时包 7z LZMA2-9（rime.dll + data） | **24,337,578** | 15,093,909 | 11,679,699 |
| 同上 zip deflate-9 | 33,159,599 | 20,447,964 | 15,852,926 |

- 运行时包的内容：rime.dll、build/、lua/（828 KB，其中 lunar.db 722 KB）、opencc/（emoji + s2t）、*.schema.yaml、default(.custom).yaml、symbols*.yaml、custom_phrase.txt、en_dicts/*.txt。**不含 *.dict.yaml 源文件，实测可以正常运行**。
- 单文件 LZMA 压缩后：rime_ice.table.bin 完整版 19.0 MB，去 tencent 9.8 MB，base 版 6.4 MB；melt_eng.prism.bin 1.60 MB；radical_pinyin 三个 bin 合计 1.41 MB；rime.dll 1.20 MB。
- 原始词库：cn_dicts 共 46,376,525 B（base 16.6 MB / 557,954 行；ext 11.9 MB / 339,199 行；tencent 17.3 MB / 981,283 行；8105 116 KB；41448 387 KB，默认不启用；others 17 KB）。LZMA 压缩后 **11.0 MB**，比打包 table.bin（19.0 MB）小 8 MB。代价是首次运行要现场部署约 30 s（x64）。
- 可选的进一步精简：t9 用不到 radical_pinyin 和 melt_eng。如果物理键盘的 rime_ice 可以不要部件拆字反查，去掉 radical_pinyin 能省约 1.4 MB（压缩后）。melt_eng.prism.bin 8.4 MB 是 rime_ice 英文混输用的，保留。
- **缺少 opencc 数据**：rime-ice 只带了 emoji.json。`simplifier@traditionalize` 需要 `s2t.json + STCharacters.ocd2 + STPhrases.ocd2`（约 971 KB，LZMA 压缩后约 0.3 MB），来自 `rime-deps-…-msvc-x64.7z` 的 `share/opencc/`。缺失时日志报 `opencc config not found: s2t.json`，简繁开关无效。补上之后实测得到 `中國`。

### 3.1 预部署陷阱（重要）

- 设置 `shared_data_dir=<data>`、`prebuilt_data_dir=<data>/build`、`staging_dir=<user>/build` 时：
  - 如果复制文件时**没有保留 mtime**（`cp` 不带 `-p`），`default.yaml` 会被判定为 "source file changed"，schema 重新编译，于是 4 个 prism 全部重建到用户目录。实测耗时 8.6 s，占用 11.3 MB。
  - **保留 mtime** 时 maintenance 只要 0.1 s，用户 build 目录为空，直接使用预编译的 bin。
- 安装器必须保留文件时间戳（Inno Setup 默认保留）。zip 便携版也要保留。构建脚本中所有复制步骤都要用保留 mtime 的方式。

## 4. user_dict 与 prism 共存（实测）

- 两个 schema 编译后都是 `translator/dictionary: rime_ice`，没有单独设置 `user_dict`，所以用户词库都是 `rime_ice.userdb`（leveldb 目录）。两个 session 同时存在于一个进程中，用户目录里只生成一个 `rime_ice.userdb`。
- 学习互通：在 t9 中输入 `744`，选第 7 个候选"使"上屏。随后在 rime_ice 中输入 `shi`，首选变成"使"；回到 t9 中输入 `744`，首选同样是"使"。
- 其他用户文件：t9 的 `custom_phrase` 用 `custom_phrase_t9`（stabledb，编码必须写数字）；rime_ice 用 `custom_phrase`，两者分开。
- prism：`rime_ice.prism.bin` 和 `t9.prism.bin` 各自独立，共用 `rime_ice.table.bin`、`rime_ice.reverse.bin`，可以同时加载（上面的两个 session 就是同时存在的）。

## 5. ctypes 实测输出（t9 schema）

```
94664486 : input='94664486' preedit='94664 486' cursor=9 sel=[0,9] preview='中国'
  中国〔zhong guo〕|🇨🇳〔〕|中火〔zhong huo〕|凶魂〔xiong hun〕|雄浑〔xiong hun〕|种过〔zhong guo〕|忠魂…|重活…
744      : preedit='744'  是〔shi〕|时|事|实|十|🔟〔〕|师|使…
9466     : 中〔zhong〕|🀄〔〕|种|重|终|众|兄〔xiong〕…
4        : 个〔ge〕|或〔huo〕|过〔guo〕|好〔hao〕|👌…（全部 810 个候选）
set_input "zhong'4486": preedit="zhong'4 486" 中华魂〔zhong hua hun〕|中国姑娘〔zhong guo gu niang〕|中国湖南|中高火|中国魂…
set_input "shi'648"  : preedit="shi'648" 是牛〔shi niu〕|食牛|石牛|是哦〔shi o〕|使〔shi〕…
set_input "zhongguo" / "zhong'guo"（t9 下全字母）: 可用，候选 中国/种过…
94664486 + BackSpace : input='9466448' 胸骨〔xiong gu〕|中古|…|印尼虎〔yin ni hu〕
部分选词 "中"(index 22)  : input 不变，preedit='中486'，sel=[3,6]，候选 或/过/国/果…；再选一个后 commit='中过'
Return   : 上屏原始 input（'94664486' / "zhong'4486"）；Space：上屏首选 '中国'；Esc：清空
traditionalization=1（补齐 opencc 后）: 中國〔zhong guo〕…
ascii_mode=1 : process_key('9') 返回 0（不消费）
rime_ice: zhongguo→中国；nihao→你好；rq→2026-09-29（lua 生效）；hello→hello
```

性能（x64，冷启动后第一次查询）：`94664486` 按键 66 ms，其中包括 mmap 冷加载；迭代完整候选列表 467 个，耗时 6.5 ms。之后逐键 `process_key` 在 0.2 到 10 ms 之间。16 位长码的完整候选列表为 475 个。

**preedit 转换的结论**：librime 返回的 preedit 是**数字加空格分段**，不会自动变成拼音。候选 comment 是完整拼音，音节之间用空格分隔。前端的转换规则：
1. 取高亮候选的 comment（comment 为空时，例如 emoji，往后找下一个 comment 非空的候选）。
2. 把音节逐个转成数字码，从 input 未确认部分的开头开始消费。
3. 消费不完的剩余数字按原样显示（或显示首个映射）。
4. input 中已经是字母的部分直接显示。

注意 preedit 的空格分段来自最佳路径，不一定和高亮候选的音节数一致（例如 `9466448` 分段为 `94664 48`，而候选"印尼虎"是 3 个音节），所以要以 comment 为准。这个转换在前端 C++ 中实现，不需要 lua filter；如果用 t9_preedit.lua 改写 cand.preedit，只影响单个候选，拼音栏仍然要前端计算。

## 6. Lua 组件清单（保留）

| 组件 | 文件 | rime_ice | t9 |
|---|---|:-:|:-:|
| lua_processor@*select_character（以词定字） | select_character.lua | ✓ | |
| lua_translator@*date_translator | date_translator.lua（require convert_ar_num_to_zh.lua） | ✓ | ✓ |
| lua_translator@*lunar | lunar.lua + lunar.db（ReverseDb） | ✓ | |
| lua_translator@*uuid | uuid.lua | ✓ | |
| lua_translator@*unicode | unicode.lua | ✓ | |
| lua_translator@*number_translator | number_translator.lua | ✓ | |
| lua_translator@*calc_translator | calc_translator.lua | ✓ | ✓ |
| lua_translator@*force_gc | force_gc.lua | ✓ | ✓ |
| lua_filter@*corrector | corrector.lua | ✓ | |
| lua_filter@*autocap_filter | autocap_filter.lua | ✓ | |
| lua_filter@*v_filter | v_filter.lua | ✓ | |
| lua_filter@*pin_cand_filter | pin_cand_filter.lua | ✓ | |
| lua_filter@*long_word_filter | long_word_filter.lua | ✓ | |
| lua_filter@*reduce_english_filter | reduce_english_filter.lua | ✓ | |
| lua_filter@*search@radical_pinyin | search.lua | ✓ | |

未挂载的 lua 文件：cn_en_spacer、en_spacer、debuger、is_in_user_dict、t9_preedit、cold_word_drop/*（8 个文件）。整个 lua/ 目录 828 KB，其中 lunar.db 占 722 KB，直接整目录保留即可。
非 lua 的依赖：`simplifier@emoji`（opencc/emoji.json + emoji.txt）、`simplifier@traditionalize`（s2t，需另外打包）、`reverse_lookup_filter@radical_reverse_lookup`、`affix_segmentor@radical_lookup`（依赖 radical_pinyin），以及 `table_translator@melt_eng / cn_en / custom_phrase`。

注意：t9 里的 date / calc 触发词是字母（`rq`、`=`），九键数字输入触发不了（实测 `7823` 出的是"如厕"）。如需要，另行在 t9.custom.yaml 里配置数字触发。

## 7. 对 SPEC 的修正与待决策

1. SPEC §5 的"大写字母码"已经过时，实际是**数字码**。拼音栏的"大写码段替换成小写拼音"应改为"数字段替换成小写拼音 + `'`"。已实测 `set_input("zhong'4486")` 可用。
2. t9_processor 在官方 librime 中不存在，建议在前端实现回车和退格的语义，并在 custom.yaml 中把它从 processors 去掉。
3. 完整词库压缩后已达 24.3 MB。加上 Host、TIP、安装器开销，很可能**超过 25 MB 目标**。可选方案：
   - (a) 接受超标；
   - (b) 打包 dict.yaml 源文件（约 11 MB），首次运行部署约 30 s；
   - (c) 去掉 radical_pinyin 反查，省约 1.4 MB。
   需要决策。
4. 需要从 rime-deps 包中额外打包 opencc 的 s2t 数据（约 0.3 MB 压缩后）。
5. rime-ice 为 GPL-3.0，librime 为 BSD-3，Lua 5.4 为 MIT，opencc 为 Apache-2.0。

## 来源
- https://github.com/rime/librime/releases/tag/1.17.0
- https://github.com/iDvel/rime-ice（commit 3aea6d3）
- https://github.com/imfuxiao/Hamster（commit 6569370，公开版）
- 仓输入法更新日志：https://ihsiao.com/apps/hamster/docs/updates/changelog/
- 仓输入法中文九键配置：https://ihsiao.com/apps/hamster/docs/guides/chinese-ninekey-configuration/
- （无关但易混淆）librime PR #1220（T9 增量音节图缓存，未合并）：https://github.com/rime/librime/pull/1220
