#include "timeutil.h"

/* Days-from-civil algorithm (Howard Hinnant). */
time_t tm_to_utc(const struct tm *t)
{
    int y = t->tm_year + 1900;
    int m = t->tm_mon + 1;
    int d = t->tm_mday;

    y -= (m <= 2);
    int era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (unsigned)((153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1);
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    long days = (long)era * 146097L + (long)doe - 719468L;

    return (time_t)(days * 86400L + t->tm_hour * 3600L + t->tm_min * 60L + t->tm_sec);
}
