#include "accessibility_linux.h"

#include <dbus/dbus.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <mutex>
#include <optional>
#include <sstream>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace platformator_editor
{
namespace
{
constexpr char AccessibleRootPath[] = "/org/a11y/atspi/accessible/root";
constexpr char AccessiblePrefix[] = "/org/a11y/atspi/accessible";
constexpr char NullPath[] = "/org/a11y/atspi/null";
constexpr char AccessibleInterface[] = "org.a11y.atspi.Accessible";
constexpr char ApplicationInterface[] = "org.a11y.atspi.Application";
constexpr char ActionInterface[] = "org.a11y.atspi.Action";
constexpr char TextInterface[] = "org.a11y.atspi.Text";
constexpr char EditableTextInterface[] = "org.a11y.atspi.EditableText";
constexpr char ComponentInterface[] = "org.a11y.atspi.Component";
constexpr char ValueInterface[] = "org.a11y.atspi.Value";
constexpr char PropertiesInterface[] = "org.freedesktop.DBus.Properties";
constexpr char IntrospectableInterface[] = "org.freedesktop.DBus.Introspectable";

struct ResolvedNode
{
    std::shared_ptr<const SemanticSnapshot> snapshot;
    const SemanticNode *node = nullptr;
    bool root = false;
};

std::string node_path(HostLayoutId host, WindowHostGeneration generation, std::string_view id)
{
    // Two independent 64-bit FNV streams keep object paths short while avoiding
    // path escaping and preserving stable IDs for the lifetime of a process.
    std::uint64_t first = 14695981039346656037ull;
    std::uint64_t second = 1099511628211ull ^ 0x9e3779b97f4a7c15ull;
    first = (first ^ host.value) * 1099511628211ull;
    first = (first ^ generation.value) * 1099511628211ull;
    second = (second ^ static_cast<unsigned char>(host.value)) * 14029467366897019727ull;
    second = (second ^ static_cast<unsigned char>(generation.value)) * 14029467366897019727ull;
    for (const unsigned char byte : id)
    {
        first = (first ^ byte) * 1099511628211ull;
        second = (second ^ static_cast<unsigned char>(byte + 0x5bu)) * 14029467366897019727ull;
    }
    std::ostringstream path;
    path << AccessiblePrefix << "/n_" << std::hex << first << '_' << second;
    return path.str();
}

std::uint32_t atspi_role(SemanticRole role) noexcept
{
    // Values are the stable ATSPI_ROLE_* values from the AT-SPI D-Bus API.
    switch (role)
    {
    case SemanticRole::Window: return 69;       // ATSPI_ROLE_WINDOW
    case SemanticRole::Group: return 39;        // ATSPI_ROLE_PANEL
    case SemanticRole::Tab: return 36;          // ATSPI_ROLE_PAGE_TAB
    case SemanticRole::Button: return 43;       // ATSPI_ROLE_PUSH_BUTTON
    case SemanticRole::CheckBox: return 7;      // ATSPI_ROLE_CHECK_BOX
    case SemanticRole::Slider: return 51;       // ATSPI_ROLE_SLIDER
    case SemanticRole::Text: return 116;        // ATSPI_ROLE_STATIC
    case SemanticRole::TextField: return 79;    // ATSPI_ROLE_ENTRY
    case SemanticRole::ListItem: return 32;     // ATSPI_ROLE_LIST_ITEM
    case SemanticRole::ProgressBar: return 42;  // ATSPI_ROLE_PROGRESS_BAR
    }
    return 67; // ATSPI_ROLE_UNKNOWN
}

const char *role_name(SemanticRole role) noexcept
{
    switch (role)
    {
    case SemanticRole::Window: return "window";
    case SemanticRole::Group: return "panel";
    case SemanticRole::Tab: return "page tab";
    case SemanticRole::Button: return "push button";
    case SemanticRole::CheckBox: return "check box";
    case SemanticRole::Slider: return "slider";
    case SemanticRole::Text: return "static";
    case SemanticRole::TextField: return "entry";
    case SemanticRole::ListItem: return "list item";
    case SemanticRole::ProgressBar: return "progress bar";
    }
    return "unknown";
}

bool append_string(DBusMessageIter &iter, const char *value)
{
    const char *safeValue = value == nullptr ? "" : value;
    return dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &safeValue);
}

bool append_path(DBusMessageIter &iter, const char *value)
{
    const char *safeValue = value == nullptr ? NullPath : value;
    return dbus_message_iter_append_basic(&iter, DBUS_TYPE_OBJECT_PATH, &safeValue);
}

bool append_reference(DBusMessageIter &iter, const char *busName, const char *path)
{
    DBusMessageIter pair;
    if (!dbus_message_iter_open_container(&iter, DBUS_TYPE_STRUCT, nullptr, &pair))
        return false;
    const bool appended = append_string(pair, busName) && append_path(pair, path);
    return dbus_message_iter_close_container(&iter, &pair) && appended;
}

bool append_basic_return(DBusConnection *connection, DBusMessage *request,
                         int type, const void *value)
{
    DBusMessage *reply = dbus_message_new_method_return(request);
    if (reply == nullptr)
        return false;
    const bool appended = dbus_message_append_args(reply, type, value, DBUS_TYPE_INVALID);
    const bool sent = appended && dbus_connection_send(connection, reply, nullptr);
    dbus_message_unref(reply);
    if (sent)
        dbus_connection_flush(connection);
    return sent;
}

DBusHandlerResult send_empty_return(DBusConnection *connection, DBusMessage *request)
{
    DBusMessage *reply = dbus_message_new_method_return(request);
    if (reply != nullptr)
    {
        dbus_connection_send(connection, reply, nullptr);
        dbus_message_unref(reply);
        dbus_connection_flush(connection);
    }
    return DBUS_HANDLER_RESULT_HANDLED;
}

DBusHandlerResult send_error(DBusConnection *connection, DBusMessage *request,
                             const char *name, const char *description)
{
    DBusMessage *reply = dbus_message_new_error(request, name, description);
    if (reply != nullptr)
    {
        dbus_connection_send(connection, reply, nullptr);
        dbus_message_unref(reply);
        dbus_connection_flush(connection);
    }
    return DBUS_HANDLER_RESULT_HANDLED;
}

bool read_basic_argument(DBusMessage *message, int type, void *value)
{
    DBusMessageIter iter;
    if (!dbus_message_iter_init(message, &iter) || dbus_message_iter_get_arg_type(&iter) != type)
        return false;
    dbus_message_iter_get_basic(&iter, value);
    return true;
}

std::vector<const SemanticNode *> children_of(const SemanticSnapshot &snapshot,
                                              const SemanticNode *parent)
{
    const std::string_view parentId = parent == nullptr ? std::string_view{} : parent->id;
    std::vector<const SemanticNode *> children;
    for (const auto &node : snapshot.nodes)
        if (node.visible && node.parentId == parentId)
            children.push_back(&node);
    return children;
}

std::string description_of(const SemanticNode &node)
{
    if (!node.help.empty() && !node.value.empty())
        return node.help + " — " + node.value;
    return node.help.empty() ? node.value : node.help;
}

std::size_t utf8_character_count(std::string_view value) noexcept
{
    return static_cast<std::size_t>(std::count_if(value.begin(), value.end(), [](char character) {
        return (static_cast<unsigned char>(character) & 0xc0u) != 0x80u;
    }));
}

std::size_t utf8_byte_offset(std::string_view value, std::size_t characterOffset) noexcept
{
    std::size_t currentCharacter = 0;
    for (std::size_t byteOffset = 0; byteOffset < value.size(); ++byteOffset)
    {
        if ((static_cast<unsigned char>(value[byteOffset]) & 0xc0u) == 0x80u)
            continue;
        if (currentCharacter == characterOffset)
            return byteOffset;
        ++currentCharacter;
    }
    return value.size();
}

std::vector<std::uint32_t> states_of(const ResolvedNode &resolved)
{
    std::vector<std::uint32_t> states;
    const auto add = [&](std::uint32_t state) {
        if (std::find(states.begin(), states.end(), state) == states.end())
            states.push_back(state);
    };
    if (resolved.root)
        return {8, 24, 30, 25}; // enabled, sensitive, visible, showing
    if (resolved.node == nullptr)
        return states;
    const auto &node = *resolved.node;
    if (node.enabled)
    {
        add(8);  // enabled
        add(24); // sensitive
    }
    if (node.visible)
    {
        add(30); // visible
        add(25); // showing
    }
    if (node.selected)
    {
        add(22); // selectable
        add(23); // selected
        if (node.role == SemanticRole::CheckBox)
            add(4); // checked
    }
    if (node.focused)
    {
        add(11); // focusable
        add(12); // focused
    }
    const bool hasChildren = std::any_of(resolved.snapshot->nodes.begin(), resolved.snapshot->nodes.end(),
        [&](const SemanticNode &child) { return child.visible && child.parentId == node.id; });
    if (hasChildren)
    {
        add(9); // expandable
        add(node.expanded ? 10u : 5u); // expanded or collapsed
    }
    else if (node.expanded)
        add(10); // expanded
    if (node.error)
        add(36); // invalid entry
    if (std::any_of(node.actions.begin(), node.actions.end(), [](const std::string &action) {
            return action == "activate" || action == "toggle";
        }))
        add(11); // focusable
    return states;
}

bool append_variant_basic(DBusMessageIter &parent, const char *propertyName,
                          const char *signature, int type, const void *value)
{
    DBusMessageIter entry;
    DBusMessageIter variant;
    if (!dbus_message_iter_open_container(&parent, DBUS_TYPE_DICT_ENTRY, nullptr, &entry))
        return false;
    const char *name = propertyName;
    bool ok = dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &name) &&
              dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, signature, &variant) &&
              dbus_message_iter_append_basic(&variant, type, value) &&
              dbus_message_iter_close_container(&entry, &variant);
    return dbus_message_iter_close_container(&parent, &entry) && ok;
}

bool append_variant_reference(DBusMessageIter &parent, const char *propertyName,
                              const char *busName, const char *path)
{
    DBusMessageIter entry;
    DBusMessageIter variant;
    DBusMessageIter pair;
    if (!dbus_message_iter_open_container(&parent, DBUS_TYPE_DICT_ENTRY, nullptr, &entry))
        return false;
    const char *name = propertyName;
    bool ok = dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &name) &&
              dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "(so)", &variant) &&
              dbus_message_iter_open_container(&variant, DBUS_TYPE_STRUCT, nullptr, &pair) &&
              append_string(pair, busName) && append_path(pair, path) &&
              dbus_message_iter_close_container(&variant, &pair) &&
              dbus_message_iter_close_container(&entry, &variant);
    return dbus_message_iter_close_container(&parent, &entry) && ok;
}

void append_property_values(DBusMessageIter &array, const ResolvedNode &node,
                            const char *appBusName, const char *nodePath,
                            std::int32_t applicationId, const char *requestedInterface)
{
    const std::string name = node.root ? "Platformator Editor" : node.node->name;
    const std::string description = node.root ? "Editor workspace" : description_of(*node.node);
    const std::string id = node.root ? "platformator-editor" : node.node->id;
    const char *nameValue = name.c_str();
    const char *descriptionValue = description.c_str();
    const char *locale = "C.UTF-8";
    const std::uint32_t interfaceVersion = 1;
    const std::uint32_t accessibleVersion = 1;
    const bool includeApplication = node.root &&
        std::strcmp(requestedInterface, ApplicationInterface) == 0;
    const bool includeAccessible = std::strcmp(requestedInterface, AccessibleInterface) == 0;
    if (includeApplication)
    {
        const char *toolkit = "Dear ImGui";
        const char *toolkitVersion = "1.92.9b";
        const char *atspiVersion = "2.1";
        append_variant_basic(array, "Version", "s", DBUS_TYPE_STRING, &toolkitVersion);
        append_variant_basic(array, "ToolkitName", "s", DBUS_TYPE_STRING, &toolkit);
        append_variant_basic(array, "ToolkitVersion", "s", DBUS_TYPE_STRING, &toolkitVersion);
        append_variant_basic(array, "AtspiVersion", "s", DBUS_TYPE_STRING, &atspiVersion);
        append_variant_basic(array, "InterfaceVersion", "u", DBUS_TYPE_UINT32, &interfaceVersion);
        append_variant_basic(array, "Id", "i", DBUS_TYPE_INT32, &applicationId);
    }
    if (includeAccessible)
    {
        append_variant_basic(array, "version", "u", DBUS_TYPE_UINT32, &accessibleVersion);
        append_variant_basic(array, "Name", "s", DBUS_TYPE_STRING, &nameValue);
        append_variant_basic(array, "Description", "s", DBUS_TYPE_STRING, &descriptionValue);
        append_variant_basic(array, "Locale", "s", DBUS_TYPE_STRING, &locale);
        const char *idValue = id.c_str();
        append_variant_basic(array, "AccessibleId", "s", DBUS_TYPE_STRING, &idValue);
        if (!node.root)
        {
            const auto parentPath = node.node->parentId.empty()
                ? std::string(AccessibleRootPath)
                : node_path(node.snapshot->hostLayoutId, node.snapshot->hostGeneration,
                            node.node->parentId);
            append_variant_reference(array, "Parent", appBusName, parentPath.c_str());
            const auto childCount = static_cast<std::int32_t>(children_of(*node.snapshot, node.node).size());
            append_variant_basic(array, "ChildCount", "i", DBUS_TYPE_INT32, &childCount);
            append_variant_basic(array, "HelpText", "s", DBUS_TYPE_STRING, &descriptionValue);
        }
        else
        {
            const char *nullBus = "";
            append_variant_reference(array, "Parent", nullBus, NullPath);
            const auto childCount = static_cast<std::int32_t>(children_of(*node.snapshot, nullptr).size());
            append_variant_basic(array, "ChildCount", "i", DBUS_TYPE_INT32, &childCount);
        }
    }
    (void)nodePath;
}

DBusHandlerResult return_property(DBusConnection *connection, DBusMessage *request,
                                  const ResolvedNode &node, const char *appBusName,
                                  const char *requestedInterface, const char *property,
                                  std::int32_t applicationId)
{
    const std::string name = node.root ? "Platformator Editor" : node.node->name;
    const std::string description = node.root ? "Editor workspace" : description_of(*node.node);
    const std::string accessibleId = node.root ? "platformator-editor" : node.node->id;
    const auto childCount = static_cast<std::int32_t>(children_of(*node.snapshot,
        node.root ? nullptr : node.node).size());
    const char *nameValue = name.c_str();
    const char *descriptionValue = description.c_str();
    const char *accessibleIdValue = accessibleId.c_str();
    const char *locale = "C.UTF-8";
    const char *toolkit = "Dear ImGui";
    const char *toolkitVersion = "1.92.9b";
    const char *atspiVersion = "2.1";
    const std::uint32_t interfaceVersion = 1;
    const std::uint32_t accessibleVersion = 1;
    const std::uint32_t layer = 2;
    const std::int16_t zOrder = 0;
    const double alpha = 1.0;

    const bool accessible = std::strcmp(requestedInterface, AccessibleInterface) == 0;
    const bool application = std::strcmp(requestedInterface, ApplicationInterface) == 0 && node.root;
    const bool component = std::strcmp(requestedInterface, ComponentInterface) == 0;
    if ((accessible && std::strcmp(property, "Name") == 0) ||
        (accessible && std::strcmp(property, "Description") == 0) ||
        (accessible && std::strcmp(property, "Locale") == 0) ||
        (accessible && std::strcmp(property, "AccessibleId") == 0) ||
        (accessible && std::strcmp(property, "HelpText") == 0) ||
        (application && (std::strcmp(property, "ToolkitName") == 0 ||
                         std::strcmp(property, "ToolkitVersion") == 0 ||
                         std::strcmp(property, "AtspiVersion") == 0 ||
                         std::strcmp(property, "Version") == 0)))
    {
        const char *value = nameValue;
        if (std::strcmp(property, "Description") == 0 || std::strcmp(property, "HelpText") == 0)
            value = descriptionValue;
        else if (std::strcmp(property, "Locale") == 0)
            value = locale;
        else if (std::strcmp(property, "AccessibleId") == 0)
            value = accessibleIdValue;
        else if (std::strcmp(property, "ToolkitName") == 0)
            value = toolkit;
        else if (std::strcmp(property, "ToolkitVersion") == 0)
            value = toolkitVersion;
        else if (std::strcmp(property, "AtspiVersion") == 0)
            value = atspiVersion;
        else if (std::strcmp(property, "Version") == 0)
            value = toolkitVersion;
        DBusMessage *reply = dbus_message_new_method_return(request);
        if (reply == nullptr)
            return DBUS_HANDLER_RESULT_NEED_MEMORY;
        DBusMessageIter out, variant;
        dbus_message_iter_init_append(reply, &out);
        bool ok = dbus_message_iter_open_container(&out, DBUS_TYPE_VARIANT, "s", &variant) &&
                  dbus_message_iter_append_basic(&variant, DBUS_TYPE_STRING, &value) &&
                  dbus_message_iter_close_container(&out, &variant);
        dbus_connection_send(connection, reply, nullptr);
        dbus_message_unref(reply);
        return ok ? DBUS_HANDLER_RESULT_HANDLED : DBUS_HANDLER_RESULT_NEED_MEMORY;
    }
    if (accessible && std::strcmp(property, "version") == 0)
        return append_basic_return(connection, request, DBUS_TYPE_UINT32, &accessibleVersion)
            ? DBUS_HANDLER_RESULT_HANDLED : DBUS_HANDLER_RESULT_NEED_MEMORY;
    if (accessible && std::strcmp(property, "ChildCount") == 0)
    {
        DBusMessage *reply = dbus_message_new_method_return(request);
        if (reply == nullptr)
            return DBUS_HANDLER_RESULT_NEED_MEMORY;
        DBusMessageIter out, variant;
        dbus_message_iter_init_append(reply, &out);
        bool ok = dbus_message_iter_open_container(&out, DBUS_TYPE_VARIANT, "i", &variant) &&
                  dbus_message_iter_append_basic(&variant, DBUS_TYPE_INT32, &childCount) &&
                  dbus_message_iter_close_container(&out, &variant);
        dbus_connection_send(connection, reply, nullptr);
        dbus_message_unref(reply);
        return ok ? DBUS_HANDLER_RESULT_HANDLED : DBUS_HANDLER_RESULT_NEED_MEMORY;
    }
    if (accessible && std::strcmp(property, "Parent") == 0)
    {
        const std::string parentPath = node.root ? NullPath : node.node->parentId.empty()
            ? AccessibleRootPath
            : node_path(node.snapshot->hostLayoutId, node.snapshot->hostGeneration,
                        node.node->parentId);
        const char *parentBus = node.root ? "" : appBusName;
        DBusMessage *reply = dbus_message_new_method_return(request);
        if (reply == nullptr)
            return DBUS_HANDLER_RESULT_NEED_MEMORY;
        DBusMessageIter out, variant, pair;
        dbus_message_iter_init_append(reply, &out);
        bool ok = dbus_message_iter_open_container(&out, DBUS_TYPE_VARIANT, "(so)", &variant) &&
                  dbus_message_iter_open_container(&variant, DBUS_TYPE_STRUCT, nullptr, &pair) &&
                  append_string(pair, parentBus) && append_path(pair, parentPath.c_str()) &&
                  dbus_message_iter_close_container(&variant, &pair) &&
                  dbus_message_iter_close_container(&out, &variant);
        dbus_connection_send(connection, reply, nullptr);
        dbus_message_unref(reply);
        return ok ? DBUS_HANDLER_RESULT_HANDLED : DBUS_HANDLER_RESULT_NEED_MEMORY;
    }
    if (application && std::strcmp(property, "InterfaceVersion") == 0)
    {
        DBusMessage *reply = dbus_message_new_method_return(request);
        if (reply == nullptr)
            return DBUS_HANDLER_RESULT_NEED_MEMORY;
        DBusMessageIter out, variant;
        dbus_message_iter_init_append(reply, &out);
        bool ok = dbus_message_iter_open_container(&out, DBUS_TYPE_VARIANT, "u", &variant) &&
                  dbus_message_iter_append_basic(&variant, DBUS_TYPE_UINT32, &interfaceVersion) &&
                  dbus_message_iter_close_container(&out, &variant);
        dbus_connection_send(connection, reply, nullptr);
        dbus_message_unref(reply);
        return ok ? DBUS_HANDLER_RESULT_HANDLED : DBUS_HANDLER_RESULT_NEED_MEMORY;
    }
    if (application && std::strcmp(property, "Id") == 0)
    {
        DBusMessage *reply = dbus_message_new_method_return(request);
        if (reply == nullptr)
            return DBUS_HANDLER_RESULT_NEED_MEMORY;
        DBusMessageIter out, variant;
        dbus_message_iter_init_append(reply, &out);
        bool ok = dbus_message_iter_open_container(&out, DBUS_TYPE_VARIANT, "i", &variant) &&
                  dbus_message_iter_append_basic(&variant, DBUS_TYPE_INT32, &applicationId) &&
                  dbus_message_iter_close_container(&out, &variant);
        dbus_connection_send(connection, reply, nullptr);
        dbus_message_unref(reply);
        return ok ? DBUS_HANDLER_RESULT_HANDLED : DBUS_HANDLER_RESULT_NEED_MEMORY;
    }
    if (component && std::strcmp(property, "Layer") == 0)
    {
        DBusMessage *reply = dbus_message_new_method_return(request);
        if (reply == nullptr)
            return DBUS_HANDLER_RESULT_NEED_MEMORY;
        DBusMessageIter out, variant;
        dbus_message_iter_init_append(reply, &out);
        bool ok = dbus_message_iter_open_container(&out, DBUS_TYPE_VARIANT, "u", &variant) &&
                  dbus_message_iter_append_basic(&variant, DBUS_TYPE_UINT32, &layer) &&
                  dbus_message_iter_close_container(&out, &variant);
        dbus_connection_send(connection, reply, nullptr);
        dbus_message_unref(reply);
        return ok ? DBUS_HANDLER_RESULT_HANDLED : DBUS_HANDLER_RESULT_NEED_MEMORY;
    }
    if (component && std::strcmp(property, "MdiZOrder") == 0)
    {
        DBusMessage *reply = dbus_message_new_method_return(request);
        if (reply == nullptr)
            return DBUS_HANDLER_RESULT_NEED_MEMORY;
        DBusMessageIter out, variant;
        dbus_message_iter_init_append(reply, &out);
        bool ok = dbus_message_iter_open_container(&out, DBUS_TYPE_VARIANT, "n", &variant) &&
                  dbus_message_iter_append_basic(&variant, DBUS_TYPE_INT16, &zOrder) &&
                  dbus_message_iter_close_container(&out, &variant);
        dbus_connection_send(connection, reply, nullptr);
        dbus_message_unref(reply);
        return ok ? DBUS_HANDLER_RESULT_HANDLED : DBUS_HANDLER_RESULT_NEED_MEMORY;
    }
    if (component && std::strcmp(property, "Alpha") == 0)
    {
        DBusMessage *reply = dbus_message_new_method_return(request);
        if (reply == nullptr)
            return DBUS_HANDLER_RESULT_NEED_MEMORY;
        DBusMessageIter out, variant;
        dbus_message_iter_init_append(reply, &out);
        bool ok = dbus_message_iter_open_container(&out, DBUS_TYPE_VARIANT, "d", &variant) &&
                  dbus_message_iter_append_basic(&variant, DBUS_TYPE_DOUBLE, &alpha) &&
                  dbus_message_iter_close_container(&out, &variant);
        dbus_connection_send(connection, reply, nullptr);
        dbus_message_unref(reply);
        return ok ? DBUS_HANDLER_RESULT_HANDLED : DBUS_HANDLER_RESULT_NEED_MEMORY;
    }
    return send_error(connection, request, DBUS_ERROR_INVALID_ARGS,
                      "The requested AT-SPI property is not supported by this object.");
}
} // namespace

struct LinuxAccessibilityProvider::Impl
{
    AccessibilitySnapshots *snapshots = nullptr;
    HostLayoutId host;
    WindowHostGeneration generation;
    DBusConnection *connection = nullptr;
    std::string appBusName;
    std::string accessibilityBusAddress;
    std::shared_ptr<const SemanticSnapshot> initialRootSnapshot;
    std::int32_t applicationId = 0;
    std::thread dispatcher;
    std::atomic<bool> stopRequested{false};
    std::atomic<bool> isRunning{false};
    std::mutex errorMutex;
    std::string dispatchError;

    std::shared_ptr<const SemanticSnapshot> merged_snapshot() const
    {
        if (snapshots == nullptr)
            return initialRootSnapshot;
        const auto registered = snapshots->acquire_registered();
        if (registered.empty())
            return initialRootSnapshot;
        auto merged = std::make_shared<SemanticSnapshot>();
        merged->hostLayoutId = host;
        merged->hostGeneration = generation;
        merged->revision = 1;
        std::size_t nodeCount = 0;
        for (const auto &snapshot : registered)
            nodeCount += snapshot->nodes.size();
        merged->nodes.reserve(nodeCount);
        for (const auto &snapshot : registered)
            merged->nodes.insert(merged->nodes.end(), snapshot->nodes.begin(), snapshot->nodes.end());
        return merged;
    }

    std::string path_for_child(const ResolvedNode &parent, const SemanticNode &child) const
    {
        if (!parent.root || snapshots == nullptr)
            return node_path(parent.snapshot->hostLayoutId, parent.snapshot->hostGeneration,
                             child.id);
        for (const auto &snapshot : snapshots->acquire_registered())
            if (std::any_of(snapshot->nodes.begin(), snapshot->nodes.end(), [&](const auto &node) {
                    return node.id == child.id;
                }))
                return node_path(snapshot->hostLayoutId, snapshot->hostGeneration, child.id);
        return node_path(parent.snapshot->hostLayoutId, parent.snapshot->hostGeneration, child.id);
    }

    ResolvedNode resolve(const char *path) const
    {
        ResolvedNode result;
        if (snapshots == nullptr || path == nullptr)
            return result;
        if (std::strcmp(path, AccessibleRootPath) == 0)
        {
            result.snapshot = merged_snapshot();
            result.root = true;
            return result;
        }
        for (const auto &snapshot : snapshots->acquire_registered())
            for (const auto &node : snapshot->nodes)
                if (node_path(snapshot->hostLayoutId, snapshot->hostGeneration, node.id) == path)
                {
                    result.snapshot = snapshot;
                    result.node = &node;
                    return result;
                }
        return result;
    }

    static DBusHandlerResult handle_message(DBusConnection *connection, DBusMessage *request,
                                            void *userData)
    {
        auto *self = static_cast<Impl *>(userData);
        try
        {
            return self->handle(connection, request);
        }
        catch (...)
        {
            return send_error(connection, request, DBUS_ERROR_FAILED,
                              "The editor could not prepare this accessibility response.");
        }
    }

    DBusHandlerResult handle(DBusConnection *bus, DBusMessage *request)
    {
        const char *interfaceName = dbus_message_get_interface(request);
        const char *member = dbus_message_get_member(request);
        const char *path = dbus_message_get_path(request);
        if (interfaceName == nullptr || member == nullptr)
            return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
        if (std::strcmp(interfaceName, IntrospectableInterface) == 0 &&
            std::strcmp(member, "Introspect") == 0)
        {
            static constexpr char xml[] =
                "<node>"
                "<interface name='org.freedesktop.DBus.Introspectable'><method name='Introspect'>"
                "<arg name='data' type='s' direction='out'/></method></interface>"
                "<interface name='org.a11y.atspi.Accessible'/>"
                "<interface name='org.a11y.atspi.Application'/>"
                "<interface name='org.a11y.atspi.Action'/>"
                "<interface name='org.a11y.atspi.Text'/>"
                "<interface name='org.a11y.atspi.EditableText'/>"
                "<interface name='org.a11y.atspi.Component'/>"
                "<interface name='org.a11y.atspi.Value'/>"
                "</node>";
            const char *xmlValue = xml;
            return append_basic_return(bus, request, DBUS_TYPE_STRING, &xmlValue)
                ? DBUS_HANDLER_RESULT_HANDLED : DBUS_HANDLER_RESULT_NEED_MEMORY;
        }

        if (std::strcmp(interfaceName, PropertiesInterface) == 0 &&
            std::strcmp(member, "Set") == 0)
        {
            DBusMessageIter args;
            if (!dbus_message_iter_init(request, &args) ||
                dbus_message_iter_get_arg_type(&args) != DBUS_TYPE_STRING)
                return send_error(bus, request, DBUS_ERROR_INVALID_ARGS, "Expected an interface name.");
            const char *propertyInterface = nullptr;
            dbus_message_iter_get_basic(&args, &propertyInterface);
            if (!dbus_message_iter_next(&args) || dbus_message_iter_get_arg_type(&args) != DBUS_TYPE_STRING)
                return send_error(bus, request, DBUS_ERROR_INVALID_ARGS, "Expected a property name.");
            const char *property = nullptr;
            dbus_message_iter_get_basic(&args, &property);
            if (!dbus_message_iter_next(&args) || dbus_message_iter_get_arg_type(&args) != DBUS_TYPE_VARIANT)
                return send_error(bus, request, DBUS_ERROR_INVALID_ARGS, "Expected a property value.");
            DBusMessageIter variant;
            dbus_message_iter_recurse(&args, &variant);
            if (path != nullptr && std::strcmp(path, AccessibleRootPath) == 0 &&
                std::strcmp(propertyInterface, ApplicationInterface) == 0 &&
                std::strcmp(property, "Id") == 0 &&
                dbus_message_iter_get_arg_type(&variant) == DBUS_TYPE_INT32)
            {
                dbus_message_iter_get_basic(&variant, &applicationId);
                return send_empty_return(bus, request);
            }
            return send_error(bus, request, DBUS_ERROR_PROPERTY_READ_ONLY,
                              "Only the AT-SPI application ID can be changed by the registry.");
        }

        const ResolvedNode resolved = resolve(path);
        if (!resolved.snapshot)
            return send_error(bus, request, DBUS_ERROR_UNKNOWN_OBJECT,
                              "This accessibility object belongs to a retired host or snapshot.");

        if (std::strcmp(interfaceName, PropertiesInterface) == 0)
        {
            DBusMessageIter args;
            if (!dbus_message_iter_init(request, &args) ||
                dbus_message_iter_get_arg_type(&args) != DBUS_TYPE_STRING)
                return send_error(bus, request, DBUS_ERROR_INVALID_ARGS, "Expected an interface name.");
            const char *propertyInterface = nullptr;
            dbus_message_iter_get_basic(&args, &propertyInterface);
            const char *property = nullptr;
            if (std::strcmp(member, "Get") == 0)
            {
                if (!dbus_message_iter_next(&args) || dbus_message_iter_get_arg_type(&args) != DBUS_TYPE_STRING)
                    return send_error(bus, request, DBUS_ERROR_INVALID_ARGS, "Expected a property name.");
                dbus_message_iter_get_basic(&args, &property);
                return return_property(bus, request, resolved, appBusName.c_str(),
                                       propertyInterface, property, applicationId);
            }
            if (std::strcmp(member, "GetAll") == 0)
            {
                DBusMessage *reply = dbus_message_new_method_return(request);
                if (reply == nullptr)
                    return DBUS_HANDLER_RESULT_NEED_MEMORY;
                DBusMessageIter out, array;
                dbus_message_iter_init_append(reply, &out);
                bool ok = dbus_message_iter_open_container(&out, DBUS_TYPE_ARRAY, "{sv}", &array);
                if (ok && std::strcmp(propertyInterface, AccessibleInterface) == 0)
                    append_property_values(array, resolved, appBusName.c_str(), path,
                                           applicationId, propertyInterface);
                else if (ok && resolved.root && std::strcmp(propertyInterface, ApplicationInterface) == 0)
                    append_property_values(array, resolved, appBusName.c_str(), path,
                                           applicationId, propertyInterface);
                else if (ok && std::strcmp(propertyInterface, ComponentInterface) == 0)
                {
                    const std::uint32_t layer = 2;
                    const std::int16_t zOrder = 0;
                    const double alpha = 1.0;
                    append_variant_basic(array, "Layer", "u", DBUS_TYPE_UINT32, &layer);
                    append_variant_basic(array, "MdiZOrder", "n", DBUS_TYPE_INT16, &zOrder);
                    append_variant_basic(array, "Alpha", "d", DBUS_TYPE_DOUBLE, &alpha);
                }
                else
                {
                    dbus_message_unref(reply);
                    return send_error(bus, request, DBUS_ERROR_INVALID_ARGS,
                                      "The requested interface is not supported by this object.");
                }
                ok = dbus_message_iter_close_container(&out, &array) && ok;
                dbus_connection_send(bus, reply, nullptr);
                dbus_message_unref(reply);
                return ok ? DBUS_HANDLER_RESULT_HANDLED : DBUS_HANDLER_RESULT_NEED_MEMORY;
            }
            return send_error(bus, request, DBUS_ERROR_UNKNOWN_METHOD, "Unsupported Properties method.");
        }

        if (std::strcmp(interfaceName, AccessibleInterface) == 0)
            return handle_accessible(bus, request, resolved, member);
        if (std::strcmp(interfaceName, ApplicationInterface) == 0 && resolved.root)
            return handle_application(bus, request, member);
        if (std::strcmp(interfaceName, ActionInterface) == 0 && !resolved.root)
            return handle_action(bus, request, resolved, member);
        if (std::strcmp(interfaceName, TextInterface) == 0 && !resolved.root &&
            resolved.node->role == SemanticRole::TextField)
            return handle_text(bus, request, resolved, member);
        if (std::strcmp(interfaceName, EditableTextInterface) == 0 && !resolved.root &&
            resolved.node->role == SemanticRole::TextField)
            return handle_editable_text(bus, request, resolved, member);
        if (std::strcmp(interfaceName, ComponentInterface) == 0)
            return handle_component(bus, request, resolved, member);
        if (std::strcmp(interfaceName, ValueInterface) == 0 && !resolved.root)
            return handle_value(bus, request, resolved, member);
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    DBusHandlerResult handle_accessible(DBusConnection *bus, DBusMessage *request,
                                        const ResolvedNode &resolved, const char *member)
    {
        if (std::strcmp(member, "GetChildren") == 0)
        {
            DBusMessage *reply = dbus_message_new_method_return(request);
            if (reply == nullptr)
                return DBUS_HANDLER_RESULT_NEED_MEMORY;
            DBusMessageIter out, array;
            dbus_message_iter_init_append(reply, &out);
            bool ok = dbus_message_iter_open_container(&out, DBUS_TYPE_ARRAY, "(so)", &array);
            if (ok)
                for (const auto *child : children_of(*resolved.snapshot,
                        resolved.root ? nullptr : resolved.node))
                    ok = append_reference(array, appBusName.c_str(),
                        path_for_child(resolved, *child).c_str()) && ok;
            ok = dbus_message_iter_close_container(&out, &array) && ok;
            dbus_connection_send(bus, reply, nullptr);
            dbus_message_unref(reply);
            return ok ? DBUS_HANDLER_RESULT_HANDLED : DBUS_HANDLER_RESULT_NEED_MEMORY;
        }
        if (std::strcmp(member, "GetChildAtIndex") == 0)
        {
            std::int32_t index = -1;
            if (!read_basic_argument(request, DBUS_TYPE_INT32, &index) || index < 0)
                return send_error(bus, request, DBUS_ERROR_INVALID_ARGS, "Invalid child index.");
            const auto children = children_of(*resolved.snapshot, resolved.root ? nullptr : resolved.node);
            if (static_cast<std::size_t>(index) >= children.size())
                return send_error(bus, request, DBUS_ERROR_INVALID_ARGS, "Child index is out of range.");
            DBusMessage *reply = dbus_message_new_method_return(request);
            if (reply == nullptr)
                return DBUS_HANDLER_RESULT_NEED_MEMORY;
            DBusMessageIter out;
            dbus_message_iter_init_append(reply, &out);
            const bool ok = append_reference(out, appBusName.c_str(),
                path_for_child(resolved, *children[static_cast<std::size_t>(index)]).c_str());
            dbus_connection_send(bus, reply, nullptr);
            dbus_message_unref(reply);
            return ok ? DBUS_HANDLER_RESULT_HANDLED : DBUS_HANDLER_RESULT_NEED_MEMORY;
        }
        if (std::strcmp(member, "GetParent") == 0 || std::strcmp(member, "GetApplication") == 0)
        {
            const bool isParent = std::strcmp(member, "GetParent") == 0;
            const char *parentBus = appBusName.c_str();
            std::string parentPath = AccessibleRootPath;
            if (isParent && resolved.root)
            {
                parentBus = "";
                parentPath = NullPath;
            }
            else if (isParent && !resolved.node->parentId.empty())
                parentPath = node_path(resolved.snapshot->hostLayoutId,
                                       resolved.snapshot->hostGeneration,
                                       resolved.node->parentId);
            DBusMessage *reply = dbus_message_new_method_return(request);
            if (reply == nullptr)
                return DBUS_HANDLER_RESULT_NEED_MEMORY;
            DBusMessageIter out;
            dbus_message_iter_init_append(reply, &out);
            const bool ok = append_reference(out, parentBus, parentPath.c_str());
            dbus_connection_send(bus, reply, nullptr);
            dbus_message_unref(reply);
            return ok ? DBUS_HANDLER_RESULT_HANDLED : DBUS_HANDLER_RESULT_NEED_MEMORY;
        }
        if (std::strcmp(member, "GetIndexInParent") == 0)
        {
            std::int32_t index = -1;
            if (!resolved.root)
            {
                const auto siblings = children_of(*resolved.snapshot,
                    resolved.node->parentId.empty() ? nullptr : [&]() -> const SemanticNode * {
                        const auto found = std::find_if(resolved.snapshot->nodes.begin(), resolved.snapshot->nodes.end(),
                            [&](const SemanticNode &node) { return node.id == resolved.node->parentId; });
                        return found == resolved.snapshot->nodes.end() ? nullptr : &*found;
                    }());
                for (std::size_t i = 0; i < siblings.size(); ++i)
                    if (siblings[i] == resolved.node)
                    {
                        index = static_cast<std::int32_t>(i);
                        break;
                    }
            }
            return append_basic_return(bus, request, DBUS_TYPE_INT32, &index)
                ? DBUS_HANDLER_RESULT_HANDLED : DBUS_HANDLER_RESULT_NEED_MEMORY;
        }
        if (std::strcmp(member, "GetName") == 0 || std::strcmp(member, "GetDescription") == 0 ||
            std::strcmp(member, "GetRoleName") == 0 || std::strcmp(member, "GetLocalizedRoleName") == 0)
        {
            std::string value;
            if (std::strcmp(member, "GetName") == 0)
                value = resolved.root ? "Platformator Editor" : resolved.node->name;
            else if (std::strcmp(member, "GetDescription") == 0)
                value = resolved.root ? "Editor workspace" : description_of(*resolved.node);
            else
                value = resolved.root ? "application" : role_name(resolved.node->role);
            const char *pointer = value.c_str();
            return append_basic_return(bus, request, DBUS_TYPE_STRING, &pointer)
                ? DBUS_HANDLER_RESULT_HANDLED : DBUS_HANDLER_RESULT_NEED_MEMORY;
        }
        if (std::strcmp(member, "GetRole") == 0)
        {
            const std::uint32_t role = resolved.root ? 75 : atspi_role(resolved.node->role);
            return append_basic_return(bus, request, DBUS_TYPE_UINT32, &role)
                ? DBUS_HANDLER_RESULT_HANDLED : DBUS_HANDLER_RESULT_NEED_MEMORY;
        }
        if (std::strcmp(member, "GetState") == 0)
        {
            DBusMessage *reply = dbus_message_new_method_return(request);
            if (reply == nullptr)
                return DBUS_HANDLER_RESULT_NEED_MEMORY;
            DBusMessageIter out, array;
            dbus_message_iter_init_append(reply, &out);
            bool ok = dbus_message_iter_open_container(&out, DBUS_TYPE_ARRAY, "u", &array);
            if (ok)
                for (const auto state : states_of(resolved))
                    ok = dbus_message_iter_append_basic(&array, DBUS_TYPE_UINT32, &state) && ok;
            ok = dbus_message_iter_close_container(&out, &array) && ok;
            dbus_connection_send(bus, reply, nullptr);
            dbus_message_unref(reply);
            return ok ? DBUS_HANDLER_RESULT_HANDLED : DBUS_HANDLER_RESULT_NEED_MEMORY;
        }
        if (std::strcmp(member, "GetRelationSet") == 0)
        {
            DBusMessage *reply = dbus_message_new_method_return(request);
            if (reply == nullptr)
                return DBUS_HANDLER_RESULT_NEED_MEMORY;
            DBusMessageIter out, array;
            dbus_message_iter_init_append(reply, &out);
            const bool ok = dbus_message_iter_open_container(&out, DBUS_TYPE_ARRAY, "(ua(so))", &array) &&
                            dbus_message_iter_close_container(&out, &array);
            dbus_connection_send(bus, reply, nullptr);
            dbus_message_unref(reply);
            return ok ? DBUS_HANDLER_RESULT_HANDLED : DBUS_HANDLER_RESULT_NEED_MEMORY;
        }
        if (std::strcmp(member, "GetAttributes") == 0)
        {
            DBusMessage *reply = dbus_message_new_method_return(request);
            if (reply == nullptr)
                return DBUS_HANDLER_RESULT_NEED_MEMORY;
            DBusMessageIter out, array;
            dbus_message_iter_init_append(reply, &out);
            bool ok = dbus_message_iter_open_container(&out, DBUS_TYPE_ARRAY, "{ss}", &array);
            if (ok && !resolved.root)
            {
                DBusMessageIter entry;
                const char *idKey = "id";
                const char *id = resolved.node->id.c_str();
                ok = dbus_message_iter_open_container(&array, DBUS_TYPE_DICT_ENTRY, nullptr, &entry) &&
                     dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &idKey) &&
                     dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &id) &&
                     dbus_message_iter_close_container(&array, &entry);
                if (ok && !resolved.node->value.empty())
                {
                    DBusMessageIter valueEntry;
                    const char *valueKey = "value";
                    const char *value = resolved.node->value.c_str();
                    ok = dbus_message_iter_open_container(&array, DBUS_TYPE_DICT_ENTRY, nullptr, &valueEntry) &&
                         dbus_message_iter_append_basic(&valueEntry, DBUS_TYPE_STRING, &valueKey) &&
                         dbus_message_iter_append_basic(&valueEntry, DBUS_TYPE_STRING, &value) &&
                         dbus_message_iter_close_container(&array, &valueEntry);
                }
            }
            ok = dbus_message_iter_close_container(&out, &array) && ok;
            dbus_connection_send(bus, reply, nullptr);
            dbus_message_unref(reply);
            return ok ? DBUS_HANDLER_RESULT_HANDLED : DBUS_HANDLER_RESULT_NEED_MEMORY;
        }
        if (std::strcmp(member, "GetInterfaces") == 0)
        {
            DBusMessage *reply = dbus_message_new_method_return(request);
            if (reply == nullptr)
                return DBUS_HANDLER_RESULT_NEED_MEMORY;
            DBusMessageIter out, array;
            dbus_message_iter_init_append(reply, &out);
            bool ok = dbus_message_iter_open_container(&out, DBUS_TYPE_ARRAY, "s", &array);
            const char *accessible = AccessibleInterface;
            if (ok)
                ok = dbus_message_iter_append_basic(&array, DBUS_TYPE_STRING, &accessible);
            if (ok && resolved.root)
            {
                const char *application = ApplicationInterface;
                ok = dbus_message_iter_append_basic(&array, DBUS_TYPE_STRING, &application);
            }
            if (ok && !resolved.root && !resolved.node->actions.empty())
            {
                const char *action = ActionInterface;
                ok = dbus_message_iter_append_basic(&array, DBUS_TYPE_STRING, &action);
            }
            if (ok && !resolved.root && resolved.node->role == SemanticRole::TextField)
            {
                const char *text = TextInterface;
                const char *editableText = EditableTextInterface;
                ok = dbus_message_iter_append_basic(&array, DBUS_TYPE_STRING, &text) &&
                     dbus_message_iter_append_basic(&array, DBUS_TYPE_STRING, &editableText);
            }
            if (ok && (resolved.root || (!resolved.root && resolved.node->bounds)))
            {
                const char *component = ComponentInterface;
                ok = dbus_message_iter_append_basic(&array, DBUS_TYPE_STRING, &component);
            }
            if (ok && !resolved.root && resolved.node->minimum && resolved.node->maximum)
            {
                const char *value = ValueInterface;
                ok = dbus_message_iter_append_basic(&array, DBUS_TYPE_STRING, &value);
            }
            ok = dbus_message_iter_close_container(&out, &array) && ok;
            dbus_connection_send(bus, reply, nullptr);
            dbus_message_unref(reply);
            return ok ? DBUS_HANDLER_RESULT_HANDLED : DBUS_HANDLER_RESULT_NEED_MEMORY;
        }
        return send_error(bus, request, DBUS_ERROR_UNKNOWN_METHOD,
                          "Unsupported AT-SPI Accessible method.");
    }

    DBusHandlerResult handle_application(DBusConnection *bus, DBusMessage *request, const char *member)
    {
        if (std::strcmp(member, "GetLocale") == 0)
        {
            const char *locale = "C.UTF-8";
            return append_basic_return(bus, request, DBUS_TYPE_STRING, &locale)
                ? DBUS_HANDLER_RESULT_HANDLED : DBUS_HANDLER_RESULT_NEED_MEMORY;
        }
        if (std::strcmp(member, "GetApplicationBusAddress") == 0)
        {
            const char *address = accessibilityBusAddress.c_str();
            return append_basic_return(bus, request, DBUS_TYPE_STRING, &address)
                ? DBUS_HANDLER_RESULT_HANDLED : DBUS_HANDLER_RESULT_NEED_MEMORY;
        }
        return send_error(bus, request, DBUS_ERROR_UNKNOWN_METHOD,
                          "Unsupported AT-SPI Application method.");
    }

    DBusHandlerResult handle_text(DBusConnection *bus, DBusMessage *request,
                                  const ResolvedNode &resolved, const char *member)
    {
        if (std::strcmp(member, "GetCharacterCount") == 0)
        {
            const auto count = utf8_character_count(resolved.node->value);
            const auto bounded = static_cast<std::int32_t>(std::min<std::size_t>(
                count, static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())));
            return append_basic_return(bus, request, DBUS_TYPE_INT32, &bounded)
                ? DBUS_HANDLER_RESULT_HANDLED : DBUS_HANDLER_RESULT_NEED_MEMORY;
        }
        if (std::strcmp(member, "GetCaretOffset") == 0)
        {
            // The immutable semantic model does not track an editable-text caret.
            const std::int32_t caretOffset = 0;
            return append_basic_return(bus, request, DBUS_TYPE_INT32, &caretOffset)
                ? DBUS_HANDLER_RESULT_HANDLED : DBUS_HANDLER_RESULT_NEED_MEMORY;
        }
        if (std::strcmp(member, "GetNSelections") == 0)
        {
            const std::int32_t selectionCount = 0;
            return append_basic_return(bus, request, DBUS_TYPE_INT32, &selectionCount)
                ? DBUS_HANDLER_RESULT_HANDLED : DBUS_HANDLER_RESULT_NEED_MEMORY;
        }
        if (std::strcmp(member, "GetText") == 0)
        {
            DBusMessageIter args;
            std::int32_t start = 0;
            std::int32_t end = -1;
            if (!dbus_message_iter_init(request, &args) ||
                dbus_message_iter_get_arg_type(&args) != DBUS_TYPE_INT32)
                return send_error(bus, request, DBUS_ERROR_INVALID_ARGS,
                                  "Expected a start character offset.");
            dbus_message_iter_get_basic(&args, &start);
            if (!dbus_message_iter_next(&args) ||
                dbus_message_iter_get_arg_type(&args) != DBUS_TYPE_INT32)
                return send_error(bus, request, DBUS_ERROR_INVALID_ARGS,
                                  "Expected an end character offset.");
            dbus_message_iter_get_basic(&args, &end);
            if (start < 0 || end < -1)
                return send_error(bus, request, DBUS_ERROR_INVALID_ARGS,
                                  "Text character offsets must be nonnegative or use -1 for the end.");
            const std::string_view text(resolved.node->value);
            const auto characterCount = utf8_character_count(text);
            const auto firstCharacter = std::min<std::size_t>(
                static_cast<std::size_t>(start), characterCount);
            const auto lastCharacter = end < 0 ? characterCount : std::min<std::size_t>(
                static_cast<std::size_t>(end), characterCount);
            const auto firstByte = utf8_byte_offset(text, firstCharacter);
            const auto lastByte = utf8_byte_offset(text, std::max(firstCharacter, lastCharacter));
            const std::string result(text.substr(firstByte, lastByte - firstByte));
            const char *value = result.c_str();
            return append_basic_return(bus, request, DBUS_TYPE_STRING, &value)
                ? DBUS_HANDLER_RESULT_HANDLED : DBUS_HANDLER_RESULT_NEED_MEMORY;
        }
        return send_error(bus, request, DBUS_ERROR_UNKNOWN_METHOD,
                          "Unsupported AT-SPI Text method.");
    }

    DBusHandlerResult handle_editable_text(DBusConnection *bus, DBusMessage *request,
                                          const ResolvedNode &resolved, const char *member)
    {
        if (std::strcmp(member, "SetTextContents") != 0)
            return send_error(bus, request, DBUS_ERROR_UNKNOWN_METHOD,
                              "Unsupported AT-SPI EditableText method.");
        const char *replacement = nullptr;
        if (!read_basic_argument(request, DBUS_TYPE_STRING, &replacement))
            return send_error(bus, request, DBUS_ERROR_INVALID_ARGS,
                              "Expected replacement UTF-8 text.");
        const std::size_t length = ::strnlen(replacement,
            AccessibilitySnapshots::MaximumActionValueBytes + 1);
        const bool accepted = length <= AccessibilitySnapshots::MaximumActionValueBytes &&
            resolved.node->enabled && snapshots != nullptr &&
            snapshots->enqueue_action({resolved.snapshot->hostLayoutId,
                resolved.snapshot->hostGeneration, resolved.snapshot->revision,
                resolved.node->id, "set_value", std::string(replacement, length)});
        const dbus_bool_t result = accepted ? 1 : 0;
        return append_basic_return(bus, request, DBUS_TYPE_BOOLEAN, &result)
            ? DBUS_HANDLER_RESULT_HANDLED : DBUS_HANDLER_RESULT_NEED_MEMORY;
    }

    DBusHandlerResult handle_action(DBusConnection *bus, DBusMessage *request,
                                    const ResolvedNode &resolved, const char *member)
    {
        const auto &actions = resolved.node->actions;
        if (std::strcmp(member, "GetNActions") == 0)
        {
            const std::int32_t count = static_cast<std::int32_t>(actions.size());
            return append_basic_return(bus, request, DBUS_TYPE_INT32, &count)
                ? DBUS_HANDLER_RESULT_HANDLED : DBUS_HANDLER_RESULT_NEED_MEMORY;
        }
        std::int32_t index = -1;
        if (!read_basic_argument(request, DBUS_TYPE_INT32, &index) || index < 0 ||
            static_cast<std::size_t>(index) >= actions.size())
            return send_error(bus, request, DBUS_ERROR_INVALID_ARGS, "Action index is out of range.");
        const auto &action = actions[static_cast<std::size_t>(index)];
        if (std::strcmp(member, "DoAction") == 0)
        {
            const dbus_bool_t accepted = resolved.node->visible && resolved.node->enabled && snapshots != nullptr &&
                snapshots->enqueue_action({resolved.snapshot->hostLayoutId,
                                           resolved.snapshot->hostGeneration,
                                           resolved.snapshot->revision,
                                           resolved.node->id, action, {}}) ? 1 : 0;
            return append_basic_return(bus, request, DBUS_TYPE_BOOLEAN, &accepted)
                ? DBUS_HANDLER_RESULT_HANDLED : DBUS_HANDLER_RESULT_NEED_MEMORY;
        }
        if (std::strcmp(member, "GetActionName") == 0 ||
            std::strcmp(member, "GetLocalizedName") == 0 ||
            std::strcmp(member, "GetActionDescription") == 0 ||
            std::strcmp(member, "GetKeyBinding") == 0)
        {
            const std::string value = std::strcmp(member, "GetActionDescription") == 0
                ? (resolved.node->help.empty() ? action + " " + resolved.node->name : resolved.node->help)
                : std::strcmp(member, "GetKeyBinding") == 0 ? std::string{} : action;
            const char *pointer = value.c_str();
            return append_basic_return(bus, request, DBUS_TYPE_STRING, &pointer)
                ? DBUS_HANDLER_RESULT_HANDLED : DBUS_HANDLER_RESULT_NEED_MEMORY;
        }
        return send_error(bus, request, DBUS_ERROR_UNKNOWN_METHOD, "Unsupported AT-SPI Action method.");
    }

    DBusHandlerResult handle_component(DBusConnection *bus, DBusMessage *request,
                                       const ResolvedNode &resolved, const char *member)
    {
        const auto bounds = !resolved.root && resolved.node->bounds
            ? *resolved.node->bounds : SemanticBounds{};
        if (std::strcmp(member, "GetExtents") == 0 || std::strcmp(member, "GetPosition") == 0 ||
            std::strcmp(member, "GetSize") == 0)
        {
            DBusMessage *reply = dbus_message_new_method_return(request);
            if (reply == nullptr)
                return DBUS_HANDLER_RESULT_NEED_MEMORY;
            DBusMessageIter out;
            dbus_message_iter_init_append(reply, &out);
            bool ok = true;
            if (std::strcmp(member, "GetExtents") == 0)
            {
                const std::int32_t x = static_cast<std::int32_t>(std::clamp(bounds.x,
                    static_cast<double>(std::numeric_limits<std::int32_t>::min()),
                    static_cast<double>(std::numeric_limits<std::int32_t>::max())));
                const std::int32_t y = static_cast<std::int32_t>(std::clamp(bounds.y,
                    static_cast<double>(std::numeric_limits<std::int32_t>::min()),
                    static_cast<double>(std::numeric_limits<std::int32_t>::max())));
                const std::int32_t width = static_cast<std::int32_t>(std::clamp(bounds.width, 0.0,
                    static_cast<double>(std::numeric_limits<std::int32_t>::max())));
                const std::int32_t height = static_cast<std::int32_t>(std::clamp(bounds.height, 0.0,
                    static_cast<double>(std::numeric_limits<std::int32_t>::max())));
                ok = dbus_message_iter_append_basic(&out, DBUS_TYPE_INT32, &x) &&
                     dbus_message_iter_append_basic(&out, DBUS_TYPE_INT32, &y) &&
                     dbus_message_iter_append_basic(&out, DBUS_TYPE_INT32, &width) &&
                     dbus_message_iter_append_basic(&out, DBUS_TYPE_INT32, &height);
            }
            else
            {
                const std::int32_t x = static_cast<std::int32_t>(std::clamp(bounds.x,
                    static_cast<double>(std::numeric_limits<std::int32_t>::min()),
                    static_cast<double>(std::numeric_limits<std::int32_t>::max())));
                const std::int32_t y = static_cast<std::int32_t>(std::clamp(bounds.y,
                    static_cast<double>(std::numeric_limits<std::int32_t>::min()),
                    static_cast<double>(std::numeric_limits<std::int32_t>::max())));
                const std::int32_t width = static_cast<std::int32_t>(std::clamp(bounds.width, 0.0,
                    static_cast<double>(std::numeric_limits<std::int32_t>::max())));
                const std::int32_t height = static_cast<std::int32_t>(std::clamp(bounds.height, 0.0,
                    static_cast<double>(std::numeric_limits<std::int32_t>::max())));
                if (std::strcmp(member, "GetPosition") == 0)
                    ok = dbus_message_iter_append_basic(&out, DBUS_TYPE_INT32, &x) &&
                         dbus_message_iter_append_basic(&out, DBUS_TYPE_INT32, &y);
                else
                    ok = dbus_message_iter_append_basic(&out, DBUS_TYPE_INT32, &width) &&
                         dbus_message_iter_append_basic(&out, DBUS_TYPE_INT32, &height);
            }
            dbus_connection_send(bus, reply, nullptr);
            dbus_message_unref(reply);
            return ok ? DBUS_HANDLER_RESULT_HANDLED : DBUS_HANDLER_RESULT_NEED_MEMORY;
        }
        if (std::strcmp(member, "Contains") == 0)
        {
            DBusMessageIter args;
            double x = 0.0, y = 0.0;
            std::uint32_t coordinateType = 0;
            if (!dbus_message_iter_init(request, &args) ||
                dbus_message_iter_get_arg_type(&args) != DBUS_TYPE_DOUBLE)
                return send_error(bus, request, DBUS_ERROR_INVALID_ARGS, "Expected a point.");
            dbus_message_iter_get_basic(&args, &x);
            dbus_message_iter_next(&args);
            dbus_message_iter_get_basic(&args, &y);
            dbus_message_iter_next(&args);
            dbus_message_iter_get_basic(&args, &coordinateType);
            (void)coordinateType;
            const dbus_bool_t contains = x >= bounds.x && y >= bounds.y &&
                x < bounds.x + bounds.width && y < bounds.y + bounds.height ? 1 : 0;
            return append_basic_return(bus, request, DBUS_TYPE_BOOLEAN, &contains)
                ? DBUS_HANDLER_RESULT_HANDLED : DBUS_HANDLER_RESULT_NEED_MEMORY;
        }
        if (std::strcmp(member, "GetAccessibleAtPoint") == 0)
        {
            DBusMessageIter args;
            double x = 0.0, y = 0.0;
            std::uint32_t coordinateType = 0;
            if (!dbus_message_iter_init(request, &args) ||
                dbus_message_iter_get_arg_type(&args) != DBUS_TYPE_DOUBLE)
                return send_error(bus, request, DBUS_ERROR_INVALID_ARGS, "Expected a point.");
            dbus_message_iter_get_basic(&args, &x);
            dbus_message_iter_next(&args);
            dbus_message_iter_get_basic(&args, &y);
            dbus_message_iter_next(&args);
            dbus_message_iter_get_basic(&args, &coordinateType);
            (void)coordinateType;
            const SemanticNode *hit = nullptr;
            double hitArea = std::numeric_limits<double>::max();
            for (const auto &node : resolved.snapshot->nodes)
                if (node.visible && node.bounds && x >= node.bounds->x && y >= node.bounds->y &&
                    x < node.bounds->x + node.bounds->width && y < node.bounds->y + node.bounds->height)
                {
                    const double area = node.bounds->width * node.bounds->height;
                    if (area < hitArea)
                    {
                        hit = &node;
                        hitArea = area;
                    }
                }
            DBusMessage *reply = dbus_message_new_method_return(request);
            if (reply == nullptr)
                return DBUS_HANDLER_RESULT_NEED_MEMORY;
            DBusMessageIter out;
            dbus_message_iter_init_append(reply, &out);
            const bool ok = append_reference(out, hit == nullptr ? "" : appBusName.c_str(),
                hit == nullptr ? NullPath : node_path(resolved.snapshot->hostLayoutId,
                    resolved.snapshot->hostGeneration, hit->id).c_str());
            dbus_connection_send(bus, reply, nullptr);
            dbus_message_unref(reply);
            return ok ? DBUS_HANDLER_RESULT_HANDLED : DBUS_HANDLER_RESULT_NEED_MEMORY;
        }
        if (std::strcmp(member, "GetLayer") == 0)
        {
            const std::uint32_t layer = 2;
            return append_basic_return(bus, request, DBUS_TYPE_UINT32, &layer)
                ? DBUS_HANDLER_RESULT_HANDLED : DBUS_HANDLER_RESULT_NEED_MEMORY;
        }
        if (std::strcmp(member, "GetMDIZOrder") == 0)
        {
            const std::int16_t order = 0;
            return append_basic_return(bus, request, DBUS_TYPE_INT16, &order)
                ? DBUS_HANDLER_RESULT_HANDLED : DBUS_HANDLER_RESULT_NEED_MEMORY;
        }
        if (std::strcmp(member, "GetAlpha") == 0)
        {
            const double alpha = 1.0;
            return append_basic_return(bus, request, DBUS_TYPE_DOUBLE, &alpha)
                ? DBUS_HANDLER_RESULT_HANDLED : DBUS_HANDLER_RESULT_NEED_MEMORY;
        }
        if (std::strcmp(member, "GrabFocus") == 0)
        {
            const dbus_bool_t grabbed = 0;
            return append_basic_return(bus, request, DBUS_TYPE_BOOLEAN, &grabbed)
                ? DBUS_HANDLER_RESULT_HANDLED : DBUS_HANDLER_RESULT_NEED_MEMORY;
        }
        return send_error(bus, request, DBUS_ERROR_UNKNOWN_METHOD,
                          "Unsupported AT-SPI Component method.");
    }

    DBusHandlerResult handle_value(DBusConnection *bus, DBusMessage *request,
                                   const ResolvedNode &resolved, const char *member)
    {
        const auto numericValue = [](const std::string &text) -> std::optional<double> {
            if (text.empty())
                return std::nullopt;
            char *end = nullptr;
            const double value = std::strtod(text.c_str(), &end);
            if (end == text.c_str() || *end != '\0' || !std::isfinite(value))
                return std::nullopt;
            return value;
        };
        if (std::strcmp(member, "SetCurrentValue") == 0)
        {
            DBusMessageIter args;
            double value = 0.0;
            if (!dbus_message_iter_init(request, &args) ||
                dbus_message_iter_get_arg_type(&args) != DBUS_TYPE_DOUBLE)
                return send_error(bus, request, DBUS_ERROR_INVALID_ARGS, "Expected a numeric value.");
            dbus_message_iter_get_basic(&args, &value);
            const bool inRange = std::isfinite(value) &&
                (!resolved.node->minimum || value >= *resolved.node->minimum) &&
                (!resolved.node->maximum || value <= *resolved.node->maximum);
            std::array<char, 64> formatted{};
            const auto conversion = inRange
                ? std::to_chars(formatted.data(), formatted.data() + formatted.size(), value,
                                std::chars_format::general, std::numeric_limits<double>::max_digits10)
                : std::to_chars_result{formatted.data(), std::errc::invalid_argument};
            const bool formattedSuccessfully = conversion.ec == std::errc{};
            const dbus_bool_t accepted = resolved.node->enabled && inRange && formattedSuccessfully &&
                snapshots != nullptr && snapshots->enqueue_action({resolved.snapshot->hostLayoutId,
                    resolved.snapshot->hostGeneration,
                    resolved.snapshot->revision, resolved.node->id, "set_value",
                    std::string(formatted.data(), conversion.ptr)}) ? 1 : 0;
            return append_basic_return(bus, request, DBUS_TYPE_BOOLEAN, &accepted)
                ? DBUS_HANDLER_RESULT_HANDLED : DBUS_HANDLER_RESULT_NEED_MEMORY;
        }
        std::optional<double> value;
        if (std::strcmp(member, "GetCurrentValue") == 0)
            value = numericValue(resolved.node->value);
        else if (std::strcmp(member, "GetMinimumValue") == 0 && resolved.node->minimum)
            value = *resolved.node->minimum;
        else if (std::strcmp(member, "GetMaximumValue") == 0 && resolved.node->maximum)
            value = *resolved.node->maximum;
        else if (std::strcmp(member, "GetMinimumIncrement") == 0)
            value = 0.0;
        if (value)
            return append_basic_return(bus, request, DBUS_TYPE_DOUBLE, &*value)
                ? DBUS_HANDLER_RESULT_HANDLED : DBUS_HANDLER_RESULT_NEED_MEMORY;
        return send_error(bus, request, DBUS_ERROR_UNKNOWN_METHOD,
                          "This semantic node does not expose the requested numeric value.");
    }

    static DBusHandlerResult object_message(DBusConnection *connection, DBusMessage *message,
                                            void *userData)
    {
        return handle_message(connection, message, userData);
    }

    static void run_dispatcher(Impl *self)
    {
        self->isRunning.store(true, std::memory_order_release);
        while (!self->stopRequested.load(std::memory_order_acquire) &&
               dbus_connection_read_write_dispatch(self->connection, 50))
        {
        }
        self->isRunning.store(false, std::memory_order_release);
    }
};

namespace
{
const DBusObjectPathVTable AccessibilityVTable = {
    nullptr,
    &LinuxAccessibilityProvider::Impl::object_message,
    nullptr,
    nullptr,
    nullptr,
    nullptr};

std::string dbus_error_text(const DBusError &error, const char *fallback)
{
    return dbus_error_is_set(&error) && error.message != nullptr ? error.message : fallback;
}

void close_connection(DBusConnection *&connection)
{
    if (connection == nullptr)
        return;
    dbus_connection_close(connection);
    dbus_connection_unref(connection);
    connection = nullptr;
}
} // namespace

LinuxAccessibilityProvider::LinuxAccessibilityProvider() : impl_(std::make_unique<Impl>())
{
}

LinuxAccessibilityProvider::~LinuxAccessibilityProvider()
{
    stop();
}

bool LinuxAccessibilityProvider::start(AccessibilitySnapshots &snapshots, HostLayoutId host,
                                       WindowHostGeneration generation, std::string &error)
{
    if (impl_->dispatcher.joinable() || impl_->connection != nullptr)
    {
        error = "The Linux AT-SPI provider is already started.";
        return false;
    }
    if (!host || !generation)
    {
        error = "The Linux AT-SPI provider requires a valid host incarnation.";
        return false;
    }
    if (!dbus_threads_init_default())
    {
        error = "libdbus thread support could not be initialized.";
        return false;
    }

    DBusError dbusError;
    dbus_error_init(&dbusError);
    DBusConnection *session = dbus_bus_get_private(DBUS_BUS_SESSION, &dbusError);
    if (session == nullptr)
    {
        error = "Could not connect to the session bus: " + dbus_error_text(dbusError, "unknown error");
        dbus_error_free(&dbusError);
        return false;
    }
    dbus_connection_set_exit_on_disconnect(session, false);
    DBusMessage *addressRequest = dbus_message_new_method_call(
        "org.a11y.Bus", "/org/a11y/bus", "org.a11y.Bus", "GetAddress");
    DBusMessage *addressReply = addressRequest != nullptr
        ? dbus_connection_send_with_reply_and_block(session, addressRequest, 1000, &dbusError) : nullptr;
    if (addressRequest != nullptr)
        dbus_message_unref(addressRequest);
    const char *address = nullptr;
    const bool addressOk = addressReply != nullptr &&
        dbus_message_get_args(addressReply, &dbusError, DBUS_TYPE_STRING, &address, DBUS_TYPE_INVALID);
    if (addressOk && address != nullptr)
        impl_->accessibilityBusAddress = address;
    if (addressReply != nullptr)
        dbus_message_unref(addressReply);
    close_connection(session);
    if (!addressOk || impl_->accessibilityBusAddress.empty())
    {
        error = "The AT-SPI accessibility bus is unavailable: " +
                dbus_error_text(dbusError, "org.a11y.Bus did not return an address");
        dbus_error_free(&dbusError);
        return false;
    }
    dbus_error_free(&dbusError);

    dbus_error_init(&dbusError);
    impl_->connection = dbus_connection_open_private(impl_->accessibilityBusAddress.c_str(), &dbusError);
    if (impl_->connection == nullptr)
    {
        error = "Could not connect to the AT-SPI accessibility bus: " +
                dbus_error_text(dbusError, "unknown error");
        dbus_error_free(&dbusError);
        return false;
    }
    dbus_connection_set_exit_on_disconnect(impl_->connection, false);
    if (!dbus_bus_register(impl_->connection, &dbusError))
    {
        error = "Could not register on the AT-SPI accessibility bus: " +
                dbus_error_text(dbusError, "unknown error");
        dbus_error_free(&dbusError);
        close_connection(impl_->connection);
        return false;
    }
    const char *uniqueName = dbus_bus_get_unique_name(impl_->connection);
    if (uniqueName == nullptr || !dbus_connection_register_fallback(
            impl_->connection, AccessiblePrefix, &AccessibilityVTable, impl_.get()))
    {
        error = "Could not register the editor AT-SPI object tree.";
        close_connection(impl_->connection);
        return false;
    }
    impl_->appBusName = uniqueName;
    impl_->snapshots = &snapshots;
    impl_->host = host;
    impl_->generation = generation;
    auto initialRootSnapshot = std::make_shared<SemanticSnapshot>();
    initialRootSnapshot->hostLayoutId = host;
    initialRootSnapshot->hostGeneration = generation;
    initialRootSnapshot->revision = 1;
    impl_->initialRootSnapshot = std::move(initialRootSnapshot);
    impl_->stopRequested.store(false, std::memory_order_release);

    DBusMessage *embed = dbus_message_new_method_call(
        "org.a11y.atspi.Registry", "/org/a11y/atspi/registry",
        "org.a11y.atspi.Socket", "Embed");
    DBusPendingCall *embedPending = nullptr;
    if (embed != nullptr)
    {
        DBusMessageIter out, pair;
        dbus_message_iter_init_append(embed, &out);
        if (dbus_message_iter_open_container(&out, DBUS_TYPE_STRUCT, nullptr, &pair) &&
            append_string(pair, impl_->appBusName.c_str()) && append_path(pair, AccessibleRootPath) &&
            dbus_message_iter_close_container(&out, &pair))
            dbus_connection_send_with_reply(impl_->connection, embed, &embedPending, 1500);
        dbus_message_unref(embed);
    }
    if (embedPending != nullptr)
    {
        dbus_connection_flush(impl_->connection);
        while (!dbus_pending_call_get_completed(embedPending) &&
               dbus_connection_read_write_dispatch(impl_->connection, 50))
        {
        }
    }
    DBusMessage *embedReply = embedPending != nullptr
        ? dbus_pending_call_steal_reply(embedPending) : nullptr;
    if (embedPending != nullptr)
        dbus_pending_call_unref(embedPending);
    if (embedReply != nullptr && dbus_message_get_type(embedReply) == DBUS_MESSAGE_TYPE_ERROR)
        dbus_set_error_from_message(&dbusError, embedReply);
    if (embedReply == nullptr)
    {
        if (!dbus_error_is_set(&dbusError))
            dbus_set_error_const(&dbusError, DBUS_ERROR_NO_REPLY, "registry handshake timed out");
        error = "Could not register the editor with the AT-SPI registry: " +
                dbus_error_text(dbusError, "registry handshake failed");
        dbus_error_free(&dbusError);
        close_connection(impl_->connection);
        impl_->snapshots = nullptr;
        return false;
    }
    if (dbus_message_get_type(embedReply) == DBUS_MESSAGE_TYPE_ERROR)
    {
        error = "Could not register the editor with the AT-SPI registry: " +
                dbus_error_text(dbusError, "registry handshake failed");
        dbus_message_unref(embedReply);
        dbus_error_free(&dbusError);
        close_connection(impl_->connection);
        impl_->snapshots = nullptr;
        return false;
    }
    dbus_message_unref(embedReply);
    dbus_error_free(&dbusError);

    impl_->dispatcher = std::thread(&Impl::run_dispatcher, impl_.get());
    return true;
}

void LinuxAccessibilityProvider::stop() noexcept
{
    if (!impl_)
        return;
    impl_->stopRequested.store(true, std::memory_order_release);
    if (impl_->dispatcher.joinable())
        impl_->dispatcher.join();
    close_connection(impl_->connection);
    impl_->snapshots = nullptr;
    impl_->appBusName.clear();
    impl_->accessibilityBusAddress.clear();
}

bool LinuxAccessibilityProvider::running() const noexcept
{
    return impl_ && impl_->isRunning.load(std::memory_order_acquire);
}
} // namespace platformator_editor
