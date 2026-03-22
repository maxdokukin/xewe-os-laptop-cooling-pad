/*********************************************************************************
 *  SPDX-License-Identifier: LicenseRef-PolyForm-NC-1.0.0-NoAI
 *
 *  Licensed under PolyForm Noncommercial 1.0.0 + No AI Use Addendum v1.0.
 *  See: LICENSE and LICENSE-NO-AI.md in the project root for full terms.
 *
 *  Required Notice: Copyright 2025 Maxim Dokukin (https://maxdokukin.com)
 *  https://github.com/maxdokukin/xewe-os
 *********************************************************************************/
// src/Modules/Software/WebInterface/WebInterface.h
#pragma once

#include "../../Module/Module.h"

#include <WebServer.h>
#include <string>
#include <sstream>
#include <iomanip>

#include "templates/index_html.h"
#include "static/script_js.h"
#include "static/styles_css.h"

struct WebInterfaceConfig : public ModuleConfig {};

class WebInterface : public Module {
public:
    explicit                    WebInterface                (SystemController& controller);

    void                        begin_routines_common       (const ModuleConfig& cfg)       override;

    void                        loop                        ()                              override;
    std::string                 status                      (const bool verbose=false)      const override;

    WebServer&                  get_server                  ()                              { return http_server; }
private:
    WebServer                   http_server                  {80};

    // CORS & Preflight Helper
    void                        send_cors_headers           ();
    void                        handle_not_found            ();

    // Static Routes
    void                        serve_main_page             ();
    void                        serve_styles                ();
    void                        serve_script                ();
    void                        serve_jinja_catch           ();
    void                        handle_command_request      ();

    // API Routes
    void                        handle_api_state            ();
    void                        handle_fan_data             ();
    void                        handle_argb_data            ();
    void                        handle_mlx90614_data        ();
    void                        handle_ui_config_get        ();
    void                        handle_ui_config_post       ();

    // External PROGMEM declarations (ensure these headers/variables are linked in your build)
    static const char           INDEX_HTML                  [] PROGMEM;
    static const char           STYLES_CSS                  [] PROGMEM;
    static const char           SCRIPT_JS                   [] PROGMEM;
};