#include <math.h>
#include <stdlib.h>
#include "joy_math.h"

#define PI_VAL 3.14159265358979323846

bool joy_vector_angle_deg(int x_pct, int y_pct, int min_mag_pct, int *angle_deg)
{
    const double mag = sqrt((double)x_pct * x_pct + (double)y_pct * y_pct);
    if (mag < (double)min_mag_pct || mag == 0.0) {
        return false;
    }
    /* atan2(x, y): вгору (y>0, x=0) -> 0°, вправо -> +90° */
    const double a = atan2((double)x_pct, (double)y_pct) * 180.0 / PI_VAL;
    long r = lround(a);
    if (r > 180)  r = 180;
    if (r < -180) r = -180;
    *angle_deg = (int)r;
    return true;
}

int joy_motor_percent(int y_pct, bool pressed_edge, bool *halt)
{
    if (pressed_edge) {
        *halt = true;
    } else if (y_pct == 0) {
        *halt = false;             /* стік у центрі — знімаємо зупинку */
    }
    if (*halt) {
        return 0;
    }
    if (y_pct > 100)  y_pct = 100;
    if (y_pct < -100) y_pct = -100;
    return y_pct;
}

int joy_axis_angle_deg(int x_pct, int max_deg)
{
    if (x_pct > 100)  x_pct = 100;
    if (x_pct < -100) x_pct = -100;
    return x_pct * max_deg / 100;
}
