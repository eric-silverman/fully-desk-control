#pragma once

#include "JiecangDesk.h"

// Minimal local dashboard on port 80. HomeSpan's HAP server lives on 1201,
// so there is no conflict. Reachable at http://desk.local (or the raw IP if
// mDNS is unavailable, e.g. from Android).
namespace webui {

void begin(jiecang::Desk *desk);
void handle();  // call from loop()

}  // namespace webui
