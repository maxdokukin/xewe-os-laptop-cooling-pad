#include "Fan.h"
#include "../../../SystemController/SystemController.h"
#include "../../Debug.h"

#ifndef IRAM_ATTR
#define IRAM_ATTR
#endif

void IRAM_ATTR Fan::tach_isr_handler(void* arg) {
    FanData* fan = static_cast<FanData*>(arg);
    if (fan) {
        fan->pulse_count = fan->pulse_count + 1;
    }
}

Fan::Fan(SystemController& controller)
      : Module(controller, "Fan", "Controls PWM fans and monitors tachometers", "fan", false, true, true)
{
    DBG_PRINTF(Fan, "Fan(): Initializing Fan module.\n");

    commands_storage.push_back({ "add", "Add a fan without a tachometer: <pwm_pin>", std::string("$") + lower(module_name) + " add 9", 1, [this](std::string_view args){ cli_add(args); } });
    commands_storage.push_back({ "add_w_tach", "Add a fan with a tachometer: <pwm_pin> <tach_pin>", std::string("$") + lower(module_name) + " add_w_tach 9 10", 2, [this](std::string_view args){ cli_add_w_tach(args); } });
    commands_storage.push_back({ "set", "Set the speed of a specific fan: <pwm_pin> <val>", std::string("$") + lower(module_name) + " set 9 255", 2, [this](std::string_view args){ cli_set(args); } });
    commands_storage.push_back({ "set_all", "Set the speed of all configured fans: <val>", std::string("$") + lower(module_name) + " set_all 255", 1, [this](std::string_view args){ cli_set_all(args); } });
    commands_storage.push_back({ "remove", "Remove a fan by its PWM pin: <pwm_pin>", std::string("$") + lower(module_name) + " remove 9", 1, [this](std::string_view args){ cli_remove(args); } });
}

Fan::~Fan() {
    DBG_PRINTF(Fan, "~Fan(): Destroying module, clearing memory.\n");
    for (auto fan : fans) {
        if (fan->has_tach) detachInterrupt(digitalPinToInterrupt(fan->pin_tach));
        delete fan;
    }
    fans.clear();
}

void Fan::begin_routines_regular(const ModuleConfig& cfg) {
    const FanConfig& fan_cfg = static_cast<const FanConfig&>(cfg);
    ema_alpha = fan_cfg.ema_alpha;
    absolute_max_rpm = fan_cfg.absolute_max_rpm;
    ui_rounding = fan_cfg.ui_rounding;

    DBG_PRINTF(Fan, "begin_routines_regular(): Called. Enabled: %d, Loaded NVS: %d\n", is_enabled(), loaded_from_nvs);
    if (is_enabled() && !loaded_from_nvs) {
        load_from_nvs();
    }
}

void Fan::loop() {
    if (is_disabled()) return;

    uint32_t current_time = millis();

    for (auto fan : fans) {
        if (fan->has_tach) {
            uint32_t dt = current_time - fan->last_calc_time;

            if (dt >= 1000) {
                noInterrupts();
                uint32_t pulses = fan->pulse_count;
                fan->pulse_count = 0;
                interrupts();

                uint32_t current_rpm = (pulses * 30000) / dt;
                fan->last_calc_time = current_time;

                if (current_rpm > absolute_max_rpm) continue;

                fan->raw_history[fan->history_idx] = current_rpm;
                fan->history_idx = (fan->history_idx + 1) % 3;
                if (fan->history_count < 3) fan->history_count++;

                uint32_t median_rpm = current_rpm;
                if (fan->history_count == 3) {
                    uint32_t a = fan->raw_history[0];
                    uint32_t b = fan->raw_history[1];
                    uint32_t c = fan->raw_history[2];
                    median_rpm = std::max(std::min(a, b), std::min(std::max(a, b), c));
                }

                if (fan->ema_rpm == 0 && median_rpm > 0) {
                    fan->ema_rpm = median_rpm;
                } else {
                    fan->ema_rpm = static_cast<uint32_t>((ema_alpha * median_rpm) + ((1.0f - ema_alpha) * fan->ema_rpm));
                }

                if (ui_rounding > 0) {
                    uint32_t half_round = ui_rounding / 2;
                    fan->displayed_rpm = ((fan->ema_rpm + half_round) / ui_rounding) * ui_rounding;
                } else {
                    fan->displayed_rpm = fan->ema_rpm;
                }
            }
        }
    }
}

void Fan::reset(const bool verbose, const bool do_restart, const bool keep_enabled) {
    DBG_PRINTF(Fan, "reset(): Resetting fan module.\n");
    nvs_clear_all();
    for (auto fan : fans) {
        if (fan->has_tach) detachInterrupt(digitalPinToInterrupt(fan->pin_tach));
        analogWrite(fan->pin_pwm, 0);
        delete fan;
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
        for (const auto fan : fans) {
            s += "  - PWM Pin: " + std::to_string(fan->pin_pwm);
            s += ", Speed: " + std::to_string(fan->speed);
            if (fan->has_tach) {
                s += ", Tach Pin: " + std::to_string(fan->pin_tach);
                s += ", RPM: " + std::to_string(get_rpm(fan->pin_pwm));
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

// --- Core API Methods ---

uint32_t Fan::get_rpm(uint8_t pwm_pin) const {
    for (const auto fan : fans) {
        if (fan->pin_pwm == pwm_pin) {
            return fan->has_tach ? fan->displayed_rpm : 0;
        }
    }
    return 0;
}

bool Fan::add(uint8_t pwm_pin) {
    if (is_disabled()) return false;
    for (const auto fan : fans) if (fan->pin_pwm == pwm_pin) return false;

    FanData* new_fan = new FanData();
    new_fan->pin_pwm = pwm_pin;
    new_fan->last_calc_time = millis();

    pinMode(new_fan->pin_pwm, OUTPUT);
    analogWrite(new_fan->pin_pwm, new_fan->speed);

    fans.push_back(new_fan);
    save_all_to_nvs(); // Automatically sync state
    return true;
}

bool Fan::add_w_tach(uint8_t pwm_pin, uint8_t tach_pin) {
    if (is_disabled()) return false;
    for (const auto fan : fans) if (fan->pin_pwm == pwm_pin) return false;

    FanData* new_fan = new FanData();
    new_fan->pin_pwm = pwm_pin;
    new_fan->pin_tach = tach_pin;
    new_fan->has_tach = true;
    new_fan->last_calc_time = millis();

    pinMode(new_fan->pin_pwm, OUTPUT);
    pinMode(new_fan->pin_tach, INPUT_PULLUP);
    attachInterruptArg(digitalPinToInterrupt(new_fan->pin_tach), Fan::tach_isr_handler, new_fan, FALLING);
    analogWrite(new_fan->pin_pwm, new_fan->speed);

    fans.push_back(new_fan);
    save_all_to_nvs(); // Automatically sync state
    return true;
}

bool Fan::remove(uint8_t pwm_pin) {
    if (is_disabled()) return false;

    auto it = std::remove_if(fans.begin(), fans.end(), [pwm_pin](FanData* f) { return f->pin_pwm == pwm_pin; });

    if (it != fans.end()) {
        FanData* target = *it;
        if (target->has_tach) detachInterrupt(digitalPinToInterrupt(target->pin_tach));
        analogWrite(pwm_pin, 0);
        delete target;
        fans.erase(it, fans.end());

        save_all_to_nvs(); // Automatically sync state
        return true;
    }
    return false;
}

bool Fan::set(uint8_t pwm_pin, uint8_t speed) {
    if (is_disabled()) return false;

    for (auto fan : fans) {
        if (fan->pin_pwm == pwm_pin) {
            fan->speed = speed;
            analogWrite(fan->pin_pwm, fan->speed);
            save_all_to_nvs(); // Automatically sync state
            return true;
        }
    }
    return false;
}

bool Fan::set_all(uint8_t speed) {
    if (is_disabled() || fans.empty()) return false;

    for (auto fan : fans) {
        fan->speed = speed;
        analogWrite(fan->pin_pwm, fan->speed);
    }

    save_all_to_nvs(); // Sync state once for all fans
    return true;
}

// --- CLI Handlers (Streamlined) ---

void Fan::cli_add(std::string_view args_sv) {
    std::string args(args_sv); trim(args);
    if (args.empty()) return;

    if (add(static_cast<uint8_t>(std::stoi(args)))) {
        controller.serial_port.print("Fan added successfully.");
    } else {
        controller.serial_port.print("Failed to add fan. Module disabled or pin already in use.");
    }
}

void Fan::cli_add_w_tach(std::string_view args_sv) {
    std::string args(args_sv); trim(args);
    auto sp = args.find(' ');
    if (sp == std::string::npos) return;

    uint8_t pwm_pin = static_cast<uint8_t>(std::stoi(args.substr(0, sp)));
    uint8_t tach_pin = static_cast<uint8_t>(std::stoi(args.substr(sp + 1)));

    if (add_w_tach(pwm_pin, tach_pin)) {
        controller.serial_port.print("Fan with tach added successfully.");
    } else {
        controller.serial_port.print("Failed to add fan. Module disabled or pin already in use.");
    }
}

void Fan::cli_set(std::string_view args_sv) {
    std::string args(args_sv); trim(args);
    auto sp = args.find(' ');
    if (sp == std::string::npos) return;

    uint8_t pwm_pin = static_cast<uint8_t>(std::stoi(args.substr(0, sp)));
    uint8_t speed = static_cast<uint8_t>(std::stoi(args.substr(sp + 1)));

    if (set(pwm_pin, speed)) {
        controller.serial_port.print("Fan speed updated.");
    } else {
        controller.serial_port.print("Failed to set fan speed. Fan not found.");
    }
}

void Fan::cli_set_all(std::string_view args_sv) {
    std::string args(args_sv); trim(args);
    if (args.empty()) return;

    uint8_t speed = static_cast<uint8_t>(std::stoi(args));

    if (set_all(speed)) {
        controller.serial_port.print("All fans set to new speed.");
    } else {
        controller.serial_port.print("Failed to set fans. Module disabled or no fans configured.");
    }
}

void Fan::cli_remove(std::string_view args_sv) {
    std::string args(args_sv); trim(args);
    if (args.empty()) return;

    if (remove(static_cast<uint8_t>(std::stoi(args)))) {
        controller.serial_port.print("Fan removed successfully.");
    } else {
        controller.serial_port.print("Failed to remove fan. Fan not found.");
    }
}

// --- NVS Storage Helpers ---

std::string Fan::serialize_fan(const FanData* fan) const {
    return std::to_string(fan->pin_pwm) + " " + std::to_string(fan->has_tach) + " " +
           std::to_string(fan->pin_tach) + " " + std::to_string(fan->speed);
}

bool Fan::deserialize_fan(const std::string& config, FanData* fan) const {
    std::string s = config;
    try {
        auto sp1 = s.find(' '); if (sp1 == std::string::npos) return false;
        fan->pin_pwm = static_cast<uint8_t>(std::stoi(s.substr(0, sp1)));
        s = s.substr(sp1 + 1);

        auto sp2 = s.find(' '); if (sp2 == std::string::npos) return false;
        fan->has_tach = static_cast<bool>(std::stoi(s.substr(0, sp2)));
        s = s.substr(sp2 + 1);

        auto sp3 = s.find(' '); if (sp3 == std::string::npos) return false;
        fan->pin_tach = static_cast<uint8_t>(std::stoi(s.substr(0, sp3)));

        fan->speed = static_cast<uint8_t>(std::stoi(s.substr(sp3 + 1)));

        // Reset dynamic tracking data
        fan->pulse_count = 0;
        fan->last_calc_time = millis();
        fan->history_idx = 0;
        fan->history_count = 0;
        fan->ema_rpm = 0;
        fan->displayed_rpm = 0;

        return true;
    } catch (...) {
        return false;
    }
}

void Fan::load_from_nvs() {
    if (is_disabled()) return;
    for (auto fan : fans) delete fan;
    fans.clear();

    int fan_count = controller.nvs.read_uint8(nvs_key, "fan_count", 0);

    for (int i = 0; i < fan_count; i++) {
        std::string key = "fan_cfg_" + std::to_string(i);
        std::string config_str = controller.nvs.read_str(nvs_key, key);

        FanData* loaded_fan = new FanData();
        if (!config_str.empty() && deserialize_fan(config_str, loaded_fan)) {
            pinMode(loaded_fan->pin_pwm, OUTPUT);
            if (loaded_fan->has_tach) {
                pinMode(loaded_fan->pin_tach, INPUT_PULLUP);
                attachInterruptArg(digitalPinToInterrupt(loaded_fan->pin_tach), Fan::tach_isr_handler, loaded_fan, FALLING);
            }
            analogWrite(loaded_fan->pin_pwm, loaded_fan->speed);
            fans.push_back(loaded_fan);
        } else {
            delete loaded_fan;
        }
    }
    loaded_from_nvs = true;
}

void Fan::save_all_to_nvs() {
    if (is_disabled()) return;
    nvs_clear_all();
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