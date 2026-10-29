#include "ui.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "config.h"
#include "events.h"

static const char *const day_names[7] = {"Sunday", "Monday", "Tuesday", "Wednesday",
                                         "Thursday", "Friday", "Saturday"};

const char *ui_screen_title(int screen)
{
    static const char *const titles[SCR_COUNT] = {"HOME", "METEO", "OPEN-METEO", "DATE/TIME",
                                                  "EVENT", "STATUS", "KEYS TEST"};
    return (screen >= 0 && screen < SCR_COUNT) ? titles[screen] : "?";
}

void ui_init(ui_t *ui, uint32_t now_ms)
{
    memset(ui, 0, sizeof(*ui));
    ui->last_input_ms = now_ms;
}

void ui_toast(ui_t *ui, const char *text, uint32_t now_ms, uint32_t duration_ms)
{
    snprintf(ui->toast, sizeof(ui->toast), "%s", text);
    ui->toast_until_ms = now_ms + duration_ms;
}

ui_action_t ui_on_key(ui_t *ui, key_event_t ev, uint32_t now_ms)
{
    if (ev.type != KEY_EV_PRESS && ev.type != KEY_EV_REPEAT) {
        return UI_ACT_NONE;
    }
    ui->last_input_ms = now_ms;
    ui->toast[0] = '\0';

    switch (ev.key) {
    case KEY_RIGHT:
        ui->screen = (ui->screen + 1) % SCR_COUNT;
        ui->sub = 0;
        ui->ev_idx = 0;
        break;
    case KEY_LEFT:
        ui->screen = (ui->screen + SCR_COUNT - 1) % SCR_COUNT;
        ui->sub = 0;
        ui->ev_idx = 0;
        break;
    case KEY_UP:
        if (ui->screen == SCR_WX) {
            ui->sub = (ui->sub + 1) % 2;
        } else if (ui->screen == SCR_EVENT && ui->ev_idx + 1 < events_count()) {
            ui->ev_idx++;                         /* старіша подія */
        }
        break;
    case KEY_DOWN:
        if (ui->screen == SCR_WX) {
            ui->sub = (ui->sub + 1) % 2;
        } else if (ui->screen == SCR_EVENT && ui->ev_idx > 0) {
            ui->ev_idx--;                         /* новіша подія */
        }
        break;
    case KEY_SELECT:
        if (ev.type == KEY_EV_PRESS && (ui->screen == SCR_EVENT || ui->screen == SCR_HOME)) {
            return UI_ACT_PHOTO;
        }
        break;
    default:
        break;
    }
    return UI_ACT_NONE;
}

void ui_tick(ui_t *ui, uint32_t now_ms)
{
    if (ui->screen != SCR_HOME && (uint32_t)(now_ms - ui->last_input_ms) > UI_IDLE_RETURN_S * 1000U) {
        ui->screen = SCR_HOME;
        ui->sub = 0;
        ui->ev_idx = 0;
    }
}

/* ---------- малювання ---------- */

static void line(ssd1306_handle_t o, int y, const char *fmt, ...)
{
    char buf[40];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    buf[21] = '\0';                               /* 21 символ у рядку */
    ssd1306_draw_string(o, 0, y, buf);
}

static void centered(ssd1306_handle_t o, int y, const char *text, int scale)
{
    int w = (int)strlen(text) * 6 * scale;
    ssd1306_draw_string_scaled(o, (128 - w) / 2 < 0 ? 0 : (128 - w) / 2, y, text, scale);
}

static void header(ssd1306_handle_t o, const ui_t *ui, const char *title)
{
    char right[16];
    snprintf(right, sizeof(right), "%d/%d", ui->screen + 1, SCR_COUNT);
    ssd1306_draw_string(o, 1, 1, title);
    ssd1306_draw_string(o, 128 - (int)strlen(right) * 6 - 1, 1, right);
    ssd1306_invert_rect(o, 0, 0, 128, 9);
}

static void footer(ssd1306_handle_t o, const ui_t *ui, const ui_view_t *v, const char *hint)
{
    if (ui->toast[0] != '\0' && (int32_t)(v->now_ms - ui->toast_until_ms) < 0) {
        line(o, 57, "%s", ui->toast);
    } else if (hint != NULL) {
        line(o, 57, "%s", hint);
    }
}

static const char *onoff(int v, const char *yes, const char *no)
{
    return v < 0 ? "--" : (v ? yes : no);
}

static void fmt_float(char *buf, size_t n, float v, const char *fmt)
{
    if (isfinite(v)) snprintf(buf, n, fmt, v);
    else snprintf(buf, n, "--");
}

static void draw_home(const ui_t *ui, ssd1306_handle_t o, const ui_view_t *v)
{
    char s[32];
    if (v->time_ok) {
        snprintf(s, sizeof(s), "%02d%c%02d", v->tm.tm_hour, (v->tm.tm_sec % 2 == 0) ? ':' : ' ', v->tm.tm_min);
    } else {
        snprintf(s, sizeof(s), "--:--");
    }
    centered(o, 0, s, 3);

    if (v->time_ok) {
        snprintf(s, sizeof(s), "%02d.%02d.%04d %.3s", v->tm.tm_mday, v->tm.tm_mon + 1,
                 v->tm.tm_year + 1900, day_names[v->tm.tm_wday % 7]);
    } else {
        snprintf(s, sizeof(s), "--.--.---- ---");
    }
    centered(o, 24, s, 1);

    if (v->st.stm_ok && isfinite(v->st.t)) snprintf(s, sizeof(s), "%.1f" DEG_STR "C", v->st.t);
    else snprintf(s, sizeof(s), "--.-" DEG_STR "C");
    centered(o, 34, s, 2);

    line(o, 49, "STM %s ESP %s RTC %s", v->st.stm_ok ? "OK" : "ER",
         v->st.esp_online ? "ON" : "OFF", v->st.rtc_ok ? "OK" : "ER");

    /* Нижній рядок: тривоги чергуються з останньою подією кожні 2 с */
    bool show_alert = v->st.active_alerts[0] && ((v->now_ms / 2000) % 2 == 0);
    event_t ev;
    if (ui->toast[0] != '\0' && (int32_t)(v->now_ms - ui->toast_until_ms) < 0) {
        line(o, 57, "%s", ui->toast);
    } else if (show_alert) {
        line(o, 57, "ALERT:%.15s", v->st.active_alerts);
    } else if (events_get(0, &ev)) {
        line(o, 57, "%s %s", ev.type, ev.stamp);
    } else {
        line(o, 57, "PIR %s", v->st.pir_state ? "MOTION" : "-");
    }
}

static void draw_meteo(const ui_t *ui, ssd1306_handle_t o, const ui_view_t *v)
{
    char a[24], b[24];
    header(o, ui, "METEO");
    bool ok = v->st.stm_ok;
    if (ok && isfinite(v->st.t)) snprintf(a, sizeof(a), "%.1f" DEG_STR "C", v->st.t);
    else snprintf(a, sizeof(a), "--.-" DEG_STR "C");
    ssd1306_draw_string_scaled(o, 0, 12, a, 2);
    ssd1306_draw_string(o, 84, 12, ok ? "STM OK" : "STM ERR");
    ssd1306_draw_string(o, 84, 21, v->st.pir_state ? "PIR:MOT" : "PIR:-");

    fmt_float(a, sizeof(a), ok ? v->st.rh : NAN, "%.1f %%");
    line(o, 30, "Humidity  %s", a);
    fmt_float(a, sizeof(a), ok ? v->st.p : NAN, "%.1f hPa");
    line(o, 39, "Pressure  %s", a);
    fmt_float(b, sizeof(b), ok ? v->st.p * 0.750062f : NAN, "%.1f mmHg");
    line(o, 48, "          %s", b);
    fmt_float(a, sizeof(a), (ok && v->st.td > -1000.0f) ? v->st.td : NAN, "%.1f " DEG_STR "C");
    line(o, 57, "Dew point %s", a);
}

static void draw_wx(const ui_t *ui, ssd1306_handle_t o, const ui_view_t *v)
{
    header(o, ui, ui->sub == 0 ? "WX NOW" : "WX FORECAST");
    const wx_view_t *w = &v->wx;
    if (!w->valid || v->st.wx_age >= 999) {
        line(o, 21, "No weather data");
        line(o, 30, v->st.wifi_rssi > -127 ? "waiting for weather" : "no Wi-Fi connection");
        return;
    }
    if (ui->sub == 0) {
        line(o, 12, "Wind %.1f m/s %s", w->wind, wind_dir_text(w->dir));
        line(o, 21, "Gust %.1f m/s", w->gust);
        line(o, 30, "Rain %.1fmm Snow %.1fcm", w->rain, w->snow);
        char rh[12];
        fmt_float(rh, sizeof(rh), w->rh, "%.0f%%");
        line(o, 39, "Humid %s Cloud %d%%", rh, w->cloud);
        if (v->st.wx_age < 999) line(o, 48, "Updated %d min ago", v->st.wx_age);
        else line(o, 48, "Update time unknown");
        line(o, 57, "UP/DN: forecast");
    } else {
        static const char *const labels[WX_DAYS] = {"Today", "+1 day", "+2 day"};
        for (int i = 0; i < WX_DAYS; i++) {
            int y = 12 + i * 18;
            if (i < w->n_days) {
                char lo[10], hi[10];
                fmt_float(lo, sizeof(lo), w->day[i].tmin, "%.0f");
                fmt_float(hi, sizeof(hi), w->day[i].tmax, "%.0f");
                line(o, y, "%-6s %s/%s" DEG_STR "C %2.0f%%", labels[i], lo, hi, w->day[i].pop);
                line(o, y + 9, " %s wind %.0fm/s", wx_cond_text(w->day[i].cond), w->day[i].wmax);
            } else {
                line(o, y, "%-6s --", labels[i]);
            }
        }
    }
}

static void draw_time(const ui_t *ui, ssd1306_handle_t o, const ui_view_t *v)
{
    char s[32];
    header(o, ui, "DATE/TIME");
    if (v->time_ok) {
        snprintf(s, sizeof(s), "%02d:%02d:%02d", v->tm.tm_hour, v->tm.tm_min, v->tm.tm_sec);
        centered(o, 12, s, 2);
        line(o, 30, "%02d.%02d.%04d  %s", v->tm.tm_mday, v->tm.tm_mon + 1, v->tm.tm_year + 1900,
             day_names[v->tm.tm_wday % 7]);
    } else {
        centered(o, 12, "--:--:--", 2);
        line(o, 30, "Time not available");
    }
    const char *src = !strcmp(v->st.time_src, "ntp") ? "NTP (Wi-Fi)"
                    : !strcmp(v->st.time_src, "rtc") ? "DS1307 only" : "none";
    line(o, 39, "Src: %s", src);
    line(o, 48, "RTC: %s", v->st.rtc_ok ? "OK" : "ERROR");
    uint32_t up = status_uptime_s();
    line(o, 57, "Uptime %02u:%02u:%02u", (unsigned)(up / 3600), (unsigned)(up / 60 % 60), (unsigned)(up % 60));
}

static void draw_event(const ui_t *ui, ssd1306_handle_t o, const ui_view_t *v)
{
    event_t ev;
    int total = events_count();
    char title[24];
    snprintf(title, sizeof(title), "EVENT %d/%d", total ? ui->ev_idx + 1 : 0, total);
    header(o, ui, title);
    if (!events_get(ui->ev_idx, &ev)) {
        line(o, 21, "No events yet");
        footer(o, ui, v, "SEL = take photo");
        return;
    }
    line(o, 12, "Type : %s", ev.type);
    line(o, 21, "When : %s", ev.stamp);
    line(o, 30, "Info : %s", ev.info);
    switch (ev.photo) {
    case PH_NONE:    line(o, 39, "Photo: none"); break;
    case PH_PENDING: line(o, 39, "Photo: waiting..."); break;
    case PH_OK:      line(o, 39, "Photo: saved"); break;
    case PH_FAIL:    line(o, 39, "Photo: FAILED %.8s", ev.file); break;
    case PH_SKIP:    line(o, 39, "Photo: skipped"); break;
    }
    if (ev.photo == PH_OK) {
        const char *slash = strrchr(ev.file, '/');
        line(o, 48, "%s", slash ? slash + 1 : ev.file);
    }
    footer(o, ui, v, "UP/DN:old  SEL:photo");
}

static void draw_status(const ui_t *ui, ssd1306_handle_t o, const ui_view_t *v)
{
    header(o, ui, "STATUS");
    const system_status_t *s = &v->st;
    line(o, 12, "STM32 %s  fail %d", s->stm_ok ? "OK" : "ERR", s->stm_fail);
    if (s->wifi_rssi > -127) line(o, 21, "Wi-Fi %d dBm", s->wifi_rssi);
    else line(o, 21, "Wi-Fi: no link");
    line(o, 30, "S3 %s SD %s CAM %s", s->esp_online ? "ON" : "OFF", onoff(s->sd_ok, "OK", "ER"),
         onoff(s->cam_ok, "OK", "ER"));
    if (s->wx_age < 999) line(o, 39, "Weather age %d min", s->wx_age);
    else line(o, 39, "Weather: no data");
    line(o, 48, "Errors %d Alerts %s", s->err_count, s->active_alerts[0] ? "YES" : "no");
    if (s->last_snap_ok >= 0) line(o, 57, "Last photo %s %s", s->last_snap_ok ? "OK" : "ERR", s->last_snap_time);
    else line(o, 57, "No photo taken yet");
}

static void draw_keys(const ui_t *ui, ssd1306_handle_t o, const ui_view_t *v)
{
    header(o, ui, "KEYS TEST");
    line(o, 12, "ADC %4d mV", v->key_mv);
    line(o, 21, "Key: %s", keys_name(v->key_now));
    line(o, 30, "Idle %d  R%d U%d", v->cal.idle_mv, v->cal.nominal_mv[KEY_RIGHT], v->cal.nominal_mv[KEY_UP]);
    line(o, 39, "D%d L%d S%d", v->cal.nominal_mv[KEY_DOWN], v->cal.nominal_mv[KEY_LEFT],
         v->cal.nominal_mv[KEY_SELECT]);
    line(o, 48, "Hold any key at power");
    line(o, 57, "on to recalibrate");
}

void ui_render(const ui_t *ui, ssd1306_handle_t o, const ui_view_t *v)
{
    ssd1306_clear(o, false);
    switch (ui->screen) {
    case SCR_HOME:   draw_home(ui, o, v); break;
    case SCR_METEO:  draw_meteo(ui, o, v); break;
    case SCR_WX:     draw_wx(ui, o, v); break;
    case SCR_TIME:   draw_time(ui, o, v); break;
    case SCR_EVENT:  draw_event(ui, o, v); break;
    case SCR_STATUS: draw_status(ui, o, v); break;
    case SCR_KEYS:   draw_keys(ui, o, v); break;
    default: break;
    }
    ssd1306_show(o);
}
