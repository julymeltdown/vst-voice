#include "seam/native_ui/accessibility_tree.hpp"
#include "seam/native_ui/accessibility_win32.hpp"
#include "seam/native_ui/native_window.hpp"

#if defined(_WIN32)

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <uiautomationcore.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace seam::native_ui {
namespace {

struct SnapshotNode final {
  SemanticNode node;
  std::int32_t parent{-1};
  std::vector<std::int32_t> children;
  std::size_t noteOffset{0U};
  std::size_t noteLimit{0U};
};

struct Snapshot final {
  std::vector<SnapshotNode> nodes;
  std::shared_ptr<const AccessibilityTree> tree;
};

struct BridgeState;
class AccessibilityElement;
AccessibilityElement* elementFor(BridgeState* state,
                                 const std::shared_ptr<const Snapshot>& snapshot,
                                 std::int32_t index);

std::wstring wideFromUtf8(std::string_view text) {
  if (text.empty()) return {};
  const auto length = MultiByteToWideChar(
      CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
      static_cast<int>(text.size()), nullptr, 0);
  if (length <= 0) return {};
  std::wstring result(static_cast<std::size_t>(length), L'\0');
  const auto converted = MultiByteToWideChar(
      CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
      static_cast<int>(text.size()), result.data(), length);
  return converted == length ? result : std::wstring{};
}

std::optional<std::string> utf8FromWide(LPCWSTR text) {
  if (text == nullptr) return std::nullopt;
  constexpr std::size_t maximumCharacters = 4096U;
  std::size_t length = 0U;
  while (length < maximumCharacters && text[length] != L'\0') ++length;
  if (length == maximumCharacters && text[length] != L'\0') {
    return std::nullopt;
  }
  if (length == 0U) return std::string{};
  const auto required = WideCharToMultiByte(
      CP_UTF8, WC_ERR_INVALID_CHARS, text, static_cast<int>(length), nullptr,
      0, nullptr, nullptr);
  if (required <= 0) return std::nullopt;
  std::string result(static_cast<std::size_t>(required), '\0');
  if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text,
                          static_cast<int>(length), result.data(), required,
                          nullptr, nullptr) != required) {
    return std::nullopt;
  }
  return result;
}

int controlType(const SemanticNode& node) noexcept {
  switch (node.role) {
    case SemanticRole::Window: return UIA_WindowControlTypeId;
    case SemanticRole::Button: return UIA_ButtonControlTypeId;
    case SemanticRole::TextField: return UIA_EditControlTypeId;
    case SemanticRole::Note:
      return hasAction(node, SemanticAction::EditText)
                 ? UIA_EditControlTypeId
                 : UIA_CustomControlTypeId;
    case SemanticRole::Timeline: return UIA_CanvasControlTypeId;
    case SemanticRole::Toolbar: return UIA_ToolBarControlTypeId;
    case SemanticRole::Panel:
    case SemanticRole::Lane:
    case SemanticRole::Status: return UIA_GroupControlTypeId;
  }
  return UIA_CustomControlTypeId;
}

bool hasAction(const SemanticNode& node, SemanticAction action) noexcept {
  return std::find(node.actions.begin(), node.actions.end(), action) !=
         node.actions.end();
}

bool isSelectionItem(const SemanticNode& node) noexcept {
  if (!hasAction(node, SemanticAction::Activate)) return false;
  if (node.role == SemanticRole::Note) return true;
  constexpr std::array<std::string_view, 4> prefixes{
      "arrangement.track.", "arrangement.region.", "voicebank.card.",
      "audio.device."};
  return std::any_of(prefixes.begin(), prefixes.end(), [&node](const auto prefix) {
    return std::string_view{node.id}.starts_with(prefix);
  });
}

bool isSelectionContainer(const Snapshot& snapshot,
                          const SnapshotNode& node) noexcept {
  if (node.noteLimit != 0U) return true;
  return std::any_of(node.children.begin(), node.children.end(),
                     [&snapshot](const auto childIndex) {
    if (childIndex < 0 ||
        static_cast<std::size_t>(childIndex) >= snapshot.nodes.size()) {
      return false;
    }
    return isSelectionItem(
        snapshot.nodes[static_cast<std::size_t>(childIndex)].node);
  });
}

std::uint64_t semanticRuntimeId(std::string_view id) noexcept {
  constexpr std::uint64_t offset = 1469598103934665603ULL;
  constexpr std::uint64_t prime = 1099511628211ULL;
  auto hash = offset;
  for (const auto character : id) {
    hash ^= static_cast<std::uint64_t>(
        static_cast<unsigned char>(character));
    hash *= prime;
  }
  return hash;
}

class AccessibilityElement final : public IRawElementProviderFragmentRoot,
                                   public IInvokeProvider,
                                   public IToggleProvider,
                                   public ISelectionItemProvider,
                                   public ISelectionProvider,
                                   public IValueProvider {
public:
  AccessibilityElement(BridgeState* state,
                       std::shared_ptr<const Snapshot> snapshot,
                       std::int32_t index)
      : state_(state), snapshot_(std::move(snapshot)), index_(index) {}

  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,
                                            void** object) override;
  ULONG STDMETHODCALLTYPE AddRef() override;
  ULONG STDMETHODCALLTYPE Release() override;

  HRESULT STDMETHODCALLTYPE get_ProviderOptions(
      ProviderOptions* value) override;
  HRESULT STDMETHODCALLTYPE GetPatternProvider(PATTERNID patternId,
                                                IUnknown** provider) override;
  HRESULT STDMETHODCALLTYPE GetPropertyValue(PROPERTYID propertyId,
                                             VARIANT* value) override;
  HRESULT STDMETHODCALLTYPE get_HostRawElementProvider(
      IRawElementProviderSimple** provider) override;

  HRESULT STDMETHODCALLTYPE Navigate(
      NavigateDirection direction,
      IRawElementProviderFragment** provider) override;
  HRESULT STDMETHODCALLTYPE GetRuntimeId(SAFEARRAY** runtimeId) override;
  HRESULT STDMETHODCALLTYPE get_BoundingRectangle(UiaRect* rectangle) override;
  HRESULT STDMETHODCALLTYPE GetEmbeddedFragmentRoots(
      SAFEARRAY** roots) override;
  HRESULT STDMETHODCALLTYPE SetFocus() override;
  HRESULT STDMETHODCALLTYPE get_FragmentRoot(
      IRawElementProviderFragmentRoot** root) override;

  HRESULT STDMETHODCALLTYPE ElementProviderFromPoint(
      double x, double y, IRawElementProviderFragment** provider) override;
  HRESULT STDMETHODCALLTYPE GetFocus(
      IRawElementProviderFragment** provider) override;
  HRESULT STDMETHODCALLTYPE Invoke() override;
  HRESULT STDMETHODCALLTYPE Toggle() override;
  HRESULT STDMETHODCALLTYPE get_ToggleState(ToggleState* state) override;
  HRESULT STDMETHODCALLTYPE Select() override;
  HRESULT STDMETHODCALLTYPE AddToSelection() override;
  HRESULT STDMETHODCALLTYPE RemoveFromSelection() override;
  HRESULT STDMETHODCALLTYPE get_IsSelected(BOOL* selected) override;
  HRESULT STDMETHODCALLTYPE get_SelectionContainer(
      IRawElementProviderSimple** container) override;
  HRESULT STDMETHODCALLTYPE GetSelection(SAFEARRAY** selection) override;
  HRESULT STDMETHODCALLTYPE get_CanSelectMultiple(BOOL* canSelectMultiple) override;
  HRESULT STDMETHODCALLTYPE get_IsSelectionRequired(BOOL* required) override;
  HRESULT STDMETHODCALLTYPE SetValue(LPCWSTR value) override;
  HRESULT STDMETHODCALLTYPE get_IsReadOnly(BOOL* readOnly) override;
  HRESULT STDMETHODCALLTYPE get_Value(BSTR* value) override;

  ~AccessibilityElement() override = default;

  [[nodiscard]] const SnapshotNode* snapshotNode() const noexcept {
    if (snapshot_ == nullptr || index_ < 0 ||
        static_cast<std::size_t>(index_) >= snapshot_->nodes.size()) {
      return nullptr;
    }
    return &snapshot_->nodes[static_cast<std::size_t>(index_)];
  }

private:
  std::atomic<ULONG> references_{1U};
  BridgeState* state_{nullptr};
  std::shared_ptr<const Snapshot> snapshot_;
  std::int32_t index_{-1};
};

struct BridgeState final {
  HWND window{nullptr};
  INativeWindowClient* client{nullptr};
  mutable std::mutex mutex;
  std::shared_ptr<const Snapshot> snapshot;
  std::shared_ptr<const Snapshot> announcedSnapshot;
  std::string announcedFocusId;

  struct ClientGeometry final {
    POINT origin{0, 0};
    double scaleX{1.0};
    double scaleY{1.0};
    ui::Rect logicalBounds{};
  };

  void flatten(const SemanticNode& node, std::int32_t parent,
               Snapshot& output) {
    const auto index = static_cast<std::int32_t>(output.nodes.size());
    output.nodes.push_back(SnapshotNode{
        .node = node,
        .parent = parent,
        .children = {},
        .noteOffset = 0U,
        .noteLimit = 0U,
    });
    if (parent >= 0) {
      output.nodes[static_cast<std::size_t>(parent)].children.push_back(index);
    }
    for (const auto& child : node.children) flatten(child, index, output);
  }

  void refresh() {
    auto next = std::make_shared<Snapshot>();
    if (client != nullptr) {
      const auto* tree = client->accessibilityTree();
      if (tree != nullptr) {
        next->tree = std::make_shared<AccessibilityTree>(*tree);
        flatten(tree->root(), -1, *next);
        std::size_t visibleNotes = 0U;
        for (const auto& node : tree->root().children) {
          if (node.role == SemanticRole::Note) ++visibleNotes;
        }
        constexpr auto pageSize = std::size_t{512U};
        for (std::size_t offset = visibleNotes;
             offset < tree->virtualizedNoteCount(); offset += pageSize) {
          const auto limit = std::min(pageSize,
                                      tree->virtualizedNoteCount() - offset);
          SemanticNode page{
              .id = "timeline.notes.page." + std::to_string(offset),
              .role = SemanticRole::Panel,
              .name = "Notes " + std::to_string(offset + 1U) + "-" +
                      std::to_string(offset + limit),
              .value = std::to_string(limit) + " notes",
              .bounds = tree->root().bounds,
              .enabled = true,
              .focused = false,
              .actions = {},
              .children = {},
              .virtualizedChildCount = limit,
          };
          const auto index = static_cast<std::int32_t>(next->nodes.size());
          next->nodes.push_back(SnapshotNode{
              .node = std::move(page),
              .parent = 0,
              .children = {},
              .noteOffset = offset,
              .noteLimit = limit,
          });
          next->nodes.front().children.push_back(index);
        }
      }
    }
    std::lock_guard lock(mutex);
    snapshot = std::move(next);
  }

  [[nodiscard]] std::shared_ptr<const Snapshot> current() const {
    std::lock_guard lock(mutex);
    return snapshot;
  }

  void primeAnnouncements(const std::shared_ptr<const Snapshot>& value) {
    if (value == nullptr) return;
    std::lock_guard lock(mutex);
    if (announcedSnapshot == nullptr) announcedSnapshot = value;
  }

  void announcePropertyChanges(
      const std::shared_ptr<const Snapshot>& value) {
    if (value == nullptr) return;
    std::shared_ptr<const Snapshot> previous;
    {
      std::lock_guard lock(mutex);
      previous = announcedSnapshot;
      announcedSnapshot = value;
    }
    if (previous == nullptr) return;

    const auto findById = [](const Snapshot& snapshot, std::string_view id) {
      return std::find_if(snapshot.nodes.begin(), snapshot.nodes.end(),
                          [id](const auto& candidate) {
        return candidate.node.id == id;
      });
    };
    const auto stringVariant = [](std::string_view text) {
      VARIANT result{};
      VariantInit(&result);
      result.vt = VT_BSTR;
      const auto wide = wideFromUtf8(text);
      result.bstrVal = SysAllocString(wide.c_str());
      return result;
    };
    const auto boolVariant = [](bool value) {
      VARIANT result{};
      VariantInit(&result);
      result.vt = VT_BOOL;
      result.boolVal = value ? VARIANT_TRUE : VARIANT_FALSE;
      return result;
    };
    const auto toggleVariant = [](const SemanticNode& node) {
      VARIANT result{};
      VariantInit(&result);
      result.vt = VT_I4;
      const auto value = std::string_view{node.value};
      result.lVal = value == "On" || value == "Playing" || value == "Enabled"
                        ? ToggleState_On
                        : ToggleState_Off;
      return result;
    };
    for (std::int32_t index = 0;
         index < static_cast<std::int32_t>(value->nodes.size()); ++index) {
      const auto& current = value->nodes[static_cast<std::size_t>(index)].node;
      const auto old = findById(*previous, current.id);
      if (old == previous->nodes.end()) continue;
      const auto& prior = old->node;
      auto* element = elementFor(this, value, index);
      if (element == nullptr) continue;
      auto* provider = static_cast<IRawElementProviderSimple*>(element);
      if (isSelectionItem(current) && current.selected != prior.selected) {
        auto oldValue = boolVariant(prior.selected);
        auto newValue = boolVariant(current.selected);
        static_cast<void>(UiaRaiseAutomationPropertyChangedEvent(
            provider, UIA_SelectionItemIsSelectedPropertyId, oldValue,
            newValue));
        VariantClear(&oldValue);
        VariantClear(&newValue);
      }
      const auto currentText = hasAction(current, SemanticAction::EditText)
                                   ? std::string_view{current.editableValue}
                                   : std::string_view{current.value};
      const auto priorText = hasAction(prior, SemanticAction::EditText)
                                 ? std::string_view{prior.editableValue}
                                 : std::string_view{prior.value};
      if (currentText != priorText) {
        auto oldValue = stringVariant(priorText);
        auto newValue = stringVariant(currentText);
        static_cast<void>(UiaRaiseAutomationPropertyChangedEvent(
            provider, UIA_ValueValuePropertyId, oldValue, newValue));
        VariantClear(&oldValue);
        VariantClear(&newValue);
      }
      if (hasAction(current, SemanticAction::Toggle) &&
          current.value != prior.value) {
        auto oldValue = toggleVariant(prior);
        auto newValue = toggleVariant(current);
        static_cast<void>(UiaRaiseAutomationPropertyChangedEvent(
            provider, UIA_ToggleToggleStatePropertyId, oldValue, newValue));
        VariantClear(&oldValue);
        VariantClear(&newValue);
      }
      if (current.enabled != prior.enabled) {
        auto oldValue = boolVariant(prior.enabled);
        auto newValue = boolVariant(current.enabled);
        static_cast<void>(UiaRaiseAutomationPropertyChangedEvent(
            provider, UIA_IsEnabledPropertyId, oldValue, newValue));
        VariantClear(&oldValue);
        VariantClear(&newValue);
      }
      element->Release();
    }
  }

  [[nodiscard]] core::Result<void> dispatch(std::string_view id,
                                             SemanticAction action) const {
    if (client == nullptr) {
      return core::failure(core::ErrorCode::InvalidState,
                           "Win32 accessibility client is unavailable");
    }
    return client->dispatchAccessibility(id, action);
  }

  [[nodiscard]] core::Result<void> setValue(std::string_view id,
                                             std::string_view value) const {
    if (client == nullptr) {
      return core::failure(core::ErrorCode::InvalidState,
                           "Win32 accessibility client is unavailable");
    }
    return client->setAccessibilityValue(id, value);
  }

  void announceFocusedElement(
      const std::shared_ptr<const Snapshot>& value) {
    if (value == nullptr || value->tree == nullptr) return;
    const auto* focused = value->tree->focusedNode();
    if (focused == nullptr) {
      std::lock_guard lock(mutex);
      announcedFocusId.clear();
      return;
    }
    {
      std::lock_guard lock(mutex);
      if (focused->id == announcedFocusId) return;
    }

    auto active = value;
    std::int32_t focusedIndex = -1;
    for (std::int32_t index = 0;
         index < static_cast<std::int32_t>(active->nodes.size()); ++index) {
      if (active->nodes[static_cast<std::size_t>(index)].node.id == focused->id) {
        focusedIndex = index;
        break;
      }
    }
    if (focusedIndex < 0) {
      for (std::int32_t index = 0;
           index < static_cast<std::int32_t>(value->nodes.size()); ++index) {
        const auto& page = value->nodes[static_cast<std::size_t>(index)];
        if (page.noteLimit == 0U) continue;
        const auto expanded = expandPage(value, index);
        if (expanded == nullptr) continue;
        for (const auto childIndex :
             expanded->nodes[static_cast<std::size_t>(index)].children) {
          if (childIndex >= 0 &&
              static_cast<std::size_t>(childIndex) < expanded->nodes.size() &&
              expanded->nodes[static_cast<std::size_t>(childIndex)].node.id ==
                  focused->id) {
            active = expanded;
            focusedIndex = childIndex;
            break;
          }
        }
        if (focusedIndex >= 0) break;
      }
    }
    if (focusedIndex < 0) return;
    auto* element = elementFor(this, active, focusedIndex);
    if (element == nullptr) return;
    {
      std::lock_guard lock(mutex);
      if (focused->id == announcedFocusId) {
        element->Release();
        return;
      }
      announcedFocusId = focused->id;
    }
    static_cast<void>(UiaRaiseAutomationEvent(
        static_cast<IRawElementProviderSimple*>(element),
        UIA_AutomationFocusChangedEventId));
    element->Release();
  }

  [[nodiscard]] ClientGeometry geometry(
      const std::shared_ptr<const Snapshot>& value) const noexcept {
    ClientGeometry result{};
    if (value == nullptr || value->nodes.empty()) return result;
    result.logicalBounds = value->nodes.front().node.bounds;
    RECT clientBounds{};
    POINT origin{0, 0};
    if (window != nullptr && GetClientRect(window, &clientBounds) != FALSE &&
        ClientToScreen(window, &origin) != FALSE) {
      result.origin = origin;
      const auto physicalWidth = static_cast<double>(
          std::max<LONG>(0, clientBounds.right - clientBounds.left));
      const auto physicalHeight = static_cast<double>(
          std::max<LONG>(0, clientBounds.bottom - clientBounds.top));
      if (result.logicalBounds.width > 0.0 && physicalWidth > 0.0) {
        result.scaleX = physicalWidth / result.logicalBounds.width;
      }
      if (result.logicalBounds.height > 0.0 && physicalHeight > 0.0) {
        result.scaleY = physicalHeight / result.logicalBounds.height;
      }
    }
    return result;
  }

  [[nodiscard]] UiaRect screenRectangle(
      const std::shared_ptr<const Snapshot>& value,
      const ui::Rect& bounds) const noexcept {
    const auto client = geometry(value);
    return UiaRect{
        .left = static_cast<double>(client.origin.x) +
                (bounds.x - client.logicalBounds.x) * client.scaleX,
        .top = static_cast<double>(client.origin.y) +
               (bounds.y - client.logicalBounds.y) * client.scaleY,
        .width = std::max(0.0, bounds.width * client.scaleX),
        .height = std::max(0.0, bounds.height * client.scaleY),
    };
  }

  [[nodiscard]] ui::Point logicalPoint(
      const std::shared_ptr<const Snapshot>& value, double x,
      double y) const noexcept {
    const auto client = geometry(value);
    return ui::Point{
        .x = client.logicalBounds.x +
             (x - static_cast<double>(client.origin.x)) / client.scaleX,
        .y = client.logicalBounds.y +
             (y - static_cast<double>(client.origin.y)) / client.scaleY,
    };
  }

  [[nodiscard]] std::shared_ptr<const Snapshot> expandPage(
      const std::shared_ptr<const Snapshot>& current,
      std::int32_t pageIndex) const {
    if (current == nullptr || current->tree == nullptr || pageIndex < 0 ||
        static_cast<std::size_t>(pageIndex) >= current->nodes.size()) {
      return {};
    }
    const auto& page = current->nodes[static_cast<std::size_t>(pageIndex)];
    if (page.noteLimit == 0U) return current;
    auto expanded = std::make_shared<Snapshot>(*current);
    if (!expanded->nodes[static_cast<std::size_t>(pageIndex)].children.empty()) {
      return expanded;
    }
    const auto notes = current->tree->materializeNotes(page.noteOffset,
                                                       page.noteLimit);
    for (const auto& note : notes) {
      const auto index = static_cast<std::int32_t>(expanded->nodes.size());
      expanded->nodes.push_back(SnapshotNode{
          .node = note,
          .parent = pageIndex,
          .children = {},
          .noteOffset = 0U,
          .noteLimit = 0U,
      });
      expanded->nodes[static_cast<std::size_t>(pageIndex)].children.push_back(
          index);
    }
    return expanded;
  }
};

AccessibilityElement* elementFor(BridgeState* state,
                                 const std::shared_ptr<const Snapshot>& snapshot,
                                 std::int32_t index) {
  if (state == nullptr || snapshot == nullptr || index < 0 ||
      static_cast<std::size_t>(index) >= snapshot->nodes.size()) {
    return nullptr;
  }
  return new AccessibilityElement(state, snapshot, index);
}

HRESULT STDMETHODCALLTYPE AccessibilityElement::QueryInterface(REFIID iid,
                                                                void** object) {
  if (object == nullptr) return E_INVALIDARG;
  *object = nullptr;
  if (iid == IID_IUnknown || iid == __uuidof(IRawElementProviderSimple) ||
      iid == __uuidof(IRawElementProviderFragment)) {
    *object = static_cast<IRawElementProviderFragmentRoot*>(this);
  } else if (iid == __uuidof(IRawElementProviderFragmentRoot) && index_ == 0) {
    *object = static_cast<IRawElementProviderFragmentRoot*>(this);
  } else if (iid == __uuidof(IInvokeProvider) && snapshotNode() != nullptr &&
             hasAction(snapshotNode()->node, SemanticAction::Activate)) {
    *object = static_cast<IInvokeProvider*>(this);
  } else if (iid == __uuidof(IToggleProvider) && snapshotNode() != nullptr &&
             hasAction(snapshotNode()->node, SemanticAction::Toggle)) {
    *object = static_cast<IToggleProvider*>(this);
  } else if (iid == __uuidof(ISelectionItemProvider) &&
             snapshotNode() != nullptr && isSelectionItem(snapshotNode()->node)) {
    *object = static_cast<ISelectionItemProvider*>(this);
  } else if (iid == __uuidof(ISelectionProvider) && snapshot_ != nullptr &&
             snapshotNode() != nullptr &&
             isSelectionContainer(*snapshot_, *snapshotNode())) {
    *object = static_cast<ISelectionProvider*>(this);
  } else if (iid == __uuidof(IValueProvider) && snapshotNode() != nullptr &&
             hasAction(snapshotNode()->node, SemanticAction::EditText)) {
    *object = static_cast<IValueProvider*>(this);
  } else {
    return E_NOINTERFACE;
  }
  AddRef();
  return S_OK;
}

ULONG STDMETHODCALLTYPE AccessibilityElement::AddRef() {
  return ++references_;
}

ULONG STDMETHODCALLTYPE AccessibilityElement::Release() {
  const auto remaining = --references_;
  if (remaining == 0U) delete this;
  return remaining;
}

HRESULT STDMETHODCALLTYPE AccessibilityElement::get_ProviderOptions(
    ProviderOptions* value) {
  if (value == nullptr) return E_INVALIDARG;
  *value = ProviderOptions_ServerSideProvider;
  return S_OK;
}

HRESULT STDMETHODCALLTYPE AccessibilityElement::GetPatternProvider(
    PATTERNID patternId, IUnknown** provider) {
  if (provider == nullptr) return E_INVALIDARG;
  *provider = nullptr;
  const auto* current = snapshotNode();
  if (patternId == UIA_InvokePatternId && current != nullptr &&
      hasAction(current->node, SemanticAction::Activate)) {
    *provider = static_cast<IInvokeProvider*>(this);
    AddRef();
  } else if (patternId == UIA_TogglePatternId && current != nullptr &&
             hasAction(current->node, SemanticAction::Toggle)) {
    *provider = static_cast<IToggleProvider*>(this);
    AddRef();
  } else if (patternId == UIA_SelectionItemPatternId && current != nullptr &&
             isSelectionItem(current->node)) {
    *provider = static_cast<ISelectionItemProvider*>(this);
    AddRef();
  } else if (patternId == UIA_SelectionPatternId && current != nullptr &&
             snapshot_ != nullptr &&
             isSelectionContainer(*snapshot_, *current)) {
    *provider = static_cast<ISelectionProvider*>(this);
    AddRef();
  } else if (patternId == UIA_ValuePatternId && current != nullptr &&
             hasAction(current->node, SemanticAction::EditText)) {
    *provider = static_cast<IValueProvider*>(this);
    AddRef();
  }
  return S_OK;
}

HRESULT STDMETHODCALLTYPE AccessibilityElement::GetPropertyValue(
    PROPERTYID propertyId, VARIANT* value) {
  if (value == nullptr) return E_INVALIDARG;
  VariantInit(value);
  const auto* current = snapshotNode();
  if (current == nullptr) return S_OK;
  const auto setString = [value](std::string_view text) {
    const auto wide = wideFromUtf8(text);
    value->vt = VT_BSTR;
    value->bstrVal = SysAllocString(wide.c_str());
    return value->bstrVal == nullptr ? E_OUTOFMEMORY : S_OK;
  };
  if (propertyId == UIA_ControlTypePropertyId) {
    value->vt = VT_I4;
    value->lVal = controlType(current->node);
  } else if (propertyId == UIA_NamePropertyId) {
    return setString(current->node.name);
  } else if (propertyId == UIA_AutomationIdPropertyId) {
    return setString(current->node.id);
  } else if (propertyId == UIA_ValueValuePropertyId) {
    return setString(hasAction(current->node, SemanticAction::EditText)
                         ? current->node.editableValue
                         : current->node.value);
  } else if (propertyId == UIA_DescriptionPropertyId) {
    return setString(current->node.description);
  } else if (propertyId == UIA_ClassNamePropertyId) {
    return setString("ProjectSEAM");
  } else if (propertyId == UIA_AriaRolePropertyId) {
    return setString(semanticRoleName(current->node.role));
  } else if (propertyId == UIA_IsEnabledPropertyId) {
    value->vt = VT_BOOL;
    value->boolVal = current->node.enabled ? VARIANT_TRUE : VARIANT_FALSE;
  } else if (propertyId == UIA_HasKeyboardFocusPropertyId) {
    value->vt = VT_BOOL;
    value->boolVal = current->node.focused ? VARIANT_TRUE : VARIANT_FALSE;
  } else if (propertyId == UIA_SelectionItemIsSelectedPropertyId) {
    value->vt = VT_BOOL;
    value->boolVal = current->node.selected ? VARIANT_TRUE : VARIANT_FALSE;
  } else if (propertyId == UIA_IsControlElementPropertyId ||
             propertyId == UIA_IsContentElementPropertyId) {
    value->vt = VT_BOOL;
    value->boolVal = VARIANT_TRUE;
  }
  return S_OK;
}

HRESULT STDMETHODCALLTYPE AccessibilityElement::get_HostRawElementProvider(
    IRawElementProviderSimple** provider) {
  if (provider == nullptr) return E_INVALIDARG;
  *provider = nullptr;
  return state_ == nullptr || state_->window == nullptr
             ? S_OK
             : UiaHostProviderFromHwnd(state_->window, provider);
}

HRESULT STDMETHODCALLTYPE AccessibilityElement::Navigate(
    NavigateDirection direction, IRawElementProviderFragment** provider) {
  if (provider == nullptr) return E_INVALIDARG;
  *provider = nullptr;
  const auto* current = snapshotNode();
  if (current == nullptr) return E_FAIL;
  auto activeSnapshot = snapshot_;
  if (current->noteLimit != 0U && current->children.empty() &&
      state_ != nullptr) {
    activeSnapshot = state_->expandPage(snapshot_, index_);
  }
  const auto* active = activeSnapshot == nullptr || index_ < 0 ||
                               static_cast<std::size_t>(index_) >=
                                   activeSnapshot->nodes.size()
                           ? nullptr
                           : &activeSnapshot->nodes[static_cast<std::size_t>(index_)];
  if (active == nullptr) return E_FAIL;
  std::int32_t target = -1;
  if (direction == NavigateDirection_Parent) {
    target = active->parent;
  } else if (direction == NavigateDirection_FirstChild &&
             !active->children.empty()) {
    target = active->children.front();
  } else if (direction == NavigateDirection_LastChild &&
             !active->children.empty()) {
    target = active->children.back();
  } else if (direction == NavigateDirection_NextSibling ||
             direction == NavigateDirection_PreviousSibling) {
    if (active->parent >= 0) {
      const auto& siblings = activeSnapshot->nodes[static_cast<std::size_t>(
          active->parent)].children;
      const auto found = std::find(siblings.begin(), siblings.end(), index_);
      if (found != siblings.end()) {
        const auto offset = direction == NavigateDirection_NextSibling ? 1 : -1;
        const auto position = std::distance(siblings.begin(), found) + offset;
        if (position >= 0 &&
            position < static_cast<std::ptrdiff_t>(siblings.size())) {
          target = siblings[static_cast<std::size_t>(position)];
        }
      }
    }
  }
  auto* next = elementFor(state_, activeSnapshot, target);
  if (next == nullptr) return S_OK;
  *provider = static_cast<IRawElementProviderFragment*>(next);
  return S_OK;
}

HRESULT STDMETHODCALLTYPE AccessibilityElement::GetRuntimeId(
    SAFEARRAY** runtimeId) {
  if (runtimeId == nullptr) return E_INVALIDARG;
  const auto* current = snapshotNode();
  if (current == nullptr) return UIA_E_ELEMENTNOTAVAILABLE;
  const auto identity = semanticRuntimeId(current->node.id);
  *runtimeId = SafeArrayCreateVector(VT_I4, 0, 3);
  if (*runtimeId == nullptr) return E_OUTOFMEMORY;
  LONG prefix = UiaAppendRuntimeId;
  LONG high = static_cast<LONG>((identity >> 32U) & 0x7fffffffULL);
  LONG low = static_cast<LONG>(identity & 0x7fffffffULL);
  LONG prefixIndex = 0;
  LONG highIndex = 1;
  LONG lowIndex = 2;
  SafeArrayPutElement(*runtimeId, &prefixIndex, &prefix);
  SafeArrayPutElement(*runtimeId, &highIndex, &high);
  SafeArrayPutElement(*runtimeId, &lowIndex, &low);
  return S_OK;
}

HRESULT STDMETHODCALLTYPE AccessibilityElement::get_BoundingRectangle(
    UiaRect* rectangle) {
  if (rectangle == nullptr) return E_INVALIDARG;
  const auto* current = snapshotNode();
  if (current == nullptr) return E_FAIL;
  if (state_ == nullptr) return E_FAIL;
  *rectangle = state_->screenRectangle(snapshot_, current->node.bounds);
  return S_OK;
}

HRESULT STDMETHODCALLTYPE AccessibilityElement::GetEmbeddedFragmentRoots(
    SAFEARRAY** roots) {
  if (roots == nullptr) return E_INVALIDARG;
  *roots = SafeArrayCreateVector(VT_UNKNOWN, 0, 0);
  return *roots == nullptr ? E_OUTOFMEMORY : S_OK;
}

HRESULT STDMETHODCALLTYPE AccessibilityElement::SetFocus() {
  const auto* current = snapshotNode();
  if (current == nullptr) return E_FAIL;
  if (!hasAction(current->node, SemanticAction::SetFocus)) {
    return UIA_E_ELEMENTNOTAVAILABLE;
  }
  if (state_ == nullptr || state_->window == nullptr) return E_FAIL;
  SetFocus(state_->window);
  const auto result = state_->dispatch(current->node.id, SemanticAction::SetFocus);
  if (!result) return E_FAIL;
  state_->refresh();
  state_->announceFocusedElement(state_->current());
  return S_OK;
}

HRESULT STDMETHODCALLTYPE AccessibilityElement::get_FragmentRoot(
    IRawElementProviderFragmentRoot** root) {
  if (root == nullptr) return E_INVALIDARG;
  *root = nullptr;
  auto* value = elementFor(state_, snapshot_, 0);
  if (value == nullptr) return E_FAIL;
  *root = static_cast<IRawElementProviderFragmentRoot*>(value);
  return S_OK;
}

HRESULT STDMETHODCALLTYPE AccessibilityElement::ElementProviderFromPoint(
    double x, double y, IRawElementProviderFragment** provider) {
  if (provider == nullptr) return E_INVALIDARG;
  *provider = nullptr;
  if (index_ != 0 || snapshot_ == nullptr) return E_NOTIMPL;
  const auto logical = state_ == nullptr
                           ? ui::Point{x, y}
                           : state_->logicalPoint(snapshot_, x, y);
  for (std::int32_t index = static_cast<std::int32_t>(snapshot_->nodes.size()) - 1;
       index >= 0; --index) {
    const auto& snapshotNode = snapshot_->nodes[static_cast<std::size_t>(index)];
    if (snapshotNode.noteLimit != 0U) continue;
    const auto& node = snapshotNode.node;
    if (node.bounds.contains(logical)) {
      auto* value = elementFor(state_, snapshot_, index);
      if (value == nullptr) return E_OUTOFMEMORY;
      *provider = static_cast<IRawElementProviderFragment*>(value);
      return S_OK;
    }
  }
  return S_OK;
}

HRESULT STDMETHODCALLTYPE AccessibilityElement::GetFocus(
    IRawElementProviderFragment** provider) {
  if (provider == nullptr) return E_INVALIDARG;
  *provider = nullptr;
  if (index_ != 0 || snapshot_ == nullptr) return E_NOTIMPL;
  for (std::int32_t index = 0;
       index < static_cast<std::int32_t>(snapshot_->nodes.size()); ++index) {
    const auto& node = snapshot_->nodes[static_cast<std::size_t>(index)];
    if (node.node.focused) {
      auto* value = elementFor(state_, snapshot_, index);
      if (value == nullptr) return E_OUTOFMEMORY;
      *provider = static_cast<IRawElementProviderFragment*>(value);
      return S_OK;
    }
    if (node.noteLimit == 0U || state_ == nullptr || snapshot_->tree == nullptr) {
      continue;
    }
    const auto expanded = state_->expandPage(snapshot_, index);
    if (expanded == nullptr) continue;
    const auto& page = expanded->nodes[static_cast<std::size_t>(index)];
    for (const auto childIndex : page.children) {
      if (childIndex < 0 ||
          static_cast<std::size_t>(childIndex) >= expanded->nodes.size()) {
        continue;
      }
      if (!expanded->nodes[static_cast<std::size_t>(childIndex)].node.focused) {
        continue;
      }
      auto* value = elementFor(state_, expanded, childIndex);
      if (value == nullptr) return E_OUTOFMEMORY;
      *provider = static_cast<IRawElementProviderFragment*>(value);
      return S_OK;
    }
  }
  return S_OK;
}

HRESULT STDMETHODCALLTYPE AccessibilityElement::Invoke() {
  const auto* current = snapshotNode();
  if (current == nullptr || state_ == nullptr ||
      !hasAction(current->node, SemanticAction::Activate)) {
    return UIA_E_ELEMENTNOTAVAILABLE;
  }
  return state_->dispatch(current->node.id, SemanticAction::Activate) ? S_OK
                                                                        : E_FAIL;
}

HRESULT STDMETHODCALLTYPE AccessibilityElement::Toggle() {
  const auto* current = snapshotNode();
  if (current == nullptr || state_ == nullptr ||
      !hasAction(current->node, SemanticAction::Toggle)) {
    return UIA_E_ELEMENTNOTAVAILABLE;
  }
  const auto result = state_->dispatch(current->node.id, SemanticAction::Toggle);
  if (!result) return E_FAIL;
  state_->refresh();
  return S_OK;
}

HRESULT STDMETHODCALLTYPE AccessibilityElement::get_ToggleState(
    ToggleState* state) {
  if (state == nullptr) return E_INVALIDARG;
  const auto* current = snapshotNode();
  if (current == nullptr || !hasAction(current->node, SemanticAction::Toggle)) {
    return UIA_E_ELEMENTNOTAVAILABLE;
  }
  const auto value = std::string_view{current->node.value};
  *state = value == "On" || value == "Playing" || value == "Enabled"
               ? ToggleState_On
               : ToggleState_Off;
  return S_OK;
}

HRESULT STDMETHODCALLTYPE AccessibilityElement::Select() {
  const auto* current = snapshotNode();
  if (current == nullptr || state_ == nullptr ||
      !isSelectionItem(current->node)) {
    return UIA_E_ELEMENTNOTAVAILABLE;
  }
  const auto result = state_->dispatch(current->node.id, SemanticAction::Activate);
  if (!result) return E_FAIL;
  state_->refresh();
  return S_OK;
}

HRESULT STDMETHODCALLTYPE AccessibilityElement::AddToSelection() {
  const auto* current = snapshotNode();
  if (current == nullptr || !isSelectionItem(current->node)) {
    return UIA_E_ELEMENTNOTAVAILABLE;
  }
  return UIA_E_INVALIDOPERATION;
}

HRESULT STDMETHODCALLTYPE AccessibilityElement::RemoveFromSelection() {
  const auto* current = snapshotNode();
  if (current == nullptr || !isSelectionItem(current->node)) {
    return UIA_E_ELEMENTNOTAVAILABLE;
  }
  return UIA_E_INVALIDOPERATION;
}

HRESULT STDMETHODCALLTYPE AccessibilityElement::get_IsSelected(BOOL* selected) {
  if (selected == nullptr) return E_INVALIDARG;
  const auto* current = snapshotNode();
  if (current == nullptr || !isSelectionItem(current->node)) {
    return UIA_E_ELEMENTNOTAVAILABLE;
  }
  *selected = current->node.selected ? TRUE : FALSE;
  return S_OK;
}

HRESULT STDMETHODCALLTYPE AccessibilityElement::get_SelectionContainer(
    IRawElementProviderSimple** container) {
  if (container == nullptr) return E_INVALIDARG;
  *container = nullptr;
  const auto* current = snapshotNode();
  if (current == nullptr || !isSelectionItem(current->node)) {
    return UIA_E_ELEMENTNOTAVAILABLE;
  }
  if (current->parent < 0) return S_OK;
  auto* parent = elementFor(state_, snapshot_, current->parent);
  if (parent == nullptr) return E_FAIL;
  *container = static_cast<IRawElementProviderSimple*>(parent);
  return S_OK;
}

HRESULT STDMETHODCALLTYPE AccessibilityElement::GetSelection(
    SAFEARRAY** selection) {
  if (selection == nullptr) return E_INVALIDARG;
  *selection = nullptr;
  const auto* current = snapshotNode();
  if (current == nullptr || state_ == nullptr || snapshot_ == nullptr ||
      !isSelectionContainer(*snapshot_, *current)) {
    return UIA_E_ELEMENTNOTAVAILABLE;
  }
  auto active = snapshot_;
  if (current->noteLimit != 0U) {
    active = state_->expandPage(snapshot_, index_);
  }
  if (active == nullptr || index_ < 0 ||
      static_cast<std::size_t>(index_) >= active->nodes.size()) {
    return UIA_E_ELEMENTNOTAVAILABLE;
  }
  const auto& container = active->nodes[static_cast<std::size_t>(index_)];
  std::vector<std::int32_t> selected;
  for (const auto childIndex : container.children) {
    if (childIndex < 0 ||
        static_cast<std::size_t>(childIndex) >= active->nodes.size()) {
      continue;
    }
    const auto& child = active->nodes[static_cast<std::size_t>(childIndex)].node;
    if (isSelectionItem(child) && child.selected) selected.push_back(childIndex);
  }
  *selection = SafeArrayCreateVector(
      VT_UNKNOWN, 0, static_cast<ULONG>(selected.size()));
  if (*selection == nullptr) return E_OUTOFMEMORY;
  for (LONG offset = 0; offset < static_cast<LONG>(selected.size()); ++offset) {
    auto* element = elementFor(state_, active, selected[static_cast<std::size_t>(offset)]);
    if (element == nullptr) {
      SafeArrayDestroy(*selection);
      *selection = nullptr;
      return E_OUTOFMEMORY;
    }
    auto* provider = static_cast<IRawElementProviderSimple*>(element);
    const auto result = SafeArrayPutElement(
        *selection, &offset, static_cast<IUnknown*>(provider));
    element->Release();
    if (FAILED(result)) {
      SafeArrayDestroy(*selection);
      *selection = nullptr;
      return result;
    }
  }
  return S_OK;
}

HRESULT STDMETHODCALLTYPE AccessibilityElement::get_CanSelectMultiple(
    BOOL* canSelectMultiple) {
  if (canSelectMultiple == nullptr) return E_INVALIDARG;
  const auto* current = snapshotNode();
  if (current == nullptr || snapshot_ == nullptr ||
      !isSelectionContainer(*snapshot_, *current)) {
    return UIA_E_ELEMENTNOTAVAILABLE;
  }
  *canSelectMultiple = FALSE;
  return S_OK;
}

HRESULT STDMETHODCALLTYPE AccessibilityElement::get_IsSelectionRequired(
    BOOL* required) {
  if (required == nullptr) return E_INVALIDARG;
  const auto* current = snapshotNode();
  if (current == nullptr || snapshot_ == nullptr ||
      !isSelectionContainer(*snapshot_, *current)) {
    return UIA_E_ELEMENTNOTAVAILABLE;
  }
  *required = FALSE;
  return S_OK;
}

HRESULT STDMETHODCALLTYPE AccessibilityElement::SetValue(LPCWSTR value) {
  const auto* current = snapshotNode();
  if (current == nullptr || state_ == nullptr ||
      !hasAction(current->node, SemanticAction::EditText)) {
    return UIA_E_ELEMENTNOTAVAILABLE;
  }
  const auto converted = utf8FromWide(value);
  if (!converted.has_value()) return E_INVALIDARG;
  const auto result = state_->setValue(current->node.id, *converted);
  if (!result) return E_FAIL;
  state_->refresh();
  return S_OK;
}

HRESULT STDMETHODCALLTYPE AccessibilityElement::get_IsReadOnly(BOOL* readOnly) {
  if (readOnly == nullptr) return E_INVALIDARG;
  const auto* current = snapshotNode();
  if (current == nullptr ||
      !hasAction(current->node, SemanticAction::EditText)) {
    return UIA_E_ELEMENTNOTAVAILABLE;
  }
  *readOnly = FALSE;
  return S_OK;
}

HRESULT STDMETHODCALLTYPE AccessibilityElement::get_Value(BSTR* value) {
  if (value == nullptr) return E_INVALIDARG;
  *value = nullptr;
  const auto* current = snapshotNode();
  if (current == nullptr ||
      !hasAction(current->node, SemanticAction::EditText)) {
    return UIA_E_ELEMENTNOTAVAILABLE;
  }
  const auto wide = wideFromUtf8(current->node.editableValue);
  *value = SysAllocString(wide.c_str());
  return *value == nullptr && !wide.empty() ? E_OUTOFMEMORY : S_OK;
}

}

struct Win32AccessibilityBridge::Impl final {
  explicit Impl(void* nativeWindow, INativeWindowClient& client)
      : state(std::make_unique<BridgeState>()) {
    state->window = static_cast<HWND>(nativeWindow);
    state->client = &client;
  }

  std::unique_ptr<BridgeState> state;
};

Win32AccessibilityBridge::Win32AccessibilityBridge(
    void* nativeWindow, INativeWindowClient& client)
    : impl_(std::make_unique<Impl>(nativeWindow, client)) {}

Win32AccessibilityBridge::~Win32AccessibilityBridge() = default;

std::intptr_t Win32AccessibilityBridge::handleGetObject(
    std::uintptr_t wParam, std::intptr_t lParam) noexcept {
  if (impl_ == nullptr || impl_->state == nullptr ||
      static_cast<LONG>(lParam) != UiaRootObjectId) {
    return 0;
  }
  impl_->state->refresh();
  const auto snapshot = impl_->state->current();
  impl_->state->primeAnnouncements(snapshot);
  auto* root = elementFor(impl_->state.get(), snapshot, 0);
  if (root == nullptr) return 0;
  const auto result = UiaReturnRawElementProvider(
      impl_->state->window, static_cast<WPARAM>(wParam),
      static_cast<LPARAM>(lParam),
      static_cast<IRawElementProviderSimple*>(root));
  root->Release();
  return static_cast<std::intptr_t>(result);
}

void Win32AccessibilityBridge::invalidate() noexcept {
  if (impl_ == nullptr || impl_->state == nullptr) return;
  impl_->state->refresh();
  const auto snapshot = impl_->state->current();
  impl_->state->announcePropertyChanges(snapshot);
  impl_->state->announceFocusedElement(snapshot);
  auto* root = elementFor(impl_->state.get(), snapshot, 0);
  if (root == nullptr) return;
  static_cast<void>(UiaRaiseAutomationEvent(
      static_cast<IRawElementProviderSimple*>(root),
      UIA_LayoutInvalidatedEventId));
  root->Release();
}

core::Result<void> installAccessibilityBridge(void* nativeWindow) {
  if (nativeWindow == nullptr) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Win32 accessibility bridge requires a native window");
  }
  return core::success();
}

}

#else

namespace seam::native_ui {

core::Result<void> installAccessibilityBridge(void*) {
  return core::failure(core::ErrorCode::Unsupported,
                       "Win32 accessibility bridge is unavailable");
}

}

#endif
