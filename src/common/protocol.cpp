#include "protocol.h"

#include <cstring>

namespace t9ime::ipc {

namespace {

void Put16(std::vector<uint8_t>& b, uint16_t v) {
  b.push_back(static_cast<uint8_t>(v));
  b.push_back(static_cast<uint8_t>(v >> 8));
}

void Put32(std::vector<uint8_t>& b, uint32_t v) {
  for (int i = 0; i < 4; ++i) b.push_back(static_cast<uint8_t>(v >> (8 * i)));
}

uint16_t Get16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | p[1] << 8); }

uint32_t Get32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | static_cast<uint32_t>(p[1]) << 8 | static_cast<uint32_t>(p[2]) << 16 |
         static_cast<uint32_t>(p[3]) << 24;
}

std::wstring ToWide(const Field& f) {
  std::wstring s(f.size / 2, L'\0');
  for (size_t i = 0; i < s.size(); ++i) s[i] = static_cast<wchar_t>(Get16(f.data + 2 * i));
  return s;
}

}  // namespace

Writer::Writer(MsgType type, uint32_t seq) {
  buf_.reserve(256);
  Put32(buf_, kMagic);
  Put16(buf_, kVersion);
  Put16(buf_, static_cast<uint16_t>(type));
  Put32(buf_, seq);
  Put32(buf_, 0);  // length, patched in Finish()
}

void Writer::Field(uint16_t tag, const void* data, uint32_t size) {
  Put16(buf_, tag);
  Put32(buf_, size);
  const auto* p = static_cast<const uint8_t*>(data);
  buf_.insert(buf_.end(), p, p + size);
}

Writer& Writer::U32(uint16_t tag, uint32_t value) {
  uint8_t b[4];
  for (int i = 0; i < 4; ++i) b[i] = static_cast<uint8_t>(value >> (8 * i));
  Field(tag, b, 4);
  return *this;
}

Writer& Writer::Str(uint16_t tag, std::wstring_view value) {
  std::vector<uint8_t> b;
  b.reserve(value.size() * 2);
  for (wchar_t c : value) Put16(b, static_cast<uint16_t>(c));
  Field(tag, b.data(), static_cast<uint32_t>(b.size()));
  return *this;
}

const std::vector<uint8_t>& Writer::Finish() {
  const uint32_t len = static_cast<uint32_t>(buf_.size() - kHeaderSize);
  for (int i = 0; i < 4; ++i) buf_[12 + i] = static_cast<uint8_t>(len >> (8 * i));
  return buf_;
}

Reader::Reader(const uint8_t* data, size_t size) {
  if (!data || size < kHeaderSize || size > kMaxMessage) return;
  if (Get32(data) != kMagic || Get16(data + 4) != kVersion) return;
  type_ = static_cast<MsgType>(Get16(data + 6));
  seq_ = Get32(data + 8);
  const uint32_t len = Get32(data + 12);
  if (len != size - kHeaderSize) return;
  size_t pos = kHeaderSize;
  while (pos < size) {
    if (size - pos < 6) return;
    const uint16_t tag = Get16(data + pos);
    const uint32_t flen = Get32(data + pos + 2);
    pos += 6;
    if (flen > size - pos) return;
    fields_.push_back({tag, data + pos, flen});
    pos += flen;
  }
  ok_ = true;
}

std::optional<uint32_t> Reader::U32(uint16_t tag) const {
  for (const Field& f : fields_) {
    if (f.tag == tag && f.size == 4) return Get32(f.data);
  }
  return std::nullopt;
}

std::optional<std::wstring> Reader::Str(uint16_t tag) const {
  for (const Field& f : fields_) {
    if (f.tag == tag && f.size % 2 == 0) return ToWide(f);
  }
  return std::nullopt;
}

std::vector<std::wstring> Reader::Strs(uint16_t tag) const {
  std::vector<std::wstring> out;
  for (const Field& f : fields_) {
    if (f.tag == tag && f.size % 2 == 0) out.push_back(ToWide(f));
  }
  return out;
}

std::vector<uint32_t> Reader::U32s(uint16_t tag) const {
  std::vector<uint32_t> out;
  for (const Field& f : fields_) {
    if (f.tag == tag && f.size == 4) out.push_back(Get32(f.data));
  }
  return out;
}

std::vector<uint8_t> Result::Encode(uint32_t seq) const {
  Writer w(MsgType::kResult, seq);
  w.Bool(kTagEaten, eaten).Bool(kTagComposing, composing).Bool(kTagAsciiMode, ascii_mode);
  if (!commit.empty()) w.Str(kTagCommit, commit);
  if (!preedit.empty()) w.Str(kTagPreedit, preedit);
  w.I32(kTagCursor, cursor).I32(kTagHighlighted, highlighted).I32(kTagPageNo, page_no);
  w.Bool(kTagLastPage, last_page);
  for (size_t i = 0; i < candidates.size(); ++i) {
    w.Str(kTagCandidate, candidates[i]);
    w.Str(kTagComment, i < comments.size() ? comments[i] : std::wstring());
  }
  if (!select_labels.empty()) w.Str(kTagSelectLabels, select_labels);
  if (!schema.empty()) w.Str(kTagSchema, schema);
  return w.Finish();
}

std::optional<Result> Result::Decode(const Reader& r) {
  if (!r.ok() || r.type() != MsgType::kResult) return std::nullopt;
  Result res;
  res.eaten = r.BoolOr(kTagEaten, false);
  res.composing = r.BoolOr(kTagComposing, false);
  res.ascii_mode = r.BoolOr(kTagAsciiMode, false);
  res.commit = r.StrOr(kTagCommit);
  res.preedit = r.StrOr(kTagPreedit);
  res.cursor = static_cast<int>(r.U32Or(kTagCursor, static_cast<uint32_t>(-1)));
  res.highlighted = static_cast<int>(r.U32Or(kTagHighlighted, 0));
  res.page_no = static_cast<int>(r.U32Or(kTagPageNo, 0));
  res.last_page = r.BoolOr(kTagLastPage, true);
  res.candidates = r.Strs(kTagCandidate);
  res.comments = r.Strs(kTagComment);
  res.select_labels = r.StrOr(kTagSelectLabels);
  res.schema = r.StrOr(kTagSchema);
  return res;
}

}  // namespace t9ime::ipc
