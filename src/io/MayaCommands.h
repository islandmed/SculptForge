// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Maya bridge commands: maya_send and maya_receive (io/MayaBridge.h). Both take the exchange folder explicitly:
// the app passes the one the user picked (File > Maya > Exchange folder...). Registered by CommandRegistry's
// constructor.
#pragma once

#include "core/Commands.h"

namespace sf {

void registerMayaCommands(CommandRegistry& registry);

} // namespace sf
