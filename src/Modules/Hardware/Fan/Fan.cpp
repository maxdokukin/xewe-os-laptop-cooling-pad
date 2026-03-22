#include "Fan.h"
#include "../../../SystemController/SystemController.h"
#include "../../../Debug.h"

#ifndef IRAM_ATTR
#define IRAM_ATTR
#endif

void IRAM_ATTR Fan::tach_isr_handler(void* arg) {
    if (auto* fan = static_cast<FanData*>(arg)) fan->pulse_count += 1;
}

Fan::Fan(SystemController& controller)
      : Module(controller, "Fan", "Controls PWM fans and monitors tachometers", "fan", true, false, true)
{
    DBG_PRINTF(Fan, "Fan(): Initializing Fan module.\n");

    commands_storage.push_back({ "add", "Add a fan without a tachometer: <pwm_pin>", std::string("$") + lower(module_name) + " add 9", 1,
        [this](std::string_view args) {
            int pwm;
            if (sscanf(std::string(args).c_str(), "%d", &pwm) == 1 && add(pwm))
                this->controller.serial_port.print("Fan added.");
            else
                this->controller.serial_port.print("Failed to add fan.");
        }
    });

    commands_storage.push_back({ "add_w_tach", "Add a fan with a tachometer: <pwm_pin> <tach_pin>", std::string("$") + lower(module_name) + " add_w_tach 9 10", 2,
        [this](std::string_view args) {
            int pwm, tach;
            if (sscanf(std::string(args).c_str(), "%d %d", &pwm, &tach) == 2 && add_w_tach(pwm, tach))
                this->controller.serial_port.print("Fan with tach added.");
            else
                this->controller.serial_port.print("Failed to add fan.");
        }
    });

    commands_storage.push_back({ "set", "Set the speed of a specific fan: <pwm_pin> <val>", std::string("$") + lower(module_name) + " set 9 255", 2,
        [this](std::string_view args) {
            int pwm, speed;
            if (sscanf(std::string(args).c_str(), "%d %d", &pwm, &speed) == 2 && set(pwm, speed))
                this->controller.serial_port.print("Speed updated.");
            else
                this->controller.serial_port.print("Failed to set fan speed.");
        }
    });

    commands_storage.push_back({ "set_all", "Set the speed of all configured fans: <val>", std::string("$") + lower(module_name) + " set_all 255", 1,
        [this](std::string_view args) {
            int speed;
            if (sscanf(std::string(args).c_str(), "%d", &speed) == 1 && set_all(speed))
                this->controller.serial_port.print("All fans updated.");
            else
                this->controller.serial_port.print("Failed to set fans.");
        }
    });

    commands_storage.push_back({ "remove", "Remove a fan by its PWM pin: <pwm_pin>", std::string("$") + lower(module_name) + " remove 9", 1,
        [this](std::string_view args) {
            int pwm;
            if (sscanf(std::string(args).c_str(), "%d", &pwm) == 1 && remove(pwm))
                this->controller.serial_port.print("Fan removed.");
            else
                this->controller.serial_port.print("Failed to remove fan.");
        }
    });
}

Fan::~Fan() {
    DBG_PRINTF(Fan, "~Fan(): Destroying module, clearing memory.\n");
    for (auto* fan : fans) free_fan(fan);
    fans.clear();
}

void Fan::begin_routines_init(const ModuleConfig& cfg) {
    DBG_PRINTF(Fan, "begin_routines_init(): Auto-initializing hardcoded fans.\n");
    add_w_tach(3, 0);
    add_w_tach(10, 1);
}

void Fan::begin_routines_regular(const ModuleConfig& cfg) {
    const auto& fan_cfg = static_cast<const FanConfig&>(cfg);
    ema_alpha = fan_cfg.ema_alpha;
    absolute_max_rpm = fan_cfg.absolute_max_rpm;
    ui_rounding = fan_cfg.ui_rounding;

    DBG_PRINTF(Fan, "begin_routines_regular(): Enabled: %d, Loaded NVS: %d\n", is_enabled(), loaded_from_nvs);
    if (is_enabled() && !loaded_from_nvs) load_from_nvs();
}

void Fan::loop() {
    if (is_disabled()) return;
    uint32_t now = millis();

    for (auto* f : fans) {
        if (!f->has_tach) continue;

        uint32_t dt = now - f->last_calc_time;
        if (dt < 1000) continue;

        noInterrupts();
        uint32_t pulses = f->pulse_count;
        f->pulse_count = 0;
        interrupts();

        uint32_t current_rpm = (pulses * 30000) / dt;
        f->last_calc_time = now;

        if (current_rpm > absolute_max_rpm) continue;

        f->raw_history[f->history_idx] = current_rpm;
        f->history_idx = (f->history_idx + 1) % 3;
        if (f->history_count < 3) f->history_count++;

        uint32_t median_rpm = current_rpm;
        if (f->history_count == 3) {
            uint32_t a = f->raw_history[0], b = f->raw_history[1], c = f->raw_history[2];
            median_rpm = std::max(std::min(a, b), std::min(std::max(a, b), c));
        }

        f->ema_rpm = (f->ema_rpm == 0 && median_rpm > 0) ? median_rpm :
                     static_cast<uint32_t>((ema_alpha * median_rpm) + ((1.0f - ema_alpha) * f->ema_rpm));

        f->displayed_rpm = ui_rounding ? ((f->ema_rpm + ui_rounding / 2) / ui_rounding) * ui_rounding : f->ema_rpm;
    }
}

void Fan::reset(const bool verbose, const bool do_restart, const bool keep_enabled) {
    DBG_PRINTF(Fan, "reset(): Resetting fan module.\n");
    nvs_clear_all();
    for (auto* fan : fans) free_fan(fan);
    fans.clear();
    Module::reset(verbose, do_restart, keep_enabled);
}

std::string Fan::status(const bool verbose) const {
    if (is_disabled()) return "Fan module disabled";
    if (fans.empty()) return "No fans are currently configured.";

    std::string s = "--- Active Fans ---\n";
    for (const auto* f : fans) {
        s += "  - PWM Pin: " + std::to_string(f->pin_pwm) + ", Speed: " + std::to_string(f->speed);
        if (f->has_tach) s += ", Tach Pin: " + std::to_string(f->pin_tach) + ", RPM: " + std::to_string(get_rpm(f->pin_pwm));
        else s += ", Tach: None";
        s += "\n";
    }
    s += "-------------------";

    if (verbose) controller.serial_port.print(s);
    return s;
}

// --- Internal Dry Helpers ---

Fan::FanData* Fan::get_fan(uint8_t pwm_pin) const {
    for (auto* f : fans) if (f->pin_pwm == pwm_pin) return f;
    return nullptr;
}

void Fan::free_fan(FanData* f) {
    if (f->has_tach) detachInterrupt(digitalPinToInterrupt(f->pin_tach));
    ledcWrite(f->pin_pwm, 0);
    ledcDetach(f->pin_pwm);
    delete f;
}

Fan::FanData* Fan::_create_and_setup(uint8_t pwm, uint8_t tach, bool has_tach, uint8_t speed) {
    FanData* f = new FanData{pwm, tach, has_tach, speed, 0, millis()};
    ledcAttach(pwm, PWM_FREQ, PWM_RES);
    if (has_tach) {
        pinMode(tach, INPUT_PULLUP);
        attachInterruptArg(digitalPinToInterrupt(tach), tach_isr_handler, f, FALLING);
    }
    ledcWrite(pwm, speed);
    fans.push_back(f);
    return f;
}

bool Fan::_add(uint8_t pwm, uint8_t tach, bool has_tach) {
    if (is_disabled() || get_fan(pwm)) return false;
    _create_and_setup(pwm, tach, has_tach, 0);
    save_all_to_nvs();
    return true;
}

// --- Core API Methods ---

uint32_t Fan::get_rpm(uint8_t pwm_pin) const {
    FanData* f = get_fan(pwm_pin);
    return (f && f->has_tach) ? f->displayed_rpm : 0;
}

bool Fan::add(uint8_t pwm_pin) { return _add(pwm_pin, 0, false); }
bool Fan::add_w_tach(uint8_t pwm_pin, uint8_t tach_pin) { return _add(pwm_pin, tach_pin, true); }

bool Fan::remove(uint8_t pwm_pin) {
    if (is_disabled()) return false;
    auto it = std::find_if(fans.begin(), fans.end(), [pwm_pin](FanData* f) { return f->pin_pwm == pwm_pin; });
    if (it == fans.end()) return false;

    free_fan(*it);
    fans.erase(it);
    save_all_to_nvs();
    return true;
}

bool Fan::set(uint8_t pwm_pin, uint8_t speed) {
    if (is_disabled()) return false;
    if (FanData* f = get_fan(pwm_pin)) {
        f->speed = speed;
        ledcWrite(f->pin_pwm, speed);
        save_all_to_nvs();
        return true;
    }
    return false;
}

bool Fan::set_all(uint8_t speed) {
    if (is_disabled() || fans.empty()) return false;
    for (auto* f : fans) {
        f->speed = speed;
        ledcWrite(f->pin_pwm, speed);
    }
    save_all_to_nvs();
    return true;
}

// --- NVS Storage Helpers ---

std::string Fan::serialize_fan(const FanData* f) const {
    char buf[64];
    snprintf(buf, sizeof(buf), "%u %d %u %u", f->pin_pwm, f->has_tach, f->pin_tach, f->speed);
    return std::string(buf);
}

bool Fan::deserialize_fan(const std::string& config, FanData* f) const {
    int has_tach;
    if (sscanf(config.c_str(), "%hhu %d %hhu %hhu", &f->pin_pwm, &has_tach, &f->pin_tach, &f->speed) != 4) return false;

    f->has_tach = has_tach;
    f->pulse_count = f->ema_rpm = f->displayed_rpm = f->history_idx = f->history_count = 0;
    f->last_calc_time = millis();
    return true;
}

void Fan::load_from_nvs() {
    if (is_disabled()) return;
    for (auto* f : fans) free_fan(f);
    fans.clear();

    int count = controller.nvs.read_uint8(nvs_key, "fan_count", 0);
    for (int i = 0; i < count; i++) {
        FanData temp;
        if (deserialize_fan(controller.nvs.read_str(nvs_key, "fan_cfg_" + std::to_string(i)), &temp)) {
            _create_and_setup(temp.pin_pwm, temp.pin_tach, temp.has_tach, temp.speed);
        }
    }
    loaded_from_nvs = true;
}

void Fan::save_all_to_nvs() {
    if (is_disabled()) return;
    nvs_clear_all();
    controller.nvs.write_uint8(nvs_key, "fan_count", fans.size());

    for (size_t i = 0; i < fans.size(); i++) {
        controller.nvs.write_str(nvs_key, "fan_cfg_" + std::to_string(i), serialize_fan(fans[i]));
    }
}

void Fan::nvs_clear_all() {
    if (is_disabled()) return;
    int count = controller.nvs.read_uint8(nvs_key, "fan_count", 0);
    for (int i = 0; i < count; i++) {
        controller.nvs.remove(nvs_key, "fan_cfg_" + std::to_string(i));
    }
    controller.nvs.write_uint8(nvs_key, "fan_count", 0);
}