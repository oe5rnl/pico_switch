/* esp-switch
 *
 * 8 On/Off Buttons auf dem CYD-Display (ESP32-2432S028)
 * mit LVGL 8 und TFT_eSPI / XPT2046 Touch.
 *
 * Basierend auf der Display-/Touch-Initialisierung aus
 * ESP32-ATT-C-LVGL8 (CYD, ILI9341, 320x240, Landscape).
 */

#include <Arduino.h>
#include <lvgl.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>

// ----------------------------
// Touch screen pins (CYD)
// ----------------------------
#define XPT2046_IRQ  36
#define XPT2046_MOSI 32
#define XPT2046_MISO 39
#define XPT2046_CLK  25
#define XPT2046_CS   33

SPIClass mySpi = SPIClass(VSPI);
XPT2046_Touchscreen ts(XPT2046_CS, XPT2046_IRQ);

// ----------------------------
// Display-Orientierung (Compile-Flag)
// Basisausrichtung Landscape = TFT_eSPI-Rotation 3. Mit -DDISPLAY_ROTATE_180
// wird Display + Touch um 180 Grad gedreht (Rotation 1).
// ----------------------------
#ifdef DISPLAY_ROTATE_180
#define DISPLAY_ROTATION 1
#else
#define DISPLAY_ROTATION 3
#endif

uint16_t touchScreenMinimumX = 200, touchScreenMaximumX = 3700;
uint16_t touchScreenMinimumY = 240, touchScreenMaximumY = 3800;

// ----------------------------
// Display
// ----------------------------
static const uint16_t screenWidth  = 320;
static const uint16_t screenHeight = 240;

static lv_disp_draw_buf_t draw_buf;
static lv_color_t buf[screenWidth * screenHeight / 10];

TFT_eSPI tft = TFT_eSPI(screenWidth, screenHeight);

// ----------------------------
// Link zum Pico-Controller (USB-Serial = UART0, GPIO1=TX, GPIO3=RX)
// Diese Schnittstelle wird AUSSCHLIESSLICH fuer die Pico-Kommunikation
// verwendet. Keine Debug-Ausgaben darueber!
// ----------------------------
#define PICO_UART        Serial
#define PICO_UART_BAUD   115200
#define PICO_DISPLAY_TIMEOUT_MS 500
#define PICO_DISPLAY_RETRY_MS   2000
#define PICO_HEARTBEAT_INTERVAL_MS 1000
#define PICO_HEARTBEAT_TIMEOUT_MS  3000

// Firmware-Version (Git-Commit-Count), von version.py als Build-Define gesetzt.
#ifndef ESP_FW_VERSION
#define ESP_FW_VERSION "00.00000"
#endif

// ----------------------------
// Switch state
// ----------------------------
#define SWITCH_COUNT 8
static bool     switch_state[SWITCH_COUNT] = { false };
static bool     feedback_error[SWITCH_COUNT] = { false };
static bool     scene_feedback_error[SWITCH_COUNT] = { false };
static String   switch_names[SWITCH_COUNT];
static bool     scene_mode = false;
static String   scene_names[SWITCH_COUNT];
static int      active_scene = -1;
static bool     scene_dirty = false;
static lv_obj_t * switch_btns[SWITCH_COUNT]  = { nullptr };
static lv_obj_t * switch_lbls[SWITCH_COUNT]  = { nullptr };
static lv_obj_t * warn_dots[SWITCH_COUNT]    = { nullptr };
static lv_obj_t * title_label = nullptr;
static lv_obj_t * ip_label    = nullptr;
static lv_obj_t * splash_label = nullptr;  // Startanzeige "sw_switch" statt Default-Buttons
static lv_obj_t * splash_ver   = nullptr;  // Firmware-Version unter der Startanzeige
static lv_obj_t * splash_credit = nullptr; // Autorenzeile unter der Firmware-Version
static bool display_config_loaded = false;
static uint32_t next_display_query_ms = 0;
static String pico_rx_line;
static bool pico_online = false;
static uint32_t last_pico_ping_ms = 0;
static uint32_t last_pico_pong_ms = 0;

static void update_switch_visual(int idx);
static void refresh_all_buttons();
static void set_splash_visible(bool visible);

static bool parse_error_line(const String &line)
{
    const bool scene_error = line.startsWith("SERROR");
    const int prefix_len = scene_error ? 6 : 5;
    if (!scene_error && !line.startsWith("ERROR")) return false;
    int colon = line.indexOf(':');
    if (colon <= prefix_len) return false;
    int idx = line.substring(prefix_len, colon).toInt() - 1;
    String value = line.substring(colon + 1);
    if (idx < 0 || idx >= SWITCH_COUNT || (value != "ON" && value != "OFF")) return false;
    if (scene_error) scene_feedback_error[idx] = value == "ON";
    else feedback_error[idx] = value == "ON";
    update_switch_visual(idx);
    return true;
}

// Verarbeitet MODE:/SCENEn:-Zeilen der Display-Konfiguration.
// Gibt true zurück, wenn die Zeile erkannt wurde.
static bool parse_mode_or_scene_line(const String &line)
{
    if (line == "MODE:SCENE") { scene_mode = true; return true; }
    if (line == "MODE:RELAY") { scene_mode = false; return true; }

    if (line.startsWith("ASCENE:")) {
        active_scene = line.substring(7).toInt() - 1;  // 0 = keine aktive Szene
        return true;
    }

    if (line.startsWith("SDIRTY:")) {
        scene_dirty = (line.substring(7) == "ON");
        return true;
    }

    if (line.startsWith("SCENE")) {
        int colon = line.indexOf(':');
        if (colon > 5) {
            int n = line.substring(5, colon).toInt();
            if (n >= 1 && n <= SWITCH_COUNT) {
                scene_names[n - 1] = line.substring(colon + 1);
                return true;
            }
        }
    }
    return false;
}

static void set_title_text(const char *text)
{
    if (!title_label) return;
    lv_label_set_text(title_label, text);
    lv_obj_align(title_label, LV_ALIGN_TOP_MID, 0, 2);
}

static void set_ip_text(const char *text)
{
    if (!ip_label) return;
    lv_label_set_text(ip_label, text);
}

// Liest eine Zeile (bis '\n') von PICO_UART, max. timeout_ms.
// Gibt true zurück, wenn eine Zeile empfangen wurde.
static bool pico_read_line(String &out, uint32_t timeout_ms)
{
    out = "";

    const uint32_t start = millis();
    while (millis() - start < timeout_ms) {
        if (!PICO_UART.available()) {
            delay(1);
            continue;
        }

        char c = (char)PICO_UART.read();
        if (c == '\r') continue;
        if (c == '\n') return true;
        if (out.length() < 128) out += c;
    }

    return false;
}

static bool pico_read_display_config(String &title)
{
    bool got_title = false;
    bool got_name[SWITCH_COUNT] = { false };
    bool got_state[SWITCH_COUNT] = { false };

    while (PICO_UART.available()) PICO_UART.read();
    PICO_UART.println("GET DISPLAY");

    const uint32_t start = millis();
    while (millis() - start < PICO_DISPLAY_TIMEOUT_MS) {
        String line;
        uint32_t remaining = PICO_DISPLAY_TIMEOUT_MS - (millis() - start);

        if (!pico_read_line(line, remaining)) break;

        if (line == "PONG") {
            last_pico_pong_ms = millis();
            continue;
        }

        if (line.startsWith("TITLE:")) {
            title = line.substring(6);
            got_title = title.length() > 0;
            continue;
        }

        if (line.startsWith("IP:")) {
            set_ip_text(line.substring(3).c_str());
            continue;
        }

        if (parse_mode_or_scene_line(line)) {
            continue;
        }

        if (parse_error_line(line)) {
            continue;
        }

        for (int i = 0; i < SWITCH_COUNT; i++) {
            String prefix = String("NAME") + String(i + 1) + ":";
            if (line.startsWith(prefix)) {
                switch_names[i] = line.substring(prefix.length());
                got_name[i] = switch_names[i].length() > 0;
                break;
            }
        }

        for (int i = 0; i < SWITCH_COUNT; i++) {
            String prefix = String("STATE") + String(i + 1) + ":";
            if (line.startsWith(prefix)) {
                String state = line.substring(prefix.length());
                switch_state[i] = state == "ON";
                got_state[i] = state == "ON" || state == "OFF";
                break;
            }
        }

        if (line == "END DISPLAY") {
            break;
        }
    }

    bool got_all_names = true;
    bool got_all_states = true;
    for (int i = 0; i < SWITCH_COUNT; i++) {
        got_all_names = got_all_names && got_name[i];
        got_all_states = got_all_states && got_state[i];
    }

    bool got_display_config = got_title && got_all_names && got_all_states;
    if (got_display_config) last_pico_pong_ms = millis();
    return got_display_config;
}

static bool handle_pico_state_line(const String &line)
{
    for (int i = 0; i < SWITCH_COUNT; i++) {
        String prefix = String("STATE") + String(i + 1) + ":";
        if (!line.startsWith(prefix)) continue;

        String state = line.substring(prefix.length());
        if (state != "ON" && state != "OFF") return false;

        switch_state[i] = state == "ON";
        update_switch_visual(i);
        return true;
    }

    return false;
}

static bool handle_pico_title_line(const String &line)
{
    if (!line.startsWith("TITLE:")) return false;

    String title = line.substring(6);
    if (title.length() > 0) set_title_text(title.c_str());

    return true;
}

static bool handle_pico_name_line(const String &line)
{
    for (int i = 0; i < SWITCH_COUNT; i++) {
        String prefix = String("NAME") + String(i + 1) + ":";
        if (!line.startsWith(prefix)) continue;

        String name = line.substring(prefix.length());
        if (name.length() == 0) return false;

        switch_names[i] = name;
        update_switch_visual(i);
        return true;
    }

    return false;
}

static void handle_pico_async_line(const String &line)
{
    if (line == "PONG") {
        last_pico_pong_ms = millis();
        if (!pico_online) {
            pico_online = true;
            display_config_loaded = false;
            next_display_query_ms = 0;
        }
        return;
    }

    if (line == "END DISPLAY") {
        refresh_all_buttons();
        return;
    }

    if (line.startsWith("IP:")) {
        set_ip_text(line.substring(3).c_str());
        return;
    }

    if (parse_mode_or_scene_line(line)) {
        refresh_all_buttons();
        return;
    }

    if (parse_error_line(line)) return;
    if (handle_pico_state_line(line)) return;
    if (handle_pico_title_line(line)) return;
    handle_pico_name_line(line);
}

static void update_pico_heartbeat()
{
    uint32_t now = millis();
    if (now - last_pico_ping_ms >= PICO_HEARTBEAT_INTERVAL_MS) {
        last_pico_ping_ms = now;
        PICO_UART.println("PING");
        PICO_UART.println("VER:" ESP_FW_VERSION);
    }

    if (pico_online && now - last_pico_pong_ms > PICO_HEARTBEAT_TIMEOUT_MS) {
        pico_online = false;
        display_config_loaded = false;
        next_display_query_ms = 0;
        set_title_text("wait for init");
        set_splash_visible(true);
    }
}

static void service_pico_uart()
{
    while (PICO_UART.available()) {
        char c = (char)PICO_UART.read();
        if (c == '\r') continue;

        if (c == '\n') {
            if (pico_rx_line.length() > 0) {
                handle_pico_async_line(pico_rx_line);
                pico_rx_line = "";
            }
            continue;
        }

        if (pico_rx_line.length() < 128) {
            pico_rx_line += c;
        } else {
            pico_rx_line = "";
        }
    }
}

static void update_display_config_from_pico()
{
    if (!pico_online) return;
    if (display_config_loaded || millis() < next_display_query_ms) return;
    next_display_query_ms = millis() + PICO_DISPLAY_RETRY_MS;

    String title;
    if (!pico_read_display_config(title)) return;

    if (title.length() > 0) set_title_text(title.c_str());

    refresh_all_buttons();

    display_config_loaded = true;
}

static void update_switch_visual(int idx)
{
    if(idx < 0 || idx >= SWITCH_COUNT) return;
    lv_obj_t * btn = switch_btns[idx];
    lv_obj_t * lbl = switch_lbls[idx];
    if(!btn || !lbl) return;

    if(scene_mode) {
        if(scene_names[idx].length() > 0) {
            lv_obj_clear_flag(btn, LV_OBJ_FLAG_HIDDEN);
            uint32_t col = scene_feedback_error[idx] ? 0xF00000 : ((idx == active_scene) ? 0x00F805 : 0x45F8FF);
            lv_obj_set_style_bg_color(btn, lv_color_hex(col), 0);
            String sname = scene_names[idx];
            sname.replace("|", "\n"); /* '|' im Szenennamen erzwingt Zeilenumbruch */
            lv_label_set_text(lbl, sname.c_str());
        }
        else {
            lv_obj_add_flag(btn, LV_OBJ_FLAG_HIDDEN);
        }
        // Warn-Punkt: sichtbar wenn aktive Szene direkt geändert wurde
        if (warn_dots[idx]) {
            if (scene_dirty && idx == active_scene)
                lv_obj_clear_flag(warn_dots[idx], LV_OBJ_FLAG_HIDDEN);
            else
                lv_obj_add_flag(warn_dots[idx], LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }

    lv_obj_clear_flag(btn, LV_OBJ_FLAG_HIDDEN);
    if (warn_dots[idx]) lv_obj_add_flag(warn_dots[idx], LV_OBJ_FLAG_HIDDEN);
    String name = switch_names[idx].length() > 0 ? switch_names[idx] : String(idx + 1);
    name.replace("|", "\n"); /* '|' im Namen erzwingt Zeilenumbruch am Display */
    if(feedback_error[idx]) {
        lv_obj_set_style_bg_color(btn, lv_color_hex(0xF00000), 0);
        lv_label_set_text_fmt(lbl, "%s\n%s", name.c_str(), switch_state[idx] ? "ON" : "OFF");
    }
    else if(switch_state[idx]) {
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x00F805), 0); /* grün */
        lv_label_set_text_fmt(lbl, "%s\nON", name.c_str());
    }
    else {
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x45F8FF), 0); /* dunkles Grau */
        lv_label_set_text_fmt(lbl, "%s\nOFF", name.c_str());
    }
}

static void apply_button_layout()
{
    /* aktivierte Szenen sammeln (für Szenen-Modus) */
    int n = 0;
    if (scene_mode) {
        for (int i = 0; i < SWITCH_COUNT; i++) {
            if (scene_names[i].length() > 0) n++;
        }
    }

    const int y_top  = 45;
    const int y_bot  = 220; /* über IP-Label */
    const int area_h = y_bot - y_top;

    if (scene_mode && n >= 1 && n <= 3) {
        int btn_w, btn_h = 150, gap_x, x_start;
        if      (n == 1) { btn_w = 200; gap_x =  0; }
        else if (n == 2) { btn_w = 140; gap_x = 12; }
        else             { btn_w =  88; gap_x = 10; }
        x_start = (screenWidth - n * btn_w - (n - 1) * gap_x) / 2;
        int y_pos = y_top + (area_h - btn_h) / 2;

        int j = 0;
        for (int i = 0; i < SWITCH_COUNT; i++) {
            if (!switch_btns[i] || !switch_lbls[i]) continue;
            if (scene_names[i].length() > 0) {
                lv_obj_set_size(switch_btns[i], btn_w, btn_h);
                lv_obj_set_pos(switch_btns[i], x_start + j * (btn_w + gap_x), y_pos);
                lv_obj_set_width(switch_lbls[i], btn_w - 12);
                lv_obj_center(switch_lbls[i]);
                if (warn_dots[i]) lv_obj_align(warn_dots[i], LV_ALIGN_TOP_RIGHT, -2, 2);
                j++;
            }
        }
    } else {
        /* Standard 4×2-Raster */
        const int cols=4, gw=70, gh=78, gx=8, gy=8;
        const int grid_w = cols * gw + (cols - 1) * gx;
        const int x_start = (screenWidth - grid_w) / 2;
        for (int i = 0; i < SWITCH_COUNT; i++) {
            if (!switch_btns[i] || !switch_lbls[i]) continue;
            lv_obj_set_size(switch_btns[i], gw, gh);
            lv_obj_set_pos(switch_btns[i], x_start + (i % cols) * (gw + gx),
                                           y_top   + (i / cols) * (gh + gy));
            lv_obj_set_width(switch_lbls[i], gw - 8);
            lv_obj_center(switch_lbls[i]);
            if (warn_dots[i]) lv_obj_align(warn_dots[i], LV_ALIGN_TOP_RIGHT, -2, 2);
        }
    }
}

static void set_splash_visible(bool visible)
{
    if (splash_label) {
        if (visible) lv_obj_clear_flag(splash_label, LV_OBJ_FLAG_HIDDEN);
        else         lv_obj_add_flag(splash_label, LV_OBJ_FLAG_HIDDEN);
    }
    if (splash_ver) {
        if (visible) lv_obj_clear_flag(splash_ver, LV_OBJ_FLAG_HIDDEN);
        else         lv_obj_add_flag(splash_ver, LV_OBJ_FLAG_HIDDEN);
    }
    if (splash_credit) {
        if (visible) lv_obj_clear_flag(splash_credit, LV_OBJ_FLAG_HIDDEN);
        else         lv_obj_add_flag(splash_credit, LV_OBJ_FLAG_HIDDEN);
    }
    if (visible) {  // Buttons ausblenden, solange die Startanzeige laeuft
        for (int i = 0; i < SWITCH_COUNT; i++) {
            if (switch_btns[i]) lv_obj_add_flag(switch_btns[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void refresh_all_buttons()
{
    set_splash_visible(false);
    apply_button_layout();
    for(int i = 0; i < SWITCH_COUNT; i++) {
        update_switch_visual(i);
    }
}

static void switch_event_cb(lv_event_t * e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if(idx < 0 || idx >= SWITCH_COUNT) return;

    if(scene_mode) {
        if(scene_names[idx].length() == 0) return;
        PICO_UART.printf("SCENE%d:GO\n", idx + 1);
        return;
    }

    switch_state[idx] = !switch_state[idx];
    update_switch_visual(idx);
    PICO_UART.printf("SW%d:%s\n", idx + 1, switch_state[idx] ? "ON" : "OFF");
}

static void create_ui(void)
{
    lv_obj_t * scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    /* Titel */
    lv_obj_t * title = lv_label_create(scr);
    lv_label_set_text(title, "wait for init");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x60d0ff), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 2);
    title_label = title;

    /* IP-Adresse unten links */
    lv_obj_t * ipl = lv_label_create(scr);
    lv_label_set_text(ipl, "");
    lv_obj_set_style_text_font(ipl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(ipl, lv_color_hex(0x808080), 0);
    lv_obj_align(ipl, LV_ALIGN_BOTTOM_LEFT, 4, -2);
    ip_label = ipl;

    /* Button-Grid: 4 Spalten x 2 Zeilen */
    const int cols      = 4;
    const int rows      = 2;
    const int btn_w     = 70;
    const int btn_h     = 78;
    const int gap_x     = 8;
    const int gap_y     = 8;
    const int grid_w    = cols * btn_w + (cols - 1) * gap_x;
    const int x_start   = (screenWidth - grid_w) / 2;
    const int y_start   = 45;

    static lv_style_t style_btn;
    static bool style_inited = false;
    if(!style_inited) {
        lv_style_init(&style_btn);
        lv_style_set_radius(&style_btn, 8);
        lv_style_set_text_color(&style_btn, lv_color_black());
        lv_style_set_text_align(&style_btn, LV_TEXT_ALIGN_CENTER);
        style_inited = true;
    }

    for(int i = 0; i < SWITCH_COUNT; i++) {
        int r = i / cols;
        int c = i % cols;

        lv_obj_t * btn = lv_btn_create(scr);
        lv_obj_set_size(btn, btn_w, btn_h);
        lv_obj_set_pos(btn, x_start + c * (btn_w + gap_x),
                            y_start + r * (btn_h + gap_y));
        lv_obj_add_style(btn, &style_btn, 0);
        lv_obj_add_event_cb(btn, switch_event_cb, LV_EVENT_CLICKED,
                            (void *)(intptr_t)i);

        lv_obj_t * lbl = lv_label_create(btn);
        lv_obj_set_width(lbl, btn_w - 8);
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_18, 0);
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(lbl);

        switch_btns[i] = btn;
        switch_lbls[i] = lbl;

        // Warn-Punkt (roter Kreis oben rechts) für direkte Schaltung im Szenen-Modus
        lv_obj_t * dot = lv_obj_create(btn);
        lv_obj_set_size(dot, 10, 10);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(dot, lv_color_hex(0xFF0000), 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(dot, 0, 0);
        lv_obj_set_style_pad_all(dot, 0, 0);
        lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_align(dot, LV_ALIGN_TOP_RIGHT, -2, 2);
        lv_obj_add_flag(dot, LV_OBJ_FLAG_HIDDEN);
        warn_dots[i] = dot;

        update_switch_visual(i);
    }

    /* Startanzeige: "pico_switch",
       bis der Pico die Schalter-/Szenendaten sendet. */
    lv_obj_t * sp = lv_label_create(scr);
    lv_label_set_text(sp, "pico_switch");
    lv_obj_set_style_text_font(sp, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(sp, lv_color_hex(0xe0e0e0), 0);
    lv_obj_align(sp, LV_ALIGN_CENTER, 0, -10);
    splash_label = sp;

    lv_obj_t * spv = lv_label_create(scr);
    lv_label_set_text(spv, "FW " ESP_FW_VERSION);
    lv_obj_set_style_text_font(spv, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(spv, lv_color_hex(0x808080), 0);
    lv_obj_align(spv, LV_ALIGN_CENTER, 0, 20);
    splash_ver = spv;

    lv_obj_t * spc = lv_label_create(scr);
    lv_label_set_text(spc, "OE5RNL & OE5NVL");
    lv_obj_set_style_text_font(spc, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(spc, lv_color_hex(0x808080), 0);
    lv_obj_align(spc, LV_ALIGN_CENTER, 0, 40);
    splash_credit = spc;


    set_splash_visible(true);
}

// ----------------------------
// LVGL display flush / touch
// ----------------------------
void my_disp_flush(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p)
{
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);

    tft.startWrite();
    tft.setAddrWindow(area->x1, area->y1, w, h);
    tft.pushColors((uint16_t *)&color_p->full, w * h, true);
    tft.endWrite();

    lv_disp_flush_ready(disp_drv);
}

void my_touchpad_read(lv_indev_drv_t *indev_drv, lv_indev_data_t *data)
{
    if(ts.touched()) {
        TS_Point p = ts.getPoint();
        if(p.x < touchScreenMinimumX) touchScreenMinimumX = p.x;
        if(p.x > touchScreenMaximumX) touchScreenMaximumX = p.x;
        if(p.y < touchScreenMinimumY) touchScreenMinimumY = p.y;
        if(p.y > touchScreenMaximumY) touchScreenMaximumY = p.y;
        data->point.x = map(p.x, touchScreenMinimumX, touchScreenMaximumX, 1, screenWidth);
        data->point.y = map(p.y, touchScreenMinimumY, touchScreenMaximumY, 1, screenHeight);
        data->state   = LV_INDEV_STATE_PR;
    }
    else {
        data->state = LV_INDEV_STATE_REL;
    }
}

void setup()
{
    PICO_UART.begin(PICO_UART_BAUD);
    delay(200);

    lv_init();

    mySpi.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
    ts.begin(mySpi);
    ts.setRotation(DISPLAY_ROTATION);

    tft.begin();
    tft.setRotation(DISPLAY_ROTATION);

    lv_disp_draw_buf_init(&draw_buf, buf, NULL, screenWidth * screenHeight / 10);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res  = screenWidth;
    disp_drv.ver_res  = screenHeight;
    disp_drv.flush_cb = my_disp_flush;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);

    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type    = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = my_touchpad_read;
    lv_indev_drv_register(&indev_drv);

    create_ui();
}

void loop()
{
    service_pico_uart();
    update_pico_heartbeat();
    update_display_config_from_pico();
    lv_timer_handler();
    delay(5);
}
