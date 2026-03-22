#include "MLX90614.h"
#include "../../../../SystemController/SystemController.h"
#include "../../../../Debug.h"
#include <Wire.h>
#include <cmath>

MLX90614::MLX90614(SystemController& controller)
      : Module(controller, "MLX90614", "Contactless I2C Temperature Sensor", "temp", true, false, true)
{
    DBG_PRINTF(MLX90614, "MLX90614(): Initializing temperature sensor module.\n");

    commands_storage.push_back({ "read", "Force a manual read of the sensor", "$temp read", 0, [this](std::string_view args){ cli_read(args); } });
    commands_storage.push_back({ "scan", "Scan the I2C bus for connected devices", "$temp scan", 0, [this](std::string_view args){ cli_scan(args); } });
    commands_storage.push_back({ "set_addr", "Set the I2C address (Hex format, e.g., 0x5A)", "$temp set_addr 0x5A", 1, [this](std::string_view args){ cli_set_addr(args); } });
    commands_storage.push_back({ "set_pins", "Set I2C SDA and SCL pins: <sda> <scl>", "$temp set_pins 21 22", 2, [this](std::string_view args){ cli_set_pins(args); } });
}

void MLX90614::begin_routines_init(const ModuleConfig& cfg) {
    const auto& temp_cfg = static_cast<const MLX90614Config&>(cfg);
    i2c_address = temp_cfg.default_i2c_address;

    if (is_disabled()) return;

    // ONLY Prompt in init if unconfigured
    if (sda_pin == 255 || scl_pin == 255) {
        controller.serial_port.print_spacer();
        controller.serial_port.print_header("MLX90614 Configuration");
        controller.serial_port.print("I2C pins are currently unconfigured. Please set them now.");

//        sda_pin = controller.serial_port.get_uint8("Enter SDA Pin (0-99): ", 0, 99);
//        scl_pin = controller.serial_port.get_uint8("Enter SCL Pin (0-99): ", 0, 99);
        sda_pin = 4;
        scl_pin = 5;


        save_to_nvs();
        controller.serial_port.print("Settings saved to NVS!");
        controller.serial_port.print_separator();
    }
}

void MLX90614::begin_routines_regular(const ModuleConfig& cfg) {
    const auto& temp_cfg = static_cast<const MLX90614Config&>(cfg);
    poll_interval_ms = temp_cfg.poll_interval_ms;
    error_temp = temp_cfg.error_temp;

    if (is_disabled()) return;

    // ONLY Load from NVS in regular
    load_from_nvs();

    // Fallback to struct config just in case NVS was empty and prompt was skipped
    if (sda_pin == 255) sda_pin = temp_cfg.sda_pin;
    if (scl_pin == 255) scl_pin = temp_cfg.scl_pin;

    if (sda_pin == 255 || scl_pin == 255) {
        controller.serial_port.print("MLX90614 Init Failed: Pins still unconfigured.");
        return;
    }

    // Fire up the hardware now that NVS is loaded
    controller.serial_port.print("MLX90614 Init: Starting I2C on SDA=" + std::to_string(sda_pin) + " SCL=" + std::to_string(scl_pin));

    #if defined(ESP32) || defined(ESP8266)
    Wire.end();
    #endif

    Wire.begin(sda_pin, scl_pin);
    cli_scan("");

    cached_object_temp = read_i2c_temp(MLX_RAM_TOBJ1);
    cached_ambient_temp = read_i2c_temp(MLX_RAM_TA);
    sensor_online = !std::isnan(cached_object_temp) && !std::isnan(cached_ambient_temp);

    if (sensor_online) {
        char buf[64];
        snprintf(buf, sizeof(buf), "MLX90614 Init Success: Valid data read from 0x%02X. Temp: %.2f C", i2c_address, cached_object_temp);
        controller.serial_port.print(buf);
    } else {
        controller.serial_port.print("MLX90614 Init Failed: Could not read valid data from configured address.");
    }

    if (is_enabled()) last_read_time = millis();
}

void MLX90614::loop() {
    if (is_disabled() || sda_pin == 255 || scl_pin == 255) return;

    uint32_t current_time = millis();
    if (current_time - last_read_time < poll_interval_ms) return;

    last_read_time = current_time;

    float obj_temp = read_i2c_temp(MLX_RAM_TOBJ1);
    float amb_temp = read_i2c_temp(MLX_RAM_TA);

    sensor_online = !std::isnan(obj_temp) && !std::isnan(amb_temp);
    if (sensor_online) {
        cached_object_temp = obj_temp;
        cached_ambient_temp = amb_temp;
    }
}

void MLX90614::reset(const bool verbose, const bool do_restart, const bool keep_enabled) {
    DBG_PRINTF(MLX90614, "reset(): Resetting temperature sensor module.\n");
    cached_object_temp = 0.0f;
    cached_ambient_temp = 0.0f;
    sensor_online = false;
    sda_pin = 255;
    scl_pin = 255;

    controller.nvs.remove(nvs_key, "i2c_addr");
    controller.nvs.remove(nvs_key, "sda_pin");
    controller.nvs.remove(nvs_key, "scl_pin");
    // If your NVS wrapper requires it, uncomment this:
    // controller.nvs.commit();

    Module::reset(verbose, do_restart, keep_enabled);
}

std::string MLX90614::status(const bool verbose) const {
    if (is_disabled()) return "MLX90614 module disabled";

    // Safely resolve the string *before* passing to snprintf
    std::string pin_status = (sda_pin == 255 || scl_pin == 255)
                             ? "[WARNING] Unconfigured"
                             : "SDA=" + std::to_string(sda_pin) + " SCL=" + std::to_string(scl_pin);

    char buf[256];
    snprintf(buf, sizeof(buf),
        "--- MLX90614 Status ---\n"
        "  Pins:      %s\n"
        "  I2C Addr:  0x%02X\n"
        "  Online:    %s\n"
        "  Object:    %.2f °C\n"
        "  Ambient:   %.2f °C\n"
        "-----------------------",
        pin_status.c_str(),
        i2c_address,
        sensor_online ? "YES" : "NO",
        cached_object_temp,
        cached_ambient_temp
    );

    std::string s(buf);
    if (verbose) controller.serial_port.print(s);
    return s;
}

// --- Core API Methods ---

float MLX90614::get_temp() const         { return (!is_disabled() && sensor_online) ? cached_object_temp : error_temp; }
float MLX90614::get_ambient_temp() const { return (!is_disabled() && sensor_online) ? cached_ambient_temp : error_temp; }
bool MLX90614::is_online() const         { return sensor_online; }

bool MLX90614::set_i2c_address(uint8_t new_address) {
    if (new_address == 0 || new_address > 0x7F) return false;

    i2c_address = new_address;
    save_to_nvs();

    if (sda_pin != 255 && scl_pin != 255) {
        sensor_online = !std::isnan(read_i2c_temp(MLX_RAM_TOBJ1));
    }
    return true;
}

// --- Internal NVS / I2C Logic ---

void MLX90614::load_from_nvs() {
    uint8_t saved_addr = controller.nvs.read_uint8(nvs_key, "i2c_addr", 0);
    if (saved_addr != 0) i2c_address = saved_addr;

    uint8_t saved_sda = controller.nvs.read_uint8(nvs_key, "sda_pin", 255);
    if (saved_sda != 255) sda_pin = saved_sda;

    uint8_t saved_scl = controller.nvs.read_uint8(nvs_key, "scl_pin", 255);
    if (saved_scl != 255) scl_pin = saved_scl;
}

void MLX90614::save_to_nvs() const {
    controller.nvs.write_uint8(nvs_key, "i2c_addr", i2c_address);
    if (sda_pin != 255) controller.nvs.write_uint8(nvs_key, "sda_pin", sda_pin);
    if (scl_pin != 255) controller.nvs.write_uint8(nvs_key, "scl_pin", scl_pin);

    // WARNING: If your NVS wrapper does not auto-commit, you MUST call commit here.
    // controller.nvs.commit();
}

float MLX90614::read_i2c_temp(uint8_t register_address) const {
    if (sda_pin == 255 || scl_pin == 255) return NAN;

    Wire.beginTransmission(i2c_address);
    Wire.write(register_address);
    if (Wire.endTransmission(false) != 0) return NAN;

    Wire.requestFrom(i2c_address, (uint8_t)3);
    if (Wire.available() >= 2) {
        uint16_t lsb = Wire.read();
        uint16_t msb = Wire.read();
        Wire.read(); // Discard PEC
        return (static_cast<float>((msb << 8) | lsb) * 0.02f) - 273.15f;
    }
    return NAN;
}

// --- CLI Handlers ---

void MLX90614::cli_read(std::string_view args_sv) {
    if (is_disabled()) return;
    if (sda_pin == 255 || scl_pin == 255) {
        controller.serial_port.print("Cannot read: Pins unconfigured. Run $temp set_pins <sda> <scl>");
        return;
    }

    float obj_temp = read_i2c_temp(MLX_RAM_TOBJ1);
    sensor_online = !std::isnan(obj_temp);

    if (!sensor_online) {
        controller.serial_port.print("Failed to read sensor! Check I2C wiring or address.");
    } else {
        cached_object_temp = obj_temp;
        controller.serial_port.print("Immediate Read: " + std::to_string(cached_object_temp) + " °C");
    }
}

void MLX90614::cli_scan(std::string_view args_sv) {
    if (sda_pin == 255 || scl_pin == 255) {
        controller.serial_port.print("Cannot scan: Pins unconfigured. Run $temp set_pins <sda> <scl>");
        return;
    }

    controller.serial_port.print("Scanning I2C Bus...");
    int nDevices = 0;
    std::string results;

    for (uint8_t address = 1; address < 127; address++) {
        Wire.beginTransmission(address);
        if (Wire.endTransmission() == 0) {
            char buf[64];
            snprintf(buf, sizeof(buf), "  - Found device at address 0x%02X%s\n", address, (address == 0x5A) ? " (Default MLX90614)" : "");
            results += buf;
            nDevices++;
        }
    }

    if (nDevices == 0) {
        controller.serial_port.print("No I2C devices found. Check wiring and pull-up resistors.\n");
    } else {
        results += "Scan complete. Found " + std::to_string(nDevices) + " device(s).\n";
        controller.serial_port.print(results);
    }
}

void MLX90614::cli_set_addr(std::string_view args_sv) {
    unsigned int new_addr;
    if (sscanf(std::string(args_sv).c_str(), "%x", &new_addr) == 1 && set_i2c_address(new_addr)) {
        char buf[64];
        snprintf(buf, sizeof(buf), "Target address updated to 0x%02X and saved to NVS.", new_addr);
        controller.serial_port.print(buf);
    } else {
        controller.serial_port.print("Invalid I2C address provided. Use hex format (e.g., 0x5A).");
    }
}

void MLX90614::cli_set_pins(std::string_view args_sv) {
    int new_sda, new_scl;
    if (sscanf(std::string(args_sv).c_str(), "%d %d", &new_sda, &new_scl) == 2) {
        sda_pin = static_cast<uint8_t>(new_sda);
        scl_pin = static_cast<uint8_t>(new_scl);
        save_to_nvs();

        controller.serial_port.print("Pins updated to SDA=" + std::to_string(sda_pin) + " SCL=" + std::to_string(scl_pin) + ". Initializing bus...");

        // Ensure proper teardown before spinning up new pins if applicable
        #if defined(ESP32) || defined(ESP8266)
        Wire.end();
        #endif

        Wire.begin(sda_pin, scl_pin);
        cli_scan("");

        float test_read = read_i2c_temp(MLX_RAM_TOBJ1);
        sensor_online = !std::isnan(test_read);

        if (sensor_online) {
            controller.serial_port.print("Success! Sensor read " + std::to_string(test_read) + " °C on new pins.");
        } else {
            controller.serial_port.print("Warning: Bus initialized, but no valid data read from sensor.");
        }
    } else {
        controller.serial_port.print("Usage error. Expected: $temp set_pins <sda> <scl>");
    }
}