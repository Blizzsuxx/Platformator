#pragma once

#include "sessionchannel.h"

#include <spawn.h>

namespace platformator_editor
{
std::unique_ptr<SessionChannelProvider> make_posix_session_channel_provider();

// Adds dup2 actions for the ordered child endpoint descriptors 3, 4, and 5.
// Source descriptors remain close-on-exec and are all above 5.
bool add_posix_session_spawn_actions(const SessionChannelPair &pair,
                                    posix_spawn_file_actions_t &actions,
                                    std::string &error);

// Called once by the session child before it can launch any game subprocess.
bool mark_session_child_descriptors_close_on_exec(std::string &error);
} // namespace platformator_editor
