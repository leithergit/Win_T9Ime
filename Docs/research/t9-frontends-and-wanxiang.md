# 九键前端实现调研：Hamster/元书、万象拼音、其他开源实现

> 调研日期：2026-09-29。源码均为浅克隆后直接阅读（scratchpad：`...\scratchpad\Hamster`、`rime_wanxiang`、`rime-ice`、`librime-hamster`、`h3doc`、`others\*`）。
> 与 SPEC §5 的关系：本文件修正了 §5 中"大写字母码"的假设，并给出拼音选择栏、preedit 重建、撤销的参考算法。

## 0. 结论速览

1. **rime-ice 当前 t9 方案用的是数字码，不是大写字母码**。`speller/algebra` 里 `derive/[abc]/2/` … `derive/[wxyz]/9/` 共 8 条 derive 串联执行，原小写拼写保留。每条 derive 都作用于前面已有的全部拼写，所以同一音节会派生出"部分字母、部分数字"的混合拼写（按键组组合，最多 2^8 种）。实际效果是 `zhe43`、`zhe'43`、`zhe'ge` 都能查到"这个"（第三方用真实 rime.dll 跑过，见 §3.1）。SPEC §5 的"2→A…"要改成数字。
2. **`t9_processor` 不在任何公开源码里**。它是元书输入法（Hamster3，闭源）内置 librime 的私有组件。元书文档只说它"用来处理九键输入的字符，如回车键、删除键等"，并要求它排在 processors 首位。imfuxiao/librime 公开 fork 的所有分支（含 `feat/t9`、`v1.17.0`）都搜不到它。**用官方 librime 1.17 必须用 `*.custom.yaml` 把它移除**，否则部署时会报找不到组件（其余功能不受影响）。
3. **`t9/isDisplayOriginalPreedit` 是前端读取的配置，不是 librime 的配置**。值为 false 时，前端用高亮候选的 comment（`spelling_hints` 给出的全拼）去替换 preedit 里的数字。开源的 Hamster v2 有完整实现（`t9pinyinToPinyin`，见 §1.4），可以直接照搬。
4. **拼音选择栏是纯前端算法**：一张"音节→数字码"的静态表，加上对第一个未确认数字段做前缀枚举。点选后把那段数字原地替换成小写拼音。Hamster 为此在自己的 librime 里加了私有 API `RimeReplaceInput(pos,count,str)`；官方 API 里可以用 `get_input` + 字符串拼接 + `set_input` 实现同样效果。
5. **万象拼音有九键方案**（`wanxiang_t9` / `wanxiang_t9i`），同样用数字码。但它重度依赖 librime-lua（10 余个 lua 组件），语法模型 `wanxiang-lts-zh-hans.gram` 有 **413 MB**，许可证是 CC-BY-4.0。**推荐 T9Ime 继续用 rime-ice t9**，万象可作为后续可选的"高精度包"（理由见 §2.4）。

---

## 1. Hamster（仓输入法）/ 元书输入法

### 1.1 仓库与代际

| 名称 | 仓库 | 状态 |
|---|---|---|
| 仓输入法 v2（开源） | https://github.com/imfuxiao/Hamster （GPL-3.0，最后 push 2025-05-13） | 含完整九键拼音栏代码（Swift） |
| 元书输入法 / Hamster3（闭源） | 仅文档仓库 https://github.com/imfuxiao/Hamster3Document | `t9_processor` 和 `isDisplayOriginalPreedit` 在这一代出现（商店 1.6.0 / TF 244 起） |
| librime fork | https://github.com/imfuxiao/librime （BSD-3，分支 `feat/t9`、`v1.17.0` 等） | **不含** `t9_processor`，公开分支里也没有 `replace_input` 实现 |
| LibrimeKit（xcframework 分发） | `librimeFramework.sh` 从 `imfuxiao/LibrimeKit` releases 下载 2.4.2 版二进制 | 仓库已不可访问（404） |

私有 API 声明见 `Hamster/Packages/RimeKit/Sources/C/rime_api.h:390`：
```c
RIME_API Bool RimeReplaceInput(RimeSessionId session_id, size_t pos, size_t count, const char* replace_char);
// RimeApi 结构体里也有：Bool (*replace_input)(RimeSessionId, size_t pos, size_t count, const char*);
```
官方 librime（本机 `rime-33e7814` 构建和 upstream 1.17.0）**都没有**这个函数。等价做法：`s = get_input(); s.replace(pos,count,str); set_input(s)`。

### 1.2 音节表（静态、离线生成）

- `Packages/HamsterKit/Sources/T9/T9Constants.swift`
  - `t9ToPinyinMapping: [String:[String]]`：数字码 → 音节列表，例如 `"226": ["ban","bao","can","cao"]`。单键条目额外带上字母和数字本身，例如 `"2": ["a","b","c","2"]`，`"3": ["d","e","f","3"]`。
  - `pinyinToT9Mapping: [String:String]`：音节 → 数字码（反向表）。
  - `t9PinyinTrie`：用 `t9ToPinyinMapping` 的全部 key 建成的 Trie，用来判断"某个数字前缀还能不能延伸成合法音节"。
- 生成方式：`Packages/HamsterKit/Tests/Algo/T9Test.swift` 中的 `testT9Mapping()`。它用约 410 个全拼音节 × 字母→数字表，按码分组后打印成 Swift 字面量，再手工粘贴进源码。（**我们按 SPEC 要求用生成脚本产出，不手写**。）

### 1.3 拼音栏候选的推导：`RimeContext.getPinyinCandidates()`

文件：`Packages/HamsterKeyboardKit/Sources/RimeContext/RimeContext.swift:957`

算法：
1. 取 `composition.preedit`（librime 原始 preedit，数字串，音节间可能有空格），去掉空格。
2. **去掉开头的非数字字符**，也就是已经点选确认成小写拼音的部分，剩下的是第一个未确认数字段及其后续。
3. 如果剩余部分不为空：对 `count = 1…n`，取 `prefix(count)`。**只要 Trie 里没有这个前缀就停止**；如果 `t9ToPinyinMapping[prefix]` 存在，就把它的音节全部加入候选。
4. 如果剩余部分为空（全部已确认）：取 preedit 最后一个空格分隔的音节，去掉非小写前缀，得到最后一个拼音。查出它的数字码，然后不断 `popLast()`，把每个前缀码对应的音节都列出来，方便改选最后一个音节。
5. 排序：
   ```swift
   sorted { a, b in
     if a.count > b.count { return true }        // 字符串越长越靠前（≈ 匹配长度降序）
     if a.count == b.count {
       if Int(a) != nil || Int(b) != nil { return false } // 数字本身放末尾
       return a < b                               // 同长按字母序
     }
     return false }
   ```
   **没有按音节频率排序**，同长度只按字母序。SPEC 要求"再按音节频率"，这一点比 Hamster 更好，需要我们自己补一份音节频率表（可以从 rime-ice 词典统计）。
6. 调用处：`View/StandarKeyboard/ChineseNineGridKeyboard.swift:140`。它订阅 `userInputKeyPublished`，输入为空时显示默认符号列表，否则显示音节列表（`UICollectionView` 左侧竖栏）。

### 1.4 点选：`ChineseNineGridKeyboard.collectionView(_:didSelectItemAt:)`

文件：`ChineseNineGridKeyboard.swift` 约 330–375 行。
1. 如果点的是非字母数字符号：先上屏首选候选，再上屏该符号，然后 `reset`。如果点的是纯数字（例如 `"2"`）：直接上屏数字。
2. 音节 `symbol`（如 `"zhe"`）：`code = pinyinToT9Mapping[symbol]`（`"943"`）。取 `getInputKeys()`（原始 input），**从头扫描，找到第一个以 `code` 开头的位置**作为 `startPos`（按 utf8 字节计数）。
3. 如果扫到末尾都没找到，并且存在 `selectCandidatePinyin`：用上次记录的 `startPos`（对应"改选最后一个音节"的场景）。
4. `RimeReplaceInput(startPos, symbol.utf8.count, symbol)`：用**等长的小写拼音**原地替换同样长度的数字，**不插入撇号**。例如 `94343` 点 `zhe` 之后变成 `zhe43`。
5. 记录 `selectCandidatePinyin = (symbol, startPos, count)`，然后 `syncContext()`。

注意：步骤 2 按"第一次出现"匹配，在 `2222` 这类重复数字串上可能错位。更稳的做法是像 EyaaCai 那样自己维护"已确认边界栈"（§3.2）。

### 1.5 撤销：`KeyboardInputViewController.deleteBackward()`（约 470–500 行）

在九宫格模式下，如果存在 `selectCandidatePinyin`：把刚才那段拼音 `RimeReplaceInput` 回原数字码（`pinyinToT9Mapping[拼音]`），清空记录后返回，**不删除任何字符**。否则执行普通的 BackSpace。**只能撤销最近一次点选**（只存一条记录）。我们应改成栈结构，支持多级撤销。

### 1.6 preedit 重建（isDisplayOriginalPreedit=false 时的语义）

文件：`RimeContext.swift:68`（`t9UserInputKey`）和 `HamsterKit/Sources/Extensions/String+.swift:94-125`。
```swift
preview = preedit
if 有高亮候选 { preview = preview.t9pinyinToPinyin(comment: cand.comment) }
return preview.replaceT9pinyin
```
- `t9pinyinToPinyin(comment:)`：comment 按空格拆成音节表（rime-ice 设置了 `spelling_hints: 100` 和 `comment_format: []`，所以 comment 就是 `"zhong guo"` 这样的全拼）。每个音节先转成数字码。preedit 也按空格拆分，逐段处理：
  - 如果 preedit 段长度 ≥ 数字码长度：把该段里的数字码 `replacingOccurrences` 成拼音；
  - 否则（用户还没打完这个音节）：取拼音的 `prefix(段长)`。
  - 最后用 `'` 连接。
- `replaceT9pinyin`：把剩下的**单个数字**映射成该键的第一个字母（`t9ToPinyinMapping[d][0]`，例如 2→a），确保 preedit 里不再出现数字。
- 元书文档强调：**任何清空 comment 的 lua 都会让 preedit 退化成数字**。因此 T9Ime 精简方案时，不能移除 `spelling_hints`，也不能加入改写 comment 的 filter（rime-ice 的 `corrector` 在 t9 方案里没有挂载，不受影响）。
- rime-ice 另有 `lua/t9_preedit.lua`（给 iRime 用），它在 filter 里直接执行 `cand.preedit = cand.comment`。这个办法更粗糙，需要 librime-lua，我们不需要。

### 1.7 t9_processor（闭源）已知信息

- 来源：元书文档 `src/content/docs/guides/chinese-ninekey-configuration.mdx`：「`t9_processor` 会用来处理九键输入的字符，如回车键、删除键等」「必须添加在首个位置」。
- 更新日志 `logs.mdx`：「fix: rime t9 插件 t9_processor 部分情况下删除键无响应」「fix: T9 模式下回车键上屏的一些问题」。
- 推测职责（未经源码证实）：回车键上屏已转换成拼音的 preedit，而不是数字串；删除键配合拼音段撤销。**在 T9Ime 里，这些职责由前端承担**：回车时上屏我们自己重建的拼音串，或者上屏原始 input；退格先弹撤销栈。在 schema 里用 patch 移除即可：
  ```yaml
  # t9.custom.yaml —— 整表重写 processors，去掉 t9_processor
  patch:
    engine/processors:
      - ascii_composer
      - recognizer
      - key_binder
      - speller
      - punctuator
      - selector
      - navigator
      - express_editor
  ```

---

## 2. 万象拼音（rime-wanxiang）

仓库：https://github.com/amzxyz/rime-wanxiang （旧名 `rime_wanxiang` 会重定向）。许可证 **CC-BY-4.0**。创建于 2024-08，4.6k star，最近一次 push 是 2026-09-28，版本 v18.0.15，**几乎每天发布**。词库和模型仓库：https://github.com/amzxyz/RIME-LMDG （CC-BY-4.0）。

### 2.1 九键方案

- `wanxiang_t9.schema.yaml`（通用）和 `wanxiang_t9i.schema.yaml`（元书/仓专用）。两者唯一的实质差别是 t9i 在 processors 首位多了 `t9_processor  #元书T9处理器`，并且 preedit 默认使用无声调全拼。
- 编码方式（数字）：
  ```yaml
  - derive/^(.*)$/\U$1/          # 派生大写
  - ...模糊/容错 derive
  - xlit/ABCDEFGHIJKLMNOPQRSTUVWXYZ/22233344455566677778889999/
  ```
  最终拼写只有"全小写"和"全数字"两种，**没有音节内的混合拼写**（与 rime-ice 不同）。跨音节混合（`zhe43`）仍然可以匹配，因为切分是按音节进行的。prism 体积也因此比 rime-ice 的 8 连 derive 小（元书文档也推荐这种 `\U` + `xform` 写法来减小 prism）。
- 词库：`translator/dictionary: wanxiang_lite`（去掉声调的 lite 词库，打包时生成）。简码 `wanxiang_abbrev_t9`（约 3.15 万行），`custom_phrase` 挂 `wanxiang_phrase_t9`。
- 语法模型：`grammar/language: wanxiang-lts-zh-hans`，需要 **librime-octagram** 插件，以及单独下载的 `wanxiang-lts-zh-hans.gram`（**413,658,156 字节 ≈ 395 MiB**）。
- 用到的 translator 选项：`core_word_length`、`max_word_length`（**librime ≥ 1.14.0**）、`always_show_comments`、`max_homophones`、`encode_commit_history`、`contextual_suggestions`。以上选项 upstream 1.17 全部支持（已在 librime master 源码中 grep 确认）。
- lua 依赖（t9 方案里）：3 个 `lua_processor`（context_reorder、super_processor、super_tips）、1 个 lua key_binder、5 个 `lua_translator`、6 个 `lua_filter`（super_lookup、custom_phrase、context_reorder、**super_comment_preedit**、super_replacer、super_filter），合计约 3.8 MB lua。**preedit 转拼音（`convert_t9_syllable`）、简码前置、字符集过滤都在 lua 里实现**。去掉 lua 仍能出字，但功能会明显退化。`custom/wanxiang_pure.*` 是官方的"无 lua"版本，但**只提供 26 键**，没有 t9 版。

### 2.2 体积

| 项 | rime-ice（t9） | 万象（t9, lite） |
|---|---|---|
| 源词库 | `cn_dicts` 45 MB，约 193 万行 | `dicts/` 78 MB（jichu 45 MB/142 万行、shici 16 MB…），发布包 base.zip 35 MB / lite.zip 32 MB |
| 语法模型 | 无（可选接入第三方 gram） | **必需**才能发挥优势：395 MiB |
| lua | t9 方案只挂 date/calc/force_gc 3 个，可全部移除 | 核心体验依赖约 15 个 lua 组件 |

### 2.3 对比表（Windows / 官方 librime 1.17 视角）

| 维度 | rime-ice `t9` | 万象 `wanxiang_t9` |
|---|---|---|
| 编码 | 数字，8 连 derive（含音节内混合拼写） | 数字，`\U`+xlit（全小写或全数字） |
| 准确率/模型 | 词频 + librime 默认造句，无语法模型 | 词频 + octagram 语法模型 + lua 上下文调频，长句明显更好（官方宣称"类大厂"） |
| 必需插件 | 无（移除 lua_* 后纯 librime 可跑） | librime-lua + librime-octagram |
| 与 1.17 兼容 | 兼容（仅需移除 `t9_processor`） | 兼容（需 ≥1.14；移除 t9i 的 `t9_processor`，或直接用 `wanxiang_t9`） |
| 前端 preedit 重建 | comment=全拼，前端替换即可 | comment=带调码（`spelling_hints: 50`，再经 lua 处理）；没有 lua 时要前端自己去声调、去 `;辅码` |
| 安装体积 | 小（几十 MB 级 build） | 大（+395 MiB gram） |
| 许可证 | GPL-3.0（词库/方案，随包分发需遵守 GPL；数据文件单独分发也可以） | CC-BY-4.0（署名即可，商业友好） |
| 维护 | 活跃（19.5k star，2026-09-25 有 push） | 非常活跃（每日发版，配置变动频繁，patch 容易失效） |
| 风险 | 低 | 版本漂移快，lua 与 schema 强耦合；大模型加载占内存和部署时间 |

### 2.4 推荐

- **默认方案用 rime-ice `t9`**，符合 SPEC 的"精简、无 lua、预部署"原则：用 `t9.custom.yaml` 重写 processors（去掉 `t9_processor`），移除 3 个 lua_translator，保留 `spelling_hints`。
- **万象作为 M 之后的可选"智能包"**：把 `wanxiang_t9` 和 gram 放进独立安装组件，要求带 lua 和 octagram 的 rime.dll（本机 `rime-33e7814-Windows-msvc-x64` 构建已包含 lua、octagram、predict 插件）。拼音栏算法与 rime-ice 共用，因为两者都是数字码，`zhe43` 这样的跨音节混合 input 两边都能用。preedit 重建时需要额外处理带调 comment（去声调，按 `;` 截断）。
- 许可证角度：如果 T9Ime 打算闭源分发，万象的 CC-BY-4.0 比 rime-ice 的 GPL-3.0 更宽松。这一点需要在计划阶段单独评估。

---

## 3. 其他开源 T9 拼音栏实现

### 3.1 Koishi-Neko/rime-t9-shiyin（九宫拾音，GPL-3.0，2026-09 活跃）

https://github.com/Koishi-Neko/rime-t9-shiyin 。Trime 前端 fork：https://github.com/Koishi-Neko/trime （分支 `shiyin-sidebar`）。
- **纯 librime-lua 实现音节栏**（引擎侧）：`lua/t9_syllable_core.lua` 用约 424 个音节的前缀树做 DFS，枚举全部合法切分（`94343` 有 23 种）。`lua_translator` 把"首音节"作为 text 为空的候选产出，comment 写 `zhe'43`。`select_notifier` 扫描 composition，找到被点中的音节后改写 `context.input = head..syl.."'"..rest`。
- 对我们最有价值的是它的**无头测试底座** `tools/harness.py`、`harness_real.py`：在 Windows 上用 ctypes 直接调用小狼毫的 `rime.dll`，沙箱用户目录，deployer 和 engine 分进程运行。它在真实 rime-ice t9 上的实测结果：
  ```
  input='94343'   -> xi | yi | zi | xie | zhe | zhei ...
  input="zhe'43"  -> 这个 | 折合 | 这和 | 这
  input='zhe43'   -> 这个 | 折合 | 这和 | 这
  input="zhe'4"   -> 这个 | 折合 | 这和 | 这
  ```
  由此证明"数字段替换成小写拼音并加撇号"再 `set_input`，可以在官方 librime 上成立。**必须配置 `speller/delimiter: " '"`**（rime-ice 已继承）。
- 踩坑清单（`docs/syllable-prototype-report.md` §9）可以直接复用：`candidate_list_begin` 之后要先 `next` 再读候选；deployer 与 engine 不能在同一进程；1.13 起用户目录里的 `default.yaml` 会被移走，要改用 `default.custom.yaml`；`uniquifier` 会合并 text 为空的候选。

### 3.2 EyaaCai/fcitx5-android-t9（万象九键 fcitx5-android fork，LGPL-2.1，2026-09）

https://github.com/EyaaCai/fcitx5-android-t9 。目录 `app/src/main/java/org/fcitx/fcitx5/android/input/keyboard/t9/`。
- `T9BranchProvider.kt`：抽象出 `T9InputContext(sessionId, revision, rawInput, preedit, candidateComments, branchSegmentStart)` 和 `T9BranchState(segmentStart, segmentEnd, branches, exhaustive)`，**用 revision 丢弃过期的异步结果**（在按键或退格与音节栏刷新发生竞态时很有用）。
- `StaticSyllableBranchProvider.kt`（实际使用的 provider）：
  - 起点 `segmentStart = max(最后一个 '1' 分隔符之后, branchSegmentStart)`；
  - 对 `length = n downTo 1` 取数字前缀，同时查"合法音节表"和**声母表**（`zh/ch/sh/b/p…`，用于简拼），用 `putIfAbsent` 去重，保证**更长的匹配排在前面**；
  - `replacement = spelling + 剩余数字`，**长度守恒**（字母与数字一一对应，索引不漂移）。
- `PreeditBranchProvider.kt`：只把引擎 preedit 和 comment 作为只读信息展示，明确声明"不能枚举全部分支"。
- 点选 `KeyboardWindow.selectT9Branch()`：fcitx 没有 set_input，所以**用 N 次 BackSpace 加逐字母发键**完成替换。同时 `t9BranchBoundaries += segmentStart + consumedLength`，**用边界栈记录已确认段**，下一轮从新边界开始枚举；退格时 `trimT9BranchBoundaries()` 弹出越界的边界，实现撤销。
- 附带 `phase0a-report.md`、`Fcitx5-万象触屏九宫格统一签名实施方案.md` 设计文档，可作参考。

### 3.3 其他

- Rizumu85/fcitx5-android-t9-phone（LGPL-2.1，物理九键手机）：`input/t9/ChineseT9*`，面向实体键，用悬浮气泡显示"拼音筛选"，搭配 `rime-ice-t9-phone` 配置。
- Endif12/trime-t9：只是主题和配置层面的改动，没有拼音栏算法代码。
- 官方 Trime / fcitx5-android 上游：没有内置九键拼音栏。

---

## 4. 对 T9Ime 的落地建议（回应 SPEC §5）

1. **码制**：改为数字 `2-9`，`1` 作分隔键（key_binder 在 has_menu 时发送 `'`）。生成脚本从 rime-ice 词典（或 `rime_ice.dict.yaml` 的全部拼写）提取音节集合，输出 `syllable → digits` 表和音节频率（按词频累加）。
2. **拼音栏**：对"第一个未确认数字段"做长度降序的前缀枚举（参考 Hamster 的 Trie 截断和 EyaaCai 的声母补充），同长度按音节频率排序，单键再补充 3–4 个字母。已确认边界用栈保存（EyaaCai 做法），不要按"第一次出现"去匹配（Hamster 的缺陷）。
3. **点选**：`input = get_input(); input = input[0:segStart] + syl + "'" + input[segStart+consumed:]; set_input(input)`。带撇号更稳，也方便撤销时定位；如果需要保持索引守恒，可以参考 Hamster 不加撇号，因为 `zhe43` 也能命中。压栈 `(segStart, 原数字串, 替换串)`。
4. **撤销**：退格时如果撤销栈非空并且光标位于最后一个确认段末尾，就把该段恢复为数字，然后 `set_input`；否则发送普通 BackSpace。
5. **preedit**：照搬 Hamster `t9pinyinToPinyin`（高亮候选 comment 替换数字码，不足长度时取前缀，剩余单数字映射为首字母，已确认段直接显示）。**无需 lua**。
6. **schema 补丁**：去掉 `t9_processor` 和 3 个 lua_translator；保留 `translator/spelling_hints` 和 `comment_format: []`。
7. **测试**：借鉴 rime-t9-shiyin 的 ctypes/rime.dll 无头 harness，用 C++ 或 Python 写 M1–M4 回归（枚举、点选、混合接力、撤销）。
