#include "Fan.h"
#include "../../../SystemController/SystemController.h"
#include <algorithm>

Fan::Fan(SystemController& controller)
      : Module(controller,
               /* module_name         */ "Fan",
               /* module_description  */ "Controls PWM fans and monitors tachometers",
               /* nvs_key             */ "fan",
               /* requires_init_setup */ false,
               /* can_be_disabled     */ true,
               /* has_cli_cmds        */ true)
{
    commands_storage.push_back({
        "add",
        "Add a fan without a tachometer: <pwm_pin>",
        std::string("$") + lower(module_name) + " add 9",
        1,
        [this](std::string_view args){ cli_add(args); }
    });

    commands_storage.push_back({
        "add_w_tach",
        "Add a fan with a tachometer: <pwm_pin> <tach_pin>",
        std::string("$") + lower(module_name) + " add_w_tach 9 10",
        2,
        [this](std::string_view args){ cli_add_w_tach(args); }
    });

    commands_storage.push_back({
        "set",
        "Set the speed of a specific fan: <pwm_pin> <val>",
        std::string("$") + lower(module_name) + " set 9 255",
        2,
        [this](std::string_view args){ cli_set(args); }
    });

    commands_storage.push_back({
        "remove",
        "Remove a fan by its PWM pin: <pwm_pin>",
        std::string("$") + lower(module_name) + " remove 9",
        1,
        [this](std::string_view args){ cli_remove(args); }
    });
}

void Fan::begin_routines_regular(const ModuleConfig& cfg) {
    if (is_enabled() && !loaded_from_nvs) {
        load_from_nvs();
    }
}

void Fan::loop() {
    if (is_disabled()) return;

    // Optional: Implement runtime tachometer reading/RPM calculation here
    // using millis() intervals and pulse counters if required later.
}

void Fan::reset(const bool verbose, const bool do_restart, const bool keep_enabled) {
    nvs_clear_all();
    for (const auto& fan : fans) {
        analogWrite(fan.pin_pwm, 0); // Turn off before clearing
    }
    fans.clear();
    Module::reset(verbose, do_restart, keep_enabled);
}

std::string Fan::status(const bool verbose) const {
    if (is_disabled()) return "Fan module disabled";

    std::string s = "";
    if (fans.empty()) {
        s = "No fans are currently configured.";
    } else {
        s = "--- Active Fans ---\n";
        for (const auto& fan : fans) {
            s += "  - PWM Pin: " + std::to_string(fan.pin_pwm);
            s += ", Speed: " + std::to_string(fan.speed);
            if (fan.has_tach) {
                s += ", Tach Pin: " + std::to_string(fan.pin_tach);
            } else {
                s += ", Tach: None";
            }
            s += "\n";
        }
        s += "-------------------";
    }

    if (verbose) controller.serial_port.print(s);
    return s;
}

// --- Core Methods ---

bool Fan::add_fan(uint8_t pwm_pin) {
    if (is_disabled()) return false;

    // Check if it already exists
    for (const auto& f : fans) {
        if (f.pin_pwm == pwm_pin) return false;
    }

    FanData new_fan = { pwm_pin, 0, false, 0 };
    pinMode(new_fan.pin_pwm, OUTPUT);
    analogWrite(new_fan.pin_pwm, new_fan.speed);

    fans.push_back(new_fan);
    return true;
}

bool Fan::add_fan_w_tach(uint8_t pwm_pin, uint8_t tach_pin) {
    if (is_disabled()) return false;

    // Check if it already exists
    for (const auto& f : fans) {
        if (f.pin_pwm == pwm_pin) return false;
    }

    FanData new_fan = { pwm_pin, tach_pin, true, 0 };
    pinMode(new_fan.pin_pwm, OUTPUT);
    pinMode(new_fan.pin_tach, INPUT_PULLUP);
    analogWrite(new_fan.pin_pwm, new_fan.speed);

    fans.push_back(new_fan);
    return true;
}

bool Fan::remove_fan(uint8_t pwm_pin) {
    if (is_disabled()) return false;

    auto it = std::remove_if(fans.begin(), fans.end(),
        [pwm_pin](const FanData& f) { return f.pin_pwm == pwm_pin; });

    if (it != fans.end()) {
        analogWrite(pwm_pin, 0); // Turn it off safely before removing
        fans.erase(it, fans.end());
        return true;
    }
    return false;
}

bool Fan::set_fan_speed(uint8_t pwm_pin, uint8_t speed) {
    if (is_disabled()) return false;

    for (auto& f : fans) {
        if (f.pin_pwm == pwm_pin) {
            f.speed = speed;
            analogWrite(f.pin_pwm, f.speed);
            return true;
        }
    }
    return false;
}

// --- CLI Handlers ---

void Fan::cli_add(std::string_view args_sv) {
    if (is_disabled()) return;

    std::string args(args_sv);
    trim(args);

    if (args.empty()) {
        controller.serial_port.print("Error: Missing PWM pin.");
        return;
    }

    uint8_t pwm_pin = static_cast<uint8_t>(std::stoi(args));

    if (add_fan(pwm_pin)) {
        save_all_to_nvs();
        controller.serial_port.print("Fan added successfully on pin " + std::to_string(pwm_pin));
    } else {
        controller.serial_port.print("Error: Fan on this PWM pin already exists.");
    }
}

void Fan::cli_add_w_tach(std::string_view args_sv) {
    if (is_disabled()) return;

    std::string args(args_sv);
    trim(args);

    auto sp = args.find(' ');
    if (sp == std::string::npos) {
        controller.serial_port.print("Error: Expected <pwm_pin> <tach_pin>");
        return;
    }

    uint8_t pwm_pin = static_cast<uint8_t>(std::stoi(args.substr(0, sp)));
    uint8_t tach_pin = static_cast<uint8_t>(std::stoi(args.substr(sp + 1)));

    if (add_fan_w_tach(pwm_pin, tach_pin)) {
        save_all_to_nvs();
        controller.serial_port.print("Fan with tach added successfully (PWM: " + std::to_string(pwm_pin) + ", Tach: " + std::to_string(tach_pin) + ")");
    } else {
        controller.serial_port.print("Error: Fan on this PWM pin already exists.");
    }
}

void Fan::cli_set(std::string_view args_sv) {
    if (is_disabled()) return;

    std::string args(args_sv);
    trim(args);

    auto sp = args.find(' ');
    if (sp == std::string::npos) {
        controller.serial_port.print("Error: Expected <pwm_pin> <speed>");
        return;
    }

    uint8_t pwm_pin = static_cast<uint8_t>(std::stoi(args.substr(0, sp)));
    uint8_t speed = static_cast<uint8_t>(std::stoi(args.substr(sp + 1)));

    if (set_fan_speed(pwm_pin, speed)) {
        save_all_to_nvs();
        controller.serial_port.print("Fan on pin " + std::to_string(pwm_pin) + " speed set to " + std::to_string(speed));
    } else {
        controller.serial_port.print("Error: No fan found on pin " + std::to_string(pwm_pin));
    }
}

void Fan::cli_remove(std::string_view args_sv) {
    if (is_disabled()) return;

    std::string args(args_sv);
    trim(args);

    if (args.empty()) {
        controller.serial_port.print("Error: Missing PWM pin.");
        return;
    }

    uint8_t pwm_pin = static_cast<uint8_t>(std::stoi(args));

    if (remove_fan(pwm_pin)) {
        save_all_to_nvs();
        controller.serial_port.print("Fan removed successfully.");
    } else {
        controller.serial_port.print("Error: No fan found on pin " + std::to_string(pwm_pin));
    }
}

// --- NVS Storage Helpers ---

std::string Fan::serialize_fan(const FanData& fan) const {
    // Format: pwm_pin has_tach tach_pin speed
    return std::to_string(fan.pin_pwm) + " " +
           std::to_string(fan.has_tach) + " " +
           std::to_string(fan.pin_tach) + " " +
           std::to_string(fan.speed);
}

bool Fan::deserialize_fan(const std::string& config, FanData& fan) const {
    std::string s = config;
    try {
        auto sp1 = s.find(' ');
        if (sp1 == std::string::npos) return false;
        fan.pin_pwm = static_cast<uint8_t>(std::stoi(s.substr(0, sp1)));
        s = s.substr(sp1 + 1);

        auto sp2 = s.find(' ');
        if (sp2 == std::string::npos) return false;
        fan.has_tach = static_cast<bool>(std::stoi(s.substr(0, sp2)));
        s = s.substr(sp2 + 1);

        auto sp3 = s.find(' ');
        if (sp3 == std::string::npos) return false;
        fan.pin_tach = static_cast<uint8_t>(std::stoi(s.substr(0, sp3)));

        fan.speed = static_cast<uint8_t>(std::stoi(s.substr(sp3 + 1)));
        return true;
    } catch (...) {
        return false;
    }
}

void Fan::load_from_nvs() {
    if (is_disabled()) return;

    fans.clear();
    int fan_count = controller.nvs.read_uint8(nvs_key, "fan_count", 0);

    for (int i = 0; i < fan_count; i++) {
        std::string key = "fan_cfg_" + std::to_string(i);
        std::string config_str = controller.nvs.read_str(nvs_key, key);

        FanData loaded_fan;
        if (!config_str.empty() && deserialize_fan(config_str, loaded_fan)) {
            // Re-initialize hardware
            pinMode(loaded_fan.pin_pwm, OUTPUT);
            if (loaded_fan.has_tach) {
                pinMode(loaded_fan.pin_tach, INPUT_PULLUP);
            }
            analogWrite(loaded_fan.pin_pwm, loaded_fan.speed);
            fans.push_back(loaded_fan);
        }
    }
    loaded_from_nvs = true;
}

void Fan::save_all_to_nvs() {
    if (is_disabled()) return;

    nvs_clear_all(); // Erase old layout
    controller.nvs.write_uint8(nvs_key, "fan_count", fans.size());

    for (size_t i = 0; i < fans.size(); i++) {
        std::string key = "fan_cfg_" + std::to_string(i);
        controller.nvs.write_str(nvs_key, key, serialize_fan(fans[i]));
    }
}

void Fan::nvs_clear_all() {
    if (is_disabled()) return;

    int fan_count = controller.nvs.read_uint8(nvs_key, "fan_count", 0);
    for (int i = 0; i < fan_count; i++) {
        std::string key = "fan_cfg_" + std::to_string(i);
        controller.nvs.remove(nvs_key, key);
    }
    controller.nvs.write_uint8(nvs_key, "fan_count", 0);
}