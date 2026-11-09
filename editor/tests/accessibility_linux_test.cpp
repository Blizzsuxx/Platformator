#include "test_support.h"

#include "accessibility.h"
#include "platform/accessibility_linux.h"

#include <dbus/dbus.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace platformator_editor;
using namespace platformator_editor_test;

namespace
{
constexpr char BusInterface[] = "org.a11y.Bus";
constexpr char RegistryInterface[] = "org.a11y.atspi.Socket";
constexpr char BusName[] = "org.a11y.Bus";
constexpr char RegistryName[] = "org.a11y.atspi.Registry";
constexpr char BusPath[] = "/org/a11y/bus";
constexpr char RegistryPath[] = "/org/a11y/atspi/registry";
constexpr char AccessibleInterface[] = "org.a11y.atspi.Accessible";
constexpr char ActionInterface[] = "org.a11y.atspi.Action";
constexpr char TextInterface[] = "org.a11y.atspi.Text";
constexpr char EditableTextInterface[] = "org.a11y.atspi.EditableText";
constexpr char ValueInterface[] = "org.a11y.atspi.Value";

void check_dbus(bool condition, DBusError &error, const char *message)
{
    if (!condition)
    {
        const std::string details = dbus_error_is_set(&error) && error.message != nullptr
            ? std::string(": ") + error.message : std::string{};
        dbus_error_free(&error);
        throw std::runtime_error(std::string(message) + details);
    }
    dbus_error_free(&error);
}

class FakeAtspiRegistry
{
  public:
    FakeAtspiRegistry()
        : address_(std::getenv("DBUS_SESSION_BUS_ADDRESS") == nullptr
              ? "" : std::getenv("DBUS_SESSION_BUS_ADDRESS"))
    {
        if (address_.empty())
            throw std::runtime_error("dbus-run-session did not provide a session bus address");
        if (!dbus_threads_init_default())
            throw std::runtime_error("libdbus thread initialization failed");

        DBusError error;
        dbus_error_init(&error);
        connection_ = dbus_bus_get_private(DBUS_BUS_SESSION, &error);
        check_dbus(connection_ != nullptr, error, "could not connect the fake AT-SPI registry");
        dbus_connection_set_exit_on_disconnect(connection_, false);

        dbus_error_init(&error);
        const int busNameResult = dbus_bus_request_name(
            connection_, BusName, DBUS_NAME_FLAG_DO_NOT_QUEUE, &error);
        check_dbus(busNameResult == DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER ||
                       busNameResult == DBUS_REQUEST_NAME_REPLY_ALREADY_OWNER,
                   error, "could not own org.a11y.Bus");

        dbus_error_init(&error);
        const int registryNameResult = dbus_bus_request_name(
            connection_, RegistryName, DBUS_NAME_FLAG_DO_NOT_QUEUE, &error);
        check_dbus(registryNameResult == DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER ||
                       registryNameResult == DBUS_REQUEST_NAME_REPLY_ALREADY_OWNER,
                   error, "could not own the fake AT-SPI registry name");

        if (!dbus_connection_register_object_path(connection_, BusPath, &VTable, this) ||
            !dbus_connection_register_object_path(connection_, RegistryPath, &VTable, this))
            throw std::runtime_error("could not register fake AT-SPI bus objects");
        dispatcher_ = std::thread([this] {
            while (!stop_.load(std::memory_order_acquire) &&
                   dbus_connection_read_write_dispatch(connection_, 50))
            {
            }
        });
    }

    ~FakeAtspiRegistry()
    {
        stop_.store(true, std::memory_order_release);
        if (connection_ != nullptr)
            dbus_connection_close(connection_);
        if (dispatcher_.joinable())
            dispatcher_.join();
        if (connection_ != nullptr)
            dbus_connection_unref(connection_);
    }

    std::string application_name() const
    {
        std::lock_guard lock(mutex_);
        return applicationName_;
    }

  private:
    static DBusHandlerResult object_message(DBusConnection *connection, DBusMessage *request,
                                            void *userData)
    {
        auto *self = static_cast<FakeAtspiRegistry *>(userData);
        const char *interface = dbus_message_get_interface(request);
        const char *member = dbus_message_get_member(request);
        if (interface == nullptr || member == nullptr)
            return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

        if (std::strcmp(interface, BusInterface) == 0 && std::strcmp(member, "GetAddress") == 0)
        {
            const char *address = self->address_.c_str();
            DBusMessage *reply = dbus_message_new_method_return(request);
            if (reply == nullptr)
                return DBUS_HANDLER_RESULT_NEED_MEMORY;
            const bool ok = dbus_message_append_args(reply, DBUS_TYPE_STRING, &address,
                                                      DBUS_TYPE_INVALID) &&
                            dbus_connection_send(connection, reply, nullptr);
            dbus_message_unref(reply);
            return ok ? DBUS_HANDLER_RESULT_HANDLED : DBUS_HANDLER_RESULT_NEED_MEMORY;
        }

        if (std::strcmp(interface, RegistryInterface) == 0 && std::strcmp(member, "Embed") == 0)
        {
            DBusMessageIter args;
            if (!dbus_message_iter_init(request, &args) ||
                dbus_message_iter_get_arg_type(&args) != DBUS_TYPE_STRUCT)
                return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
            DBusMessageIter pair;
            dbus_message_iter_recurse(&args, &pair);
            const char *name = nullptr;
            if (dbus_message_iter_get_arg_type(&pair) != DBUS_TYPE_STRING)
                return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
            dbus_message_iter_get_basic(&pair, &name);
            {
                std::lock_guard lock(self->mutex_);
                self->applicationName_ = name == nullptr ? "" : name;
            }
            DBusMessage *reply = dbus_message_new_method_return(request);
            if (reply == nullptr)
                return DBUS_HANDLER_RESULT_NEED_MEMORY;
            const bool ok = dbus_connection_send(connection, reply, nullptr);
            dbus_message_unref(reply);
            return ok ? DBUS_HANDLER_RESULT_HANDLED : DBUS_HANDLER_RESULT_NEED_MEMORY;
        }
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    static const DBusObjectPathVTable VTable;
    std::string address_;
    DBusConnection *connection_ = nullptr;
    std::atomic<bool> stop_{false};
    std::thread dispatcher_;
    mutable std::mutex mutex_;
    std::string applicationName_;
};

const DBusObjectPathVTable FakeAtspiRegistry::VTable = {
    nullptr, &FakeAtspiRegistry::object_message, nullptr, nullptr, nullptr, nullptr};

DBusMessage *call(DBusConnection *connection, const std::string &destination,
                  const std::string &path, const char *interface, const char *member,
                  DBusMessage *request = nullptr)
{
    DBusMessage *message = request != nullptr ? request : dbus_message_new_method_call(
        destination.c_str(), path.c_str(), interface, member);
    if (message == nullptr)
        throw std::runtime_error("could not allocate D-Bus request");
    DBusError error;
    dbus_error_init(&error);
    DBusMessage *reply = dbus_connection_send_with_reply_and_block(connection, message, 2000, &error);
    dbus_message_unref(message);
    if (reply == nullptr)
    {
        const std::string details = dbus_error_is_set(&error) && error.message != nullptr
            ? std::string(": ") + error.message : std::string{};
        dbus_error_free(&error);
        throw std::runtime_error("D-Bus request failed" + details);
    }
    dbus_error_free(&error);
    return reply;
}

std::vector<std::string> child_paths(DBusConnection *client, const std::string &application,
                                     const std::string &parentPath)
{
    DBusMessage *reply = call(client, application, parentPath, AccessibleInterface, "GetChildren");
    std::vector<std::string> result;
    DBusMessageIter args;
    if (dbus_message_iter_init(reply, &args) && dbus_message_iter_get_arg_type(&args) == DBUS_TYPE_ARRAY)
    {
        DBusMessageIter array;
        dbus_message_iter_recurse(&args, &array);
        while (dbus_message_iter_get_arg_type(&array) == DBUS_TYPE_STRUCT)
        {
            DBusMessageIter pair;
            dbus_message_iter_recurse(&array, &pair);
            if (dbus_message_iter_get_arg_type(&pair) == DBUS_TYPE_STRING)
                dbus_message_iter_next(&pair);
            if (dbus_message_iter_get_arg_type(&pair) == DBUS_TYPE_OBJECT_PATH)
            {
                const char *path = nullptr;
                dbus_message_iter_get_basic(&pair, &path);
                result.emplace_back(path == nullptr ? "" : path);
            }
            dbus_message_iter_next(&array);
        }
    }
    dbus_message_unref(reply);
    return result;
}

std::string read_text(DBusConnection *client, const std::string &application,
                      const std::string &path, std::int32_t start, std::int32_t end)
{
    DBusMessage *request = dbus_message_new_method_call(
        application.c_str(), path.c_str(), TextInterface, "GetText");
    if (request == nullptr || !dbus_message_append_args(request,
            DBUS_TYPE_INT32, &start, DBUS_TYPE_INT32, &end, DBUS_TYPE_INVALID))
    {
        if (request != nullptr)
            dbus_message_unref(request);
        throw std::runtime_error("could not prepare AT-SPI Text.GetText request");
    }
    DBusMessage *reply = call(client, application, path, TextInterface, "GetText", request);
    DBusError error;
    dbus_error_init(&error);
    const char *value = nullptr;
    const bool ok = dbus_message_get_args(reply, &error, DBUS_TYPE_STRING, &value,
                                          DBUS_TYPE_INVALID);
    check_dbus(ok, error, "AT-SPI Text.GetText returned the wrong type");
    const std::string result = value == nullptr ? "" : value;
    dbus_message_unref(reply);
    return result;
}

void verify_text_and_value_interfaces()
{
    FakeAtspiRegistry registry;
    AccessibilitySnapshots snapshots;
    const HostLayoutId host{3};
    const WindowHostGeneration generation{1};
    SemanticSnapshot snapshot;
    snapshot.hostLayoutId = host;
    snapshot.hostGeneration = generation;
    snapshot.revision = 1;
    snapshot.nodes = {
        SemanticNode{.id = "text-field", .role = SemanticRole::TextField,
                     .name = "Search", .value = "café 🐈", .visible = true,
                     .enabled = true, .actions = {"set_value"}},
        SemanticNode{.id = "number-field", .role = SemanticRole::TextField,
                     .name = "Width", .value = "5", .minimum = 0.0, .maximum = 10.0,
                     .visible = true, .enabled = true, .actions = {"set_value"}}};
    require(snapshots.publish(std::move(snapshot)), "the fake AT-SPI host snapshot must publish");
    SemanticSnapshot detachedSnapshot;
    detachedSnapshot.hostLayoutId = HostLayoutId{4};
    detachedSnapshot.hostGeneration = WindowHostGeneration{2};
    detachedSnapshot.revision = 1;
    detachedSnapshot.nodes = {
        SemanticNode{.id = "host/window/4", .role = SemanticRole::Window,
                     .name = "Detached Scene", .visible = true, .expanded = true},
        SemanticNode{.id = "detached-button", .parentId = "host/window/4",
                     .role = SemanticRole::Button, .name = "Activate detached scene",
                     .visible = true, .enabled = true, .actions = {"activate"}}};
    require(snapshots.publish(std::move(detachedSnapshot)),
            "the detached host snapshot must publish under its own generation");

    LinuxAccessibilityProvider provider;
    std::string error;
    require(provider.start(snapshots, host, generation, error),
            error.empty() ? "the provider must register against the fake AT-SPI bus" : error.c_str());
    for (int attempt = 0; attempt < 100 && !provider.running(); ++attempt)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    require(provider.running(), "the registered AT-SPI provider must start its dispatch thread");

    const std::string application = registry.application_name();
    require(!application.empty(), "the fake registry must receive the provider Embed request");
    DBusError dbusError;
    dbus_error_init(&dbusError);
    DBusConnection *client = dbus_bus_get_private(DBUS_BUS_SESSION, &dbusError);
    check_dbus(client != nullptr, dbusError, "could not connect the AT-SPI test client");
    dbus_connection_set_exit_on_disconnect(client, false);

    const auto children = child_paths(client, application, "/org/a11y/atspi/accessible/root");
    require(children.size() == 3,
            "the application root must expose semantic controls and the detached host window");
    const auto get_name = [&](const std::string &path) {
        DBusMessage *reply = call(client, application, path, AccessibleInterface, "GetName");
        DBusError error;
        dbus_error_init(&error);
        const char *name = nullptr;
        const bool ok = dbus_message_get_args(reply, &error, DBUS_TYPE_STRING, &name,
                                               DBUS_TYPE_INVALID);
        check_dbus(ok, error, "AT-SPI Accessible.GetName returned the wrong type");
        const std::string result = name == nullptr ? "" : name;
        dbus_message_unref(reply);
        return result;
    };
    const auto detachedRoot = std::find_if(children.begin(), children.end(), [&](const auto &path) {
        return get_name(path) == "Detached Scene";
    });
    require(detachedRoot != children.end(),
            "the detached host must appear as a named window under the application root");
    const auto interfaces = [&](const std::string &path) {
        DBusMessage *reply = call(client, application, path, AccessibleInterface, "GetInterfaces");
        std::vector<std::string> values;
        DBusMessageIter args;
        if (dbus_message_iter_init(reply, &args) && dbus_message_iter_get_arg_type(&args) == DBUS_TYPE_ARRAY)
        {
            DBusMessageIter array;
            dbus_message_iter_recurse(&args, &array);
            while (dbus_message_iter_get_arg_type(&array) == DBUS_TYPE_STRING)
            {
                const char *value = nullptr;
                dbus_message_iter_get_basic(&array, &value);
                values.emplace_back(value == nullptr ? "" : value);
                dbus_message_iter_next(&array);
            }
        }
        dbus_message_unref(reply);
        return values;
    };

    const auto textInterfaces = interfaces(children[0]);
    require(std::find(textInterfaces.begin(), textInterfaces.end(), TextInterface) != textInterfaces.end() &&
                std::find(textInterfaces.begin(), textInterfaces.end(), EditableTextInterface) != textInterfaces.end(),
            "text fields must advertise AT-SPI Text and EditableText");

    DBusMessage *countReply = call(client, application, children[0], TextInterface, "GetCharacterCount");
    std::int32_t characterCount = 0;
    dbus_error_init(&dbusError);
    const bool countOk = dbus_message_get_args(countReply, &dbusError,
        DBUS_TYPE_INT32, &characterCount, DBUS_TYPE_INVALID);
    dbus_message_unref(countReply);
    check_dbus(countOk, dbusError, "AT-SPI Text.GetCharacterCount returned the wrong type");
    require(characterCount == 6 && read_text(client, application, children[0], 3, 6) == "é 🐈",
            "AT-SPI text character offsets must preserve multibyte UTF-8 boundaries");

    const auto set_text = [&](const std::string &path, const std::string &value) {
        DBusMessage *request = dbus_message_new_method_call(
            application.c_str(), path.c_str(), EditableTextInterface, "SetTextContents");
        const char *text = value.c_str();
        if (request == nullptr || !dbus_message_append_args(request, DBUS_TYPE_STRING, &text,
                                                              DBUS_TYPE_INVALID))
        {
            if (request != nullptr)
                dbus_message_unref(request);
            throw std::runtime_error("could not prepare AT-SPI EditableText request");
        }
        DBusMessage *reply = call(client, application, path, EditableTextInterface,
                                  "SetTextContents", request);
        dbus_bool_t accepted = 0;
        dbus_error_init(&dbusError);
        const bool ok = dbus_message_get_args(reply, &dbusError, DBUS_TYPE_BOOLEAN,
                                               &accepted, DBUS_TYPE_INVALID);
        dbus_message_unref(reply);
        check_dbus(ok, dbusError, "AT-SPI EditableText.SetTextContents returned the wrong type");
        return accepted != 0;
    };

    require(set_text(children[0], "新しい値"),
            "AT-SPI EditableText must accept a bounded UTF-8 replacement");
    std::vector<SemanticAction> delivered;
    require(snapshots.dispatch_pending(4, [&](const SemanticAction &action, const SemanticNode &) {
                delivered.push_back(action);
            }) == 1 && delivered.size() == 1 && delivered.front().nodeId == "text-field" &&
                delivered.front().action == "set_value" && delivered.front().value == "新しい値",
            "EditableText must use the validated owner-thread semantic action queue");
    require(!set_text(children[0], std::string(AccessibilitySnapshots::MaximumActionValueBytes + 1, 'x')) &&
                snapshots.pending_actions() == 0,
            "oversized AT-SPI text replacement must be rejected before queueing");

    const auto numberInterfaces = interfaces(children[1]);
    require(std::find(numberInterfaces.begin(), numberInterfaces.end(), ValueInterface) != numberInterfaces.end(),
            "numeric text fields must retain their AT-SPI Value interface");
    DBusMessage *setNumberRequest = dbus_message_new_method_call(
        application.c_str(), children[1].c_str(), ValueInterface, "SetCurrentValue");
    const double number = 7.5;
    require(setNumberRequest != nullptr && dbus_message_append_args(
                setNumberRequest, DBUS_TYPE_DOUBLE, &number, DBUS_TYPE_INVALID),
            "the test must prepare a numeric Value.SetCurrentValue request");
    DBusMessage *numberReply = call(client, application, children[1], ValueInterface,
                                    "SetCurrentValue", setNumberRequest);
    dbus_bool_t numberAccepted = 0;
    dbus_error_init(&dbusError);
    const bool numberReplyOk = dbus_message_get_args(numberReply, &dbusError,
        DBUS_TYPE_BOOLEAN, &numberAccepted, DBUS_TYPE_INVALID);
    dbus_message_unref(numberReply);
    check_dbus(numberReplyOk, dbusError, "AT-SPI Value.SetCurrentValue returned the wrong type");
    require(numberAccepted != 0, "a current in-range AT-SPI numeric value must be accepted");
    delivered.clear();
    require(snapshots.dispatch_pending(4, [&](const SemanticAction &action, const SemanticNode &) {
                delivered.push_back(action);
            }) == 1 && delivered.front().action == "set_value" && delivered.front().value == "7.5",
            "numeric AT-SPI edits must use the advertised set_value action");

    DBusMessage *rangeRequest = dbus_message_new_method_call(
        application.c_str(), children[1].c_str(), ValueInterface, "SetCurrentValue");
    const double outOfRange = 12.0;
    require(rangeRequest != nullptr && dbus_message_append_args(
                rangeRequest, DBUS_TYPE_DOUBLE, &outOfRange, DBUS_TYPE_INVALID),
            "the test must prepare an out-of-range Value.SetCurrentValue request");
    DBusMessage *rangeReply = call(client, application, children[1], ValueInterface,
                                   "SetCurrentValue", rangeRequest);
    dbus_bool_t rangeAccepted = 1;
    dbus_error_init(&dbusError);
    const bool rangeReplyOk = dbus_message_get_args(rangeReply, &dbusError,
        DBUS_TYPE_BOOLEAN, &rangeAccepted, DBUS_TYPE_INVALID);
    dbus_message_unref(rangeReply);
    check_dbus(rangeReplyOk, dbusError, "AT-SPI Value.SetCurrentValue returned the wrong type");
    require(rangeAccepted == 0 && snapshots.pending_actions() == 0,
            "out-of-range AT-SPI numeric edits must be rejected before queueing");

    const auto detachedChildren = child_paths(client, application, *detachedRoot);
    require(detachedChildren.size() == 1 && get_name(detachedChildren.front()) ==
                "Activate detached scene",
            "detached window controls must retain their parent and semantic name");
    DBusMessage *actionRequest = dbus_message_new_method_call(
        application.c_str(), detachedChildren.front().c_str(), ActionInterface, "DoAction");
    const std::int32_t actionIndex = 0;
    require(actionRequest != nullptr && dbus_message_append_args(
                actionRequest, DBUS_TYPE_INT32, &actionIndex, DBUS_TYPE_INVALID),
            "the test must prepare an AT-SPI action request for the detached host");
    DBusMessage *actionReply = call(client, application, detachedChildren.front(),
                                    ActionInterface, "DoAction", actionRequest);
    dbus_bool_t actionAccepted = 0;
    dbus_error_init(&dbusError);
    const bool actionReplyOk = dbus_message_get_args(actionReply, &dbusError,
        DBUS_TYPE_BOOLEAN, &actionAccepted, DBUS_TYPE_INVALID);
    dbus_message_unref(actionReply);
    check_dbus(actionReplyOk, dbusError, "detached AT-SPI Action.DoAction returned the wrong type");
    require(actionAccepted != 0, "a detached host action must be accepted against its own snapshot");
    delivered.clear();
    require(snapshots.dispatch_pending(4, [&](const SemanticAction &action, const SemanticNode &) {
                delivered.push_back(action);
            }) == 1 && delivered.size() == 1 &&
                delivered.front().hostLayoutId == HostLayoutId{4} &&
                delivered.front().hostGeneration == WindowHostGeneration{2} &&
                delivered.front().nodeId == "detached-button",
            "detached AT-SPI actions must dispatch to their own validated host incarnation");
    snapshots.retire(HostLayoutId{4}, WindowHostGeneration{2});
    bool stalePathRejected = false;
    try
    {
        DBusMessage *staleReply = call(client, application, detachedChildren.front(),
                                       AccessibleInterface, "GetName");
        dbus_message_unref(staleReply);
    }
    catch (const std::runtime_error &)
    {
        stalePathRejected = true;
    }
    require(stalePathRejected,
            "retired detached host object paths must not resolve into a current host snapshot");

    dbus_connection_close(client);
    dbus_connection_unref(client);
    provider.stop();
    provider.stop();
    require(!provider.running(), "AT-SPI stop must drain the provider thread and be repeatable");
}

void verify_graceful_unavailable_path()
{
    AccessibilitySnapshots snapshots;
    LinuxAccessibilityProvider provider;
    std::string error;
    const bool started = provider.start(snapshots, HostLayoutId{1},
                                        WindowHostGeneration{1}, error);
    if (!started)
        require(!error.empty() && !provider.running(),
                "an unavailable AT-SPI registry must return a clear error without starting a worker");
    else
    {
        for (int attempt = 0; attempt < 100 && !provider.running(); ++attempt)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        require(provider.running(), "a registered AT-SPI provider must run its dispatch thread");
    }
    provider.stop();
    provider.stop();
    require(!provider.running(), "AT-SPI stop must drain the provider thread and be repeatable");
}
} // namespace

int main()
{
    try
    {
        if (std::getenv("DBUS_SESSION_BUS_ADDRESS") == nullptr)
            verify_graceful_unavailable_path();
        else
            verify_text_and_value_interfaces();
        std::cout << "Linux AT-SPI provider test passed\n";
        return EXIT_SUCCESS;
    }
    catch (const std::exception &error)
    {
        std::cerr << "Linux AT-SPI provider test failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
