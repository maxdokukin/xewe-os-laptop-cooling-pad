#pragma once

#include "../../Module/Module.h"
#include <vector>
#include <string>
#include <algorithm>
#include <cstdio>

struct FanConfig : public ModuleConfig {
    float    ema_alpha = 0.3f;
    uint32_t absolute_max_rpm = 10000;
    uint32_t ui_rounding = 10;
};

class Fan : public Module {
public:
    explicit                    Fan                         (SystemController& controller);
    virtual                     ~Fan                        ();

    void                        begin_routines_init         (const ModuleConfig& cfg)       override;
    void                        begin_routines_regular      (const ModuleConfig& cfg)       override;
    void                        loop                        ()                              override;
    void                        reset                       (const bool verbose=false,
                                                             const bool do_restart=true,
                                                             const bool keep_enabled=true)  override;
    std::string                 status                      (const bool verbose=false)      const override;

    bool                        add                         (uint8_t pwm_pin);
    bool                        add_w_tach                  (uint8_t pwm_pin, uint8_t tach_pin);
    bool                        remove                      (uint8_t pwm_pin);
    bool                        set                         (uint8_t pwm_pin, uint8_t speed);
    bool                        set_all                     (uint8_t speed);
    uint32_t                    get_rpm                     (uint8_t pwm_pin)               const;

private:
    struct FanData {
        uint8_t pin_pwm = 0, pin_tach = 0;
        bool    has_tach = false;
        uint8_t speed = 0;

        volatile uint32_t pulse_count = 0;
        uint32_t last_calc_time = 0;

        uint32_t raw_history[3] = {0, 0, 0};
        uint8_t  history_idx = 0, history_count = 0;
        uint32_t ema_rpm = 0, displayed_rpm = 0;
    };

    static void                 tach_isr_handler            (void* arg);

    // Helpers to dramatically reduce bloat and reuse
    FanData* get_fan                     (uint8_t pwm_pin) const;
    void                        free_fan                    (FanData* fan);
    FanData* _create_and_setup           (uint8_t pwm, uint8_t tach, bool has_tach, uint8_t speed);
    bool                        _add                        (uint8_t pwm, uint8_t tach, bool has_tach);

    void                        load_from_nvs               ();
    void                        save_all_to_nvs             ();
    std::string                 serialize_fan               (const FanData* fan) const;
    bool                        deserialize_fan             (const std::string& config, FanData* fan) const;
    void                        nvs_clear_all               ();

    void                        cli_add                     (std::string_view args);
    void                        cli_add_w_tach              (std::string_view args);
    void                        cli_set                     (std::string_view args);
    void                        cli_set_all                 (std::string_view args);
    void                        cli_remove                  (std::string_view args);

    std::vector<FanData*>       fans;
    bool                        loaded_from_nvs             {false};

    float                       ema_alpha                   {0.3f};
    uint32_t                    absolute_max_rpm            {10000};
    uint32_t                    ui_rounding                 {10};

    static constexpr uint32_t   PWM_FREQ                    = 25000;
    static constexpr uint8_t    PWM_RES                     = 8;
};