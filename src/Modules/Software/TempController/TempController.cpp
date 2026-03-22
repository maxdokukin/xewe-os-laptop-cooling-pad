#include "TempController.h"
#include "../../../SystemController/SystemController.h"
#include "../../../Debug.h"

// Note: Ensure your SystemController header declares .fans and .temp modules
// so this module can interface with them inside loop().

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

    // Use static_cast instead of dynamic_cast because RTTI is disabled
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

    // Minimal timing logic to avoid flooding the fan/I2C buses
    uint32_t now = millis();
    if (now - last_update_time >= update_interval_ms) {
        last_update_time = now;

        // Fetch current temperature from MLX sensor
        float current_temp = controller.mlx90614.get_temp();

        // Calculate target speed
        uint8_t target_speed = get_target_speed(current_temp);

        // Apply target speed to all fans
        controller.fan.set_all(target_speed);
    }
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

#include <ArduinoJson.h>

std::string TempController::get_json() const {
    // Allocate JSON document
    JsonDocument doc;

    // 1. Map internal colors to the UI's expected "curve_edge_colors" object
    JsonObject colors = doc["curve_edge_colors"].to<JsonObject>();
    colors["start"] = cold_color;
    colors["end"] = hot_color;

    // 2. Map internal curve to the UI's expected "temp_curve" array
    JsonArray curve_arr = doc["temp_curve"].to<JsonArray>();
    for (const auto& point : curve) {
        JsonObject p = curve_arr.add<JsonObject>();
        p["temp"] = point.temp;
        p["speed"] = point.fan_speed; // Translate internal 'fan_speed' to UI 'speed'
    }

    // Serialize and return
    std::string output;
    serializeJson(doc, output);
    return output;
}

// --- API Methods ---

bool TempController::add_point(float temp, uint8_t fan_speed) {
    if (is_disabled()) return false;

    // Remove if exists to update it
    remove_point(temp);

    curve.push_back({temp, fan_speed});
    std::sort(curve.begin(), curve.end());

    save_all_to_nvs();
    return true;
}

bool TempController::remove_point(float temp) {
    if (is_disabled()) return false;

    // Using a tight epsilon since float equality checking can be risky
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

    // If lower than coldest point, cap at coldest speed
    if (current_temp <= curve.front().temp) return curve.front().fan_speed;

    // If higher than hottest point, cap at hottest speed
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
    return 0; // Fallback
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
        controller.serial_port.print("Curve point added.");
    } else {
        controller.serial_port.print("Failed to add curve point.");
    }
}

void TempController::cli_remove_point(std::string_view args) {
    float temp;
    if (sscanf(std::string(args).c_str(), "%f", &temp) == 1 && remove_point(temp)) {
        controller.serial_port.print("Curve point removed.");
    } else {
        controller.serial_port.print("Failed to remove curve point.");
    }
}

void TempController::cli_set_cold_color(std::string_view args) {
    std::string color = std::string(args);
    if (set_cold_color(color)) controller.serial_port.print("Cold color set to " + color);
    else controller.serial_port.print("Failed to set cold color.");
}

void TempController::cli_set_hot_color(std::string_view args) {
    std::string color = std::string(args);
    if (set_hot_color(color)) controller.serial_port.print("Hot color set to " + color);
    else controller.serial_port.print("Failed to set hot color.");
}

void TempController::cli_print_json(std::string_view args) {
    controller.serial_port.print(get_json());
}

// --- NVS Storage Helpers ---

void TempController::load_from_nvs() {
    if (is_disabled()) return;
    curve.clear();

    cold_color = controller.nvs.read_str(nvs_key, "cold_color", "#0000FF");
    hot_color = controller.nvs.read_str(nvs_key, "hot_color", "#FF0000");

    int count = controller.nvs.read_uint8(nvs_key, "curve_count", 0);
    for (int i = 0; i < count; i++) {
        std::string raw = controller.nvs.read_str(nvs_key, "curve_pt_" + std::to_string(i));
        float temp; int speed;
        if (sscanf(raw.c_str(), "%f %d", &temp, &speed) == 2) {
            curve.push_back({temp, static_cast<uint8_t>(speed)});
        }
    }

    std::sort(curve.begin(), curve.end());
    loaded_from_nvs = true;
}

void TempController::save_all_to_nvs() {
    if (is_disabled()) return;

    controller.nvs.write_str(nvs_key, "cold_color", cold_color);
    controller.nvs.write_str(nvs_key, "hot_color", hot_color);

    // Clear old points first to avoid orphan configs
    int old_count = controller.nvs.read_uint8(nvs_key, "curve_count", 0);
    for (int i = 0; i < old_count; i++) {
        controller.nvs.remove(nvs_key, "curve_pt_" + std::to_string(i));
    }

    controller.nvs.write_uint8(nvs_key, "curve_count", curve.size());

    for (size_t i = 0; i < curve.size(); i++) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.2f %d", curve[i].temp, curve[i].fan_speed);
        controller.nvs.write_str(nvs_key, "curve_pt_" + std::to_string(i), buf);
    }
}

void TempController::nvs_clear_all() {
    if (is_disabled()) return;

    controller.nvs.remove(nvs_key, "cold_color");
    controller.nvs.remove(nvs_key, "hot_color");

    int count = controller.nvs.read_uint8(nvs_key, "curve_count", 0);
    for (int i = 0; i < count; i++) {
        controller.nvs.remove(nvs_key, "curve_pt_" + std::to_string(i));
    }
    controller.nvs.write_uint8(nvs_key, "curve_count", 0);
}