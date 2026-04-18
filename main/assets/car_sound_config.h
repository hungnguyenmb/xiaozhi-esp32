#pragma once

#include <string_view>

namespace CarSounds {
    extern const char ogg_car_engine_start_start[] asm("_binary_car_engine_start_ogg_start");
    extern const char ogg_car_engine_start_end[] asm("_binary_car_engine_start_ogg_end");
    static const std::string_view OGG_ENGINE_START {
        static_cast<const char*>(ogg_car_engine_start_start),
        static_cast<size_t>(ogg_car_engine_start_end - ogg_car_engine_start_start)
    };

    extern const char ogg_car_engine_rev_start[] asm("_binary_car_engine_rev_ogg_start");
    extern const char ogg_car_engine_rev_end[] asm("_binary_car_engine_rev_ogg_end");
    static const std::string_view OGG_ENGINE_REV {
        static_cast<const char*>(ogg_car_engine_rev_start),
        static_cast<size_t>(ogg_car_engine_rev_end - ogg_car_engine_rev_start)
    };

    extern const char ogg_car_skid_start[] asm("_binary_car_skid_ogg_start");
    extern const char ogg_car_skid_end[] asm("_binary_car_skid_ogg_end");
    static const std::string_view OGG_SKID {
        static_cast<const char*>(ogg_car_skid_start),
        static_cast<size_t>(ogg_car_skid_end - ogg_car_skid_start)
    };

    extern const char ogg_car_brake_scrub_start[] asm("_binary_car_brake_scrub_ogg_start");
    extern const char ogg_car_brake_scrub_end[] asm("_binary_car_brake_scrub_ogg_end");
    static const std::string_view OGG_BRAKE_SCRUB {
        static_cast<const char*>(ogg_car_brake_scrub_start),
        static_cast<size_t>(ogg_car_brake_scrub_end - ogg_car_brake_scrub_start)
    };

    extern const char ogg_car_tieng_no_may_start[] asm("_binary_car_tieng_no_may_ogg_start");
    extern const char ogg_car_tieng_no_may_end[] asm("_binary_car_tieng_no_may_ogg_end");
    static const std::string_view OGG_TIENG_NO_MAY {
        static_cast<const char*>(ogg_car_tieng_no_may_start),
        static_cast<size_t>(ogg_car_tieng_no_may_end - ogg_car_tieng_no_may_start)
    };
}
