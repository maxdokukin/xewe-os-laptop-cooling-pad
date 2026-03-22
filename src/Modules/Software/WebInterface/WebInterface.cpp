// src/Modules/Software/WebInterface/WebInterface.cpp

#include "WebInterface.h"
#include "../../../SystemController/SystemController.h"
#include <ArduinoJson.h>

WebInterface::WebInterface(SystemController& controller)
      : Module(controller,
               /* module_name         */ "Web_Interface",
               /* module_description  */ "Allows to interact with other devices on the local network",
               /* nvs_key             */ "wb",
               /* requires_init_setup */ false,
               /* can_be_disabled     */ true,
               /* has_cli_cmds        */ true)
{}

void WebInterface::begin_routines_common (const ModuleConfig& cfg) {
    // --- Static File Routing ---
    http_server.on("/", HTTP_GET, std::bind(&WebInterface::serve_main_page, this));
    http_server.on("/static/styles.css", HTTP_GET, std::bind(&WebInterface::serve_styles, this));
    http_server.on("/static/script.js", HTTP_GET, std::bind(&WebInterface::serve_script, this));
    http_server.on("/%7B%7B%20url_for('static',%20filename='style.css')%20%7D%7D", HTTP_GET, std::bind(&WebInterface::serve_jinja_catch, this));

    // --- Legacy / Debug Command Route ---
    http_server.on("/cmd", HTTP_GET, std::bind(&WebInterface::handle_command_request, this));

    // --- API GET Routes ---
    http_server.on("/api/state", HTTP_GET, std::bind(&WebInterface::handle_api_state, this));
    http_server.on("/fan/data", HTTP_GET, std::bind(&WebInterface::handle_fan_data, this));
    http_server.on("/argb/data", HTTP_GET, std::bind(&WebInterface::handle_argb_data, this));
    http_server.on("/mlx90614/data", HTTP_GET, std::bind(&WebInterface::handle_mlx90614_data, this));
    http_server.on("/ui/config", HTTP_GET, std::bind(&WebInterface::handle_ui_config_get, this));

    // --- API POST Route ---
    http_server.on("/ui/config", HTTP_POST, std::bind(&WebInterface::handle_ui_config_post, this));

    // --- 404 & CORS Preflight ---
    http_server.onNotFound(std::bind(&WebInterface::handle_not_found, this));

    http_server.begin();
    controller.serial_port.print("Web Interface now available at:\nhttp://" + controller.wifi.get_local_ip() + "\n");
}

void WebInterface::loop () {
    if (is_disabled()) return;
    http_server.handleClient();
}

std::string WebInterface::status (const bool verbose) const {
    if (is_disabled()) return "Disabled";

    std::ostringstream out;

    unsigned long uptime_s = millis() / 1000UL;
    int days  = static_cast<int>(uptime_s / 86400UL);
    int hours = static_cast<int>((uptime_s % 86400UL) / 3600UL);
    int mins  = static_cast<int>((uptime_s % 3600UL) / 60UL);
    int secs  = static_cast<int>(uptime_s % 60UL);

    uint32_t free_heap  = ESP.getFreeHeap();
    uint32_t total_heap = ESP.getHeapSize();
    uint32_t used_heap  = total_heap - free_heap;
    float heap_usage    = (total_heap ? (used_heap * 100.0f) / total_heap : 0.0f);

    out << "--- Web Server Status ---\n";
    out << "  - Uptime:       "
        << days << "d "
        << std::setw(2) << std::setfill('0') << hours << ':'
        << std::setw(2) << std::setfill('0') << mins  << ':'
        << std::setw(2) << std::setfill('0') << secs  << '\n';
    out << "  - Memory Usage: "
        << std::fixed << std::setprecision(2) << heap_usage << "% ("
        << used_heap << " / " << total_heap << " bytes)\n";
    out << "-------------------------\n";

    if (verbose) {
        controller.serial_port.print(out.str());
    }

    return out.str();
}

// --- Helpers & Global CORS ---
void WebInterface::send_cors_headers() {
    http_server.sendHeader("Access-Control-Allow-Origin", "*");
    http_server.sendHeader("Access-Control-Allow-Methods", "POST, GET, OPTIONS, DELETE, PUT");
    http_server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
}

void WebInterface::handle_not_found() {
    if (is_disabled()) return;

    if (http_server.method() == HTTP_OPTIONS) {
        // Preflight CORS request
        send_cors_headers();
        http_server.send(204);
    } else {
        send_cors_headers();
        http_server.send(404, "text/plain", "Not Found");
    }
}

// --- Static File Handlers ---
void WebInterface::serve_main_page() {
    if (is_disabled()) return;
    send_cors_headers();
    http_server.send_P(200, "text/html", INDEX_HTML);
}

void WebInterface::serve_styles() {
    if (is_disabled()) return;
    send_cors_headers();
    http_server.send_P(200, "text/css", STYLES_CSS);
}

void WebInterface::serve_script() {
    if (is_disabled()) return;
    send_cors_headers();
    http_server.send_P(200, "application/javascript", SCRIPT_JS);
}

void WebInterface::serve_jinja_catch() {
    if (is_disabled()) return;
    send_cors_headers();
    http_server.send_P(200, "text/css", STYLES_CSS);
}

void WebInterface::handle_command_request() {
    if (is_disabled()) return;

    if (http_server.hasArg("c")) {
        std::string command_text = http_server.arg("c").c_str();

        controller.serial_port.print("Got cmd from web: \n" + command_text);
        controller.command_parser.parse(command_text);

        send_cors_headers();
        http_server.send(200, "text/plain", "OK");
    } else {
        send_cors_headers();
        http_server.send(400, "text/plain", "Empty Command");
    }
}

// --- API GET Handlers ---
void WebInterface::handle_api_state() {
    if (is_disabled()) return;
    send_cors_headers();

    std::string response = "{";
    response += "\"fan_data\":" + controller.fan.get_json() + ",";
    response += "\"argb_data\":" + controller.argb.get_json() + ",";
    response += "\"sensor_data\":" + controller.mlx90614.get_json() + ",";
    response += "\"ui_config\":" + controller.temp_controller.get_json();
    response += "}";

    http_server.send(200, "application/json", response.c_str());
}

void WebInterface::handle_fan_data() {
    if (is_disabled()) return;
    send_cors_headers();
    http_server.send(200, "application/json", controller.fan.get_json().c_str());
}

void WebInterface::handle_argb_data() {
    if (is_disabled()) return;
    send_cors_headers();
    http_server.send(200, "application/json", controller.argb.get_json().c_str());
}

void WebInterface::handle_mlx90614_data() {
    if (is_disabled()) return;
    send_cors_headers();
    http_server.send(200, "application/json", controller.mlx90614.get_json().c_str());
}

void WebInterface::handle_ui_config_get() {
    if (is_disabled()) return;
    send_cors_headers();
    http_server.send(200, "application/json", controller.temp_controller.get_json().c_str());
}

// --- API POST Handlers ---
void WebInterface::handle_ui_config_post() {
    if (is_disabled()) return;
    send_cors_headers();

    // Workaround for some core versions misplacing JSON bodies
    String payload = http_server.arg("plain");
    if (payload.isEmpty()) {
        for (uint8_t i = 0; i < http_server.args(); i++) {
            if (http_server.argName(i) == "plain" || http_server.argName(i) == "") {
                payload = http_server.arg(i);
                break;
            }
        }
    }

    if (payload.isEmpty()) {
        http_server.send(400, "application/json", "{\"error\":\"Empty payload\"}");
        return;
    }

#if ARDUINOJSON_VERSION_MAJOR >= 7
    JsonDocument doc;
#else
    DynamicJsonDocument doc(1024);
#endif

    DeserializationError error = deserializeJson(doc, payload);

    if (error) {
        http_server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
        return;
    }

    // 1. Process Curve Edge Colors
    JsonObject colors = doc["curve_edge_colors"];
    if (!colors.isNull()) {
        if (!colors["start"].isNull()) {
            controller.temp_controller.set_cold_color(colors["start"].as<const char*>());
        }
        if (!colors["end"].isNull()) {
            controller.temp_controller.set_hot_color(colors["end"].as<const char*>());
        }
    }

    // 2. Process Temp Curve (Clear old, insert new)
    JsonArray incCurve = doc["temp_curve"];
    if (!incCurve.isNull()) {

        // STEP A: Fetch the current state and remove all existing points
        std::string current_state_str = controller.temp_controller.get_json();

#if ARDUINOJSON_VERSION_MAJOR >= 7
        JsonDocument current_state_doc;
#else
        DynamicJsonDocument current_state_doc(1024);
#endif
        deserializeJson(current_state_doc, current_state_str);

        JsonArray current_curve = current_state_doc["temp_curve"];
        if (!current_curve.isNull()) {
            for (JsonVariant v : current_curve) {
                if (!v["temp"].isNull()) {
                    controller.temp_controller.remove_point(v["temp"].as<float>());
                }
            }
        }

        // STEP B: Add the fresh points from the UI payload safely
        for (JsonVariant v : incCurve) {
            if (!v["temp"].isNull() && !v["speed"].isNull()) {
                float t = v["temp"].as<float>();
                uint8_t s = v["speed"].as<uint8_t>();
                controller.temp_controller.add_point(t, s);
            }
        }
    }

    // 3. Return the fresh, newly updated JSON state back to the UI
    std::string response = "{\"ok\":true,\"ui_config\":" + controller.temp_controller.get_json() + "}";
    http_server.send(200, "application/json", response.c_str());
}