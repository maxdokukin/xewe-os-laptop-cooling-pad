#pragma once

#include "../../Module/Module.h"
#include <vector>
#include <string>
#include <algorithm>

struct FanTempControllerConfig : public ModuleConfig {
    uint32_t update_interval_ms = 1000; // How often to poll temp and update fans
    uint8_t  failsafe_pwm = 255;        // Speed to set if the curve is empty
};

class FanTempController : public Module {
public:
    struct CurvePoint {
        float temp;
        uint8_t pwm;

        // Sorting logic so the curve is always strictly ascending by temperature
        bool operator<(const CurvePoint& other) const {
            return temp < other.temp;
        }
    };

    explicit                    FanTempController           (SystemController& controller);
    virtual                     ~FanTempController          ();

    void                        begin_routines_regular      (const ModuleConfig& cfg)       override;
    void                        loop                        ()                              override;
    void                        reset                       (const bool verbose=false,
                                                             const bool do_restart=true,
                                                             const bool keep_enabled=true)  override;
    std::string                 status                      (const bool verbose=false)      const override;

    // Core API Methods
    bool                        add_point                   (float temp, uint8_t pwm);
    bool                        remove_point                (float temp);
    void                        clear_curve                 ();
    uint8_t                     calculate_pwm               (float current_temp) const;

private:
    // NVS Serialization
    void                        load_from_nvs               ();
    void                        save_to_nvs                 ();

    // CLI Handlers
    void                        cli_add_point               (std::string_view args);
    void                        cli_remove_point            (std::string_view args);
    void                        cli_clear                   (std::string_view args);

    std::vector<CurvePoint>     curve;
    bool                        loaded_from_nvs             {false};
    uint32_t                    last_update_time            {0};
    uint8_t                     last_applied_pwm            {0};

    // Config cache
    uint32_t                    update_interval_ms          {1000};
    uint8_t                     failsafe_pwm                {255};
};