/* UART0-Loopback-Test fuer das ESP-Display (CYD)
 *
 * Prueft die serielle Schnittstelle TXD0/RXD0 (UART0, GPIO1/GPIO3) per
 * Loopback: TX0 wird mit einer kurzen Drahtbruecke auf RX0 gelegt. Das
 * Programm sendet Testbytes ueber UART0 und liest sie sofort wieder ein.
 *
 * Das CYD wird extern versorgt (kein USB noetig). Die komplette Ausgabe
 * erfolgt daher AUSSCHLIESSLICH auf dem Display (LVGL) -- ueber UART0
 * werden nur die Testbytes gesendet, sonst nichts, damit das Echo nicht
 * verfaelscht wird.
 *
 * Verdrahtung:  GPIO1 (TXD0)  <-->  GPIO3 (RXD0)   (kurze Drahtbruecke)
 *
 * Build/Flash:  pio run -e loopback -t upload
 */

#include <Arduino.h>
#include <lvgl.h>
#include <TFT_eSPI.h>

// ---- Zu testende UART: UART0 = TXD0/RXD0 (GPIO1/GPIO3) -------------------
static const int      TEST_TX_PIN = 1;    // GPIO1  = TXD0
static const int      TEST_RX_PIN = 3;    // GPIO3  = RXD0
static const uint32_t TEST_BAUD   = 115200;
#define TEST_UART Serial                  // UART0

// ---- Display (CYD, 320x240 Landscape) -----------------------------------
static const uint16_t screenWidth  = 320;
static const uint16_t screenHeight = 240;

static lv_disp_draw_buf_t draw_buf;
static lv_color_t buf[screenWidth * screenHeight / 10];
TFT_eSPI tft = TFT_eSPI(screenWidth, screenHeight);

static lv_obj_t *lbl_result = nullptr;   // grosses PASS/FAIL
static lv_obj_t *lbl_detail = nullptr;   // OK-Zaehler
static lv_obj_t *lbl_last   = nullptr;   // letztes TX/RX
static lv_obj_t *result_box = nullptr;

static uint32_t tests_total  = 0;
static uint32_t tests_passed = 0;

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

static void build_ui()
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "UART0 Loopback (TXD0/RXD0)");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x60d0ff), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 4);

    lv_obj_t *hint = lv_label_create(scr);
    lv_label_set_text(hint, "Bruecke GPIO1 (TX0) <-> GPIO3 (RX0)");
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0xaaaaaa), 0);
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 30);

    result_box = lv_obj_create(scr);
    lv_obj_set_size(result_box, 220, 74);
    lv_obj_align(result_box, LV_ALIGN_CENTER, 0, -6);
    lv_obj_set_style_radius(result_box, 10, 0);
    lv_obj_set_style_border_width(result_box, 0, 0);
    lv_obj_set_style_bg_color(result_box, lv_color_hex(0x333333), 0);
    lv_obj_clear_flag(result_box, LV_OBJ_FLAG_SCROLLABLE);

    lbl_result = lv_label_create(result_box);
    lv_label_set_text(lbl_result, "WARTE");
    lv_obj_set_style_text_font(lbl_result, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(lbl_result, lv_color_white(), 0);
    lv_obj_center(lbl_result);

    lbl_detail = lv_label_create(scr);
    lv_label_set_text(lbl_detail, "OK: -/-");
    lv_obj_set_style_text_font(lbl_detail, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(lbl_detail, lv_color_white(), 0);
    lv_obj_align(lbl_detail, LV_ALIGN_BOTTOM_MID, 0, -34);

    lbl_last = lv_label_create(scr);
    lv_label_set_text(lbl_last, "TX=--  RX=--");
    lv_obj_set_style_text_font(lbl_last, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_last, lv_color_hex(0xaaaaaa), 0);
    lv_obj_align(lbl_last, LV_ALIGN_BOTTOM_MID, 0, -10);
}

// Sendet ein Byte auf UART0, wartet bis timeout_ms auf das Echo.
static bool loopback_byte(uint8_t tx, uint8_t &rx, uint32_t timeout_ms)
{
    while (TEST_UART.available()) TEST_UART.read();  // RX-Puffer leeren
    TEST_UART.write(tx);
    TEST_UART.flush();

    const uint32_t start = millis();
    while (millis() - start < timeout_ms) {
        if (TEST_UART.available()) {
            rx = (uint8_t)TEST_UART.read();
            return true;
        }
    }
    return false;
}

static void run_pass()
{
    const uint8_t pattern[] = {0x00, 0x55, 0xAA, 0xFF, 'A', '5', 0x0D, 0x0A};
    const uint8_t count = sizeof(pattern);
    uint8_t ok = 0;
    uint8_t last_tx = 0, last_rx = 0;
    bool last_got = false;

    for (uint8_t i = 0; i < count; i++) {
        uint8_t v = pattern[i];
        uint8_t r = 0;
        tests_total++;
        const bool got = loopback_byte(v, r, 50);
        last_tx = v;
        last_rx = r;
        last_got = got;
        if (got && r == v) {
            ok++;
            tests_passed++;
        }
    }

    const bool pass = (ok == count);
    lv_obj_set_style_bg_color(result_box,
        pass ? lv_color_hex(0x1f8b3a) : lv_color_hex(0xb02020), 0);
    lv_label_set_text(lbl_result, pass ? "PASS" : "FAIL");

    lv_label_set_text_fmt(lbl_detail, "OK: %u/%u   kumuliert %u/%u",
                          ok, count, tests_passed, tests_total);

    if (last_got) {
        lv_label_set_text_fmt(lbl_last, "TX=0x%02X  RX=0x%02X", last_tx, last_rx);
    } else {
        lv_label_set_text_fmt(lbl_last, "TX=0x%02X  RX=-- (kein Echo)", last_tx);
    }
}

void setup()
{
    lv_init();

    tft.begin();
    tft.setRotation(3);

    lv_disp_draw_buf_init(&draw_buf, buf, NULL, screenWidth * screenHeight / 10);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res  = screenWidth;
    disp_drv.ver_res  = screenHeight;
    disp_drv.flush_cb = my_disp_flush;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);

    build_ui();

    // UART0 nur fuer den Loopback -- keine Konsolenausgaben hierher!
    TEST_UART.begin(TEST_BAUD, SERIAL_8N1, TEST_RX_PIN, TEST_TX_PIN);
    delay(200);
}

void loop()
{
    run_pass();

    const uint32_t start = millis();
    while (millis() - start < 1500) {
        lv_timer_handler();
        delay(5);
    }
}
