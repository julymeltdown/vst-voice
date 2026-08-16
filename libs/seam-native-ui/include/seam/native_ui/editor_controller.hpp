#pragma once

#include "seam/application/editor_session.hpp"
#include "seam/application/project_factory.hpp"
#include "seam/core/result.hpp"
#include "seam/native_ui/editor_scene.hpp"
#include "seam/ui/text_composition_model.hpp"

#include <functional>
#include <optional>
#include <string>

namespace seam::native_ui {

enum class PointerButton { NoButton, Left, Middle, Right };

enum class NativeKey {
  Unknown,
  Space,
  Enter,
  Escape,
  Delete,
  Backspace,
  Left,
  Right,
  Up,
  Down,
  Z,
  Y,
  C,
  S,
  R,
  Plus,
  Minus,
};

struct InputModifiers final {
  bool shift{false};
  bool control{false};
  bool alt{false};
  bool command{false};

  [[nodiscard]] bool primaryShortcut() const noexcept {
    return control || command;
  }
};

struct PointerEvent final {
  ui::Point position;
  PointerButton button{PointerButton::NoButton};
  InputModifiers modifiers;
  int clickCount{1};
};

struct KeyEvent final {
  NativeKey key{NativeKey::Unknown};
  InputModifiers modifiers;
  bool repeat{false};
};

struct TextInputRequest final {
  domain::LyricTokenId lyricId;
  ui::Rect logicalBounds;
  std::u32string currentText;
};

struct EditorHostCallbacks final {
  std::function<void()> requestRepaint;
  std::function<void(const TextInputRequest&)> beginTextInput;
  std::function<void()> endTextInput;
  std::function<void(bool)> setPlaying;
};

class NativeEditorController final {
public:
  NativeEditorController(application::EditorSession& session,
                         application::ProjectFactory& factory,
                         domain::RegionId regionId,
                         EditorHostCallbacks callbacks = {});

  [[nodiscard]] ui::PianoRollModel& pianoRoll() noexcept { return pianoRoll_; }
  [[nodiscard]] const ui::PianoRollModel& pianoRoll() const noexcept {
    return pianoRoll_;
  }
  [[nodiscard]] EditorSceneState sceneState() const;
  [[nodiscard]] bool playing() const noexcept { return playing_; }
  [[nodiscard]] bool textInputActive() const noexcept { return composition_.active(); }

  void resize(double logicalWidth, double logicalHeight) noexcept;
  [[nodiscard]] core::Result<void> pointerDown(const PointerEvent& event);
  [[nodiscard]] core::Result<void> pointerMove(const PointerEvent& event);
  [[nodiscard]] core::Result<void> pointerUp(const PointerEvent& event);
  [[nodiscard]] core::Result<void> keyDown(const KeyEvent& event);
  void scroll(double deltaX, double deltaY, ui::Point anchor,
              InputModifiers modifiers) noexcept;

  [[nodiscard]] core::Result<void> beginLyricEdit(domain::NoteId noteId);
  [[nodiscard]] core::Result<void> updateTextComposition(
      std::u32string text, ui::CompositionSelection selection);
  [[nodiscard]] core::Result<void> commitTextComposition(std::u32string text);
  void cancelTextComposition() noexcept;

  void setAudioState(bool online, std::string backend);
  void setDirty(bool dirty) noexcept;
  void setPlayheadPixel(double value) noexcept;
  void setCharacterMetadata(std::string name, std::string style) {
    characterName_ = std::move(name);
    characterStyle_ = std::move(style);
  }

private:
  enum class DragMode { None, MoveNotes, BoxSelect };

  [[nodiscard]] ui::Point modelPoint(ui::Point windowPoint) const noexcept;
  [[nodiscard]] std::optional<ui::Rect> noteWindowBounds(domain::NoteId noteId) const;
  void repaint() const;
  void finishTextInput() const;

  application::EditorSession& session_;
  application::ProjectFactory& factory_;
  domain::RegionId regionId_;
  ui::PianoRollModel pianoRoll_;
  ui::TextCompositionModel composition_;
  EditorSceneLayout layout_;
  EditorHostCallbacks callbacks_;
  DragMode dragMode_{DragMode::None};
  ui::Point dragStart_;
  ui::Point dragCurrent_;
  bool dragAdditive_{false};
  bool playing_{false};
  bool dirty_{false};
  bool audioOnline_{false};
  std::string audioBackend_{"OFFLINE"};
  double logicalWidth_{1440.0};
  double logicalHeight_{900.0};
  double playheadPixel_{0.0};
  std::string characterName_;
  std::string characterStyle_;
};

}  // namespace seam::native_ui
