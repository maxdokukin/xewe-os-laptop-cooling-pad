/*********************************************************************************
 *  SPDX-License-Identifier: LicenseRef-PolyForm-NC-1.0.0-NoAI
 *
 *  Licensed under PolyForm Noncommercial 1.0.0 + No AI Use Addendum v1.0.
 *  See: LICENSE and LICENSE-NO-AI.md in the project root for full terms.
 *
 *  Required Notice: Copyright 2025 Maxim Dokukin (https://maxdokukin.com)
 *  https://github.com/maxdokukin/xewe-os
 *********************************************************************************/
#pragma once

#include "../Modules/Module/Module.h"

#include "../Modules/Software/SerialPort/SerialPort.h"
#include "../Modules/Software/System/System.h"
#include "../Modules/Software/CommandParser/CommandParser.h"
#include "../Modules/Hardware/Nvs/Nvs.h"

#include "../Modules/Hardware/Fan/Fan.h"
#include "../Modules/Sensors/Temperature/MLX90614/MLX90614.h"
#include "../Modules/Hardware/ARGB/ARGB.h"
#include "../Modules/Software/TempController/TempController.h"

#include "../Modules/Software/Wifi/Wifi.h"
#include "../Modules/Software/WebInterface/WebInterface.h"

#include <array>
#include <vector>

class SystemController {
public:
    SystemController();

    void                        begin();
    void                        loop();

    SerialPort                  serial_port;
    Nvs                         nvs;
    System                      system;
    CommandParser               command_parser;
    Fan                         fan;
    MLX90614                    mlx90614;
    ARGB                        argb;
    TempController              temp_controller;
    Wifi                        wifi;
    WebInterface                web_interface;

    vector<Module*>&            get_modules                 () { return modules; }
private:
    vector<Module*>             modules                     {};
};