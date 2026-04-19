#pragma once
#include "stats.h"
#include "config.h"

namespace Api {
    enum class Result { Ok, Unauthorized, Network, Parse, NoOrg };

    Result fetchUsage(const AppConfig& cfg, Usage& out);
    Result fetchOrgId(const String& sessionKey, String& out);
}
