#pragma once
// Touch panel layout: pure geometry in DIPs, no window or rendering dependency.
//
//  +--------------------------------------------------+
//  | handle / preedit strip                            |
//  | candidate bar ........................... [ v ]  |
//  +--------+----------------------------+----------+
//  | pinyin | 1 分词  | 2 ABC  | 3 DEF   |  ⌫       |
//  |  bar   | 4 GHI   | 5 JKL  | 6 MNO   |  清空     |
//  | (list) | 7 PQRS  | 8 TUV  | 9 WXYZ  |  换行     |
//  +--------+------+---------------+-----+----------+
//  |  符号  | 123  |    0 空格      |中/英 |  隐藏     |
//  +--------+------+---------------+-----+----------+
// (Docs/T9_2.png, after the iFlytek iOS nine-key layout)
//
// English (Docs/T9_ABC.jpg): a full QWERTY keyboard (letters go straight to the field; the small
// character on each key is typed by a long press), also used for passwords:
//  | q w e r t y u i o p |
//  |  a s d f g h j k l  |
//  | ⇧  z x c v b n m  ⌫ |
//  | 符号 123 , ␣ . 英/中 隐藏 换行 |

#include <functional>
#include <string>
#include <vector>

namespace t9ime::panel {

struct RectF {
  float x = 0, y = 0, w = 0, h = 0;
  bool Contains(float px, float py) const { return px >= x && px < x + w && py >= y && py < y + h; }
  float Right() const { return x + w; }
  float Bottom() const { return y + h; }
};

enum class Mode { kChinese, kEnglish, kNumber, kSymbol };

enum class Action {
  kNone,
  kKey,           // `text` is the key: '1'..'9' on nine-key layouts
  kText,          // commit `text` directly (numbers, symbols, quick punctuation)
  kBackspace,
  kClear,         // 重输
  kSpace,         // long press: `text` ("0")
  kEnter,
  kSymbols,
  kNumbers,
  kToggleLanguage,
  kBack,          // leave number / symbol layout
  kHide,
  kExpand,        // toggle expanded candidate grid
  kCandidate,     // `index` into the candidate list
  kPinyin,        // `index` into the pinyin bar
  kSymbolCategory,  // `index` into the symbol categories
  kHandle,        // drag the window
  kLetter,        // QWERTY key: types `label`; long press types `text`
  kShift,         // QWERTY: tap = next letter upper case, double tap = caps lock
};

// Scrollable regions; list items are clipped to their region.
enum class Region { kNone, kCandidateBar, kSideList, kGrid };

struct Element {
  Action action = Action::kNone;
  RectF rect;
  std::wstring label;
  std::wstring sublabel;
  std::string text;
  int index = -1;
  Region region = Region::kNone;
  bool function_key = false;  // drawn with the function key style
  bool selected = false;      // e.g. current symbol category, highlighted candidate
};

struct RegionRect {
  Region region;
  RectF rect;
  bool horizontal;
};

// Everything the layout depends on.
struct LayoutInput {
  float width = 400, height = 300;  // DIPs
  Mode mode = Mode::kChinese;
  bool composing = false;
  bool expanded = false;
  bool shift = false;      // QWERTY: upper case letters
  bool caps_lock = false;  // QWERTY: shift stays on
  std::wstring preedit;
  std::vector<std::wstring> candidates;  // whole list fetched so far
  int highlighted = 0;
  std::vector<std::wstring> side_items;  // pinyin bar, quick punctuation or categories
  int side_selected = -1;
  std::vector<std::wstring> symbols;     // symbol layout grid
  float candidate_scroll = 0, side_scroll = 0, grid_scroll = 0;
  // Text width in DIPs at the candidate font size.
  std::function<float(const std::wstring&)> measure;
};

struct Layout {
  std::vector<Element> elements;
  std::vector<RegionRect> regions;
  RectF handle;
  float candidate_content = 0, side_content = 0, grid_content = 0;  // scroll extents
};

struct Metrics {
  static constexpr float kHandle = 22;      // top strip
  static constexpr float kCandidateBar = 44;
  static constexpr float kSideRatio = 0.17f;
  static constexpr float kFunctionRatio = 0.17f;
  static constexpr float kExpandButton = 44;
  static constexpr float kCandidatePad = 14;
  static constexpr float kSideItem = 40;
  static constexpr float kGridCell = 48;
  static constexpr float kGap = 3;
  static constexpr float kResizeGrip = 36;  // right end of the handle strip
};

Layout BuildLayout(const LayoutInput& in);

// Topmost element at a point (list items only inside their region), or nullptr.
const Element* HitTest(const Layout& layout, float x, float y);
Region RegionAt(const Layout& layout, float x, float y);

}  // namespace t9ime::panel
