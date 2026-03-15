#include <rtc.h>

#include "rtc_driver.h"

static bool is_leap_year(int16_t year) {
    return ((year % 4 == 0) && (year % 100 != 0)) || (year % 400 == 0);
}

static int8_t days_in_month(int16_t year, int8_t month) {
    static const int8_t k_days[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month == 2) {
        return (int8_t)(is_leap_year(year) ? 29 : 28);
    }
    if (month < 1 || month > 12) {
        return 31;
    }
    return k_days[month - 1];
}

static void normalize_datetime(app_datetime_t *dt) {
    if (dt->year < 2000) dt->year = 2000;
    if (dt->year > 2099) dt->year = 2099;
    if (dt->month < 1) dt->month = 1;
    if (dt->month > 12) dt->month = 12;

    int8_t max_day = days_in_month(dt->year, dt->month);
    if (dt->day < 1) dt->day = 1;
    if (dt->day > max_day) dt->day = max_day;

    if (dt->hour < 0) dt->hour = 0;
    if (dt->hour > 23) dt->hour = 23;
    if (dt->minute < 0) dt->minute = 0;
    if (dt->minute > 59) dt->minute = 59;
    if (dt->second < 0) dt->second = 0;
    if (dt->second > 59) dt->second = 59;
}

static bool rtc_time_is_reasonable(const app_datetime_t *dt) {
    if (dt->year < 2020 || dt->year > 2099) return false;
    if (dt->month < 1 || dt->month > 12) return false;
    if (dt->day < 1 || dt->day > days_in_month(dt->year, dt->month)) return false;
    if (dt->hour < 0 || dt->hour > 23) return false;
    if (dt->minute < 0 || dt->minute > 59) return false;
    if (dt->second < 0 || dt->second > 59) return false;
    return true;
}

bool rtc_driver_init(void) {
    if (rtc_init() != 0) {
        return false;
    }

    app_datetime_t dt;
    if (!rtc_driver_get_datetime(&dt)) {
        static const app_datetime_t k_default_dt = {2025, 1, 1, 12, 0, 0};
        rtc_driver_set_datetime(&k_default_dt);
    }
    return true;
}

bool rtc_driver_get_datetime(app_datetime_t *dt) {
    int year = 0;
    int month = 0;
    int day = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;

    if (rtc_timer_get(&year, &month, &day, &hour, &minute, &second) != 0) {
        return false;
    }

    dt->year = (int16_t)year;
    dt->month = (int8_t)month;
    dt->day = (int8_t)day;
    dt->hour = (int8_t)hour;
    dt->minute = (int8_t)minute;
    dt->second = (int8_t)second;
    normalize_datetime(dt);
    return rtc_time_is_reasonable(dt);
}

bool rtc_driver_set_datetime(const app_datetime_t *dt) {
    app_datetime_t normalized = *dt;
    normalize_datetime(&normalized);

    return rtc_timer_set(normalized.year, normalized.month, normalized.day,
                         normalized.hour, normalized.minute, normalized.second) == 0;
}
