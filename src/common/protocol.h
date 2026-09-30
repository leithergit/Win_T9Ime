#pragma once
// T9Ime IPC wire format, shared by the TIP (in every application) and T9Host.
//
//   header  : magic u32 'T9IM' | version u16 | type u16 | seq u32 | length u32
//   payload : fields, each  tag u16 | length u32 | bytes
//
// Integers are little endian. Strings are UTF-16 (the TIP side lives in
// Win32 text land; the host converts). Unknown tags are skipped, so fields can
// be added without breaking older peers. No external dependencies.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace t9ime::ipc {

constexpr uint32_t kMagic = 0x4D493954;  // "T9IM"
constexpr uint16_t kVersion = 1;
constexpr size_t kHeaderSize = 16;
constexpr size_t kMaxMessage = 1 << 20;

enum class MsgType : uint16_t {
  // TIP -> Host requests (each answered by kResult unless noted)
  kHello = 1,          // pid, tid, exe, flags
  kKey = 2,            // keycode (X11 keysym), mask, test
  kFocusIn = 3,        // -> kAck
  kFocusOut = 4,       // -> kAck
  kSelectCandidate = 5,  // index on the current page
  kChangePage = 6,     // backward
  kClearComposition = 7,
  kQueryState = 8,     // current state without input
  kSetAsciiMode = 9,   // value (IMM32 conversion mode / langbar)
  kDiagnostics = 30,   // -> kAck with kTagValue text (t9diag)
  // Control pipe (T9Ctl / t9ctl.exe). Every request is answered by kAck with
  // the panel state (kTagVisible, kTagMode, kTagX/Y/Width/Height), or kError.
  kCtlQuery = 50,
  kCtlShow = 51,             // optional kTagMode
  kCtlHide = 52,
  kCtlToggle = 53,
  kCtlSetMode = 54,          // kTagMode
  kCtlDock = 55,
  kCtlSetPosition = 56,      // kTagX, kTagY (screen pixels, top-left)
  kCtlActivate = 57,         // kTagHwnd (0: foreground window): switch its application to T9Ime
  kCtlDeactivate = 58,       // kTagHwnd (0: foreground window): switch it to another input method
  kCtlRegisterNotify = 59,   // kTagHwnd: post "T9Ime.Visibility" on show / hide / move
  kCtlUnregisterNotify = 60, // kTagHwnd
  // TIP -> Host on the events pipe, once after connecting (-> no answer)
  kEventHello = 20,    // client (id from the kHello ack)
  // Host -> TIP on the events pipe
  kPushCommit = 110,   // commit (text from the touch panel)
  // Host -> TIP
  kResult = 100,
  kAck = 101,
  kError = 102,
};

enum Tag : uint16_t {
  // request fields
  kTagPid = 1,
  kTagTid = 2,
  kTagExe = 3,
  kTagFlags = 4,
  kTagKeycode = 5,
  kTagMask = 6,
  kTagTest = 7,
  kTagIndex = 8,
  kTagBackward = 9,
  kTagValue = 10,
  kTagClient = 11,       // client id (kHello ack, kEventHello)
  kTagHwnd = 12,         // focus window (low 32 bits suffice for HWNDs)
  kTagInputScope = 13,   // repeated
  kTagTouch = 14,        // focus change caused by touch / pen
  kTagReadOnly = 15,
  kTagText = 16,         // free text (diagnostics)
  // result fields
  kTagEaten = 30,
  kTagCommit = 31,
  kTagPreedit = 32,
  kTagCursor = 33,       // UTF-16 index into the preedit
  kTagCandidate = 34,    // repeated: candidate text
  kTagComment = 35,      // repeated, parallel to kTagCandidate
  kTagHighlighted = 36,
  kTagPageNo = 37,
  kTagLastPage = 38,
  kTagComposing = 39,
  kTagAsciiMode = 40,
  kTagSelectLabels = 41,  // e.g. "1234567890"
  kTagSchema = 42,
  // control fields
  kTagMode = 50,     // 1 Chinese, 2 English, 3 numbers, 4 symbols (T9_MODE_*)
  kTagVisible = 51,
  kTagX = 52,
  kTagY = 53,
  kTagWidth = 54,
  kTagHeight = 55,
};

// Hello flags
constexpr uint32_t kFlagAppContainer = 1;
constexpr uint32_t kFlagElevated = 2;
// FocusIn flags
constexpr uint32_t kFocusNoContext = 4;  // focused field has no TSF document (IMM disabled): no pushes
constexpr uint32_t kFocusActivated = 8;  // reported while T9Ime is being activated in this thread

class Writer {
 public:
  explicit Writer(MsgType type, uint32_t seq = 0);
  MsgType type() const { return static_cast<MsgType>(buf_[6] | buf_[7] << 8); }
  Writer& U32(uint16_t tag, uint32_t value);
  Writer& I32(uint16_t tag, int32_t value) { return U32(tag, static_cast<uint32_t>(value)); }
  Writer& Bool(uint16_t tag, bool value) { return U32(tag, value ? 1 : 0); }
  Writer& Str(uint16_t tag, std::wstring_view value);
  // Finished message (header length filled in).
  const std::vector<uint8_t>& Finish();

 private:
  void Field(uint16_t tag, const void* data, uint32_t size);
  std::vector<uint8_t> buf_;
};

struct Field {
  uint16_t tag;
  const uint8_t* data;
  uint32_t size;
};

class Reader {
 public:
  // Validates the header and all field bounds; `ok()` is false on any error.
  Reader(const uint8_t* data, size_t size);
  bool ok() const { return ok_; }
  MsgType type() const { return type_; }
  uint32_t seq() const { return seq_; }

  std::optional<uint32_t> U32(uint16_t tag) const;
  uint32_t U32Or(uint16_t tag, uint32_t fallback) const { return U32(tag).value_or(fallback); }
  bool BoolOr(uint16_t tag, bool fallback) const { return U32(tag).value_or(fallback ? 1 : 0) != 0; }
  std::optional<std::wstring> Str(uint16_t tag) const;
  std::wstring StrOr(uint16_t tag) const { return Str(tag).value_or(std::wstring()); }
  // All string values of a repeated tag, in order.
  std::vector<std::wstring> Strs(uint16_t tag) const;
  std::vector<uint32_t> U32s(uint16_t tag) const;

 private:
  std::vector<Field> fields_;
  MsgType type_{};
  uint32_t seq_ = 0;
  bool ok_ = false;
};

// Decoded kResult, used by both sides.
struct Result {
  bool eaten = false;
  std::wstring commit;
  std::wstring preedit;
  int cursor = -1;
  std::vector<std::wstring> candidates;
  std::vector<std::wstring> comments;
  int highlighted = 0;
  int page_no = 0;
  bool last_page = true;
  bool composing = false;
  bool ascii_mode = false;
  std::wstring select_labels;
  std::wstring schema;

  std::vector<uint8_t> Encode(uint32_t seq) const;
  static std::optional<Result> Decode(const Reader& r);
};

}  // namespace t9ime::ipc
