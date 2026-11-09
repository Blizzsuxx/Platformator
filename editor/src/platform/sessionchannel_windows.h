#pragma once

#include "sessionchannel.h"

namespace platformator_editor
{
std::unique_ptr<SessionChannelProvider> make_windows_session_channel_provider();
} // namespace platformator_editor
