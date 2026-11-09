#include "accessibility_windows.h"

#define NOMINMAX
#include <CommCtrl.h>
#include <OleAuto.h>
#include <UIAutomationCore.h>
#include <UIAutomationCoreApi.h>
#include <Windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cwchar>
#include <iterator>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace platformator_editor
{
namespace
{
std::wstring wide_string(std::string_view value) noexcept
{
    try
    {
        if (value.empty() || value.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
            return {};
        const int required = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                                 static_cast<int>(value.size()), nullptr, 0);
        if (required <= 0)
            return {};
        std::wstring result(static_cast<std::size_t>(required), L'\0');
        if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
                                result.data(), required) != required)
            return {};
        return result;
    }
    catch (...)
    {
        return {};
    }
}

std::string utf8_string(std::wstring_view value) noexcept
{
    try
    {
        if (value.empty() || value.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
            return {};
        const int required = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                                                 static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
        if (required <= 0)
            return {};
        std::string result(static_cast<std::size_t>(required), '\0');
        if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
                                result.data(), required, nullptr, nullptr) != required)
            return {};
        return result;
    }
    catch (...)
    {
        return {};
    }
}

std::uint64_t stable_hash(std::string_view value) noexcept
{
    std::uint64_t hash = 14695981039346656037ull;
    for (const unsigned char byte : value)
        hash = (hash ^ byte) * 1099511628211ull;
    return hash;
}

int control_type(SemanticRole role) noexcept
{
    switch (role)
    {
    case SemanticRole::Window:
        return UIA_WindowControlTypeId;
    case SemanticRole::Group:
        return UIA_GroupControlTypeId;
    case SemanticRole::Tab:
        return UIA_TabItemControlTypeId;
    case SemanticRole::Button:
        return UIA_ButtonControlTypeId;
    case SemanticRole::CheckBox:
        return UIA_CheckBoxControlTypeId;
    case SemanticRole::Slider:
        return UIA_SliderControlTypeId;
    case SemanticRole::Text:
        return UIA_TextControlTypeId;
    case SemanticRole::TextField:
        return UIA_EditControlTypeId;
    case SemanticRole::ListItem:
        return UIA_ListItemControlTypeId;
    case SemanticRole::ProgressBar:
        return UIA_ProgressBarControlTypeId;
    }
    return UIA_CustomControlTypeId;
}

class WindowsAccessibilityState
{
  public:
    WindowsAccessibilityState(AccessibilitySnapshots& snapshots, HostLayoutId host, WindowHostGeneration generation,
                              HWND hwnd, double scaleX, double scaleY)
        : snapshots_(&snapshots), host_(host), generation_(generation), hwnd_(hwnd), scaleX_(scaleX), scaleY_(scaleY)
    {
    }

    std::shared_ptr<const SemanticSnapshot> snapshot() const noexcept
    {
        try
        {
            std::lock_guard lock(mutex_);
            if (retired_ || snapshots_ == nullptr)
                return {};
            return snapshots_->acquire(host_, generation_);
        }
        catch (...)
        {
            return {};
        }
    }

    bool active() const noexcept
    {
        try
        {
            std::lock_guard lock(mutex_);
            return !retired_ && snapshots_ != nullptr && hwnd_ != nullptr;
        }
        catch (...)
        {
            return false;
        }
    }

    HWND hwnd() const noexcept
    {
        try
        {
            std::lock_guard lock(mutex_);
            return retired_ ? nullptr : hwnd_;
        }
        catch (...)
        {
            return nullptr;
        }
    }

    bool enqueue(std::string_view nodeId, std::string_view action, std::string_view value = {}) noexcept
    {
        try
        {
            std::lock_guard lock(mutex_);
            if (retired_ || snapshots_ == nullptr)
                return false;
            const auto snapshot = snapshots_->acquire(host_, generation_);
            if (!snapshot)
                return false;
            const auto node = std::find_if(snapshot->nodes.begin(), snapshot->nodes.end(),
                                           [&](const SemanticNode& candidate) { return candidate.id == nodeId; });
            if (node == snapshot->nodes.end() || !node->visible || !node->enabled ||
                std::find(node->actions.begin(), node->actions.end(), action) == node->actions.end())
                return false;
            return snapshots_->enqueue_action(SemanticAction{
                host_, generation_, snapshot->revision, std::string(nodeId), std::string(action), std::string(value)});
        }
        catch (...)
        {
            return false;
        }
    }

    bool rectangle(const SemanticNode* node, UiaRect& rectangle) const noexcept
    {
        try
        {
            HWND hwnd = nullptr;
            double scaleX = 1.0;
            double scaleY = 1.0;
            {
                std::lock_guard lock(mutex_);
                if (retired_ || hwnd_ == nullptr)
                    return false;
                hwnd = hwnd_;
                scaleX = scaleX_;
                scaleY = scaleY_;
            }
            if (const UINT dpi = GetDpiForWindow(hwnd); dpi != 0)
                scaleX = scaleY = static_cast<double>(dpi) / 96.0;
            RECT client{};
            if (node == nullptr)
            {
                RECT window{};
                if (!GetWindowRect(hwnd, &window))
                    return false;
                rectangle = UiaRect{static_cast<double>(window.left), static_cast<double>(window.top),
                                    static_cast<double>(window.right - window.left),
                                    static_cast<double>(window.bottom - window.top)};
                return true;
            }
            if (!node->bounds || !node->visible || !GetClientRect(hwnd, &client))
                return false;
            POINT origin{static_cast<LONG>(node->bounds->x * scaleX), static_cast<LONG>(node->bounds->y * scaleY)};
            if (!ClientToScreen(hwnd, &origin))
                return false;
            rectangle = UiaRect{static_cast<double>(origin.x), static_cast<double>(origin.y),
                                node->bounds->width * scaleX, node->bounds->height * scaleY};
            return true;
        }
        catch (...)
        {
            rectangle = UiaRect{0.0, 0.0, 0.0, 0.0};
            return false;
        }
    }

    void retire() noexcept
    {
        try
        {
            std::lock_guard lock(mutex_);
            retired_ = true;
            snapshots_ = nullptr;
            hwnd_ = nullptr;
        }
        catch (...)
        {
        }
    }

    HostLayoutId host() const noexcept { return host_; }
    WindowHostGeneration generation() const noexcept { return generation_; }

  private:
    mutable std::mutex mutex_;
    AccessibilitySnapshots* snapshots_ = nullptr;
    HostLayoutId host_;
    WindowHostGeneration generation_;
    HWND hwnd_ = nullptr;
    double scaleX_ = 1.0;
    double scaleY_ = 1.0;
    bool retired_ = false;
};

const SemanticNode* find_node(const SemanticSnapshot& snapshot, std::string_view id)
{
    const auto node = std::find_if(snapshot.nodes.begin(), snapshot.nodes.end(),
                                   [&](const SemanticNode& candidate) { return candidate.id == id; });
    return node == snapshot.nodes.end() ? nullptr : &*node;
}

class WindowsSemanticProvider final : public IRawElementProviderSimple,
                                      public IRawElementProviderFragment,
                                      public IRawElementProviderFragmentRoot,
                                      public IInvokeProvider,
                                      public IValueProvider,
                                      public IToggleProvider,
                                      public ISelectionItemProvider
{
  public:
    WindowsSemanticProvider(std::shared_ptr<WindowsAccessibilityState> state, std::string nodeId, bool root)
        : state_(std::move(state)), nodeId_(std::move(nodeId)), root_(root)
    {
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** result) override
    {
        if (result == nullptr)
            return E_POINTER;
        *result = nullptr;
        if (iid == __uuidof(IUnknown) || iid == __uuidof(IRawElementProviderSimple))
            *result = static_cast<IRawElementProviderSimple*>(this);
        else if (iid == __uuidof(IRawElementProviderFragment))
            *result = static_cast<IRawElementProviderFragment*>(this);
        else if (root_ && iid == __uuidof(IRawElementProviderFragmentRoot))
            *result = static_cast<IRawElementProviderFragmentRoot*>(this);
        else if (iid == __uuidof(IInvokeProvider) && supports_action("activate"))
            *result = static_cast<IInvokeProvider*>(this);
        else if (iid == __uuidof(IValueProvider) && has_value())
            *result = static_cast<IValueProvider*>(this);
        else if (iid == __uuidof(IToggleProvider) && supports_toggle())
            *result = static_cast<IToggleProvider*>(this);
        else if (iid == __uuidof(ISelectionItemProvider) && supports_selection())
            *result = static_cast<ISelectionItemProvider*>(this);
        else
            return E_NOINTERFACE;
        AddRef();
        return S_OK;
    }

    ULONG STDMETHODCALLTYPE AddRef() override { return references_.fetch_add(1, std::memory_order_relaxed) + 1; }

    ULONG STDMETHODCALLTYPE Release() override
    {
        const ULONG remaining = references_.fetch_sub(1, std::memory_order_acq_rel) - 1;
        if (remaining == 0)
            delete this;
        return remaining;
    }

    HRESULT STDMETHODCALLTYPE get_ProviderOptions(ProviderOptions* options) override
    {
        if (options == nullptr)
            return E_POINTER;
        *options = ProviderOptions_ServerSideProvider;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetPatternProvider(PATTERNID patternId, IUnknown** provider) override
    {
        if (provider == nullptr)
            return E_POINTER;
        *provider = nullptr;
        if (patternId == UIA_InvokePatternId && supports_action("activate"))
            return QueryInterface(__uuidof(IInvokeProvider), reinterpret_cast<void**>(provider));
        if (patternId == UIA_ValuePatternId && has_value())
            return QueryInterface(__uuidof(IValueProvider), reinterpret_cast<void**>(provider));
        if (patternId == UIA_TogglePatternId && supports_toggle())
            return QueryInterface(__uuidof(IToggleProvider), reinterpret_cast<void**>(provider));
        if (patternId == UIA_SelectionItemPatternId && supports_selection())
            return QueryInterface(__uuidof(ISelectionItemProvider), reinterpret_cast<void**>(provider));
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetPropertyValue(PROPERTYID propertyId, VARIANT* value) override
    {
        if (value == nullptr)
            return E_POINTER;
        VariantInit(value);
        if (!state_ || !state_->active())
            return UIA_E_ELEMENTNOTAVAILABLE;
        const auto snapshot = current_snapshot();
        const SemanticNode* node = snapshot && !nodeId_.empty() ? find_node(*snapshot, nodeId_) : nullptr;
        if (!root_ && (node == nullptr || !node->visible))
            return UIA_E_ELEMENTNOTAVAILABLE;
        switch (propertyId)
        {
        case UIA_ControlTypePropertyId:
            value->vt = VT_I4;
            value->lVal = node == nullptr ? UIA_WindowControlTypeId : control_type(node->role);
            return S_OK;
        case UIA_NamePropertyId:
            return set_string(value, node == nullptr ? L"Platformator Editor" : wide_string(node->name));
        case UIA_AutomationIdPropertyId:
            return set_string(value, node == nullptr ? L"platformator-editor-window" : wide_string(node->id));
        case UIA_HelpTextPropertyId:
            return set_string(value, node == nullptr ? L"" : wide_string(node->help));
        case UIA_IsEnabledPropertyId:
            value->vt = VT_BOOL;
            value->boolVal = node == nullptr || node->enabled ? VARIANT_TRUE : VARIANT_FALSE;
            return S_OK;
        case UIA_IsOffscreenPropertyId:
            value->vt = VT_BOOL;
            value->boolVal = node != nullptr && !node->visible ? VARIANT_TRUE : VARIANT_FALSE;
            return S_OK;
        case UIA_IsKeyboardFocusablePropertyId:
            value->vt = VT_BOOL;
            value->boolVal = node != nullptr && node->enabled ? VARIANT_TRUE : VARIANT_FALSE;
            return S_OK;
        case UIA_HasKeyboardFocusPropertyId:
            value->vt = VT_BOOL;
            value->boolVal = node != nullptr && node->focused ? VARIANT_TRUE : VARIANT_FALSE;
            return S_OK;
        case UIA_ValueValuePropertyId:
            return set_string(value, node == nullptr ? L"" : wide_string(node->value));
        case UIA_ToggleToggleStatePropertyId:
            if (node != nullptr && node->role == SemanticRole::CheckBox)
            {
                value->vt = VT_I4;
                value->lVal = node->selected ? ToggleState_On : ToggleState_Off;
                return S_OK;
            }
            return S_OK;
        case UIA_SelectionItemIsSelectedPropertyId:
            if (node != nullptr && node->role == SemanticRole::Tab)
            {
                value->vt = VT_BOOL;
                value->boolVal = node->selected ? VARIANT_TRUE : VARIANT_FALSE;
                return S_OK;
            }
            return S_OK;
        case UIA_IsControlElementPropertyId:
        case UIA_IsContentElementPropertyId:
            value->vt = VT_BOOL;
            value->boolVal = node == nullptr || node->visible ? VARIANT_TRUE : VARIANT_FALSE;
            return S_OK;
        default:
            return S_OK;
        }
    }

    HRESULT STDMETHODCALLTYPE get_HostRawElementProvider(IRawElementProviderSimple** provider) override
    {
        if (provider == nullptr)
            return E_POINTER;
        *provider = nullptr;
        const HWND hwnd = state_ ? state_->hwnd() : nullptr;
        if (hwnd == nullptr)
            return S_OK;
        return UiaHostProviderFromHwnd(hwnd, provider);
    }

    HRESULT STDMETHODCALLTYPE Navigate(NavigateDirection direction, IRawElementProviderFragment** provider) override
    {
        if (provider == nullptr)
            return E_POINTER;
        *provider = nullptr;
        const auto snapshot = current_snapshot();
        if (!snapshot || !state_ || !state_->active())
            return UIA_E_ELEMENTNOTAVAILABLE;
        const SemanticNode* current = nodeId_.empty() ? nullptr : find_node(*snapshot, nodeId_);
        if (!nodeId_.empty() && (current == nullptr || !current->visible))
            return UIA_E_ELEMENTNOTAVAILABLE;

        std::string target;
        if (direction == NavigateDirection_Parent)
        {
            if (current == nullptr)
                return S_OK;
            target = current->parentId;
        }
        else if (direction == NavigateDirection_FirstChild || direction == NavigateDirection_LastChild)
        {
            const std::string_view parentId = current == nullptr ? std::string_view{} : current->id;
            const SemanticNode* child = nullptr;
            if (direction == NavigateDirection_FirstChild)
            {
                for (const auto& candidate : snapshot->nodes)
                    if (candidate.visible && candidate.parentId == parentId)
                    {
                        child = &candidate;
                        break;
                    }
            }
            else
            {
                for (auto candidate = snapshot->nodes.rbegin(); candidate != snapshot->nodes.rend(); ++candidate)
                    if (candidate->visible && candidate->parentId == parentId)
                    {
                        child = &*candidate;
                        break;
                    }
            }
            if (child == nullptr)
                return S_OK;
            target = child->id;
        }
        else
        {
            if (current == nullptr)
                return S_OK;
            const auto currentIndex =
                std::find_if(snapshot->nodes.begin(), snapshot->nodes.end(),
                             [&](const SemanticNode& candidate) { return candidate.id == current->id; });
            if (currentIndex == snapshot->nodes.end())
                return UIA_E_ELEMENTNOTAVAILABLE;
            if (direction == NavigateDirection_NextSibling)
            {
                for (auto candidate = std::next(currentIndex); candidate != snapshot->nodes.end(); ++candidate)
                    if (candidate->visible && candidate->parentId == current->parentId)
                    {
                        target = candidate->id;
                        break;
                    }
            }
            else if (direction == NavigateDirection_PreviousSibling)
            {
                auto candidate = currentIndex;
                while (candidate != snapshot->nodes.begin())
                {
                    --candidate;
                    if (candidate->visible && candidate->parentId == current->parentId)
                    {
                        target = candidate->id;
                        break;
                    }
                }
            }
            else
                return E_INVALIDARG;
            if (target.empty())
                return S_OK;
        }

        const bool targetIsRoot = target.empty();
        auto* result = new (std::nothrow) WindowsSemanticProvider(state_, std::move(target), targetIsRoot);
        if (result == nullptr)
            return E_OUTOFMEMORY;
        *provider = static_cast<IRawElementProviderFragment*>(result);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetRuntimeId(SAFEARRAY** runtimeId) override
    {
        if (runtimeId == nullptr)
            return E_POINTER;
        *runtimeId = nullptr;
        if (!state_ || !state_->active())
            return UIA_E_ELEMENTNOTAVAILABLE;
        SAFEARRAYBOUND bounds{7, 0};
        SAFEARRAY* array = SafeArrayCreate(VT_I4, 1, &bounds);
        if (array == nullptr)
            return E_OUTOFMEMORY;
        LONG* values = nullptr;
        if (FAILED(SafeArrayAccessData(array, reinterpret_cast<void**>(&values))))
        {
            SafeArrayDestroy(array);
            return E_FAIL;
        }
        const auto hash = stable_hash(nodeId_);
        const std::uint64_t host = state_->host().value;
        const std::uint64_t generation = state_->generation().value;
        values[0] = UiaAppendRuntimeId;
        values[1] = static_cast<LONG>(host & 0xffffffffu);
        values[2] = static_cast<LONG>(host >> 32);
        values[3] = static_cast<LONG>(generation & 0xffffffffu);
        values[4] = static_cast<LONG>(generation >> 32);
        values[5] = static_cast<LONG>(hash & 0xffffffffu);
        values[6] = static_cast<LONG>(hash >> 32);
        SafeArrayUnaccessData(array);
        *runtimeId = array;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE get_BoundingRectangle(UiaRect* rectangle) override
    {
        if (rectangle == nullptr)
            return E_POINTER;
        const auto snapshot = current_snapshot();
        const auto* node = snapshot && !nodeId_.empty() ? find_node(*snapshot, nodeId_) : nullptr;
        if (!root_ && (node == nullptr || !node->visible))
            return UIA_E_ELEMENTNOTAVAILABLE;
        if (!state_ || !state_->rectangle(node, *rectangle))
            *rectangle = UiaRect{0.0, 0.0, 0.0, 0.0};
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetEmbeddedFragmentRoots(SAFEARRAY** roots) override
    {
        if (roots == nullptr)
            return E_POINTER;
        *roots = nullptr;
        return state_ && state_->active() ? S_OK : UIA_E_ELEMENTNOTAVAILABLE;
    }

    HRESULT STDMETHODCALLTYPE SetFocus() override
    {
        if (state_ && !nodeId_.empty() && supports_action("reveal") && state_->enqueue(nodeId_, "reveal"))
            return S_OK;
        return UIA_E_NOTSUPPORTED;
    }

    HRESULT STDMETHODCALLTYPE get_FragmentRoot(IRawElementProviderFragmentRoot** root) override
    {
        if (root == nullptr)
            return E_POINTER;
        *root = nullptr;
        if (!state_ || !state_->active())
            return UIA_E_ELEMENTNOTAVAILABLE;
        auto* provider = new (std::nothrow) WindowsSemanticProvider(state_, {}, true);
        if (provider == nullptr)
            return E_OUTOFMEMORY;
        *root = static_cast<IRawElementProviderFragmentRoot*>(provider);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE ElementProviderFromPoint(double x, double y,
                                                       IRawElementProviderFragment** provider) override
    {
        if (provider == nullptr)
            return E_POINTER;
        *provider = nullptr;
        const auto snapshot = current_snapshot();
        if (!snapshot || !state_ || !state_->active())
            return UIA_E_ELEMENTNOTAVAILABLE;
        const SemanticNode* best = nullptr;
        double bestArea = std::numeric_limits<double>::max();
        for (const auto& node : snapshot->nodes)
        {
            if (!node.visible || !node.bounds)
                continue;
            UiaRect rectangle{};
            if (!state_->rectangle(&node, rectangle) || x < rectangle.left || y < rectangle.top ||
                x > rectangle.left + rectangle.width || y > rectangle.top + rectangle.height)
                continue;
            const double area = rectangle.width * rectangle.height;
            if (area <= bestArea)
            {
                best = &node;
                bestArea = area;
            }
        }
        auto* result = new (std::nothrow)
            WindowsSemanticProvider(state_, best == nullptr ? std::string{} : best->id, best == nullptr);
        if (result == nullptr)
            return E_OUTOFMEMORY;
        *provider = static_cast<IRawElementProviderFragment*>(result);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetFocus(IRawElementProviderFragment** provider) override
    {
        if (provider == nullptr)
            return E_POINTER;
        *provider = nullptr;
        const auto snapshot = current_snapshot();
        if (!snapshot || !state_ || !state_->active())
            return UIA_E_ELEMENTNOTAVAILABLE;
        const auto focused = std::find_if(snapshot->nodes.begin(), snapshot->nodes.end(),
                                          [](const SemanticNode& node) { return node.visible && node.focused; });
        auto* result = new (std::nothrow) WindowsSemanticProvider(
            state_, focused == snapshot->nodes.end() ? std::string{} : focused->id, focused == snapshot->nodes.end());
        if (result == nullptr)
            return E_OUTOFMEMORY;
        *provider = static_cast<IRawElementProviderFragment*>(result);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Invoke() override
    {
        return state_ && !nodeId_.empty() && state_->enqueue(nodeId_, "activate") ? S_OK : UIA_E_ELEMENTNOTAVAILABLE;
    }

    HRESULT STDMETHODCALLTYPE SetValue(LPCWSTR value) override
    {
        if (value == nullptr)
            return E_INVALIDARG;
        constexpr std::size_t maximumWideUnits = AccessibilitySnapshots::MaximumActionValueBytes + 1;
        const std::size_t length = wcsnlen_s(value, maximumWideUnits);
        if (length >= maximumWideUnits)
            return E_INVALIDARG;
        if (!state_ || !state_->active() || nodeId_.empty())
            return UIA_E_ELEMENTNOTAVAILABLE;
        const std::string encoded = utf8_string(std::wstring_view(value, length));
        if ((length != 0 && encoded.empty()) || encoded.size() > AccessibilitySnapshots::MaximumActionValueBytes)
            return E_INVALIDARG;
        return state_->enqueue(nodeId_, "set_value", encoded) ? S_OK : UIA_E_NOTSUPPORTED;
    }

    HRESULT STDMETHODCALLTYPE get_Value(BSTR* value) override
    {
        if (value == nullptr)
            return E_POINTER;
        *value = nullptr;
        const auto snapshot = current_snapshot();
        const auto* node = snapshot && !nodeId_.empty() ? find_node(*snapshot, nodeId_) : nullptr;
        if (node == nullptr || !node->visible)
            return UIA_E_ELEMENTNOTAVAILABLE;
        const auto wide = wide_string(node->value);
        *value = SysAllocStringLen(wide.data(), static_cast<UINT>(wide.size()));
        return *value == nullptr && !wide.empty() ? E_OUTOFMEMORY : S_OK;
    }

    HRESULT STDMETHODCALLTYPE get_IsReadOnly(BOOL* readOnly) override
    {
        if (readOnly == nullptr)
            return E_POINTER;
        if (!state_ || !state_->active())
            return UIA_E_ELEMENTNOTAVAILABLE;
        *readOnly = supports_action("set_value") ? FALSE : TRUE;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Toggle() override
    {
        if (!state_ || nodeId_.empty())
            return UIA_E_ELEMENTNOTAVAILABLE;
        const auto snapshot = current_snapshot();
        const auto* node = snapshot ? find_node(*snapshot, nodeId_) : nullptr;
        if (node == nullptr || node->role != SemanticRole::CheckBox || !node->enabled)
            return UIA_E_ELEMENTNOTAVAILABLE;
        if (std::find(node->actions.begin(), node->actions.end(), "toggle") != node->actions.end())
            return state_->enqueue(nodeId_, "toggle") ? S_OK : UIA_E_ELEMENTNOTAVAILABLE;
        if (std::find(node->actions.begin(), node->actions.end(), "set_value") != node->actions.end())
            return state_->enqueue(nodeId_, "set_value", node->selected ? "false" : "true") ? S_OK
                                                                                            : UIA_E_ELEMENTNOTAVAILABLE;
        return UIA_E_NOTSUPPORTED;
    }

    HRESULT STDMETHODCALLTYPE get_ToggleState(ToggleState* toggleState) override
    {
        if (toggleState == nullptr)
            return E_POINTER;
        const auto snapshot = current_snapshot();
        const auto* node = snapshot && !nodeId_.empty() ? find_node(*snapshot, nodeId_) : nullptr;
        if (node == nullptr || node->role != SemanticRole::CheckBox)
            return UIA_E_ELEMENTNOTAVAILABLE;
        *toggleState = node->selected ? ToggleState_On : ToggleState_Off;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Select() override { return select_node(); }

    HRESULT STDMETHODCALLTYPE AddToSelection() override { return select_node(); }

    HRESULT STDMETHODCALLTYPE RemoveFromSelection() override { return UIA_E_NOTSUPPORTED; }

    HRESULT STDMETHODCALLTYPE get_IsSelected(BOOL* selected) override
    {
        if (selected == nullptr)
            return E_POINTER;
        const auto snapshot = current_snapshot();
        const auto* node = snapshot && !nodeId_.empty() ? find_node(*snapshot, nodeId_) : nullptr;
        if (node == nullptr || node->role != SemanticRole::Tab)
            return UIA_E_ELEMENTNOTAVAILABLE;
        *selected = node->selected ? TRUE : FALSE;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE get_SelectionContainer(IRawElementProviderSimple** container) override
    {
        if (container == nullptr)
            return E_POINTER;
        *container = nullptr;
        const auto snapshot = current_snapshot();
        const auto* node = snapshot && !nodeId_.empty() ? find_node(*snapshot, nodeId_) : nullptr;
        if (node == nullptr || node->role != SemanticRole::Tab || !state_ || !state_->active())
            return UIA_E_ELEMENTNOTAVAILABLE;
        const bool parentIsRoot = node->parentId.empty();
        auto* provider = new (std::nothrow) WindowsSemanticProvider(state_, node->parentId, parentIsRoot);
        if (provider == nullptr)
            return E_OUTOFMEMORY;
        *container = static_cast<IRawElementProviderSimple*>(provider);
        return S_OK;
    }

  private:
    ~WindowsSemanticProvider() = default;

    bool supports_action(std::string_view action) const noexcept
    {
        if (!state_ || nodeId_.empty())
            return false;
        const auto snapshot = current_snapshot();
        const auto* node = snapshot ? find_node(*snapshot, nodeId_) : nullptr;
        return node != nullptr && node->visible && node->enabled &&
               std::find(node->actions.begin(), node->actions.end(), action) != node->actions.end();
    }

    bool has_value() const noexcept
    {
        if (!state_ || nodeId_.empty())
            return false;
        const auto snapshot = current_snapshot();
        const auto* node = snapshot ? find_node(*snapshot, nodeId_) : nullptr;
        return node != nullptr && node->visible &&
               (node->role == SemanticRole::Slider || node->role == SemanticRole::TextField);
    }

    bool supports_toggle() const noexcept
    {
        if (!state_ || nodeId_.empty())
            return false;
        const auto snapshot = current_snapshot();
        const auto* node = snapshot ? find_node(*snapshot, nodeId_) : nullptr;
        return node != nullptr && node->role == SemanticRole::CheckBox;
    }

    bool supports_selection() const noexcept
    {
        if (!state_ || nodeId_.empty())
            return false;
        const auto snapshot = current_snapshot();
        const auto* node = snapshot ? find_node(*snapshot, nodeId_) : nullptr;
        return node != nullptr && node->role == SemanticRole::Tab;
    }

    HRESULT select_node()
    {
        const auto snapshot = current_snapshot();
        const auto* node = snapshot && !nodeId_.empty() ? find_node(*snapshot, nodeId_) : nullptr;
        if (node == nullptr || node->role != SemanticRole::Tab || !node->enabled || !state_)
            return UIA_E_ELEMENTNOTAVAILABLE;
        return state_->enqueue(nodeId_, "activate") ? S_OK : UIA_E_ELEMENTNOTAVAILABLE;
    }

    static HRESULT set_string(VARIANT* value, const std::wstring& text)
    {
        value->vt = VT_BSTR;
        value->bstrVal = SysAllocStringLen(text.data(), static_cast<UINT>(text.size()));
        return value->bstrVal == nullptr && !text.empty() ? E_OUTOFMEMORY : S_OK;
    }

    std::shared_ptr<const SemanticSnapshot> current_snapshot() const noexcept
    {
        try
        {
            if (!state_ || !state_->active())
                return {};
            auto snapshot = state_ ? state_->snapshot() : std::shared_ptr<const SemanticSnapshot>{};
            if (snapshot)
            {
                std::lock_guard lock(snapshotMutex_);
                retainedSnapshot_ = snapshot;
                return retainedSnapshot_;
            }
            std::lock_guard lock(snapshotMutex_);
            return retainedSnapshot_;
        }
        catch (...)
        {
            return {};
        }
    }

    std::atomic<ULONG> references_{1};
    std::shared_ptr<WindowsAccessibilityState> state_;
    std::string nodeId_;
    bool root_ = false;
    mutable std::mutex snapshotMutex_;
    mutable std::shared_ptr<const SemanticSnapshot> retainedSnapshot_;
};

} // namespace

struct WindowsAccessibilityProvider::Impl
{
    HWND hwnd = nullptr;
    WindowsSemanticProvider* root = nullptr;
    std::shared_ptr<WindowsAccessibilityState> state;
};

namespace
{
LRESULT CALLBACK accessibility_subclass_proc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR subclassId,
                                             DWORD_PTR referenceData)
{
    auto* impl = reinterpret_cast<WindowsAccessibilityProvider::Impl*>(referenceData);
    (void)subclassId;
    if (impl != nullptr && message == WM_GETOBJECT &&
        static_cast<DWORD>(lParam) == static_cast<DWORD>(UiaRootObjectId) && impl->root != nullptr)
        return UiaReturnRawElementProvider(hwnd, wParam, lParam, static_cast<IRawElementProviderSimple*>(impl->root));
    if (message == WM_NCDESTROY)
    {
        if (impl != nullptr && impl->state)
            impl->state->retire();
        UiaReturnRawElementProvider(hwnd, 0, 0, nullptr);
        RemoveWindowSubclass(hwnd, accessibility_subclass_proc, reinterpret_cast<UINT_PTR>(impl));
    }
    return DefSubclassProc(hwnd, message, wParam, lParam);
}
} // namespace

WindowsAccessibilityProvider::WindowsAccessibilityProvider() : impl_(std::make_unique<Impl>()) {}

WindowsAccessibilityProvider::~WindowsAccessibilityProvider() { stop(); }

bool WindowsAccessibilityProvider::start(AccessibilitySnapshots& snapshots, SDL_Window* window, HostLayoutId host,
                                         WindowHostGeneration generation, std::string& error)
{
    stop();
    if (window == nullptr || !host || !generation)
    {
        error = "UI Automation provider requires a live SDL window and host identity";
        return false;
    }
    const SDL_PropertiesID properties = SDL_GetWindowProperties(window);
    HWND hwnd = static_cast<HWND>(SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
    if (hwnd == nullptr)
    {
        error = "SDL did not expose a Win32 window handle for UI Automation";
        return false;
    }
    int logicalWidth = 0;
    int logicalHeight = 0;
    int pixelWidth = 0;
    int pixelHeight = 0;
    SDL_GetWindowSize(window, &logicalWidth, &logicalHeight);
    SDL_GetWindowSizeInPixels(window, &pixelWidth, &pixelHeight);
    const double scaleX = logicalWidth > 0 && pixelWidth > 0 ? static_cast<double>(pixelWidth) / logicalWidth : 1.0;
    const double scaleY = logicalHeight > 0 && pixelHeight > 0 ? static_cast<double>(pixelHeight) / logicalHeight : 1.0;
    impl_->hwnd = hwnd;
    impl_->state = std::make_shared<WindowsAccessibilityState>(snapshots, host, generation, hwnd, scaleX, scaleY);
    impl_->root = new (std::nothrow) WindowsSemanticProvider(impl_->state, {}, true);
    if (impl_->root == nullptr)
    {
        impl_->state->retire();
        impl_->state.reset();
        impl_->hwnd = nullptr;
        error = "could not allocate the UI Automation fragment root";
        return false;
    }
    if (!SetWindowSubclass(hwnd, accessibility_subclass_proc, reinterpret_cast<UINT_PTR>(impl_.get()),
                           reinterpret_cast<DWORD_PTR>(impl_.get())))
    {
        impl_->root->Release();
        impl_->root = nullptr;
        impl_->state->retire();
        impl_->state.reset();
        impl_->hwnd = nullptr;
        error = "could not attach the UI Automation window provider";
        return false;
    }
    return true;
}

void WindowsAccessibilityProvider::stop() noexcept
{
    if (!impl_)
        return;
    if (impl_->state)
        impl_->state->retire();
    if (impl_->hwnd != nullptr)
        RemoveWindowSubclass(impl_->hwnd, accessibility_subclass_proc, reinterpret_cast<UINT_PTR>(impl_.get()));
    if (impl_->hwnd != nullptr)
        UiaReturnRawElementProvider(impl_->hwnd, 0, 0, nullptr);
    if (impl_->root != nullptr)
        impl_->root->Release();
    impl_->hwnd = nullptr;
    impl_->root = nullptr;
    impl_->state.reset();
}

bool WindowsAccessibilityProvider::running() const noexcept
{
    return impl_ && impl_->hwnd != nullptr && impl_->root != nullptr;
}
} // namespace platformator_editor
