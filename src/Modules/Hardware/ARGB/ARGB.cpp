#include "ARGB.h"
#include "../../../SystemController/SystemController.h"
#include "../../../Debug.h"

ARGB::ARGB(SystemController& controller)
      : Module(controller, "ARGB", "Adafruit NeoPixel ARGB controller", "argb", true, false, true)
{
    DBG_PRINTF(ARGB, "ARGB(): Initializing ARGB module.\n");

    commands_storage.push_back({ "add", "Add a WS2812B LED strip: <pin>", std::string("$") + lower(module_name) + " add 4", 1, [this](std::string_view args){ cli_add(args); } });
    commands_storage.push_back({ "remove", "Remove an LED strip by its pin: <pin>", std::string("$") + lower(module_name) + " remove 4", 1, [this](std::string_view args){ cli_remove(args); } });
    commands_storage.push_back({ "set_state", "Turn LED strip on/off: <pin> <1|0>", std::string("$") + lower(module_name) + " set_state 4 1", 2, [this](std::string_view args){ cli_set_state(args); } });
    commands_storage.push_back({ "set_rgb", "Set RGB color: <pin> <r> <g> <b>", std::string("$") + lower(module_name) + " set_rgb 4 255 0 0", 4, [this](std::string_view args){ cli_set_rgb(args); } });
    commands_storage.push_back({ "print_json", "Print ARGB data in JSON format", std::string("$") + lower(module_name) + " print_json", 0, [this](std::string_view args){ cli_print_json(args); } });
}

ARGB::~ARGB() {
    DBG_PRINTF(ARGB, "~ARGB(): Destroying module, clearing memory.\n");
    for (auto* led : leds) free_led(led);
    leds.clear();
}

void ARGB::begin_routines_init(const ModuleConfig& cfg) {
    DBG_PRINTF(ARGB, "begin_routines_init(): Initialization routines run.\n");
}

void ARGB::begin_routines_regular(const ModuleConfig& cfg) {
    DBG_PRINTF(ARGB, "begin_routines_regular(): Enabled: %d, Loaded NVS: %d\n", is_enabled(), loaded_from_nvs);
    if (is_enabled() && !loaded_from_nvs) {
        load_from_nvs();

        // Initialize pins 6 and 7 if empty
        if (leds.empty()) {
            add(6);
            add(7);
        }
    }
}

void ARGB::loop() {
    if (is_disabled()) return;
}

void ARGB::reset(const bool verbose, const bool do_restart, const bool keep_enabled) {
    DBG_PRINTF(ARGB, "reset(): Resetting ARGB module.\n");
    nvs_clear_all();
    for (auto* led : leds) free_led(led);
    leds.clear();
    Module::reset(verbose, do_restart, keep_enabled);
}

std::string ARGB::status(const bool verbose) const {
    if (is_disabled()) return "ARGB module disabled";
    if (leds.empty()) return "No ARGB LEDs are currently configured.";

    std::string s = "--- Active ARGB LEDs ---\n";
    for (const auto* l : leds) {
        s += "  - Pin: " + std::to_string(l->pin) +
             " (Len: " + std::to_string(DEFAULT_STRIP_LENGTH) + ")" +
             ", State: " + (l->state ? "ON" : "OFF") +
             ", RGB: (" + std::to_string(l->r) + ", " + std::to_string(l->g) + ", " + std::to_string(l->b) + ")\n";
    }
    s += "------------------------";

    if (verbose) controller.serial_port.print(s);
    return s;
}

std::string ARGB::get_json() const {
    if (is_disabled() || leds.empty()) return "[]";

#if ARDUINOJSON_VERSION_MAJOR >= 7
    JsonDocument doc;
#else
    DynamicJsonDocument doc(1024);
#endif

    JsonArray array = doc.to<JsonArray>();
    for (const auto* l : leds) {
#if ARDUINOJSON_VERSION_MAJOR >= 7
        JsonObject obj = array.add<JsonObject>();
#else
        JsonObject obj = array.createNestedObject();
#endif
        obj["pin"] = l->pin;
        obj["state"] = l->state;
        obj["r"] = l->r;
        obj["g"] = l->g;
        obj["b"] = l->b;
    }

    std::string output;
    serializeJson(doc, output);
    return output;
}

// --- Internal Helpers ---

ARGB::ARGBData* ARGB::get_led(uint8_t pin) const {
    for (auto* l : leds) if (l->pin == pin) return l;
    return nullptr;
}

void ARGB::free_led(ARGBData* l) {
    if (l->strip) {
        l->strip->clear();
        l->strip->show();
        delete l->strip;
    }
    delete l;
}

void ARGB::update_hardware(const ARGBData* l) const {
    if (!l->strip) return;
    if (l->state) l->strip->fill(l->strip->Color(l->r, l->g, l->b));
    else l->strip->clear();
    l->strip->show();
}

// --- Core API Methods ---

bool ARGB::add(uint8_t pin) {
    if (is_disabled() || get_led(pin)) return false;

    Adafruit_NeoPixel* new_strip = new Adafruit_NeoPixel(DEFAULT_STRIP_LENGTH, pin, NEO_GRB + NEO_KHZ800);
    new_strip->begin();

    ARGBData* l = new ARGBData{pin, false, 255, 255, 255, new_strip};
    leds.push_back(l);

    update_hardware(l);
    save_all_to_nvs();
    return true;
}

bool ARGB::remove(uint8_t pin) {
    if (is_disabled()) return false;
    auto it = std::find_if(leds.begin(), leds.end(), [pin](ARGBData* l) { return l->pin == pin; });
    if (it == leds.end()) return false;

    free_led(*it);
    leds.erase(it);
    save_all_to_nvs();
    return true;
}

bool ARGB::set_state(uint8_t pin, bool state, bool save_to_nvs) {
    if (is_disabled()) return false;
    if (ARGBData* l = get_led(pin)) {
        if (l->state != state) {
            l->state = state;
            update_hardware(l);
            if (save_to_nvs) save_all_to_nvs();
        }
        return true;
    }
    return false;
}

bool ARGB::set_rgb(uint8_t pin, uint8_t r, uint8_t g, uint8_t b, bool save_to_nvs) {
    if (is_disabled()) return false;
    if (ARGBData* l = get_led(pin)) {
        if (l->r != r || l->g != g || l->b != b) {
            l->r = r; l->g = g; l->b = b;
            if (l->state) update_hardware(l);
            if (save_to_nvs) save_all_to_nvs();
        }
        return true;
    }
    return false;
}

bool ARGB::set_all_state(bool state, bool save_to_nvs) {
    if (is_disabled()) return false;
    bool changed = false;
    for (auto* l : leds) {
        if (l->state != state) {
            l->state = state;
            update_hardware(l);
            changed = true;
        }
    }
    if (changed && save_to_nvs) save_all_to_nvs();
    return true;
}

bool ARGB::set_all_rgb(uint8_t r, uint8_t g, uint8_t b, bool save_to_nvs) {
    if (is_disabled()) return false;
    bool changed = false;
    for (auto* l : leds) {
        if (l->r != r || l->g != g || l->b != b) {
            l->r = r; l->g = g; l->b = b;
            if (l->state) update_hardware(l);
            changed = true;
        }
    }
    if (changed && save_to_nvs) save_all_to_nvs();
    return true;
}

// --- CLI Handlers ---
void ARGB::cli_add(std::string_view args) {
    int pin;
    if (sscanf(std::string(args).c_str(), "%d", &pin) == 1 && add(pin)) controller.serial_port.print("LED strip added.");
    else controller.serial_port.print("Failed to add LED strip.");
}
void ARGB::cli_remove(std::string_view args) {
    int pin;
    if (sscanf(std::string(args).c_str(), "%d", &pin) == 1 && remove(pin)) controller.serial_port.print("LED strip removed.");
    else controller.serial_port.print("Failed to remove LED strip.");
}
void ARGB::cli_set_state(std::string_view args) {
    int pin, state;
    if (sscanf(std::string(args).c_str(), "%d %d", &pin, &state) == 2 && set_state(pin, state > 0)) controller.serial_port.print("State updated.");
    else controller.serial_port.print("Failed to update LED state.");
}
void ARGB::cli_set_rgb(std::string_view args) {
    int pin, r, g, b;
    if (sscanf(std::string(args).c_str(), "%d %d %d %d", &pin, &r, &g, &b) == 4 && set_rgb(pin, r, g, b)) controller.serial_port.print("RGB updated.");
    else controller.serial_port.print("Failed to update RGB.");
}
void ARGB::cli_print_json(std::string_view args) {
    controller.serial_port.print(get_json());
}

// --- NVS Storage Helpers ---
std::string ARGB::serialize_led(const ARGBData* l) const {
    char buf[64];
    snprintf(buf, sizeof(buf), "%u %d %u %u %u", l->pin, l->state, l->r, l->g, l->b);
    return std::string(buf);
}

bool ARGB::deserialize_led(const std::string& config, ARGBData* l) const {
    unsigned int pin, state, r, g, b;
    if (sscanf(config.c_str(), "%u %u %u %u %u", &pin, &state, &r, &g, &b) != 5) return false;

    l->pin = static_cast<uint8_t>(pin);
    l->state = (state > 0);
    l->r = static_cast<uint8_t>(r);
    l->g = static_cast<uint8_t>(g);
    l->b = static_cast<uint8_t>(b);
    return true;
}

void ARGB::load_from_nvs() {
    if (is_disabled()) return;
    for (auto* l : leds) free_led(l);
    leds.clear();

    int count = controller.nvs.read_uint8(nvs_key, "argb_count", 0);
    for (int i = 0; i < count; i++) {
        ARGBData temp;
        if (deserialize_led(controller.nvs.read_str(nvs_key, "argb_cfg_" + std::to_string(i)), &temp)) {
            Adafruit_NeoPixel* new_strip = new Adafruit_NeoPixel(DEFAULT_STRIP_LENGTH, temp.pin, NEO_GRB + NEO_KHZ800);
            new_strip->begin();

            ARGBData* l = new ARGBData{temp.pin, temp.state, temp.r, temp.g, temp.b, new_strip};
            leds.push_back(l);
            update_hardware(l);
        }
    }
    loaded_from_nvs = true;
}

void ARGB::save_all_to_nvs() {
    if (is_disabled()) return;
    nvs_clear_all();
    controller.nvs.write_uint8(nvs_key, "argb_count", leds.size());
    for (size_t i = 0; i < leds.size(); i++) {
        controller.nvs.write_str(nvs_key, "argb_cfg_" + std::to_string(i), serialize_led(leds[i]));
    }
}

void ARGB::nvs_clear_all() {
    if (is_disabled()) return;
    int count = controller.nvs.read_uint8(nvs_key, "argb_count", 0);
    for (int i = 0; i < count; i++) {
        controller.nvs.remove(nvs_key, "argb_cfg_" + std::to_string(i));
    }
    controller.nvs.write_uint8(nvs_key, "argb_count", 0);
}