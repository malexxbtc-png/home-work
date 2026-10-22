#pragma once

#include <time.h>

/* Broken-down UTC time -> Unix time (timegm without touching the TZ variable). */
time_t tm_to_utc(const struct tm *t);
