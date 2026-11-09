#import <AppKit/AppKit.h>
#import <SDL3/SDL.h>

#include "accessibility_macos.h"

#include <algorithm>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>

namespace platformator_editor
{
class MacAccessibilityState
{
  public:
    MacAccessibilityState(AccessibilitySnapshots& snapshots, HostLayoutId host, WindowHostGeneration generation,
                          NSView* container)
        : snapshots_(&snapshots), host_(host), generation_(generation), container_(container)
    {
        [container_ retain];
    }

    ~MacAccessibilityState() { retire(); }

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

    NSView* retained_container() const noexcept
    {
        try
        {
            std::lock_guard lock(mutex_);
            if (retired_ || container_ == nil)
                return nil;
            return [container_ retain];
        }
        catch (...)
        {
            return nil;
        }
    }

    void retire() noexcept
    {
        try
        {
            std::lock_guard lock(mutex_);
            retired_ = true;
            snapshots_ = nullptr;
            if (container_ != nil)
            {
                [container_ release];
                container_ = nil;
            }
        }
        catch (...)
        {
        }
    }

  private:
    mutable std::mutex mutex_;
    AccessibilitySnapshots* snapshots_ = nullptr;
    HostLayoutId host_;
    WindowHostGeneration generation_;
    NSView* container_ = nil;
    bool retired_ = false;
};

NSString* ns_string(std::string_view value)
{
    NSString* result = [[NSString alloc] initWithBytes:value.data() length:value.size() encoding:NSUTF8StringEncoding];
    return [result autorelease] ?: @"";
}

const SemanticNode* find_node(const SemanticSnapshot& snapshot, std::string_view id)
{
    const auto node = std::find_if(snapshot.nodes.begin(), snapshot.nodes.end(),
                                   [&](const SemanticNode& candidate) { return candidate.id == id; });
    return node == snapshot.nodes.end() ? nullptr : &*node;
}

NSString* native_role(SemanticRole role) noexcept
{
    switch (role)
    {
    case SemanticRole::Window:
        return NSAccessibilityWindowRole;
    case SemanticRole::Group:
        return NSAccessibilityGroupRole;
    case SemanticRole::Tab:
        return NSAccessibilityTabButtonRole;
    case SemanticRole::Button:
        return NSAccessibilityButtonRole;
    case SemanticRole::CheckBox:
        return NSAccessibilityCheckBoxRole;
    case SemanticRole::Slider:
        return NSAccessibilitySliderRole;
    case SemanticRole::Text:
        return NSAccessibilityStaticTextRole;
    case SemanticRole::TextField:
        return NSAccessibilityTextFieldRole;
    case SemanticRole::ListItem:
        return NSAccessibilityRowRole;
    case SemanticRole::ProgressBar:
        return NSAccessibilityProgressIndicatorRole;
    }
    return NSAccessibilityUnknownRole;
}

void attach_accessibility_child(NSView* container, NSAccessibilityElement* child)
{
    if ([container respondsToSelector:@selector(accessibilityAddChildElement:)])
    {
        [container accessibilityAddChildElement:child];
        return;
    }
    if (![container respondsToSelector:@selector(setAccessibilityChildren:)])
        return;
    NSArray* existing = [container accessibilityChildren];
    NSMutableArray* children = existing == nil ? [[NSMutableArray alloc] init] : [existing mutableCopy];
    if (![children containsObject:child])
        [children addObject:child];
    [container setAccessibilityChildren:children];
    [children release];
}

void detach_accessibility_child(NSView* container, NSAccessibilityElement* child)
{
    if ([container respondsToSelector:@selector(accessibilityRemoveChildElement:)])
    {
        [container accessibilityRemoveChildElement:child];
        return;
    }
    if (![container respondsToSelector:@selector(setAccessibilityChildren:)])
        return;
    NSArray* existing = [container accessibilityChildren];
    NSMutableArray* children = existing == nil ? [[NSMutableArray alloc] init] : [existing mutableCopy];
    [children removeObjectIdenticalTo:child];
    [container setAccessibilityChildren:children];
    [children release];
}
} // namespace platformator_editor

using namespace platformator_editor;

@interface PlatformatorSemanticElement : NSAccessibilityElement
{
  @private
    std::shared_ptr<MacAccessibilityState> _state;
    std::string _nodeId;
}
- (id)initWithState:(std::shared_ptr<MacAccessibilityState>)state nodeId:(std::string)nodeId container:(id)container;
@end

@implementation PlatformatorSemanticElement
- (id)initWithState:(std::shared_ptr<MacAccessibilityState>)state nodeId:(std::string)nodeId container:(id)container
{
    self = [super init];
    if (self != nil)
    {
        _state = std::move(state);
        _nodeId = std::move(nodeId);
        [self setAccessibilityParent:container];
        [self setAccessibilityElement:YES];
    }
    return self;
}

- (void)dealloc
{
    _state.reset();
    _nodeId.clear();
    [super dealloc];
}

- (std::shared_ptr<const SemanticSnapshot>)currentSnapshot
{
    return _state ? _state->snapshot() : std::shared_ptr<const SemanticSnapshot>{};
}

- (const SemanticNode*)currentNode:(std::shared_ptr<const SemanticSnapshot>*)snapshotOut
{
    auto snapshot = [self currentSnapshot];
    if (!snapshot)
        return nullptr;
    const auto* node = find_node(*snapshot, _nodeId);
    if (node == nullptr || !node->visible)
        return nullptr;
    if (snapshotOut != nullptr)
        *snapshotOut = std::move(snapshot);
    return node;
}

- (BOOL)isAccessibilityElement
{
    if (_nodeId.empty())
        return YES;
    return [self currentNode:nullptr] != nullptr;
}

- (NSString*)accessibilityIdentifier
{
    return _nodeId.empty() ? @"platformator-editor-window" : ns_string(_nodeId);
}

- (NSString*)accessibilityRole
{
    if (_nodeId.empty())
        return NSAccessibilityWindowRole;
    const auto* node = [self currentNode:nullptr];
    return node == nullptr ? NSAccessibilityUnknownRole : native_role(node->role);
}

- (NSString*)accessibilityLabel
{
    if (_nodeId.empty())
        return @"Platformator Editor";
    const auto* node = [self currentNode:nullptr];
    return node == nullptr ? @"" : ns_string(node->name);
}

- (NSString*)accessibilityHelp
{
    const auto* node = [self currentNode:nullptr];
    return node == nullptr ? @"" : ns_string(node->help);
}

- (id)accessibilityValue
{
    const auto* node = [self currentNode:nullptr];
    if (node == nullptr)
        return nil;
    if (node->role == SemanticRole::CheckBox)
        return [NSNumber numberWithBool:node->selected];
    if (!node->value.empty())
        return ns_string(node->value);
    if (node->selected)
        return @"selected";
    return nil;
}

- (BOOL)accessibilityEnabled
{
    if (_nodeId.empty())
        return YES;
    const auto* node = [self currentNode:nullptr];
    return node != nullptr && node->enabled;
}

- (BOOL)accessibilitySelected
{
    const auto* node = [self currentNode:nullptr];
    return node != nullptr && node->selected;
}

- (BOOL)accessibilityExpanded
{
    const auto* node = [self currentNode:nullptr];
    return node != nullptr && node->expanded;
}

- (BOOL)isAccessibilityFocused
{
    const auto* node = [self currentNode:nullptr];
    return node != nullptr && node->focused;
}

- (NSArray*)accessibilityChildren
{
    if (!_nodeId.empty() && [self currentNode:nullptr] == nullptr)
        return @[];
    const auto snapshot = [self currentSnapshot];
    if (!snapshot)
        return @[];
    NSMutableArray* children = [NSMutableArray array];
    for (const auto& node : snapshot->nodes)
    {
        if (!node.visible || node.parentId != _nodeId)
            continue;
        auto* child = [[PlatformatorSemanticElement alloc] initWithState:_state nodeId:node.id container:self];
        [children addObject:child];
        [child release];
    }
    return children;
}

- (NSRect)accessibilityFrame
{
    NSView* container = _state ? _state->retained_container() : nil;
    if (container == nil)
        return NSZeroRect;
    NSRect parentFrame = [container bounds];
    if (!_nodeId.empty())
    {
        const auto* node = [self currentNode:nullptr];
        if (node == nullptr || !node->bounds)
        {
            [container release];
            return NSZeroRect;
        }
        parentFrame = NSMakeRect(node->bounds->x, NSHeight([container bounds]) - node->bounds->y - node->bounds->height,
                                 node->bounds->width, node->bounds->height);
    }
    const NSRect screenFrame = NSAccessibilityFrameInView(container, parentFrame);
    [container release];
    return screenFrame;
}

- (BOOL)accessibilityPerformPress
{
    if (_nodeId.empty() || !_state)
        return NO;
    const auto* node = [self currentNode:nullptr];
    if (node == nullptr || !node->enabled)
        return NO;
    const auto& actions = node->actions;
    if (std::find(actions.begin(), actions.end(), "toggle") != actions.end())
        return _state->enqueue(_nodeId, "toggle") ? YES : NO;
    if (std::find(actions.begin(), actions.end(), "activate") != actions.end())
        return _state->enqueue(_nodeId, "activate") ? YES : NO;
    if (node->role == SemanticRole::CheckBox && std::find(actions.begin(), actions.end(), "set_value") != actions.end())
        return _state->enqueue(_nodeId, "set_value", node->selected ? "false" : "true") ? YES : NO;
    if (std::find(actions.begin(), actions.end(), "reveal") != actions.end())
        return _state->enqueue(_nodeId, "reveal") ? YES : NO;
    return NO;
}

- (BOOL)accessibilityIsAttributeSettable:(NSString*)attribute
{
    if (![attribute isEqualToString:NSAccessibilityValueAttribute])
        return NO;
    const auto* node = [self currentNode:nullptr];
    return node != nullptr && node->enabled &&
           std::find(node->actions.begin(), node->actions.end(), "set_value") != node->actions.end();
}

- (void)accessibilitySetValue:(id)value forAttribute:(NSString*)attribute
{
    try
    {
        if (![self accessibilityIsAttributeSettable:attribute] || value == nil || !_state)
            return;
        const auto* node = [self currentNode:nullptr];
        if (node == nullptr)
            return;
        std::string replacement;
        if (node->role == SemanticRole::CheckBox && [value respondsToSelector:@selector(boolValue)])
            replacement = [value boolValue] ? "true" : "false";
        else if ([value respondsToSelector:@selector(UTF8String)])
        {
            const char* utf8 = [value UTF8String];
            if (utf8 != nullptr)
                replacement = utf8;
        }
        else if ([value respondsToSelector:@selector(stringValue)])
        {
            const char* utf8 = [[value stringValue] UTF8String];
            if (utf8 != nullptr)
                replacement = utf8;
        }
        _state->enqueue(_nodeId, "set_value", replacement);
    }
    catch (...)
    {
    }
}
@end

namespace platformator_editor
{
struct MacAccessibilityProvider::Impl
{
    std::shared_ptr<MacAccessibilityState> state;
    NSWindow* window = nil;
    NSView* container = nil;
    PlatformatorSemanticElement* root = nil;
};

MacAccessibilityProvider::MacAccessibilityProvider() : impl_(std::make_unique<Impl>()) {}

MacAccessibilityProvider::~MacAccessibilityProvider() { stop(); }

bool MacAccessibilityProvider::start(AccessibilitySnapshots& snapshots, SDL_Window* window, HostLayoutId host,
                                     WindowHostGeneration generation, std::string& error)
{
    stop();
    if (!NSThread.isMainThread)
    {
        error = "NSAccessibility providers must attach on the Cocoa main thread";
        return false;
    }
    if (window == nullptr || !host || !generation)
    {
        error = "NSAccessibility provider requires a live SDL window and host identity";
        return false;
    }
    const SDL_PropertiesID properties = SDL_GetWindowProperties(window);
    NSWindow* nativeWindow =
        (NSWindow*)SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr);
    NSView* container = [nativeWindow contentView];
    if (nativeWindow == nil || container == nil)
    {
        error = "SDL did not expose a Cocoa window content view for accessibility";
        return false;
    }

    impl_->window = [nativeWindow retain];
    impl_->container = [container retain];
    impl_->state = std::make_shared<MacAccessibilityState>(snapshots, host, generation, container);
    impl_->root = [[PlatformatorSemanticElement alloc] initWithState:impl_->state
                                                              nodeId:std::string {}
                                                           container:container];
    attach_accessibility_child(container, impl_->root);
    NSAccessibilityPostNotification(nativeWindow, NSAccessibilityLayoutChangedNotification);
    return true;
}

void MacAccessibilityProvider::stop() noexcept
{
    if (!impl_)
        return;
    if (impl_->state)
        impl_->state->retire();
    if (impl_->container != nil && impl_->root != nil)
        detach_accessibility_child(impl_->container, impl_->root);
    if (impl_->window != nil)
        NSAccessibilityPostNotification(impl_->window, NSAccessibilityLayoutChangedNotification);
    [impl_->root release];
    [impl_->container release];
    [impl_->window release];
    impl_->root = nil;
    impl_->container = nil;
    impl_->window = nil;
    impl_->state.reset();
}

bool MacAccessibilityProvider::running() const noexcept { return impl_ && impl_->root != nil; }
} // namespace platformator_editor
