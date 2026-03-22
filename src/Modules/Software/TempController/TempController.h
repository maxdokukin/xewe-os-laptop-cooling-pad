#pragma once

#include "../../Module/Module.h"
#include <vector>
#include <string>
#include <algorithm>
#include <cstdio>
#include <ArduinoJson.h>

struct TempControllerConfig : public ModuleConfig {
    uint32_t update_interval_ms = 1000;
};

struct TempPoint {
    float temp;
    uint8_t fan_speed; // Always stored as 0-100%

    // Sort by temperature ascending
    bool operator<(const TempPoint& other) const {
        return temp < other.temp;
    }
};

class TempController : public Module {
public:
    explicit                    TempController              (SystemController& controller);
    virtual                     ~TempController             () override;

    void                        begin_routines_init         (const ModuleConfig& cfg)       override;
    void                        begin_routines_regular      (const ModuleConfig& cfg)       override;
    void                        loop                        ()                              override;
    void                        reset                       (const bool verbose=false,
                                                             const bool do_restart=true,
                                                             const bool keep_enabled=true)  override;
    std::string                 status                      (const bool verbose=false)      const override;

    // Target API
    bool                        add_point                   (float temp, uint8_t fan_speed_pct);
    bool                        remove_point                (float temp);
    uint8_t                     get_target_speed            (float current_temp)            const;

    // Color API
    bool                        set_cold_color              (const std::string& hex_color);
    bool                        set_hot_color               (const std::string& hex_color);
    std::string                 get_cold_color              ()                              const;
    std::string                 get_hot_color               ()                              const;

    // Serialization
    std::string                 get_json                    ()                              const;

private:
    std::vector<TempPoint>      curve;
    std::string                 cold_color                  {"#0000FF"}; // Default Blue
    std::string                 hot_color                   {"#FF0000"}; // Default Red

    uint32_t                    last_update_time            {0};
    uint32_t                    update_interval_ms          {1000};
    bool                        loaded_from_nvs             {false};

    void                        load_from_nvs               ();
    void                        save_all_to_nvs             ();
    void                        nvs_clear_all               ();
    void                        update_argb_colors          (float current_temp);

    // CLI Handlers
    void                        cli_add_point               (std::string_view args);
    void                        cli_remove_point            (std::string_view args);
    void                        cli_set_cold_color          (std::string_view args);
    void                        cli_set_hot_color           (std::string_view args);
    void                        cli_print_json              (std::string_view args);
};