#pragma once

#include "../../Module/Module.h"
#include <vector>
#include <string>
#include <algorithm>
#include <cstdio>
#include <Arduino.h> // Included for millis()

// Include the Adafruit NeoPixel & ArduinoJson libraries
#include <Adafruit_NeoPixel.h>
#include <ArduinoJson.h>

struct ARGBConfig : public ModuleConfig {
    // Add any global ARGB config here if needed in the future
};

class ARGB : public Module {
public:
    explicit                    ARGB                        (SystemController& controller);
    virtual                     ~ARGB                       ();

    void                        begin_routines_init         (const ModuleConfig& cfg)       override;
    void                        begin_routines_regular      (const ModuleConfig& cfg)       override;
    void                        loop                        ()                              override;
    void                        reset                       (const bool verbose=false,
                                                             const bool do_restart=true,
                                                             const bool keep_enabled=true)  override;
    std::string                 status                      (const bool verbose=false)      const override;

    bool                        add                         (uint8_t pin);
    bool                        remove                      (uint8_t pin);

    // Note: save_to_nvs defaults to true for normal usage, but can be bypassed for dynamic temp scaling
    bool                        set_state                   (uint8_t pin, bool state, bool save_to_nvs = true);
    bool                        set_rgb                     (uint8_t pin, uint8_t r, uint8_t g, uint8_t b, bool save_to_nvs = true);

    // Broadcast API for driving all LEDs simultaneously
    bool                        set_all_state               (bool state, bool save_to_nvs = true);
    bool                        set_all_rgb                 (uint8_t r, uint8_t g, uint8_t b, bool save_to_nvs = true);

    // API to return ARGB data in JSON format
    std::string                 get_json                    ()                              const;

private:
    static constexpr uint16_t   DEFAULT_STRIP_LENGTH        = 12;
    static constexpr uint32_t   TRANSITION_DURATION_MS      = 1000;

    struct ARGBData {
        uint8_t             pin = 0;
        bool                state = false;

        // Target RGB for NVS and JSON
        uint8_t             r = 0;
        uint8_t             g = 0;
        uint8_t             b = 0;

        // Active animation values
        float               current_r = 0.0f;
        float               current_g = 0.0f;
        float               current_b = 0.0f;

        uint8_t             start_r = 0;
        uint8_t             start_g = 0;
        uint8_t             start_b = 0;

        uint32_t            transition_start_time = 0;
        bool                transitioning = false;

        Adafruit_NeoPixel* strip = nullptr;
    };

    ARGBData* get_led                     (uint8_t pin) const;
    void                        free_led                    (ARGBData* led);
    void                        update_hardware             (const ARGBData* led) const;

    void                        load_from_nvs               ();
    void                        save_all_to_nvs             ();
    std::string                 serialize_led               (const ARGBData* led) const;
    bool                        deserialize_led             (const std::string& config, ARGBData* led) const;
    void                        nvs_clear_all               ();

    void                        cli_add                     (std::string_view args);
    void                        cli_remove                  (std::string_view args);
    void                        cli_set_state               (std::string_view args);
    void                        cli_set_rgb                 (std::string_view args);
    void                        cli_print_json              (std::string_view args);

    std::vector<ARGBData*>      leds;
    bool                        loaded_from_nvs             {false};
};