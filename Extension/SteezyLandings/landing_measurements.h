#pragma once
#include <algorithm>
#include <cmath>
namespace dingosdk::steezy_landings {
// One flight's measurements remain available until the classifier consumes them. The first
// grounded frame can precede classification; it must not erase the just-finished flight.
struct FlightMeasurements {
    double seconds{}, peak_y{}, drop{};
    bool airborne{}, pending{}, landed{}, time_known{}, height_known{};
    void finish(double y, bool position_known) noexcept {
        if (!pending || landed) return;
        height_known = height_known && position_known && std::isfinite(y);
        if (height_known) drop = std::max(0.0, peak_y - y);
        landed = true;
    }
    void sample(bool in_air, double dt, double y, bool position_known) noexcept {
        if (in_air) {
            if (!airborne) {
                seconds = drop = 0; pending = true; landed = false; time_known = true;
                height_known = position_known && std::isfinite(y); peak_y = height_known ? y : 0;
            }
            if (!landed) {
                const bool valid_dt = std::isfinite(dt) && dt > 0 && dt <= .25;
                time_known = time_known && valid_dt;
                if (valid_dt) seconds += dt;
                height_known = height_known && position_known && std::isfinite(y);
                if (height_known) peak_y = std::max(peak_y, y);
            }
        } else if (airborne) finish(y, position_known);
        airborne = in_air;
    }
    void consume(double y, bool position_known) noexcept { finish(y, position_known); pending = false; }
};
}
