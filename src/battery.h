#pragma once

namespace Battery {
    // Reads current battery voltage in millivolts through the T-Display S3's
    // 2:1 divider on GPIO4. Averages a few samples to damp ADC noise.
    int millivolts();

    // Piecewise-linear LiPo state-of-charge estimate (0..100) from millivolts.
    int percent(int mv);

    // True if voltage is being held by the charger IC (USB plugged in and
    // charging or topped off). Heuristic: sustained >= 4.15 V.
    bool isCharging(int mv);
}
