#include "FanTempController.h"
#include "../../../SystemController/SystemController.h"
#include "../../../Debug.h"

FanTempController::FanTempController(SystemController& controller)
      : Module(controller, "FanTempController", "Maps temperature readings to fan PWM speeds", "fantemp", false, true, true)
{
    DBG_PRINTF(FanTempController, "FanTempController(): Initializing logic module.\n");

    commands_storage.push_back({ "add", "Add a point to the curve: <temp_C> <pwm_val>", std::string("$") + lower(module_name) + " add 45.0 128", 2, [this](std::string_view args){ cli_add_point(args); } });
    commands_storage.push_back({ "remove", "Remove a point by temperature: <temp_C>", std::string("$") + lower(module_name) + " remove 45.0", 1, [this](std::string_view args){ cli_remove_point(args); } });
    commands_storage.push_back({ "clear", "Wipe the entire fan curve", std::string("$") + lower(module_name) + " clear", 0, [this](std::string_view args){ cli_clear(args); } });
}

FanTempController::~FanTempController() {
    DBG_PRINTF(FanTempController, "~FanTempController(): Destroying module.\n");
    curve.clear();
}

void FanTempController::begin_routines_regular(const ModuleConfig& cfg) {
    const FanTempControllerConfig& ft_cfg = static_cast<const FanTempControllerConfig&>(cfg);
    update_interval_ms = ft_cfg.update_interval_ms;
    failsafe_pwm = ft_cfg.failsafe_pwm;

    if (is_enabled() && !loaded_from_nvs) {
        load_from_nvs();
    }
}

void FanTempController::loop() {
    if (is_disabled()) return;

    uint32_t current_time = millis();
    if (current_time - last_update_time >= update_interval_ms) {
        last_update_time = current_time;

        // 1. Pull Temperature from your sensor module
        float current_temp = controller.mlx90614.get_temp();

        // 2. Map Temperature to PWM
        uint8_t target_pwm = calculate_pwm(current_temp);

        // 3. Only update fans if the target changed (prevents I2C/PWM bus spam)
        if (target_pwm != last_applied_pwm) {
            controller.fan.set_all(target_pwm);
            last_applied_pwm = target_pwm;
        }
    }
}

void FanTempController::reset(const bool verbose, const bool do_restart, const bool keep_enabled) {
    DBG_PRINTF(FanTempController, "reset(): Resetting fan curve module.\n");
    clear_curve();
    Module::reset(verbose, do_restart, keep_enabled);
}

std::string FanTempController::status(const bool verbose) const {
    if (is_disabled()) return "FanTempController is disabled.";

    std::string s = "--- Current Fan Curve ---\n";
    if (curve.empty()) {
        s += "  [WARNING] Curve is empty! Running at Failsafe PWM: " + std::to_string(failsafe_pwm) + "\n";
    } else {
        for (size_t i = 0; i < curve.size(); i++) {
            // Strip trailing zeros from float for clean UI
            std::string t_str = std::to_string(curve[i].temp);
            t_str.erase(t_str.find_last_not_of('0') + 1, std::string::npos);
            if (t_str.back() == '.') t_str.pop_back();

            s += "  Point " + std::to_string(i+1) + ": " + t_str + " °C -> " + std::to_string(curve[i].pwm) + " PWM\n";
        }
    }
    s += "-------------------------\n";
    s += "Last Applied PWM: " + std::to_string(last_applied_pwm);

    if (verbose) controller.serial_port.print(s);
    return s;
}

// --- Core API Methods ---

uint8_t FanTempController::calculate_pwm(float current_temp) const {
    if (curve.empty()) return failsafe_pwm;

    // If temp is below the lowest defined point, clamp to the minimum speed
    if (current_temp <= curve.front().temp) return curve.front().pwm;

    // If temp is above the highest defined point, clamp to the maximum speed
    if (current_temp >= curve.back().temp) return curve.back().pwm;

    // Interpolate between the two closest points
    for (size_t i = 0; i < curve.size() - 1; i++) {
        if (current_temp >= curve[i].temp && current_temp <= curve[i+1].temp) {
            float temp_range = curve[i+1].temp - curve[i].temp;
            float pwm_range = static_cast<float>(curve[i+1].pwm) - static_cast<float>(curve[i].pwm);
            float temp_offset = current_temp - curve[i].temp;

            return curve[i].pwm + static_cast<uint8_t>((temp_offset / temp_range) * pwm_range);
        }
    }

    return failsafe_pwm; // Fallback
}

bool FanTempController::add_point(float temp, uint8_t pwm) {
    if (is_disabled()) return false;

    // Overwrite if temperature already exists in the curve
    auto it = std::find_if(curve.begin(), curve.end(), [temp](const CurvePoint& p) {
        return std::abs(p.temp - temp) < 0.01f; // Float equality check
    });

    if (it != curve.end()) {
        it->pwm = pwm;
    } else {
        curve.push_back({temp, pwm});
    }

    // Always sort ascending by temperature so interpolation works perfectly
    std::sort(curve.begin(), curve.end());

    save_to_nvs();
    return true;
}

bool FanTempController::remove_point(float temp) {
    if (is_disabled() || curve.empty()) return false;

    auto it = std::remove_if(curve.begin(), curve.end(), [temp](const CurvePoint& p) {
        return std::abs(p.temp - temp) < 0.01f;
    });

    if (it != curve.end()) {
        curve.erase(it, curve.end());
        save_to_nvs();
        return true;
    }
    return false;
}

void FanTempController::clear_curve() {
    if (is_disabled()) return;
    curve.clear();
    save_to_nvs();
}

// --- CLI Handlers ---

void FanTempController::cli_add_point(std::string_view args_sv) {
    std::string args(args_sv); trim(args);
    auto sp = args.find(' ');
    if (sp == std::string::npos) return;

    float temp = std::stof(args.substr(0, sp));
    uint8_t pwm = static_cast<uint8_t>(std::stoi(args.substr(sp + 1)));

    if (add_point(temp, pwm)) {
        controller.serial_port.print("Curve point added/updated.");
    }
}

void FanTempController::cli_remove_point(std::string_view args_sv) {
    std::string args(args_sv); trim(args);
    if (args.empty()) return;

    if (remove_point(std::stof(args))) {
        controller.serial_port.print("Curve point removed.");
    } else {
        controller.serial_port.print("Point not found in curve.");
    }
}

void FanTempController::cli_clear(std::string_view args_sv) {
    clear_curve();
    controller.serial_port.print("Fan curve cleared. Fans will run at failsafe speed.");
}

// --- NVS Storage Helpers ---

void FanTempController::save_to_nvs() {
    controller.nvs.write_uint8(nvs_key, "curve_count", curve.size());

    for (size_t i = 0; i < curve.size(); i++) {
        std::string key = "pt_" + std::to_string(i);
        std::string val = std::to_string(curve[i].temp) + " " + std::to_string(curve[i].pwm);
        controller.nvs.write_str(nvs_key, key, val);
    }
}

void FanTempController::load_from_nvs() {
    curve.clear();
    int curve_count = controller.nvs.read_uint8(nvs_key, "curve_count", 0);

    for (int i = 0; i < curve_count; i++) {
        std::string key = "pt_" + std::to_string(i);
        std::string val = controller.nvs.read_str(nvs_key, key);

        if (!val.empty()) {
            auto sp = val.find(' ');
            if (sp != std::string::npos) {
                float temp = std::stof(val.substr(0, sp));
                uint8_t pwm = static_cast<uint8_t>(std::stoi(val.substr(sp + 1)));
                curve.push_back({temp, pwm});
            }
        }
    }

    // Guarantee sorted state upon load
    std::sort(curve.begin(), curve.end());
    loaded_from_nvs = true;
}