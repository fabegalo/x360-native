#pragma once
#include "../../common/session_title_app.h"

class IrukaApp : public x360n::SessionTitleApp {
 public:
  IrukaApp() : SessionTitleApp("iruka", kPPCImageConfig) {}
};
