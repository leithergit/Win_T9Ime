# Weasel upstream tracking

T9Ime's TSF text service (`src/tip/`) is derived from Weasel's `WeaselTSF`
(<https://github.com/rime/weasel>, GPL-3.0). The code was rewritten rather than
copied file by file (no boost, new IPC, per-instance key state), so upstream
changes have to be ported by hand.

- Fork point: `d73f6295e8252ed2f7b9c12bae32e9001b1afdaa` (2026-08-18,
  "fix(WeaselServer): avoid tray refresh blocking IPC pipe").
- Once per milestone, review upstream changes in the directories we derive from:

  ```
  git clone https://github.com/rime/weasel && cd weasel
  git log --oneline d73f629..master -- WeaselTSF
  ```

  Composition / edit-session compatibility fixes are the ones worth porting.

| T9Ime file | Weasel origin |
|---|---|
| src/tip/text_service.* | WeaselTSF/WeaselTSF.*, KeyEventSink.cpp, Composition.cpp, EditSession.cpp, ThreadMgrEventSink.cpp, TextEditSink.cpp, Compartment.cpp |
| src/tip/key_event.* | WeaselTSF/KeyEvent.cpp |
| src/tip/register.* | WeaselTSF/Register.cpp |
| src/tip/candidate_ui.* | WeaselTSF/CandidateList.cpp (UIElement part) |
| src/tip/lang_bar.* | WeaselTSF/LanguageBar.cpp |
| src/tip/display_attribute.* | WeaselTSF/DisplayAttribute*.cpp, EnumDisplayAttributeInfo.cpp |

Not taken: WeaselUI (candidate window, replaced by `src/tip/candidate_window`),
WeaselIPC / WeaselIPCServer (replaced by `src/common/protocol`, `pipe`,
`src/host/ipc`), WeaselServer, WeaselDeployer, WeaselSetup, the NSIS installer.
