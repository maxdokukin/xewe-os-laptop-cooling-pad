#include "Fan.h"
#include "../../../SystemController/SystemController.h"
#include "../../../Debug.h"
#include <algorithm>

// Fallback macro just in case compiling on a non-ESP architecture
#ifndef IRAM_ATTR
#define IRAM_ATTR
#endif

// Class-scoped ISR definition. IRAM_ATTR stays here.
void IRAM_ATTR Fan::tach_isr_handler(void* arg) {
    FanData* fan = static_cast<FanData*>(arg);
    if (fan) {
        fan->pulse_count = fan->pulse_count + 1;
    }
}

Fan::Fan(SystemController& controller)
      : Module(controller,
               /* module_name         */ "Fan",
               /* module_description  */ "Controls PWM fans and monitors tachometers",
               /* nvs_key             */ "fan",
               /* requires_init_setup */ false,
               /* can_be_disabled     */ true,
               /* has_cli_cmds        */ true)
{
    DBG_PRINTF(Fan, "Fan(): Initializing Fan module.\n");

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

Fan::~Fan() {
    DBG_PRINTF(Fan, "~Fan(): Destroying module, clearing memory.\n");
    for (auto fan : fans) {
        if (fan->has_tach) {
            detachInterrupt(digitalPinToInterrupt(fan->pin_tach));
        }
        delete fan;
    }
    fans.clear();
}

void Fan::begin_routines_regular(const ModuleConfig& cfg) {
    DBG_PRINTF(Fan, "begin_routines_regular(): Called. Enabled: %d, Loaded from NVS: %d\n", is_enabled(), loaded_from_nvs);
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

            // Calculate RPM every 1000 milliseconds
            if (dt >= 1000) {
                // Safely grab the pulse count and reset it
                noInterrupts();
                uint32_t pulses = fan->pulse_count;
                fan->pulse_count = 0;
                interrupts();

                // Standard PC fans output 2 pulses per revolution.
                // RPM = (pulses / 2) * (60,000 / dt) => (pulses * 30000) / dt
                fan->rpm = (pulses * 30000) / dt;
                fan->last_calc_time = current_time;
            }
        }
    }
}

void Fan::reset(const bool verbose, const bool do_restart, const bool keep_enabled) {
    DBG_PRINTF(Fan, "reset(): Resetting fan module. verbose=%d, do_restart=%d, keep_enabled=%d\n", verbose, do_restart, keep_enabled);
    nvs_clear_all();
    for (auto fan : fans) {
        DBG_PRINTF(Fan, "reset(): Zeroing PWM and detaching interrupts for pin %d before clearing memory.\n", fan->pin_pwm);
        if (fan->has_tach) {
            detachInterrupt(digitalPinToInterrupt(fan->pin_tach));
        }
        analogWrite(fan->pin_pwm, 0);
        delete fan;
    }
    fans.clear();
    Module::reset(verbose, do_restart, keep_enabled);
}

std::string Fan::status(const bool verbose) const {
    DBG_PRINTF(Fan, "status(): Fetching module status. verbose=%d\n", verbose);
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
                s += ", RPM: " + std::to_string(fan->rpm);
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
    DBG_PRINTF(Fan, "add_fan(): Attempting to add fan on PWM pin %d.\n", pwm_pin);
    if (is_disabled()) return false;

    for (const auto fan : fans) {
        if (fan->pin_pwm == pwm_pin) {
            DBG_PRINTF(Fan, "add_fan(): ERROR - Fan on PWM pin %d already exists.\n", pwm_pin);
            return false;
        }
    }

    FanData* new_fan = new FanData{ pwm_pin, 0, false, 0, 0, 0, millis() };
    pinMode(new_fan->pin_pwm, OUTPUT);
    analogWrite(new_fan->pin_pwm, new_fan->speed);

    fans.push_back(new_fan);
    DBG_PRINTF(Fan, "add_fan(): Successfully added fan on PWM pin %d.\n", pwm_pin);
    return true;
}

bool Fan::add_fan_w_tach(uint8_t pwm_pin, uint8_t tach_pin) {
    DBG_PRINTF(Fan, "add_fan_w_tach(): Attempting to add fan on PWM pin %d with Tach pin %d.\n", pwm_pin, tach_pin);
    if (is_disabled()) return false;

    for (const auto fan : fans) {
        if (fan->pin_pwm == pwm_pin) {
            DBG_PRINTF(Fan, "add_fan_w_tach(): ERROR - Fan on PWM pin %d already exists.\n", pwm_pin);
            return false;
        }
    }

    FanData* new_fan = new FanData{ pwm_pin, tach_pin, true, 0, 0, 0, millis() };
    pinMode(new_fan->pin_pwm, OUTPUT);
    pinMode(new_fan->pin_tach, INPUT_PULLUP);

    attachInterruptArg(digitalPinToInterrupt(new_fan->pin_tach), Fan::tach_isr_handler, new_fan, FALLING);

    analogWrite(new_fan->pin_pwm, new_fan->speed);

    fans.push_back(new_fan);
    DBG_PRINTF(Fan, "add_fan_w_tach(): Successfully added fan on PWM pin %d (Tach %d).\n", pwm_pin, tach_pin);
    return true;
}

bool Fan::remove_fan(uint8_t pwm_pin) {
    DBG_PRINTF(Fan, "remove_fan(): Attempting to remove fan on PWM pin %d.\n", pwm_pin);
    if (is_disabled()) return false;

    auto it = std::remove_if(fans.begin(), fans.end(),
        [pwm_pin](FanData* f) { return f->pin_pwm == pwm_pin; });

    if (it != fans.end()) {
        DBG_PRINTF(Fan, "remove_fan(): Fan found. Detaching ISR, zeroing PWM and erasing from memory.\n");
        FanData* target = *it;
        if (target->has_tach) {
            detachInterrupt(digitalPinToInterrupt(target->pin_tach));
        }
        analogWrite(pwm_pin, 0);
        delete target;

        fans.erase(it, fans.end());
        return true;
    }

    DBG_PRINTF(Fan, "remove_fan(): ERROR - Fan on PWM pin %d not found.\n", pwm_pin);
    return false;
}

bool Fan::set_fan_speed(uint8_t pwm_pin, uint8_t speed) {
    DBG_PRINTF(Fan, "set_fan_speed(): Setting speed %d for PWM pin %d.\n", speed, pwm_pin);
    if (is_disabled()) return false;

    for (auto fan : fans) {
        if (fan->pin_pwm == pwm_pin) {
            fan->speed = speed;
            analogWrite(fan->pin_pwm, fan->speed);
            DBG_PRINTF(Fan, "set_fan_speed(): Speed updated successfully.\n");
            return true;
        }
    }

    DBG_PRINTF(Fan, "set_fan_speed(): ERROR - Fan on PWM pin %d not found.\n", pwm_pin);
    return false;
}

// --- CLI Handlers ---

void Fan::cli_add(std::string_view args_sv) {
    std::string args(args_sv);
    trim(args);
    DBG_PRINTF(Fan, "cli_add(): Invoked with args: '%s'\n", args.c_str());

    if (is_disabled()) return;

    if (args.empty()) {
        DBG_PRINTF(Fan, "cli_add(): ERROR - Missing PWM pin argument.\n");
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
    std::string args(args_sv);
    trim(args);
    DBG_PRINTF(Fan, "cli_add_w_tach(): Invoked with args: '%s'\n", args.c_str());

    if (is_disabled()) return;

    auto sp = args.find(' ');
    if (sp == std::string::npos) {
        DBG_PRINTF(Fan, "cli_add_w_tach(): ERROR - Malformed arguments.\n");
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
    std::string args(args_sv);
    trim(args);
    DBG_PRINTF(Fan, "cli_set(): Invoked with args: '%s'\n", args.c_str());

    if (is_disabled()) return;

    auto sp = args.find(' ');
    if (sp == std::string::npos) {
        DBG_PRINTF(Fan, "cli_set(): ERROR - Malformed arguments.\n");
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
    std::string args(args_sv);
    trim(args);
    DBG_PRINTF(Fan, "cli_remove(): Invoked with args: '%s'\n", args.c_str());

    if (is_disabled()) return;

    if (args.empty()) {
        DBG_PRINTF(Fan, "cli_remove(): ERROR - Missing PWM pin argument.\n");
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

std::string Fan::serialize_fan(const FanData* fan) const {
    std::string serialized = std::to_string(fan->pin_pwm) + " " +
                             std::to_string(fan->has_tach) + " " +
                             std::to_string(fan->pin_tach) + " " +
                             std::to_string(fan->speed);
    DBG_PRINTF(Fan, "serialize_fan(): Serialized fan to string: '%s'\n", serialized.c_str());
    return serialized;
}

bool Fan::deserialize_fan(const std::string& config, FanData* fan) const {
    DBG_PRINTF(Fan, "deserialize_fan(): Attempting to parse config string: '%s'\n", config.c_str());
    std::string s = config;
    try {
        auto sp1 = s.find(' ');
        if (sp1 == std::string::npos) return false;
        fan->pin_pwm = static_cast<uint8_t>(std::stoi(s.substr(0, sp1)));
        s = s.substr(sp1 + 1);

        auto sp2 = s.find(' ');
        if (sp2 == std::string::npos) return false;
        fan->has_tach = static_cast<bool>(std::stoi(s.substr(0, sp2)));
        s = s.substr(sp2 + 1);

        auto sp3 = s.find(' ');
        if (sp3 == std::string::npos) return false;
        fan->pin_tach = static_cast<uint8_t>(std::stoi(s.substr(0, sp3)));

        fan->speed = static_cast<uint8_t>(std::stoi(s.substr(sp3 + 1)));
        fan->pulse_count = 0;
        fan->rpm = 0;
        fan->last_calc_time = millis();

        DBG_PRINTF(Fan, "deserialize_fan(): Successfully parsed config.\n");
        return true;
    } catch (...) {
        DBG_PRINTF(Fan, "deserialize_fan(): ERROR - Exception caught during parsing.\n");
        return false;
    }
}

void Fan::load_from_nvs() {
    DBG_PRINTF(Fan, "load_from_nvs(): Starting to load fans from NVS.\n");
    if (is_disabled()) return;

    for (auto fan : fans) delete fan;
    fans.clear();

    int fan_count = controller.nvs.read_uint8(nvs_key, "fan_count", 0);
    DBG_PRINTF(Fan, "load_from_nvs(): Found %d fans in NVS.\n", fan_count);

    for (int i = 0; i < fan_count; i++) {
        std::string key = "fan_cfg_" + std::to_string(i);
        std::string config_str = controller.nvs.read_str(nvs_key, key);

        FanData* loaded_fan = new FanData();
        if (!config_str.empty() && deserialize_fan(config_str, loaded_fan)) {
            DBG_PRINTF(Fan, "load_from_nvs(): Applying hardware states for loaded fan (PWM %d).\n", loaded_fan->pin_pwm);
            pinMode(loaded_fan->pin_pwm, OUTPUT);

            if (loaded_fan->has_tach) {
                pinMode(loaded_fan->pin_tach, INPUT_PULLUP);
                attachInterruptArg(digitalPinToInterrupt(loaded_fan->pin_tach), Fan::tach_isr_handler, loaded_fan, FALLING);
            }
            analogWrite(loaded_fan->pin_pwm, loaded_fan->speed);
            fans.push_back(loaded_fan);
        } else {
            DBG_PRINTF(Fan, "load_from_nvs(): ERROR - Failed to load or deserialize key '%s'.\n", key.c_str());
            delete loaded_fan;
        }
    }
    loaded_from_nvs = true;
    DBG_PRINTF(Fan, "load_from_nvs(): Finished loading.\n");
}

void Fan::save_all_to_nvs() {
    DBG_PRINTF(Fan, "save_all_to_nvs(): Saving %zu fans to NVS.\n", fans.size());
    if (is_disabled()) return;

    nvs_clear_all();
    controller.nvs.write_uint8(nvs_key, "fan_count", fans.size());

    for (size_t i = 0; i < fans.size(); i++) {
        std::string key = "fan_cfg_" + std::to_string(i);
        controller.nvs.write_str(nvs_key, key, serialize_fan(fans[i]));
        DBG_PRINTF(Fan, "save_all_to_nvs(): Saved key '%s'.\n", key.c_str());
    }
}

void Fan::nvs_clear_all() {
    DBG_PRINTF(Fan, "nvs_clear_all(): Erasing all fan configurations from NVS.\n");
    if (is_disabled()) return;

    int fan_count = controller.nvs.read_uint8(nvs_key, "fan_count", 0);
    for (int i = 0; i < fan_count; i++) {
        std::string key = "fan_cfg_" + std::to_string(i);
        controller.nvs.remove(nvs_key, key);
        DBG_PRINTF(Fan, "nvs_clear_all(): Removed key '%s'.\n", key.c_str());
    }
    controller.nvs.write_uint8(nvs_key, "fan_count", 0);
}