#pragma once
#include "../../common/session_title_app.h"

class CastleCrashersApp : public x360n::SessionTitleApp {
 public:
  CastleCrashersApp() : SessionTitleApp("castle_crashers", kPPCImageConfig) {}
};
