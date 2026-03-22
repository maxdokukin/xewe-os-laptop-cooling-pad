#pragma once

#include "../../Module/Module.h"
#include <vector>
#include <string>
#include <algorithm>

struct FanConfig : public ModuleConfig {
    // Industry-Grade Filtering Parameters
    float    ema_alpha = 0.3f;           // Smoothing factor (0.0 to 1.0)
    uint32_t absolute_max_rpm = 10000;   // Absolute hardware cap for EMI noise
    uint32_t ui_rounding = 10;           // Hysteresis: Rounds the final UI value to the nearest 10 RPM
};

class Fan : public Module {
public:
    explicit                    Fan                         (SystemController& controller);
    virtual                     ~Fan                        ();

    void                        begin_routines_regular      (const ModuleConfig& cfg)       override;
    void                        loop                        ()                              override;
    void                        reset                       (const bool verbose=false,
                                                             const bool do_restart=true,
                                                             const bool keep_enabled=true)  override;
    std::string                 status                      (const bool verbose=false)      const override;

    // Core module methods
    bool                        add_fan                     (uint8_t pwm_pin);
    bool                        add_fan_w_tach              (uint8_t pwm_pin, uint8_t tach_pin);
    bool                        remove_fan                  (uint8_t pwm_pin);
    bool                        set_fan_speed               (uint8_t pwm_pin, uint8_t speed);

    // API to get the final stabilized RPM
    uint32_t                    get_rpm                     (uint8_t pwm_pin)               const;

private:
    struct FanData {
        uint8_t pin_pwm = 0;
        uint8_t pin_tach = 0;
        bool    has_tach = false;
        uint8_t speed = 0;

        // Interrupt specific fields
        volatile uint32_t pulse_count = 0;
        uint32_t last_calc_time = 0;

        // Median Filter Buffer
        uint32_t raw_history[3] = {0, 0, 0};
        uint8_t  history_idx = 0;
        uint8_t  history_count = 0;

        // Smoothing Data
        uint32_t ema_rpm = 0;
        uint32_t displayed_rpm = 0;
    };

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

    std::vector<FanData*>       fans;
    bool                        loaded_from_nvs             {false};

    // Internal storage for configuration
    float                       ema_alpha                   {0.3f};
    uint32_t                    absolute_max_rpm            {10000};
    uint32_t                    ui_rounding                 {10};
};