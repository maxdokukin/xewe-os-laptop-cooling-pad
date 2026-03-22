#pragma once

#include "../../Module/Module.h"
#include <vector>
#include <string>
#include <algorithm>
#include <cstdio>

// Include the Adafruit NeoPixel library
#include <Adafruit_NeoPixel.h>

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
    bool                        set_state                   (uint8_t pin, bool state);
    bool                        set_rgb                     (uint8_t pin, uint8_t r, uint8_t g, uint8_t b);

private:
    static constexpr uint16_t   DEFAULT_STRIP_LENGTH        = 12;

    struct ARGBData {
        uint8_t             pin = 0;
        bool                state = false;
        uint8_t             r = 0;
        uint8_t             g = 0;
        uint8_t             b = 0;
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

    std::vector<ARGBData*>      leds;
    bool                        loaded_from_nvs             {false};
};