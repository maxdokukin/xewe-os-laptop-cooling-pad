#include "TempController.h"
#include "../../../SystemController/SystemController.h"
#include "../../../Debug.h"

TempController::TempController(SystemController& controller)
      : Module(controller, "TempController", "Temperature to Fan Curve Controller", "tempctrl", true, false, true)
{
    DBG_PRINTF(TempController, "TempController(): Initializing TempController module.\n");

    commands_storage.push_back({ "add_point", "Add a curve point: <temp> <fan_speed>", std::string("$") + lower(module_name) + " add_point 40.5 50", 2, [this](std::string_view args){ cli_add_point(args); } });
    commands_storage.push_back({ "remove_point", "Remove a curve point by temp: <temp>", std::string("$") + lower(module_name) + " remove_point 40.5", 1, [this](std::string_view args){ cli_remove_point(args); } });
    commands_storage.push_back({ "set_cold", "Set coldest hex color: <#RRGGBB>", std::string("$") + lower(module_name) + " set_cold #00FF00", 1, [this](std::string_view args){ cli_set_cold_color(args); } });
    commands_storage.push_back({ "set_hot", "Set hottest hex color: <#RRGGBB>", std::string("$") + lower(module_name) + " set_hot #FF0000", 1, [this](std::string_view args){ cli_set_hot_color(args); } });
    commands_storage.push_back({ "print_json", "Print config in JSON format", std::string("$") + lower(module_name) + " print_json", 0, [this](std::string_view args){ cli_print_json(args); } });
}

TempController::~TempController() {
    DBG_PRINTF(TempController, "~TempController(): Destroying module.\n");
    curve.clear();
}

void TempController::begin_routines_init(const ModuleConfig& cfg) {
    DBG_PRINTF(TempController, "begin_routines_init(): Initialization routines run.\n");

    const auto* tcfg = static_cast<const TempControllerConfig*>(&cfg);
    update_interval_ms = tcfg->update_interval_ms;
}

void TempController::begin_routines_regular(const ModuleConfig& cfg) {
    if (is_enabled() && !loaded_from_nvs) {
        load_from_nvs();
    }
}

void TempController::loop() {
    if (is_disabled() || curve.empty()) return;

    uint32_t now = millis();
    if (now - last_update_time >= update_interval_ms) {
        last_update_time = now;

        float current_temp = controller.mlx90614.get_temp();
        uint8_t target_speed = get_target_speed(current_temp);

        controller.fan.set_all(target_speed);
        update_argb_colors(current_temp); // Synchronize LED colors with the temperature
    }
}

void TempController::update_argb_colors(float current_temp) {
    if (curve.empty()) return;

    // RULE: "TURN OFF IF FALLS BELOW THE MIN POINT TEMP"
    float min_temp = curve.front().temp;
    if (current_temp < min_temp) {
        controller.argb.set_all_state(false, false); // False = skip NVS save
        return;
    }

    // RULE: "TURN ON WHEN THE TEMP REACHEDS FIST POINT AND START DISPLAYING GRADIENT"
    controller.argb.set_all_state(true, false);

    // Simple hex string parsing lambda
    auto parse_hex = [](const std::string& hex, int& r, int& g, int& b) {
        if (hex.length() >= 7 && hex[0] == '#') {
            sscanf(hex.c_str(), "#%02x%02x%02x", &r, &g, &b);
        }
    };

    int r1 = 0, g1 = 0, b1 = 255; // Default Cold (Blue)
    int r2 = 255, g2 = 0, b2 = 0; // Default Hot (Red)
    parse_hex(cold_color, r1, g1, b1);
    parse_hex(hot_color, r2, g2, b2);

    float max_temp = curve.back().temp;
    uint8_t r, g, b;

    // Cap to hottest color if we exceed the curve's max point
    if (current_temp >= max_temp || min_temp == max_temp) {
        r = r2; g = g2; b = b2;
    } else {
        // Calculate the interpolation percentage (0.0 to 1.0)
        float t = (current_temp - min_temp) / (max_temp - min_temp);

        r = static_cast<uint8_t>(r1 + (r2 - r1) * t);
        g = static_cast<uint8_t>(g1 + (g2 - g1) * t);
        b = static_cast<uint8_t>(b1 + (b2 - b1) * t);
    }

    // Pass false to prevent constant NVS flash writes!
    controller.argb.set_all_rgb(r, g, b, false);
}

void TempController::reset(const bool verbose, const bool do_restart, const bool keep_enabled) {
    DBG_PRINTF(TempController, "reset(): Resetting TempController module.\n");
    nvs_clear_all();
    curve.clear();
    cold_color = "#0000FF";
    hot_color = "#FF0000";
    Module::reset(verbose, do_restart, keep_enabled);
}

std::string TempController::status(const bool verbose) const {
    if (is_disabled()) return "TempController module disabled.";

    std::string s = "--- Temp Curve Points ---\n";
    if (curve.empty()) {
        s += "  (No points configured)\n";
    } else {
        for (const auto& p : curve) {
            char buf[64];
            snprintf(buf, sizeof(buf), "  - Temp: %.2fC -> Speed: %d\n", p.temp, p.fan_speed);
            s += buf;
        }
    }
    s += "Cold Color: " + cold_color + "\n";
    s += "Hot Color:  " + hot_color + "\n";
    s += "-------------------------";

    if (verbose) controller.serial_port.print(s);
    return s;
}

std::string TempController::get_json() const {
#if ARDUINOJSON_VERSION_MAJOR >= 7
    JsonDocument doc;
#else
    DynamicJsonDocument doc(1024);
#endif

    // Map internal colors to the UI's expected "curve_edge_colors" object
#if ARDUINOJSON_VERSION_MAJOR >= 7
    JsonObject colors = doc["curve_edge_colors"].to<JsonObject>();
#else
    JsonObject colors = doc.createNestedObject("curve_edge_colors");
#endif
    colors["start"] = cold_color;
    colors["end"] = hot_color;

    // Map internal curve to the UI's expected "temp_curve" array
#if ARDUINOJSON_VERSION_MAJOR >= 7
    JsonArray curve_arr = doc["temp_curve"].to<JsonArray>();
    for (const auto& point : curve) {
        JsonObject p = curve_arr.add<JsonObject>();
        p["temp"] = point.temp;
        p["speed"] = point.fan_speed;
    }
#else
    JsonArray curve_arr = doc.createNestedArray("temp_curve");
    for (const auto& point : curve) {
        JsonObject p = curve_arr.createNestedObject();
        p["temp"] = point.temp;
        p["speed"] = point.fan_speed;
    }
#endif

    std::string output;
    serializeJson(doc, output);
    return output;
}

// --- API Methods ---

bool TempController::add_point(float temp, uint8_t fan_speed) {
    if (is_disabled()) return false;

    remove_point(temp); // Overwrite if it exists

    curve.push_back({temp, fan_speed});
    std::sort(curve.begin(), curve.end());

    save_all_to_nvs();
    return true;
}

bool TempController::remove_point(float temp) {
    if (is_disabled()) return false;

    // Tight epsilon check for float comparison
    auto it = std::remove_if(curve.begin(), curve.end(), [temp](const TempPoint& p) {
        return std::abs(p.temp - temp) < 0.01f;
    });

    if (it != curve.end()) {
        curve.erase(it, curve.end());
        save_all_to_nvs();
        return true;
    }
    return false;
}

uint8_t TempController::get_target_speed(float current_temp) const {
    if (curve.empty()) return 0;

    if (current_temp <= curve.front().temp) return curve.front().fan_speed;
    if (current_temp >= curve.back().temp) return curve.back().fan_speed;

    // Linear interpolation
    for (size_t i = 0; i < curve.size() - 1; ++i) {
        if (current_temp >= curve[i].temp && current_temp <= curve[i + 1].temp) {
            float t1 = curve[i].temp;
            float t2 = curve[i + 1].temp;
            float s1 = curve[i].fan_speed;
            float s2 = curve[i + 1].fan_speed;

            float interpolated = s1 + (s2 - s1) * ((current_temp - t1) / (t2 - t1));
            return static_cast<uint8_t>(interpolated);
        }
    }
    return 0;
}

bool TempController::set_cold_color(const std::string& hex_color) {
    if (is_disabled() || hex_color.empty()) return false;
    cold_color = hex_color;
    save_all_to_nvs();
    return true;
}

bool TempController::set_hot_color(const std::string& hex_color) {
    if (is_disabled() || hex_color.empty()) return false;
    hot_color = hex_color;
    save_all_to_nvs();
    return true;
}

std::string TempController::get_cold_color() const { return cold_color; }
std::string TempController::get_hot_color() const { return hot_color; }

// --- CLI Handlers ---

void TempController::cli_add_point(std::string_view args) {
    float temp; int speed;
    if (sscanf(std::string(args).c_str(), "%f %d", &temp, &speed) == 2 && add_point(temp, speed)) {
        controller.serial_port.print("Curve point added.\n");
    } else {
        controller.serial_port.print("Failed to add curve point.\n");
    }
}

void TempController::cli_remove_point(std::string_view args) {
    float temp;
    if (sscanf(std::string(args).c_str(), "%f", &temp) == 1 && remove_point(temp)) {
        controller.serial_port.print("Curve point removed.\n");
    } else {
        controller.serial_port.print("Failed to remove curve point.\n");
    }
}

void TempController::cli_set_cold_color(std::string_view args) {
    std::string color = std::string(args);
    if (set_cold_color(color)) controller.serial_port.print("Cold color set to " + color + "\n");
    else controller.serial_port.print("Failed to set cold color.\n");
}

void TempController::cli_set_hot_color(std::string_view args) {
    std::string color = std::string(args);
    if (set_hot_color(color)) controller.serial_port.print("Hot color set to " + color + "\n");
    else controller.serial_port.print("Failed to set hot color.\n");
}

void TempController::cli_print_json(std::string_view args) {
    controller.serial_port.print(get_json() + "\n");
}

// --- NVS Storage Helpers ---

void TempController::load_from_nvs() {
    if (is_disabled()) return;
    curve.clear();

    // Load hex colors
    cold_color = controller.nvs.read_str(nvs_key, "cold_color", "#0000FF");
    hot_color = controller.nvs.read_str(nvs_key, "hot_color", "#FF0000");

    // Load temperature curve as a single string (Format: temp:speed;temp:speed;)
    std::string curve_data = controller.nvs.read_str(nvs_key, "curve_data", "");

    size_t start = 0;
    size_t end = curve_data.find(';');

    while (end != std::string::npos) {
        std::string token = curve_data.substr(start, end - start);
        float temp; int speed;
        if (sscanf(token.c_str(), "%f:%d", &temp, &speed) == 2) {
            curve.push_back({temp, static_cast<uint8_t>(speed)});
        }
        start = end + 1;
        end = curve_data.find(';', start);
    }

    std::sort(curve.begin(), curve.end());
    loaded_from_nvs = true;
}

void TempController::save_all_to_nvs() {
    if (is_disabled()) return;

    // Save colors
    controller.nvs.write_str(nvs_key, "cold_color", cold_color);
    controller.nvs.write_str(nvs_key, "hot_color", hot_color);

    // Serialize entire curve into a single string to save flash wear and prevent orphan keys
    std::string curve_data = "";
    for (const auto& p : curve) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.2f:%d;", p.temp, p.fan_speed);
        curve_data += buf;
    }

    controller.nvs.write_str(nvs_key, "curve_data", curve_data);
}

void TempController::nvs_clear_all() {
    if (is_disabled()) return;

    controller.nvs.remove(nvs_key, "cold_color");
    controller.nvs.remove(nvs_key, "hot_color");
    controller.nvs.remove(nvs_key, "curve_data");
}