#include "MLX90614.h"
#include "../../../../SystemController/SystemController.h"
#include "../../../../Debug.h"
#include <Wire.h>

MLX90614::MLX90614(SystemController& controller)
      : Module(controller, "MLX90614", "Contactless I2C Temperature Sensor", "temp", true, false, true)
{
    DBG_PRINTF(MLX90614, "MLX90614(): Initializing temperature sensor module.\n");

    commands_storage.push_back({ "read", "Force a manual read of the sensor", std::string("$") + lower(module_name) + " read", 0, [this](std::string_view args){ cli_read(args); } });
    commands_storage.push_back({ "scan", "Scan the I2C bus for connected devices", std::string("$") + lower(module_name) + " scan", 0, [this](std::string_view args){ cli_scan(args); } });
    commands_storage.push_back({ "set_addr", "Set the I2C address (Hex format, e.g., 0x5A)", std::string("$") + lower(module_name) + " set_addr 0x5A", 1, [this](std::string_view args){ cli_set_addr(args); } });
    commands_storage.push_back({ "set_pins", "Set I2C SDA and SCL pins: <sda> <scl>", std::string("$") + lower(module_name) + " set_pins 21 22", 2, [this](std::string_view args){ cli_set_pins(args); } });
}

MLX90614::~MLX90614() {
    DBG_PRINTF(MLX90614, "~MLX90614(): Destroying module.\n");
}

void MLX90614::begin_routines_init(const ModuleConfig& cfg) {
    const MLX90614Config& temp_cfg = static_cast<const MLX90614Config&>(cfg);

    // Set fallback defaults from config struct
    if (sda_pin == 255) sda_pin = temp_cfg.sda_pin;
    if (scl_pin == 255) scl_pin = temp_cfg.scl_pin;
    i2c_address = temp_cfg.default_i2c_address;

    if (is_enabled()) {
        if (!loaded_from_nvs) {
            load_from_nvs(); // Overrides code configs with NVS values if they exist
        }

        // INTERACTIVE PIN CONFIGURATION: Blocks until valid pins are entered
        if (sda_pin == 255 || scl_pin == 255) {
            controller.serial_port.print_spacer();
            controller.serial_port.print_header("MLX90614 Configuration");
            controller.serial_port.print("I2C pins are currently unconfigured. Please set them now.");

            sda_pin = controller.serial_port.get_uint8("Enter SDA Pin (0-99): ", 0, 99);
            scl_pin = controller.serial_port.get_uint8("Enter SCL Pin (0-99): ", 0, 99);

            save_to_nvs(); // Immediately save so we don't prompt on next boot

            controller.serial_port.print("Settings saved to NVS!");
            controller.serial_port.print_separator();
        }

        // Initialize the bus with strictly configured pins
        std::string init_msg = "MLX90614 Init: Starting I2C on SDA=" + std::to_string(sda_pin) + " SCL=" + std::to_string(scl_pin) + "\n";
        controller.serial_port.print(init_msg);
        Wire.begin(sda_pin, scl_pin);

        // Perform automated bus scan
        cli_scan("");

        // Confirm target address and attempt initial read
        cached_object_temp = read_i2c_temp(MLX_RAM_TOBJ1);
        cached_ambient_temp = read_i2c_temp(MLX_RAM_TA);

        if (!isnan(cached_object_temp) && !isnan(cached_ambient_temp)) {
            sensor_online = true;
            char hex_str[10]; snprintf(hex_str, sizeof(hex_str), "0x%02X", i2c_address);
            controller.serial_port.print("MLX90614 Init Success: Valid data read from " + std::string(hex_str) + ". Temp: " + std::to_string(cached_object_temp) + " °C");
        } else {
            sensor_online = false;
            controller.serial_port.print("MLX90614 Init Failed: Could not read valid data from configured address.");
        }
    }
}

void MLX90614::begin_routines_regular(const ModuleConfig& cfg) {
    const MLX90614Config& temp_cfg = static_cast<const MLX90614Config&>(cfg);
    poll_interval_ms = temp_cfg.poll_interval_ms;
    error_temp = temp_cfg.error_temp;

    if (is_enabled()) {
        last_read_time = millis();
    }
}

void MLX90614::loop() {
    if (is_disabled() || sda_pin == 255 || scl_pin == 255) return;

    uint32_t current_time = millis();
    if (current_time - last_read_time >= poll_interval_ms) {
        last_read_time = current_time;

        float obj_temp = read_i2c_temp(MLX_RAM_TOBJ1);
        float amb_temp = read_i2c_temp(MLX_RAM_TA);

        if (isnan(obj_temp) || isnan(amb_temp)) {
            sensor_online = false;
        } else {
            sensor_online = true;
            cached_object_temp = obj_temp;
            cached_ambient_temp = amb_temp;
        }
    }
}

void MLX90614::reset(const bool verbose, const bool do_restart, const bool keep_enabled) {
    DBG_PRINTF(MLX90614, "reset(): Resetting temperature sensor module.\n");
    cached_object_temp = 0.0f;
    cached_ambient_temp = 0.0f;
    sensor_online = false;

    // Wipe configuration from NVS to force user to set it up again
    controller.nvs.remove(nvs_key, "i2c_addr");
    controller.nvs.remove(nvs_key, "sda_pin");
    controller.nvs.remove(nvs_key, "scl_pin");

    // Clear local cache
    sda_pin = 255;
    scl_pin = 255;
    loaded_from_nvs = false;

    Module::reset(verbose, do_restart, keep_enabled);
}

std::string MLX90614::status(const bool verbose) const {
    if (is_disabled()) return "MLX90614 module disabled";

    std::string s = "--- MLX90614 Status ---\n";

    if (sda_pin == 255 || scl_pin == 255) {
        s += "  [WARNING] Pins unconfigured. Run: $temp set_pins <sda> <scl>\n";
    } else {
        s += "  Pins:      SDA=" + std::to_string(sda_pin) + " SCL=" + std::to_string(scl_pin) + "\n";
    }

    char hex_str[10];
    snprintf(hex_str, sizeof(hex_str), "0x%02X", i2c_address);
    s += "  I2C Addr:  " + std::string(hex_str) + "\n";
    s += "  Online:    " + std::string(sensor_online ? "YES" : "NO") + "\n";
    s += "  Object:    " + std::to_string(cached_object_temp) + " °C\n";
    s += "  Ambient:   " + std::to_string(cached_ambient_temp) + " °C\n";
    s += "-----------------------";

    if (verbose) controller.serial_port.print(s);
    return s;
}

// --- Core API Methods ---

float MLX90614::get_temp() const {
    if (is_disabled() || !sensor_online) return error_temp;
    return cached_object_temp;
}

float MLX90614::get_ambient_temp() const {
    if (is_disabled() || !sensor_online) return error_temp;
    return cached_ambient_temp;
}

bool MLX90614::is_online() const {
    return sensor_online;
}

bool MLX90614::set_i2c_address(uint8_t new_address) {
    if (new_address == 0 || new_address > 0x7F) return false;

    i2c_address = new_address;
    save_to_nvs();

    if (sda_pin != 255 && scl_pin != 255) {
        float test_read = read_i2c_temp(MLX_RAM_TOBJ1);
        sensor_online = !isnan(test_read);
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

    loaded_from_nvs = true;
}

void MLX90614::save_to_nvs() {
    controller.nvs.write_uint8(nvs_key, "i2c_addr", i2c_address);
    if (sda_pin != 255) controller.nvs.write_uint8(nvs_key, "sda_pin", sda_pin);
    if (scl_pin != 255) controller.nvs.write_uint8(nvs_key, "scl_pin", scl_pin);
}

float MLX90614::read_i2c_temp(uint8_t register_address) {
    if (sda_pin == 255 || scl_pin == 255) return NAN;

    Wire.beginTransmission(i2c_address);
    Wire.write(register_address);
    if (Wire.endTransmission(false) != 0) {
        return NAN;
    }

    Wire.requestFrom(i2c_address, (uint8_t)3);

    if (Wire.available() >= 2) {
        uint16_t lsb = Wire.read();
        uint16_t msb = Wire.read();
        Wire.read();

        uint16_t raw_temp = (msb << 8) | lsb;
        return (static_cast<float>(raw_temp) * 0.02f) - 273.15f;
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
    if (isnan(obj_temp)) {
        controller.serial_port.print("Failed to read sensor! Check I2C wiring or address.");
    } else {
        cached_object_temp = obj_temp;
        sensor_online = true;
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
    std::string results = "";

    for (uint8_t address = 1; address < 127; address++) {
        Wire.beginTransmission(address);
        uint8_t error = Wire.endTransmission();

        if (error == 0) {
            char hex_str[10];
            snprintf(hex_str, sizeof(hex_str), "0x%02X", address);
            results += "  - Found device at address " + std::string(hex_str);
            if (address == 0x5A) results += " (Default MLX90614)";
            results += "\n";
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
    std::string args(args_sv); trim(args);
    if (args.empty()) return;

    try {
        uint8_t new_addr = static_cast<uint8_t>(std::stoul(args, nullptr, 16));
        if (set_i2c_address(new_addr)) {
            char hex_str[10]; snprintf(hex_str, sizeof(hex_str), "0x%02X", new_addr);
            controller.serial_port.print("Target address updated to " + std::string(hex_str) + " and saved to NVS.");
        } else {
            controller.serial_port.print("Invalid I2C address provided.");
        }
    } catch (...) {
        controller.serial_port.print("Failed to parse address. Use hex format (e.g., 0x5A).");
    }
}

void MLX90614::cli_set_pins(std::string_view args_sv) {
    std::string args(args_sv); trim(args);
    auto sp = args.find(' ');
    if (sp == std::string::npos) {
        controller.serial_port.print("Usage error. Expected: $temp set_pins <sda> <scl>");
        return;
    }

    try {
        uint8_t new_sda = static_cast<uint8_t>(std::stoi(args.substr(0, sp)));
        uint8_t new_scl = static_cast<uint8_t>(std::stoi(args.substr(sp + 1)));

        sda_pin = new_sda;
        scl_pin = new_scl;
        save_to_nvs();

        controller.serial_port.print("Pins updated to SDA=" + std::to_string(sda_pin) + " SCL=" + std::to_string(scl_pin) + ". Initializing bus...");

        // Boot up the bus now that we have real pins
        Wire.begin(sda_pin, scl_pin);
        cli_scan("");

        float test_read = read_i2c_temp(MLX_RAM_TOBJ1);
        if (!isnan(test_read)) {
            sensor_online = true;
            controller.serial_port.print("Success! Sensor read " + std::to_string(test_read) + " °C on new pins.");
        } else {
            sensor_online = false;
            controller.serial_port.print("Warning: Bus initialized, but no valid data read from sensor.");
        }
    } catch (...) {
        controller.serial_port.print("Failed to parse pins. Make sure they are valid integers.");
    }
}