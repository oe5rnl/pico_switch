/* UART0-Loopback-Test fuer den Pico (RP2350 + W6300)
 *
 * Eigenstaendiges Testprogramm: prueft die serielle Schnittstelle UART0
 * (GP0 = TX, GP1 = RX) per Loopback. GP0 wird mit einer kurzen Draht-
 * bruecke auf GP1 gelegt. Das Programm sendet zyklisch ein Byte-Muster
 * ueber UART0 und liest es sofort wieder ein. Das Ergebnis wird ueber
 * eine Webseite ausgegeben (gleiche Netzwerk-Bootstrap-Logik wie die
 * Originalfirmware: GP15 HIGH/offen -> DHCP, LOW -> statische IP).
 *
 * Dies ist ein separates Build-Target; die Originalfirmware
 * (switch_w6300_relay) bleibt unveraendert erhalten.
 *
 * Verdrahtung:  GP0 (Pin 1, TX)  <-->  GP1 (Pin 2, RX)   (Drahtbruecke)
 * Aufruf:       http://<IP>/      (aktualisiert sich automatisch)
 */

#include <array>
#include <cstdint>
#include <cstdio>
#include <string>

#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/uart.h"

extern "C" {
#include "dhcp.h"
#include "socket.h"
#include "wizchip_conf.h"
#include "wizchip_spi.h"
}

namespace cfg {
constexpr uint16_t HTTP_PORT = 80;
constexpr uint8_t HTTP_SOCKET_COUNT = 4;
constexpr uint8_t DHCP_SELECT_PIN = 15;
constexpr uint8_t DHCP_SOCKET = 0;
constexpr uint8_t DHCP_RETRY_COUNT = 5;
constexpr size_t ETHERNET_BUF_SIZE = 2048;

// UART0 unter Test
static uart_inst_t *const TEST_UART = uart0;
constexpr uint UART_TX_PIN = 0;   // GP0
constexpr uint UART_RX_PIN = 1;   // GP1
constexpr uint UART_BAUD = 115200;
}  // namespace cfg

static wiz_NetInfo g_net_info = {
    .mac = {0xDE, 0xAD, 0xBE, 0xEF, 0x63, 0x02},
    .ip = {192, 168, 88, 188},
    .sn = {255, 255, 255, 0},
    .gw = {192, 168, 88, 254},
#if _WIZCHIP_ > W5500
    .lla = {0xfe, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x08, 0xdc, 0xff, 0xfe, 0x57, 0x57, 0x25},
    .gua = {0},
    .sn6 = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    .gw6 = {0},
    .dns = {1, 1, 1, 1},
    .dns6 = {0x20, 0x01, 0x48, 0x60, 0x48, 0x60, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x88, 0x88},
    .ipmode = NETINFO_STATIC_ALL,
    .dhcp = NETINFO_STATIC
#else
    .dns = {1, 1, 1, 1},
    .dhcp = NETINFO_STATIC
#endif
};

static std::array<uint8_t, cfg::ETHERNET_BUF_SIZE> ethernet_buf = {};
static bool dhcp_assigned = false;
static bool dhcp_conflict = false;

// ---- Loopback-Testergebnisse (fuer die Webseite) ------------------------
static uint32_t total_bytes = 0;
static uint32_t passed_bytes = 0;
static uint32_t total_runs = 0;
static uint32_t passed_runs = 0;
static uint8_t last_ok = 0;
static uint8_t last_count = 0;
static uint8_t last_tx = 0;
static uint8_t last_rx = 0;
static bool last_got = false;
static bool last_run_pass = false;

static uint32_t millis32() {
  return to_ms_since_boot(get_absolute_time());
}

// ---- UART0-Loopback ------------------------------------------------------
static void uart_test_init() {
  uart_init(cfg::TEST_UART, cfg::UART_BAUD);
  gpio_set_function(cfg::UART_TX_PIN, GPIO_FUNC_UART);
  gpio_set_function(cfg::UART_RX_PIN, GPIO_FUNC_UART);
  uart_set_hw_flow(cfg::TEST_UART, false, false);
  uart_set_format(cfg::TEST_UART, 8, 1, UART_PARITY_NONE);
  uart_set_fifo_enabled(cfg::TEST_UART, true);
}

// Sendet ein Byte, wartet bis timeout_us auf das Echo.
static bool loopback_byte(uint8_t tx, uint8_t &rx, uint32_t timeout_us) {
  while (uart_is_readable(cfg::TEST_UART)) uart_getc(cfg::TEST_UART);  // RX leeren
  uart_putc_raw(cfg::TEST_UART, tx);
  if (!uart_is_readable_within_us(cfg::TEST_UART, timeout_us)) return false;
  rx = uart_getc(cfg::TEST_UART);
  return true;
}

static void run_loopback_pass() {
  static const uint8_t pattern[] = {0x00, 0x55, 0xAA, 0xFF, 'A', '5', 0x0D, 0x0A};
  const uint8_t count = sizeof(pattern);
  uint8_t ok = 0;

  for (uint8_t i = 0; i < count; ++i) {
    uint8_t v = pattern[i];
    uint8_t r = 0;
    ++total_bytes;
    const bool got = loopback_byte(v, r, 5000);
    last_tx = v;
    last_rx = r;
    last_got = got;
    if (got && r == v) {
      ++ok;
      ++passed_bytes;
    }
  }

  last_ok = ok;
  last_count = count;
  last_run_pass = (ok == count);
  ++total_runs;
  if (last_run_pass) ++passed_runs;
}

// ---- Netzwerk (Bootstrap wie Originalfirmware) --------------------------
static bool dhcp_requested_at_boot() {
  gpio_init(cfg::DHCP_SELECT_PIN);
  gpio_set_dir(cfg::DHCP_SELECT_PIN, GPIO_IN);
  gpio_pull_up(cfg::DHCP_SELECT_PIN);
  sleep_ms(10);
  const bool request_dhcp = gpio_get(cfg::DHCP_SELECT_PIN);
  printf("Netzwerkmodus GP%u: %s\n", cfg::DHCP_SELECT_PIN, request_dhcp ? "HIGH -> DHCP" : "LOW -> statisch");
  return request_dhcp;
}

static void dhcp_assign() {
  getIPfromDHCP(g_net_info.ip);
  getGWfromDHCP(g_net_info.gw);
  getSNfromDHCP(g_net_info.sn);
  getDNSfromDHCP(g_net_info.dns);
  g_net_info.dhcp = NETINFO_DHCP;
#if _WIZCHIP_ > W5500
  g_net_info.ipmode = NETINFO_DHCP_V4;
#endif
  network_initialize(g_net_info);
  print_network_information(g_net_info);
  dhcp_assigned = true;
}

static void dhcp_conflict_handler() {
  printf("DHCP-Konflikt: zugewiesene IP ist bereits belegt.\n");
  dhcp_conflict = true;
}

static bool acquire_dhcp_address() {
  dhcp_assigned = false;
  dhcp_conflict = false;
  g_net_info.dhcp = NETINFO_DHCP;
#if _WIZCHIP_ > W5500
  g_net_info.ipmode = NETINFO_DHCP_V4;
#endif
  network_initialize(g_net_info);
  DHCP_init(cfg::DHCP_SOCKET, ethernet_buf.data());
  reg_dhcp_cbfunc(dhcp_assign, dhcp_assign, dhcp_conflict_handler);

  uint8_t retries = 0;
  uint32_t last_tick = millis32();
  while (!dhcp_assigned && !dhcp_conflict) {
    const uint32_t now = millis32();
    if (now - last_tick >= 1000) {
      last_tick += 1000;
      DHCP_time_handler();
    }
    const uint8_t result = DHCP_run();
    if (dhcp_assigned || result == DHCP_IP_LEASED) {
      dhcp_assigned = true;
      break;
    }
    if (result == DHCP_FAILED) {
      if (++retries >= cfg::DHCP_RETRY_COUNT) break;
    } else if (result == DHCP_STOPPED) {
      break;
    }
    sleep_ms(10);
  }
  DHCP_stop();
  return dhcp_assigned && !dhcp_conflict;
}

static void init_network() {
  const bool use_dhcp = dhcp_requested_at_boot();
  printf("W6300 init (PIO/QSPI Quad) ...\n");
  wizchip_spi_initialize();
  wizchip_cris_initialize();
  wizchip_reset();
  wizchip_initialize();
  wizchip_check();
  if (use_dhcp && acquire_dhcp_address()) {
    printf("DHCP: %u.%u.%u.%u\n", g_net_info.ip[0], g_net_info.ip[1], g_net_info.ip[2], g_net_info.ip[3]);
    return;
  }
  if (use_dhcp) printf("DHCP fehlgeschlagen, Fallback auf statische IP.\n");
  g_net_info.dhcp = NETINFO_STATIC;
#if _WIZCHIP_ > W5500
  g_net_info.ipmode = NETINFO_STATIC_ALL;
#endif
  network_initialize(g_net_info);
  print_network_information(g_net_info);
}

// ---- Minimaler HTTP-Server ----------------------------------------------
static bool send_all(uint8_t sn, const std::string &data) {
  size_t sent_total = 0;
  while (sent_total < data.size()) {
    uint16_t chunk = static_cast<uint16_t>(std::min<size_t>(data.size() - sent_total, 1400));
    int32_t sent = send(sn, reinterpret_cast<uint8_t *>(const_cast<char *>(data.data() + sent_total)), chunk);
    if (sent <= 0) return false;
    sent_total += static_cast<size_t>(sent);
  }
  return true;
}

static void close_socket(uint8_t sn) {
  disconnect(sn);
  sleep_ms(1);
  close(sn);
}

static std::string hex_byte(uint8_t v) {
  char b[5];
  std::snprintf(b, sizeof(b), "0x%02X", v);
  return b;
}

static std::string build_page() {
  const bool pass = last_run_pass && total_runs > 0;
  const char *color = (total_runs == 0) ? "#888" : (pass ? "#1f8b3a" : "#b02020");
  const char *verdict = (total_runs == 0) ? "WARTE" : (pass ? "PASS" : "FAIL");

  std::string last_line;
  if (total_runs == 0) {
    last_line = "noch kein Durchlauf";
  } else if (last_got) {
    last_line = "TX=" + hex_byte(last_tx) + "  RX=" + hex_byte(last_rx);
  } else {
    last_line = "TX=" + hex_byte(last_tx) + "  RX=-- (kein Echo)";
  }

  std::string html =
      "<!DOCTYPE html><html lang=\"de\"><head><meta charset=\"UTF-8\">"
      "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
      "<meta http-equiv=\"refresh\" content=\"1\">"
      "<title>Pico UART0 Loopback</title>"
      "<style>body{font-family:'Segoe UI',system-ui,sans-serif;background:#0d1117;"
      "color:#c9d1d9;margin:0;padding:20px;text-align:center}"
      "h1{color:#60d0ff;font-size:1.3rem}.hint{color:#8b949e;font-size:.9rem}"
      ".verdict{margin:20px auto;max-width:320px;padding:24px;border-radius:12px;"
      "font-size:2.4rem;font-weight:700;color:#fff;background:";
  html += color;
  html += "}.stat{font-size:1rem;margin:6px}.mono{font-family:monospace}</style></head><body>";
  html += "<h1>Pico UART0 Loopback (GP0/GP1)</h1>";
  html += "<div class=\"hint\">Bruecke GP0 (Pin 1, TX) &harr; GP1 (Pin 2, RX)</div>";
  html += "<div class=\"verdict\">";
  html += verdict;
  html += "</div>";
  html += "<div class=\"stat\">Letzter Durchlauf: <b>" + std::to_string(last_ok) + "/" +
          std::to_string(last_count) + "</b> Bytes OK</div>";
  html += "<div class=\"stat\">Kumuliert Durchlaeufe: " + std::to_string(passed_runs) + "/" +
          std::to_string(total_runs) + " OK</div>";
  html += "<div class=\"stat\">Kumuliert Bytes: " + std::to_string(passed_bytes) + "/" +
          std::to_string(total_bytes) + " OK</div>";
  html += "<div class=\"stat mono\">" + last_line + "</div>";
  html += "<div class=\"hint\" style=\"margin-top:18px\">Ohne Bruecke muss FAIL / kein Echo erscheinen.</div>";
  html += "</body></html>";
  return html;
}

static void send_page(uint8_t sn) {
  const std::string body = build_page();
  std::string response = "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\n";
  response += "Content-Length: " + std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n";
  response += body;
  send_all(sn, response);
  close_socket(sn);
}

static void service_socket(uint8_t sn) {
  uint8_t status = getSn_SR(sn);
  switch (status) {
    case SOCK_CLOSED:
      socket(sn, Sn_MR_TCP, cfg::HTTP_PORT, 0x00);
      listen(sn);
      break;
    case SOCK_INIT:
      listen(sn);
      break;
    case SOCK_ESTABLISHED: {
      uint16_t available = getSn_RX_RSR(sn);
      if (!available) break;
      std::string raw;
      raw.resize(std::min<uint16_t>(available, 2048));
      int32_t received = recv(sn, reinterpret_cast<uint8_t *>(raw.data()), static_cast<uint16_t>(raw.size()));
      if (received <= 0) break;
      send_page(sn);
      break;
    }
    case SOCK_CLOSE_WAIT:
      close_socket(sn);
      break;
    default:
      break;
  }
}

int main() {
  stdio_init_all();
  sleep_ms(3000);
  printf("OE5RNL> Pico UART0 Loopback-Test startet\n");
  uart_test_init();
  init_network();
  for (uint8_t sn = 0; sn < cfg::HTTP_SOCKET_COUNT; ++sn) {
    socket(sn, Sn_MR_TCP, cfg::HTTP_PORT, 0x00);
    listen(sn);
  }
  printf("Test-Webseite: http://%u.%u.%u.%u/\n", g_net_info.ip[0], g_net_info.ip[1], g_net_info.ip[2], g_net_info.ip[3]);

  uint32_t last_pass = 0;
  while (true) {
    for (uint8_t sn = 0; sn < cfg::HTTP_SOCKET_COUNT; ++sn) service_socket(sn);
    if (millis32() - last_pass >= 500) {
      last_pass = millis32();
      run_loopback_pass();
    }
    sleep_ms(1);
  }
}
