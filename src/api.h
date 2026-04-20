#pragma once
#include "stats.h"
#include "config.h"

namespace Api {
    enum class Result { Ok, Unauthorized, Network, Parse, NoOrg };

    Result fetchUsage(const AppConfig& cfg, Usage& out);
    Result fetchOrgId(const String& sessionKey, String& out);

    // Fires a minimal conversation against claude.ai so the 5h window opens,
    // then deletes the conversation so it doesn't clutter the chat sidebar.
    // Logs each step over Serial at 115200 for iterative investigation.
    Result openWindow(const AppConfig& cfg);
}
