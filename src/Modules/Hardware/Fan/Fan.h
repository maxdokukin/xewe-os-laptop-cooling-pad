#pragma once

#include "../../Module/Module.h"
#include <vector>
#include <string>

struct FanConfig : public ModuleConfig {};

class Fan : public Module {
public:
    explicit                    Fan                         (SystemController& controller);
    virtual                     ~Fan                        ();

    void                        begin_routines_regular      (const ModuleConfig& cfg)       override;
    void                        loop                        ()                              override;
    void                        reset                       (const bool verbose=false,
                                                             const bool do_restart=true,
                                                             const bool keep_enabled=true)  override;
    string                      status                      (const bool verbose=false)      const override;

    // Core module methods
    bool                        add_fan                     (uint8_t pwm_pin);
    bool                        add_fan_w_tach              (uint8_t pwm_pin, uint8_t tach_pin);
    bool                        remove_fan                  (uint8_t pwm_pin);
    bool                        set_fan_speed               (uint8_t pwm_pin, uint8_t speed);

private:
    struct FanData {
        uint8_t pin_pwm;
        uint8_t pin_tach;
        bool    has_tach;
        uint8_t speed;

        // Interrupt & RPM specific fields
        volatile uint32_t pulse_count;
        uint32_t rpm;
        uint32_t last_calc_time;
    };

    // Removed IRAM_ATTR here; it belongs on the definition in the .cpp file
    static void                 tach_isr_handler            (void* arg);

    // NVS serialization helpers
    void                        load_from_nvs               ();
    void                        save_all_to_nvs             ();
    std::string                 serialize_fan               (const FanData* fan) const;
    bool                        deserialize_fan             (const std::string& config, FanData* fan) const;
    void                        nvs_clear_all               ();

    // CLI Handlers
    void                        cli_add                     (std::string_view args);
    void                        cli_add_w_tach              (std::string_view args);
    void                        cli_set                     (std::string_view args);
    void                        cli_remove                  (std::string_view args);

    // Storing as pointers so memory addresses remain stable for the ISR when vector resizes
    std::vector<FanData*>       fans;
    bool                        loaded_from_nvs             {false};
};