#pragma once

#include "../../../Module/Module.h"
#include <string>

struct MLX90614Config : public ModuleConfig {
    uint8_t  sda_pin = 255;
    uint8_t  scl_pin = 255;
    uint8_t  default_i2c_address = 0x5A;
    uint32_t poll_interval_ms = 500;
    float    error_temp = 255.0f;
};

class MLX90614 : public Module {
public:
    explicit                    MLX90614                    (SystemController& controller);
    virtual                     ~MLX90614                   () override = default;

    void                        begin_routines_init         (const ModuleConfig& cfg)       override;
    void                        begin_routines_regular      (const ModuleConfig& cfg)       override;
    void                        loop                        ()                              override;
    void                        reset                       (const bool verbose=false,
                                                             const bool do_restart=true,
                                                             const bool keep_enabled=true)  override;
    std::string                 status                      (const bool verbose=false)      const override;

    // Core API Methods
    float                       get_temp                    () const;
    float                       get_ambient_temp            () const;
    bool                        is_online                   () const;
    bool                        set_i2c_address             (uint8_t new_address);
    std::string                 get_json                    () const;

private:
    float                       read_i2c_temp               (uint8_t register_address) const;
    void                        load_from_nvs               ();
    void                        save_to_nvs                 () const;

    // CLI Handlers
    void                        cli_read                    (std::string_view args);
    void                        cli_scan                    (std::string_view args);
    void                        cli_set_addr                (std::string_view args);
    void                        cli_set_pins                (std::string_view args);
    void                        cli_print_json              (std::string_view args); // <-- Added CLI handler

    // State & Cache
    float                       cached_object_temp          {0.0f};
    float                       cached_ambient_temp         {0.0f};
    bool                        sensor_online               {false};
    uint32_t                    last_read_time              {0};

    // Config
    uint8_t                     sda_pin                     {255};
    uint8_t                     scl_pin                     {255};
    uint8_t                     i2c_address                 {0x5A};
    uint32_t                    poll_interval_ms            {500};
    float                       error_temp                  {255.0f};

    // MLX90614 RAM Addresses
    static constexpr uint8_t    MLX_RAM_TA                  = 0x06;
    static constexpr uint8_t    MLX_RAM_TOBJ1               = 0x07;
};