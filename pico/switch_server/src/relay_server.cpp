#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/mutex.h"
#include "pico/flash.h"
#include "hardware/gpio.h"
#include "hardware/flash.h"
#include "hardware/regs/addressmap.h"
#include "hardware/sync.h"
#include "hardware/uart.h"

#include "fw_version.h"

extern "C" {
#include "dhcp.h"
#include "socket.h"
#include "wizchip_conf.h"
#include "wizchip_spi.h"
#include "wizchip_qspi_pio.h"

extern char __flash_binary_end;

// Vom WIZnet-Port (wizchip_spi.c) gesetztes QSPI-Handle; von unserer eigenen,
// nicht-blockierenden WIZchip-Init genutzt (wizchip_init_no_phy_wait()).
extern wiznet_spi_handle_t spi_handle;
}

namespace cfg {
constexpr uint8_t RELAY_COUNT = 8;
constexpr uint8_t SCENE_COUNT = 8;
constexpr std::array<uint8_t, RELAY_COUNT> RELAY_PINS = {2, 3, 4, 5, 6, 7, 8, 9};
constexpr std::array<uint8_t, RELAY_COUNT> FEEDBACK_PINS = {10, 11, 12, 13, 14, 26, 27, 28};
constexpr bool RELAY_ACTIVE_LOW = false;
constexpr uint32_t DEFAULT_FEEDBACK_TIMEOUT_MS = 500;
constexpr uint32_t MIN_FEEDBACK_TIMEOUT_MS = 10;
constexpr uint32_t MAX_FEEDBACK_TIMEOUT_MS = 10000;
#ifdef INPUT_MODE_BUTTON
// Taster-Modus: GP10-28 sind entprellte Taster; die Zeit ist die Entprellzeit.
constexpr uint32_t DEFAULT_INPUT_TIME_MS = 25;
constexpr uint32_t MIN_INPUT_TIME_MS = 5;
constexpr uint32_t MAX_INPUT_TIME_MS = 2000;
#else
constexpr uint32_t DEFAULT_INPUT_TIME_MS = DEFAULT_FEEDBACK_TIMEOUT_MS;
constexpr uint32_t MIN_INPUT_TIME_MS = MIN_FEEDBACK_TIMEOUT_MS;
constexpr uint32_t MAX_INPUT_TIME_MS = MAX_FEEDBACK_TIMEOUT_MS;
#endif
constexpr uint16_t HTTP_PORT = 80;
constexpr uint8_t HTTP_SOCKET_COUNT = 8;
constexpr uint8_t MAX_SSE_SOCKETS = HTTP_SOCKET_COUNT > 2 ? HTTP_SOCKET_COUNT - 2 : 1;
constexpr uint8_t DHCP_SELECT_PIN = 15;
constexpr uint8_t DHCP_SOCKET = 0;
constexpr uint8_t DHCP_RETRY_COUNT = 5;
constexpr uint32_t PHY_LINK_WAIT_MS = 4000;  // max. Wartezeit auf LAN-Link vor DHCP
constexpr size_t ETHERNET_BUF_SIZE = 2048;
constexpr uint32_t SESSION_LIFETIME_MS = 30UL * 60UL * 1000UL;
constexpr uint32_t SSE_KEEPALIVE_MS = 1UL * 1000UL;
constexpr uint32_t GUEST_LIFETIME_MS = 60UL * 1000UL;
constexpr uint32_t PERSIST_MAGIC = 0x4f453558;
constexpr uint32_t PERSIST_VERSION = 7;
constexpr size_t MAX_USERS = 8;
constexpr size_t MAX_API_KEYS = 8;
constexpr size_t MAX_GUEST_VISITORS = 16;
constexpr const char *DEFAULT_ADMIN_USER = "admin";
constexpr const char *OLD_ADMIN_HASH = "8c6976e5b5410415bde908bd4dee15dfb167a9c873fc4bb8a81f6f2ab448a918";
constexpr const char *DEFAULT_ADMIN_HASH = "01d1b800ad11a2bc64e75374283bb735a2e9fe0df3f142c73b740962adc07683";
}

struct PersistedUser {
  char name[32];
  char hash[65];
  char role[8];
};

struct PersistedApiKey {
  char key[17];
  char comment[65];
};

struct PersistedConfigV1 {
  uint32_t magic;
  uint32_t version;
  uint8_t relay_states[cfg::RELAY_COUNT];
  uint8_t public_access;
  char relay_names[cfg::RELAY_COUNT][33];
  char site_title[65];
  char site_subtitle[65];
  uint32_t user_count;
  PersistedUser users[cfg::MAX_USERS];
  uint32_t api_key_count;
  char api_keys[cfg::MAX_API_KEYS][17];
};

struct PersistedConfigV2 {
  uint32_t magic;
  uint32_t version;
  uint8_t relay_states[cfg::RELAY_COUNT];
  uint8_t public_access;
  char relay_names[cfg::RELAY_COUNT][33];
  char site_title[65];
  char site_subtitle[65];
  uint32_t user_count;
  PersistedUser users[cfg::MAX_USERS];
  uint32_t api_key_count;
  PersistedApiKey api_keys[cfg::MAX_API_KEYS];
};

struct PersistedConfigV3 {
  uint32_t magic;
  uint32_t version;
  uint8_t relay_states[cfg::RELAY_COUNT];
  uint8_t public_access;
  char relay_names[cfg::RELAY_COUNT][33];
  char site_title[65];
  char site_subtitle[65];
  uint32_t user_count;
  PersistedUser users[cfg::MAX_USERS];
  uint32_t api_key_count;
  PersistedApiKey api_keys[cfg::MAX_API_KEYS];
  uint8_t static_ip[4];
  uint8_t static_sn[4];
  uint8_t static_gw[4];
};

struct PersistedConfigV4 {
  uint32_t magic;
  uint32_t version;
  uint8_t relay_states[cfg::RELAY_COUNT];
  uint8_t public_access;
  char relay_names[cfg::RELAY_COUNT][33];
  char site_title[65];
  char site_subtitle[65];
  uint32_t user_count;
  PersistedUser users[cfg::MAX_USERS];
  uint32_t api_key_count;
  PersistedApiKey api_keys[cfg::MAX_API_KEYS];
  uint8_t static_ip[4];
  uint8_t static_sn[4];
  uint8_t static_gw[4];
  uint8_t scene_mode;
  uint8_t scene_enabled[cfg::SCENE_COUNT];
  char scene_names[cfg::SCENE_COUNT][33];
  uint8_t scene_action[cfg::SCENE_COUNT][cfg::RELAY_COUNT];
};

struct PersistedConfigV5 {
  uint32_t magic;
  uint32_t version;
  uint8_t relay_states[cfg::RELAY_COUNT];
  uint8_t public_access;
  char relay_names[cfg::RELAY_COUNT][33];
  char site_title[65];
  char site_subtitle[65];
  uint32_t user_count;
  PersistedUser users[cfg::MAX_USERS];
  uint32_t api_key_count;
  PersistedApiKey api_keys[cfg::MAX_API_KEYS];
  uint8_t static_ip[4];
  uint8_t static_sn[4];
  uint8_t static_gw[4];
  uint8_t scene_mode;
  uint8_t scene_enabled[cfg::SCENE_COUNT];
  char scene_names[cfg::SCENE_COUNT][33];
  uint8_t scene_action[cfg::SCENE_COUNT][cfg::RELAY_COUNT];
  uint8_t active_scene;
};

struct PersistedConfigV6 {
  uint32_t magic;
  uint32_t version;
  uint8_t relay_states[cfg::RELAY_COUNT];
  uint8_t public_access;
  char relay_names[cfg::RELAY_COUNT][33];
  char site_title[65];
  char site_subtitle[65];
  uint32_t user_count;
  PersistedUser users[cfg::MAX_USERS];
  uint32_t api_key_count;
  PersistedApiKey api_keys[cfg::MAX_API_KEYS];
  uint8_t static_ip[4];
  uint8_t static_sn[4];
  uint8_t static_gw[4];
  uint8_t scene_mode;
  uint8_t scene_enabled[cfg::SCENE_COUNT];
  char scene_names[cfg::SCENE_COUNT][33];
  uint8_t scene_action[cfg::SCENE_COUNT][cfg::RELAY_COUNT];  // 0=aus, 1=ein, 2=unveraendert
  uint8_t active_scene;  // 0=keine, 1..SCENE_COUNT=zuletzt aktive Szene
  uint8_t relay_active_low[cfg::RELAY_COUNT];  // je Kanal: 1=LOW-aktiv, 0=HIGH-aktiv
};

struct PersistedConfig {
  uint32_t magic;
  uint32_t version;
  uint8_t relay_states[cfg::RELAY_COUNT];
  uint8_t public_access;
  char relay_names[cfg::RELAY_COUNT][33];
  char site_title[65];
  char site_subtitle[65];
  uint32_t user_count;
  PersistedUser users[cfg::MAX_USERS];
  uint32_t api_key_count;
  PersistedApiKey api_keys[cfg::MAX_API_KEYS];
  uint8_t static_ip[4];
  uint8_t static_sn[4];
  uint8_t static_gw[4];
  uint8_t scene_mode;
  uint8_t scene_enabled[cfg::SCENE_COUNT];
  char scene_names[cfg::SCENE_COUNT][33];
  uint8_t scene_action[cfg::SCENE_COUNT][cfg::RELAY_COUNT];
  uint8_t active_scene;
  uint8_t relay_active_low[cfg::RELAY_COUNT];
  uint8_t feedback_enabled[cfg::RELAY_COUNT];
  uint8_t feedback_active_low[cfg::RELAY_COUNT];
  uint32_t feedback_timeout_ms;
};

struct User {
  std::string hash;
  std::string role;
};

struct ApiKeyEntry {
  std::string key;
  std::string comment;
};

struct Session {
  std::string token;
  std::string username;
  std::string role;
  uint32_t expires;
};

struct GuestVisitor {
  std::string ip;
  uint32_t expires;
};

struct HttpRequest {
  std::string method;
  std::string path;
  std::string query;
  std::string body;
  std::string client_ip;
  std::map<std::string, std::string> headers;
};

enum class ConfigLoadResult {
  Loaded,
  Empty,
  LayoutChanged
};

// Szene: pro Relais 0=aus, 1=ein, 2=unveraendert. Nur aktivierte Szenen sind bedienbar.
struct Scene {
  bool enabled = false;
  std::string name;
  std::array<uint8_t, cfg::RELAY_COUNT> action = {2, 2, 2, 2, 2, 2, 2, 2};
};

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

static std::array<bool, cfg::RELAY_COUNT> relay_states = {false, false, false, false, false, false, false, false};
static std::array<std::string, cfg::RELAY_COUNT> relay_names;
static std::array<bool, cfg::RELAY_COUNT> relay_active_low = {false, false, false, false, false, false, false, false};
static std::array<bool, cfg::RELAY_COUNT> feedback_enabled{};
static std::array<bool, cfg::RELAY_COUNT> feedback_active_low{};
static uint32_t feedback_timeout_ms = cfg::DEFAULT_INPUT_TIME_MS;
static std::array<bool, cfg::RELAY_COUNT> feedback_pending{};
static std::array<bool, cfg::RELAY_COUNT> feedback_error{};
static std::array<uint32_t, cfg::RELAY_COUNT> feedback_deadline{};
static std::array<int8_t, cfg::RELAY_COUNT> feedback_source_scene = {-1, -1, -1, -1, -1, -1, -1, -1};
static std::array<bool, cfg::SCENE_COUNT> scene_feedback_error{};
#ifdef INPUT_MODE_BUTTON
static std::array<bool, cfg::RELAY_COUNT> button_pressed{};   // entprellter Tasterzustand (true = gedrueckt)
static std::array<bool, cfg::RELAY_COUNT> button_raw{};       // zuletzt gelesener Rohpegel (true = gedrueckt)
static std::array<uint32_t, cfg::RELAY_COUNT> button_since{}; // Zeitpunkt der letzten Rohpegel-Aenderung
#endif
static std::string site_title = "Relais-Steuerung";
static std::string site_subtitle = "Relais-&Uuml;bersicht";
static std::string esp_fw_version = "-";  // vom ESP-Display per UART gemeldet (VER:)
static bool public_access = false;
static bool scene_mode = false;
static std::array<Scene, cfg::SCENE_COUNT> scenes{};
static int active_scene = -1;  // zuletzt aktivierte Szene (-1 = keine)
static bool scene_dirty = false;  // Relais im Szenen-Modus direkt geändert
static bool esp_link_display_dirty = false;
static bool esp_link_state_dirty = false;
static std::array<uint8_t, 4> static_ip = {192, 168, 88, 188};
static std::array<uint8_t, 4> static_sn = {255, 255, 255, 0};
static std::array<uint8_t, 4> static_gw = {192, 168, 88, 254};
static std::map<std::string, User> users_db;
static std::vector<ApiKeyEntry> api_keys_db;
static std::vector<Session> sessions;
static std::vector<GuestVisitor> guest_visitors;
static std::vector<std::string> persistent_admin_tokens;
static std::array<bool, cfg::HTTP_SOCKET_COUNT> sse_socket = {};
static std::array<bool, cfg::HTTP_SOCKET_COUNT> sse_show_users = {};
static std::array<uint8_t, cfg::ETHERNET_BUF_SIZE> ethernet_buf = {};
static uint32_t last_keepalive = 0;
static uint32_t random_state = 0x12345678;
static bool persist_write_locked = false;
static bool dhcp_assigned = false;
static bool dhcp_conflict = false;
static bool g_use_dhcp = false;  // Bootstrap-Modus: true = DHCP angefordert
static bool lan_link_up = true;  // zuletzt bekannter PHY-Link-Status (W6300)
static uint32_t last_link_check = 0;

// ---- Dual-Core-Synchronisation -------------------------------------------
// core0 = Steuerung (ESP-UART, Relais-GPIO, Feedback). core1 = Netzwerk
// (W6300, HTTP, DHCP, Flash). Ein einziger rekursiver Mutex schuetzt den
// geteilten Steuerzustand (Relaiszustaende, Namen, Titel, Szenen, Feedback).
// Regel: g_state_mtx NIE ueber Netz-I/O oder Flash-Schreiben halten -
// Serializer geben Kopien zurueck, gesendet wird ausserhalb des Locks. Dadurch
// blockiert ein haengender Netz-Send auf core1 niemals das Schalten auf core0.
static recursive_mutex_t g_state_mtx;
static volatile bool g_sse_dirty = false;      // core0 -> core1: SSE-Broadcast anfordern
static volatile bool g_persist_dirty = false;  // core0 -> core1: Flash-Speichern anfordern
static volatile bool g_ip_status_dirty = false;  // core1 -> core0: IP-/Link-Status ans ESP senden
static volatile bool g_core1_started = false;  // true, sobald core1 (Netz) laeuft

struct StateLock {
  StateLock() { recursive_mutex_enter_blocking(&g_state_mtx); }
  ~StateLock() { recursive_mutex_exit(&g_state_mtx); }
  StateLock(const StateLock &) = delete;
  StateLock &operator=(const StateLock &) = delete;
};

static_assert(sizeof(PersistedConfig) <= FLASH_SECTOR_SIZE, "PersistedConfig must fit into one flash sector");

static uint32_t millis32() {
  return to_ms_since_boot(get_absolute_time());
}

static wiz_NetInfo current_net_info() {
  wiz_NetInfo net_info = g_net_info;
  ctlnetwork(CN_GET_NETINFO, &net_info);
  return net_info;
}

static std::string format_mac(const uint8_t mac[6]) {
  char buffer[18];
  std::snprintf(buffer, sizeof(buffer), "%02X:%02X:%02X:%02X:%02X:%02X",
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return buffer;
}

static std::string format_ipv4(const uint8_t ip[4]) {
  return std::to_string(ip[0]) + "." + std::to_string(ip[1]) + "." +
         std::to_string(ip[2]) + "." + std::to_string(ip[3]);
}

// Text fuer die Display-Statuszeile: Kabel ab -> Hinweis statt IP.
static std::string ip_status_text() {
  if (!lan_link_up) return "LAN connection lost";
  if (g_use_dhcp && !dhcp_assigned) return "connecting LAN";
  return format_ipv4(g_net_info.ip);
}

static bool parse_ipv4(const std::string &text, std::array<uint8_t, 4> &out) {
  size_t pos = 0;
  for (size_t part = 0; part < out.size(); ++part) {
    if (pos >= text.size()) return false;
    int value = 0;
    size_t digits = 0;
    while (pos < text.size() && std::isdigit(static_cast<unsigned char>(text[pos]))) {
      value = value * 10 + (text[pos] - '0');
      if (value > 255) return false;
      ++pos;
      ++digits;
    }
    if (digits == 0) return false;
    out[part] = static_cast<uint8_t>(value);
    if (part + 1 < out.size()) {
      if (pos >= text.size() || text[pos] != '.') return false;
      ++pos;
    }
  }
  return pos == text.size();
}

static void apply_static_network_to_runtime() {
  std::memcpy(g_net_info.ip, static_ip.data(), static_ip.size());
  std::memcpy(g_net_info.sn, static_sn.data(), static_sn.size());
  std::memcpy(g_net_info.gw, static_gw.data(), static_gw.size());
  g_net_info.dhcp = NETINFO_STATIC;
#if _WIZCHIP_ > W5500
  g_net_info.ipmode = NETINFO_STATIC_ALL;
#endif
}

static std::string network_mode_text(const wiz_NetInfo &net_info) {
  return net_info.dhcp == NETINFO_DHCP ? "DHCP" : "Static";
}

static bool dhcp_requested_at_boot() {
  gpio_init(cfg::DHCP_SELECT_PIN);
  gpio_set_dir(cfg::DHCP_SELECT_PIN, GPIO_IN);
  gpio_pull_up(cfg::DHCP_SELECT_PIN);
  sleep_ms(10);
  const bool request_dhcp = gpio_get(cfg::DHCP_SELECT_PIN);
  printf("Netzwerkmodus-Bootstrap GP%u: %s\n", cfg::DHCP_SELECT_PIN, request_dhcp ? "HIGH -> DHCP" : "LOW -> statische IP");
  return request_dhcp;
}

static bool expired(uint32_t deadline) {
  return static_cast<int32_t>(millis32() - deadline) >= 0;
}

static std::string trim(std::string value) {
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) value.erase(value.begin());
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) value.pop_back();
  return value;
}

static std::string lower(std::string value) {
  for (char &ch : value) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  return value;
}

static std::string normalize_username(const std::string &value) {
  return lower(trim(value));
}

static std::string html_escape(const std::string &value) {
  std::string out;
  out.reserve(value.size() + 8);
  for (char ch : value) {
    if (ch == '&') out += "&amp;";
    else if (ch == '<') out += "&lt;";
    else if (ch == '>') out += "&gt;";
    else if (ch == '"') out += "&quot;";
    else out += ch;
  }
  return out;
}

static std::string json_escape(const std::string &value) {
  std::string out;
  out.reserve(value.size() + 8);
  for (char ch : value) {
    if (ch == '"') out += "\\\"";
    else if (ch == '\\') out += "\\\\";
    else if (ch == '\n') out += "\\n";
    else if (ch == '\r') out += "\\r";
    else out += ch;
  }
  return out;
}

static std::string js_string_arg(const std::string &value) {
  return html_escape("\"" + json_escape(value) + "\"");
}

static int hex_value(char ch) {
  if (ch >= '0' && ch <= '9') return ch - '0';
  if (ch >= 'a' && ch <= 'f') return 10 + ch - 'a';
  if (ch >= 'A' && ch <= 'F') return 10 + ch - 'A';
  return -1;
}

static std::string url_decode(const std::string &value) {
  std::string out;
  for (size_t i = 0; i < value.size(); ++i) {
    if (value[i] == '+') out += ' ';
    else if (value[i] == '%' && i + 2 < value.size() && hex_value(value[i + 1]) >= 0 && hex_value(value[i + 2]) >= 0) {
      out += static_cast<char>((hex_value(value[i + 1]) << 4) | hex_value(value[i + 2]));
      i += 2;
    } else out += value[i];
  }
  return out;
}

static std::string query_param(const std::string &query, const std::string &name) {
  size_t start = 0;
  while (start <= query.size()) {
    size_t amp = query.find('&', start);
    std::string part = query.substr(start, amp == std::string::npos ? std::string::npos : amp - start);
    size_t eq = part.find('=');
    if (eq != std::string::npos && url_decode(part.substr(0, eq)) == name) return url_decode(part.substr(eq + 1));
    if (amp == std::string::npos) break;
    start = amp + 1;
  }
  return "";
}

static std::map<std::string, std::string> parse_form(const std::string &body) {
  std::map<std::string, std::string> form;
  size_t start = 0;
  while (start <= body.size()) {
    size_t amp = body.find('&', start);
    std::string part = body.substr(start, amp == std::string::npos ? std::string::npos : amp - start);
    size_t eq = part.find('=');
    if (eq != std::string::npos) form[url_decode(part.substr(0, eq))] = url_decode(part.substr(eq + 1));
    if (amp == std::string::npos) break;
    start = amp + 1;
  }
  return form;
}

static std::string cookie_value(const std::string &header, const std::string &name) {
  size_t start = 0;
  const std::string prefix = name + "=";
  while (start < header.size()) {
    size_t semi = header.find(';', start);
    std::string part = trim(header.substr(start, semi == std::string::npos ? std::string::npos : semi - start));
    if (part.rfind(prefix, 0) == 0) return part.substr(prefix.size());
    if (semi == std::string::npos) break;
    start = semi + 1;
  }
  return "";
}

static uint32_t rotr(uint32_t x, uint8_t n) {
  return (x >> n) | (x << (32 - n));
}

static std::string sha256_hex(const std::string &input) {
  static const uint32_t k[64] = {
      0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
      0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
      0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
      0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
      0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
      0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
      0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
      0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
  uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
  uint64_t bit_len = static_cast<uint64_t>(input.size()) * 8ULL;
  size_t padded_len = ((input.size() + 1 + 8 + 63) / 64) * 64;
  std::vector<uint8_t> msg(padded_len, 0);
  std::memcpy(msg.data(), input.data(), input.size());
  msg[input.size()] = 0x80;
  for (int i = 0; i < 8; ++i) msg[padded_len - 1 - i] = static_cast<uint8_t>((bit_len >> (8 * i)) & 0xff);
  for (size_t offset = 0; offset < padded_len; offset += 64) {
    uint32_t w[64];
    for (int i = 0; i < 16; ++i) {
      w[i] = (static_cast<uint32_t>(msg[offset + i * 4]) << 24) | (static_cast<uint32_t>(msg[offset + i * 4 + 1]) << 16) |
             (static_cast<uint32_t>(msg[offset + i * 4 + 2]) << 8) | static_cast<uint32_t>(msg[offset + i * 4 + 3]);
    }
    for (int i = 16; i < 64; ++i) w[i] = w[i - 16] + (rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3)) + w[i - 7] + (rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10));
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
    for (int i = 0; i < 64; ++i) {
      uint32_t temp1 = hh + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & f) ^ ((~e) & g)) + k[i] + w[i];
      uint32_t temp2 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
      hh = g; g = f; f = e; e = d + temp1; d = c; c = b; b = a; a = temp1 + temp2;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
  }
  char out[65];
  for (int i = 0; i < 8; ++i) std::snprintf(out + i * 8, 9, "%08lx", static_cast<unsigned long>(h[i]));
  out[64] = 0;
  return out;
}

static uint32_t next_random() {
  random_state ^= random_state << 13;
  random_state ^= random_state >> 17;
  random_state ^= random_state << 5;
  return random_state;
}

static std::string random_hex(size_t bytes) {
  static const char *hex = "0123456789abcdef";
  std::string out;
  out.reserve(bytes * 2);
  for (size_t i = 0; i < bytes; ++i) {
    uint8_t value = static_cast<uint8_t>(next_random() & 0xff);
    out += hex[value >> 4];
    out += hex[value & 0x0f];
  }
  return out;
}

static void copy_cstr(char *dest, size_t dest_size, const std::string &value) {
  if (!dest_size) return;
  std::snprintf(dest, dest_size, "%s", value.c_str());
}

static uintptr_t flash_binary_end_offset() {
  return reinterpret_cast<uintptr_t>(&__flash_binary_end) - XIP_BASE;
}

static uintptr_t align_up_to_sector(uintptr_t value) {
  return (value + FLASH_SECTOR_SIZE - 1) & ~(FLASH_SECTOR_SIZE - 1);
}

static std::array<uintptr_t, 4> persist_flash_offsets() {
  return {
      256u * 1024u - FLASH_SECTOR_SIZE,
      512u * 1024u - FLASH_SECTOR_SIZE,
      1024u * 1024u - FLASH_SECTOR_SIZE,
      PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE,
  };
}

static bool persist_flash_offset_usable(uintptr_t offset) {
  if (offset + FLASH_SECTOR_SIZE > PICO_FLASH_SIZE_BYTES) return false;
  return offset >= align_up_to_sector(flash_binary_end_offset());
}

static const PersistedConfig *persisted_config_ptr(uintptr_t offset) {
  return reinterpret_cast<const PersistedConfig *>(XIP_BASE + offset);
}

static bool is_erased_flash(const void *data, size_t size) {
  const uint8_t *bytes = static_cast<const uint8_t *>(data);
  for (size_t i = 0; i < size; ++i) {
    if (bytes[i] != 0xff) return false;
  }
  return true;
}

// Fuehrt das eigentliche Flash-Erase/Program aus. Wird via flash_safe_execute
// (core1) oder mit gesperrten Interrupts (Boot, core0) aufgerufen.
struct FlashWriteCtx {
  const uint8_t *sector;
  const uintptr_t *offsets;
  size_t count;
  bool wrote;
};

static void flash_write_cb(void *param) {
  FlashWriteCtx *ctx = static_cast<FlashWriteCtx *>(param);
  for (size_t i = 0; i < ctx->count; ++i) {
    const uintptr_t offset = ctx->offsets[i];
    if (!persist_flash_offset_usable(offset)) continue;
    bool duplicate = false;
    for (size_t j = 0; j < i; ++j) {
      if (ctx->offsets[j] == offset) { duplicate = true; break; }
    }
    if (duplicate) continue;
    flash_range_erase(offset, FLASH_SECTOR_SIZE);
    flash_range_program(offset, ctx->sector, FLASH_SECTOR_SIZE);
    ctx->wrote = true;
  }
}

static void save_config() {
  if (persist_write_locked) {
    printf("Persistenz-Schreibschutz aktiv, Konfiguration nicht gespeichert.\n");
    return;
  }

  PersistedConfig config{};
  std::array<uint8_t, FLASH_SECTOR_SIZE> sector{};
  {
    StateLock snapshot;  // konsistenter Snapshot des geteilten Steuerzustands unter Lock
  config.magic = cfg::PERSIST_MAGIC;
  config.version = cfg::PERSIST_VERSION;
  for (uint8_t i = 0; i < cfg::RELAY_COUNT; ++i) {
    config.relay_states[i] = relay_states[i] ? 1 : 0;
    copy_cstr(config.relay_names[i], sizeof(config.relay_names[i]), relay_names[i]);
    config.relay_active_low[i] = relay_active_low[i] ? 1 : 0;
    config.feedback_enabled[i] = feedback_enabled[i] ? 1 : 0;
    config.feedback_active_low[i] = feedback_active_low[i] ? 1 : 0;
  }
  config.feedback_timeout_ms = feedback_timeout_ms;
  config.public_access = public_access ? 1 : 0;
  copy_cstr(config.site_title, sizeof(config.site_title), site_title);
  copy_cstr(config.site_subtitle, sizeof(config.site_subtitle), site_subtitle);

  for (const auto &entry : users_db) {
    if (config.user_count >= cfg::MAX_USERS) break;
    PersistedUser &user = config.users[config.user_count++];
    copy_cstr(user.name, sizeof(user.name), entry.first);
    copy_cstr(user.hash, sizeof(user.hash), entry.second.hash);
    copy_cstr(user.role, sizeof(user.role), entry.second.role);
  }
  for (const ApiKeyEntry &entry : api_keys_db) {
    if (config.api_key_count >= cfg::MAX_API_KEYS) break;
    PersistedApiKey &stored = config.api_keys[config.api_key_count++];
    copy_cstr(stored.key, sizeof(stored.key), entry.key);
    copy_cstr(stored.comment, sizeof(stored.comment), entry.comment);
  }
  std::memcpy(config.static_ip, static_ip.data(), static_ip.size());
  std::memcpy(config.static_sn, static_sn.data(), static_sn.size());
  std::memcpy(config.static_gw, static_gw.data(), static_gw.size());
  config.scene_mode = scene_mode ? 1 : 0;
  for (uint8_t s = 0; s < cfg::SCENE_COUNT; ++s) {
    config.scene_enabled[s] = scenes[s].enabled ? 1 : 0;
    copy_cstr(config.scene_names[s], sizeof(config.scene_names[s]), scenes[s].name);
    for (uint8_t r = 0; r < cfg::RELAY_COUNT; ++r) config.scene_action[s][r] = scenes[s].action[r];
  }
  config.active_scene = (active_scene >= 0 && active_scene < cfg::SCENE_COUNT) ? static_cast<uint8_t>(active_scene + 1) : 0;

  std::memset(sector.data(), 0xff, sector.size());
  std::memcpy(sector.data(), &config, sizeof(config));
  }  // g_state_mtx hier freigeben: Flash-Schreiben laeuft OHNE Lock

  std::array<uintptr_t, 4> offsets = persist_flash_offsets();
  FlashWriteCtx ctx{sector.data(), offsets.data(), offsets.size(), false};
  // Flash-Erase/Program macht den Flash fuer BEIDE Kerne unzugaenglich. Sobald
  // core1 laeuft, laufen wir hier auf core1 und sperren core0 per
  // flash_safe_execute aus; vor dem core1-Start (Boot-Migration) genuegt das
  // Sperren der Interrupts.
  if (g_core1_started) {
    if (flash_safe_execute(flash_write_cb, &ctx, 3000) != PICO_OK)
      printf("flash_safe_execute fehlgeschlagen, Konfiguration nicht gespeichert.\n");
  } else {
    uint32_t interrupts = save_and_disable_interrupts();
    flash_write_cb(&ctx);
    restore_interrupts(interrupts);
  }
  if (!ctx.wrote) printf("Kein sicherer Persistenz-Slot gefunden, Konfiguration nicht gespeichert.\n");
}

static ConfigLoadResult load_config() {
  bool saw_invalid = false;
  bool migrated = false;
  for (uintptr_t offset : persist_flash_offsets()) {
    if (!persist_flash_offset_usable(offset)) continue;
    const PersistedConfig *config = persisted_config_ptr(offset);
    if (is_erased_flash(config, sizeof(PersistedConfig))) {
      continue;
    }
    if (config->magic != cfg::PERSIST_MAGIC) {
      saw_invalid = true;
      continue;
    }
    if (config->version != cfg::PERSIST_VERSION) {
      // Versuch, von einer alten Layout-Version zu migrieren.
      if (config->version == 6) {
        const PersistedConfigV6 *old_cfg = reinterpret_cast<const PersistedConfigV6 *>(config);
        printf("Migration von Persistenz-Layout v6 -> v%lu (Slot 0x%08lx).\n",
               static_cast<unsigned long>(cfg::PERSIST_VERSION), static_cast<unsigned long>(offset));
        for (uint8_t i = 0; i < cfg::RELAY_COUNT; ++i) {
          relay_states[i] = old_cfg->relay_states[i] != 0;
          if (old_cfg->relay_names[i][0]) relay_names[i] = old_cfg->relay_names[i];
          relay_active_low[i] = old_cfg->relay_active_low[i] != 0;
        }
        public_access = old_cfg->public_access != 0;
        if (old_cfg->site_title[0]) site_title = old_cfg->site_title;
        if (old_cfg->site_subtitle[0]) site_subtitle = old_cfg->site_subtitle;
        users_db.clear();
        uint32_t user_count = std::min<uint32_t>(old_cfg->user_count, cfg::MAX_USERS);
        for (uint32_t i = 0; i < user_count; ++i) {
          const PersistedUser &stored = old_cfg->users[i];
          if (stored.name[0] && stored.hash[0])
            users_db[normalize_username(stored.name)] = {stored.hash, stored.role[0] ? stored.role : "user"};
        }
        api_keys_db.clear();
        uint32_t key_count = std::min<uint32_t>(old_cfg->api_key_count, cfg::MAX_API_KEYS);
        for (uint32_t i = 0; i < key_count; ++i) {
          if (old_cfg->api_keys[i].key[0]) api_keys_db.push_back({old_cfg->api_keys[i].key, old_cfg->api_keys[i].comment});
        }
        std::memcpy(static_ip.data(), old_cfg->static_ip, static_ip.size());
        std::memcpy(static_sn.data(), old_cfg->static_sn, static_sn.size());
        std::memcpy(static_gw.data(), old_cfg->static_gw, static_gw.size());
        scene_mode = old_cfg->scene_mode != 0;
        for (uint8_t s = 0; s < cfg::SCENE_COUNT; ++s) {
          scenes[s].enabled = old_cfg->scene_enabled[s] != 0;
          scenes[s].name = old_cfg->scene_names[s];
          for (uint8_t r = 0; r < cfg::RELAY_COUNT; ++r) {
            uint8_t action = old_cfg->scene_action[s][r];
            scenes[s].action[r] = action <= 2 ? action : 2;
          }
        }
        if (old_cfg->active_scene >= 1 && old_cfg->active_scene <= cfg::SCENE_COUNT &&
            scenes[old_cfg->active_scene - 1].enabled) {
          active_scene = static_cast<int>(old_cfg->active_scene) - 1;
        } else {
          active_scene = -1;
        }
        migrated = true;
        break;
      }
      if (config->version == 5) {
        const PersistedConfigV5 *old_cfg = reinterpret_cast<const PersistedConfigV5 *>(config);
        printf("Migration von Persistenz-Layout v5 -> v%lu (Slot 0x%08lx).\n",
               static_cast<unsigned long>(cfg::PERSIST_VERSION), static_cast<unsigned long>(offset));
        for (uint8_t i = 0; i < cfg::RELAY_COUNT; ++i) {
          relay_states[i] = old_cfg->relay_states[i] != 0;
          if (old_cfg->relay_names[i][0]) relay_names[i] = old_cfg->relay_names[i];
          relay_active_low[i] = cfg::RELAY_ACTIVE_LOW;
        }
        public_access = old_cfg->public_access != 0;
        if (old_cfg->site_title[0]) site_title = old_cfg->site_title;
        if (old_cfg->site_subtitle[0]) site_subtitle = old_cfg->site_subtitle;
        users_db.clear();
        uint32_t user_count = std::min<uint32_t>(old_cfg->user_count, cfg::MAX_USERS);
        for (uint32_t i = 0; i < user_count; ++i) {
          const PersistedUser &stored = old_cfg->users[i];
          if (stored.name[0] && stored.hash[0])
            users_db[normalize_username(stored.name)] = {stored.hash, stored.role[0] ? stored.role : "user"};
        }
        api_keys_db.clear();
        uint32_t key_count = std::min<uint32_t>(old_cfg->api_key_count, cfg::MAX_API_KEYS);
        for (uint32_t i = 0; i < key_count; ++i) {
          if (old_cfg->api_keys[i].key[0]) api_keys_db.push_back({old_cfg->api_keys[i].key, old_cfg->api_keys[i].comment});
        }
        std::memcpy(static_ip.data(), old_cfg->static_ip, static_ip.size());
        std::memcpy(static_sn.data(), old_cfg->static_sn, static_sn.size());
        std::memcpy(static_gw.data(), old_cfg->static_gw, static_gw.size());
        scene_mode = old_cfg->scene_mode != 0;
        for (uint8_t s = 0; s < cfg::SCENE_COUNT; ++s) {
          scenes[s].enabled = old_cfg->scene_enabled[s] != 0;
          scenes[s].name = old_cfg->scene_names[s];
          for (uint8_t r = 0; r < cfg::RELAY_COUNT; ++r) {
            uint8_t a = old_cfg->scene_action[s][r];
            scenes[s].action[r] = (a <= 2) ? a : 2;
          }
        }
        if (old_cfg->active_scene >= 1 && old_cfg->active_scene <= cfg::SCENE_COUNT &&
            scenes[old_cfg->active_scene - 1].enabled) {
          active_scene = static_cast<int>(old_cfg->active_scene) - 1;
        } else {
          active_scene = -1;
        }
        migrated = true;
        break;
      }
      if (config->version == 4) {
        const PersistedConfigV4 *old_cfg = reinterpret_cast<const PersistedConfigV4 *>(config);
        printf("Migration von Persistenz-Layout v4 -> v%lu (Slot 0x%08lx).\n",
               static_cast<unsigned long>(cfg::PERSIST_VERSION), static_cast<unsigned long>(offset));
        for (uint8_t i = 0; i < cfg::RELAY_COUNT; ++i) {
          relay_states[i] = old_cfg->relay_states[i] != 0;
          if (old_cfg->relay_names[i][0]) relay_names[i] = old_cfg->relay_names[i];
        }
        public_access = old_cfg->public_access != 0;
        if (old_cfg->site_title[0]) site_title = old_cfg->site_title;
        if (old_cfg->site_subtitle[0]) site_subtitle = old_cfg->site_subtitle;
        users_db.clear();
        uint32_t user_count = std::min<uint32_t>(old_cfg->user_count, cfg::MAX_USERS);
        for (uint32_t i = 0; i < user_count; ++i) {
          const PersistedUser &stored = old_cfg->users[i];
          if (stored.name[0] && stored.hash[0])
            users_db[normalize_username(stored.name)] = {stored.hash, stored.role[0] ? stored.role : "user"};
        }
        api_keys_db.clear();
        uint32_t key_count = std::min<uint32_t>(old_cfg->api_key_count, cfg::MAX_API_KEYS);
        for (uint32_t i = 0; i < key_count; ++i) {
          if (old_cfg->api_keys[i].key[0]) api_keys_db.push_back({old_cfg->api_keys[i].key, old_cfg->api_keys[i].comment});
        }
        std::memcpy(static_ip.data(), old_cfg->static_ip, static_ip.size());
        std::memcpy(static_sn.data(), old_cfg->static_sn, static_sn.size());
        std::memcpy(static_gw.data(), old_cfg->static_gw, static_gw.size());
        scene_mode = old_cfg->scene_mode != 0;
        for (uint8_t s = 0; s < cfg::SCENE_COUNT; ++s) {
          scenes[s].enabled = old_cfg->scene_enabled[s] != 0;
          scenes[s].name = old_cfg->scene_names[s];
          for (uint8_t r = 0; r < cfg::RELAY_COUNT; ++r) {
            uint8_t a = old_cfg->scene_action[s][r];
            scenes[s].action[r] = (a <= 2) ? a : 2;
          }
        }
        active_scene = -1;
        migrated = true;
        break;
      }
      if (config->version == 3) {
        const PersistedConfigV3 *old_cfg = reinterpret_cast<const PersistedConfigV3 *>(config);
        printf("Migration von Persistenz-Layout v3 -> v%lu (Slot 0x%08lx).\n",
               static_cast<unsigned long>(cfg::PERSIST_VERSION), static_cast<unsigned long>(offset));
        for (uint8_t i = 0; i < cfg::RELAY_COUNT; ++i) {
          relay_states[i] = old_cfg->relay_states[i] != 0;
          if (old_cfg->relay_names[i][0]) relay_names[i] = old_cfg->relay_names[i];
        }
        public_access = old_cfg->public_access != 0;
        if (old_cfg->site_title[0]) site_title = old_cfg->site_title;
        if (old_cfg->site_subtitle[0]) site_subtitle = old_cfg->site_subtitle;
        users_db.clear();
        uint32_t user_count = std::min<uint32_t>(old_cfg->user_count, cfg::MAX_USERS);
        for (uint32_t i = 0; i < user_count; ++i) {
          const PersistedUser &stored = old_cfg->users[i];
          if (stored.name[0] && stored.hash[0])
            users_db[normalize_username(stored.name)] = {stored.hash, stored.role[0] ? stored.role : "user"};
        }
        api_keys_db.clear();
        uint32_t key_count = std::min<uint32_t>(old_cfg->api_key_count, cfg::MAX_API_KEYS);
        for (uint32_t i = 0; i < key_count; ++i) {
          if (old_cfg->api_keys[i].key[0]) api_keys_db.push_back({old_cfg->api_keys[i].key, old_cfg->api_keys[i].comment});
        }
        std::memcpy(static_ip.data(), old_cfg->static_ip, static_ip.size());
        std::memcpy(static_sn.data(), old_cfg->static_sn, static_sn.size());
        std::memcpy(static_gw.data(), old_cfg->static_gw, static_gw.size());
        migrated = true;
        break;
      }
      if (config->version == 2) {
        const PersistedConfigV2 *old_cfg = reinterpret_cast<const PersistedConfigV2 *>(config);
        printf("Migration von Persistenz-Layout v2 -> v%lu (Slot 0x%08lx).\n",
               static_cast<unsigned long>(cfg::PERSIST_VERSION), static_cast<unsigned long>(offset));
        for (uint8_t i = 0; i < cfg::RELAY_COUNT; ++i) {
          relay_states[i] = old_cfg->relay_states[i] != 0;
          if (old_cfg->relay_names[i][0]) relay_names[i] = old_cfg->relay_names[i];
        }
        public_access = old_cfg->public_access != 0;
        if (old_cfg->site_title[0]) site_title = old_cfg->site_title;
        if (old_cfg->site_subtitle[0]) site_subtitle = old_cfg->site_subtitle;
        users_db.clear();
        uint32_t user_count = std::min<uint32_t>(old_cfg->user_count, cfg::MAX_USERS);
        for (uint32_t i = 0; i < user_count; ++i) {
          const PersistedUser &stored = old_cfg->users[i];
          if (stored.name[0] && stored.hash[0])
            users_db[normalize_username(stored.name)] = {stored.hash, stored.role[0] ? stored.role : "user"};
        }
        api_keys_db.clear();
        uint32_t key_count = std::min<uint32_t>(old_cfg->api_key_count, cfg::MAX_API_KEYS);
        for (uint32_t i = 0; i < key_count; ++i) {
          if (old_cfg->api_keys[i].key[0]) api_keys_db.push_back({old_cfg->api_keys[i].key, old_cfg->api_keys[i].comment});
        }
        migrated = true;
        break;
      }
      if (config->version == 1) {
        const PersistedConfigV1 *old_cfg = reinterpret_cast<const PersistedConfigV1 *>(config);
        printf("Migration von Persistenz-Layout v1 -> v%lu (Slot 0x%08lx).\n",
               static_cast<unsigned long>(cfg::PERSIST_VERSION), static_cast<unsigned long>(offset));
        for (uint8_t i = 0; i < cfg::RELAY_COUNT; ++i) {
          relay_states[i] = old_cfg->relay_states[i] != 0;
          if (old_cfg->relay_names[i][0]) relay_names[i] = old_cfg->relay_names[i];
        }
        public_access = old_cfg->public_access != 0;
        if (old_cfg->site_title[0]) site_title = old_cfg->site_title;
        if (old_cfg->site_subtitle[0]) site_subtitle = old_cfg->site_subtitle;
        users_db.clear();
        uint32_t user_count = std::min<uint32_t>(old_cfg->user_count, cfg::MAX_USERS);
        for (uint32_t i = 0; i < user_count; ++i) {
          const PersistedUser &stored = old_cfg->users[i];
          if (stored.name[0] && stored.hash[0])
            users_db[normalize_username(stored.name)] = {stored.hash, stored.role[0] ? stored.role : "user"};
        }
        api_keys_db.clear();
        uint32_t key_count = std::min<uint32_t>(old_cfg->api_key_count, cfg::MAX_API_KEYS);
        for (uint32_t i = 0; i < key_count; ++i) {
          if (old_cfg->api_keys[i][0]) api_keys_db.push_back({old_cfg->api_keys[i], ""});
        }
        migrated = true;
        break;
      }
      saw_invalid = true;
      continue;
    }
    printf("Konfiguration aus Persistenz-Slot 0x%08lx geladen.\n", static_cast<unsigned long>(offset));
    for (uint8_t i = 0; i < cfg::RELAY_COUNT; ++i) {
      relay_states[i] = config->relay_states[i] != 0;
      if (config->relay_names[i][0]) relay_names[i] = config->relay_names[i];
      relay_active_low[i] = config->relay_active_low[i] != 0;
      feedback_enabled[i] = config->feedback_enabled[i] != 0;
      feedback_active_low[i] = config->feedback_active_low[i] != 0;
    }
    feedback_timeout_ms = std::clamp<uint32_t>(config->feedback_timeout_ms,
                                                cfg::MIN_INPUT_TIME_MS,
                                                cfg::MAX_INPUT_TIME_MS);
    public_access = config->public_access != 0;
    if (config->site_title[0]) site_title = config->site_title;
    if (config->site_subtitle[0]) site_subtitle = config->site_subtitle;
    users_db.clear();
    uint32_t user_count = std::min<uint32_t>(config->user_count, cfg::MAX_USERS);
    for (uint32_t i = 0; i < user_count; ++i) {
      const PersistedUser &stored = config->users[i];
      if (stored.name[0] && stored.hash[0]) users_db[normalize_username(stored.name)] = {stored.hash, stored.role[0] ? stored.role : "user"};
    }
    api_keys_db.clear();
    uint32_t key_count = std::min<uint32_t>(config->api_key_count, cfg::MAX_API_KEYS);
    for (uint32_t i = 0; i < key_count; ++i) {
      if (config->api_keys[i].key[0]) api_keys_db.push_back({config->api_keys[i].key, config->api_keys[i].comment});
    }
    std::memcpy(static_ip.data(), config->static_ip, static_ip.size());
    std::memcpy(static_sn.data(), config->static_sn, static_sn.size());
    std::memcpy(static_gw.data(), config->static_gw, static_gw.size());
    scene_mode = config->scene_mode != 0;
    for (uint8_t s = 0; s < cfg::SCENE_COUNT; ++s) {
      scenes[s].enabled = config->scene_enabled[s] != 0;
      scenes[s].name = config->scene_names[s];
      for (uint8_t r = 0; r < cfg::RELAY_COUNT; ++r) {
        uint8_t a = config->scene_action[s][r];
        scenes[s].action[r] = (a <= 2) ? a : 2;
      }
    }
    if (config->active_scene >= 1 && config->active_scene <= cfg::SCENE_COUNT &&
        scenes[config->active_scene - 1].enabled) {
      active_scene = static_cast<int>(config->active_scene) - 1;
    } else {
      active_scene = -1;
    }
    return ConfigLoadResult::Loaded;
  }
  if (migrated) {
    save_config();
    return ConfigLoadResult::Loaded;
  }
  return saw_invalid ? ConfigLoadResult::LayoutChanged : ConfigLoadResult::Empty;
}

#ifdef PERSIST_WIPE
// Einmaliger Werksreset: loescht alle Persistenz-Slots, damit load_config()
// danach "Empty" liefert (statt bei einem unbekannten/inkompatiblen Layout den
// persist_write_locked-Schutz zu setzen, der jedes Speichern blockiert). Wird
// nur mit -DPERSIST_WIPE=ON gebaut und laeuft VOR dem core1-Start -> ein
// direktes Erase mit gesperrten Interrupts ist hier sicher (wie im Boot-Pfad
// von save_config()). Danach OHNE das Flag neu bauen/flashen.
static void wipe_persist_slots() {
  uint32_t interrupts = save_and_disable_interrupts();
  for (uintptr_t offset : persist_flash_offsets()) {
    if (!persist_flash_offset_usable(offset)) continue;
    flash_range_erase(offset, FLASH_SECTOR_SIZE);
  }
  restore_interrupts(interrupts);
  printf("PERSIST_WIPE: alle Persistenz-Slots geloescht (Werksreset).\n");
}
#endif

static Session *get_session(const std::string &token) {
  if (token.empty()) return nullptr;
  for (auto it = sessions.begin(); it != sessions.end();) {
    if (expired(it->expires)) {
      it = sessions.erase(it);
      continue;
    }
    if (it->token == token) {
      it->expires = millis32() + cfg::SESSION_LIFETIME_MS;
      return &(*it);
    }
    ++it;
  }
  return nullptr;
}

static Session *session_from_headers(const HttpRequest &req) {
  auto it = req.headers.find("cookie");
  if (it == req.headers.end()) return nullptr;
  return get_session(cookie_value(it->second, "sid"));
}

static std::string create_session(const std::string &username, const std::string &role) {
  if (sessions.size() >= 12) sessions.erase(sessions.begin());
  Session session;
  session.token = random_hex(16);
  session.username = username;
  session.role = role;
  session.expires = millis32() + cfg::SESSION_LIFETIME_MS;
  sessions.push_back(session);
  return session.token;
}

static void delete_session(const std::string &token) {
  sessions.erase(std::remove_if(sessions.begin(), sessions.end(), [&](const Session &s) { return s.token == token; }), sessions.end());
  persistent_admin_tokens.erase(std::remove(persistent_admin_tokens.begin(), persistent_admin_tokens.end(), token), persistent_admin_tokens.end());
}

static void delete_sessions_for_user(const std::string &username) {
  const std::string normalized = normalize_username(username);
  std::vector<std::string> removed_tokens;
  for (const Session &s : sessions) {
    if (normalize_username(s.username) == normalized) removed_tokens.push_back(s.token);
  }
  sessions.erase(std::remove_if(sessions.begin(), sessions.end(), [&](const Session &s) { return normalize_username(s.username) == normalized; }), sessions.end());
  for (const std::string &tok : removed_tokens) {
    persistent_admin_tokens.erase(std::remove(persistent_admin_tokens.begin(), persistent_admin_tokens.end(), tok), persistent_admin_tokens.end());
  }
}

static size_t admin_user_count() {
  size_t count = 0;
  for (const auto &entry : users_db) {
    if (entry.second.role == "admin") ++count;
  }
  return count;
}

static void prune_sessions() {
  sessions.erase(std::remove_if(sessions.begin(), sessions.end(), [](const Session &s) { return expired(s.expires); }), sessions.end());
}

static void prune_guest_visitors() {
  guest_visitors.erase(std::remove_if(guest_visitors.begin(), guest_visitors.end(), [](const GuestVisitor &visitor) { return expired(visitor.expires); }), guest_visitors.end());
}

static void mark_guest_visitor(const std::string &ip) {
  if (ip.empty()) return;
  prune_guest_visitors();
  uint32_t expires = millis32() + cfg::GUEST_LIFETIME_MS;
  for (GuestVisitor &visitor : guest_visitors) {
    if (visitor.ip == ip) {
      visitor.expires = expires;
      return;
    }
  }
  if (guest_visitors.size() >= cfg::MAX_GUEST_VISITORS) guest_visitors.erase(guest_visitors.begin());
  guest_visitors.push_back({ip, expires});
}

static uint8_t relay_gpio_value(uint8_t idx, bool on) {
  if (relay_active_low[idx]) return on ? 0 : 1;
  return on ? 1 : 0;
}

static void apply_relay(uint8_t idx) {
  gpio_put(cfg::RELAY_PINS[idx], relay_gpio_value(idx, relay_states[idx]));
}

static bool feedback_matches(uint8_t idx) {
  const bool input_high = gpio_get(cfg::FEEDBACK_PINS[idx]) != 0;
  const bool feedback_on = feedback_active_low[idx] ? !input_high : input_high;
  return feedback_on == relay_states[idx];
}

static void update_scene_feedback_errors() {
  scene_feedback_error.fill(false);
  for (uint8_t i = 0; i < cfg::RELAY_COUNT; ++i) {
    const int8_t scene = feedback_source_scene[i];
    if (feedback_error[i] && scene >= 0 && scene < cfg::SCENE_COUNT) scene_feedback_error[scene] = true;
  }
}

static void start_feedback_check(uint8_t idx, int8_t source_scene) {
  feedback_source_scene[idx] = source_scene;
  feedback_error[idx] = false;
  if (!feedback_enabled[idx] || feedback_matches(idx)) {
    feedback_pending[idx] = false;
    feedback_source_scene[idx] = -1;
  } else {
    feedback_pending[idx] = true;
    feedback_deadline[idx] = millis32() + feedback_timeout_ms;
  }
  update_scene_feedback_errors();
}

static bool service_relay_feedback() {
  StateLock lock;
  bool changed = false;
  const uint32_t now = millis32();
  for (uint8_t i = 0; i < cfg::RELAY_COUNT; ++i) {
    if (!feedback_enabled[i] || feedback_matches(i)) {
      if (feedback_pending[i] || feedback_error[i]) changed = true;
      feedback_pending[i] = false;
      feedback_error[i] = false;
      feedback_source_scene[i] = -1;
    } else if (feedback_pending[i] && static_cast<int32_t>(now - feedback_deadline[i]) >= 0) {
      feedback_pending[i] = false;
      feedback_error[i] = true;
      changed = true;
    }
  }
  if (changed) update_scene_feedback_errors();
  return changed;
}

// Prueft, ob die aktuellen Relaiszustaende der Szenenvorgabe entsprechen
// ("unveraendert"/Aktion 2 wird ignoriert, da nicht Teil der Szenenkonfig).
static bool scene_state_matches(uint8_t idx) {
  for (uint8_t r = 0; r < cfg::RELAY_COUNT; ++r) {
    const uint8_t a = scenes[idx].action[r];
    if (a == 2) continue;
    if (relay_states[r] != (a == 1)) return false;
  }
  return true;
}

static void set_relay(uint8_t idx, bool on) {
  {
    StateLock lock;
    relay_states[idx] = on;
    apply_relay(idx);
    start_feedback_check(idx, -1);
    if (scene_mode && active_scene >= 0) {
      scene_dirty = !scene_state_matches(active_scene);  // sauber, sobald wieder die Szenenkonfig erreicht ist
    } else {
      active_scene = -1;
    }
  }
  g_persist_dirty = true;  // core1 speichert im naechsten Netz-Loop in den Flash
  esp_link_state_dirty = true;
  g_sse_dirty = true;
}

// Wendet eine (aktivierte) Szene momentan an: setzt Relais gemaess Vorgabe, ueberspringt "unveraendert".
static void activate_scene(uint8_t idx) {
  bool persist = false;
  {
    StateLock lock;
    if (idx >= cfg::SCENE_COUNT || !scenes[idx].enabled) return;
    const bool selection_changed = (active_scene != idx);
    active_scene = idx;
    scene_dirty = false;
    bool changed = false;
    for (uint8_t r = 0; r < cfg::RELAY_COUNT; ++r) {
      const uint8_t a = scenes[idx].action[r];
      if (a == 2) continue;
      const bool on = (a == 1);
      if (relay_states[r] != on) {
        relay_states[r] = on;
        apply_relay(r);
        changed = true;
      }
      start_feedback_check(r, static_cast<int8_t>(idx));
    }
    persist = changed || selection_changed;
  }
  if (persist) g_persist_dirty = true;
  esp_link_state_dirty = true;
  g_sse_dirty = true;
}

#ifdef INPUT_MODE_BUTTON
// Liest die entprellten Taster (GP10-28). Eine steigende Flanke (Druck) schaltet
// den zugehoerigen Kanal um (Toggle) bzw. loest im Szenen-Modus die Szene aus.
// Laeuft auf core0 parallel zu den Web-/ESP-Buttons. Rueckgabe: LED-Zustand geaendert.
static bool service_buttons() {
  StateLock lock;
  const uint32_t now = millis32();
  const uint32_t debounce = feedback_timeout_ms;
  bool led_changed = false;
  for (uint8_t i = 0; i < cfg::RELAY_COUNT; ++i) {
    const bool raw = gpio_get(cfg::FEEDBACK_PINS[i]) == 0;  // Pull-up: gedrueckt = LOW
    if (raw != button_raw[i]) {
      button_raw[i] = raw;
      button_since[i] = now;
    } else if (raw != button_pressed[i] &&
               static_cast<int32_t>(now - button_since[i]) >= static_cast<int32_t>(debounce)) {
      button_pressed[i] = raw;
      led_changed = true;
      if (raw) {  // steigende Flanke: Aktion ausloesen
        if (scene_mode) {
          if (i < cfg::SCENE_COUNT && scenes[i].enabled) activate_scene(i);
        } else {
          set_relay(i, !relay_states[i]);
        }
      }
    }
  }
  return led_changed;
}
#endif

static std::string active_users_array_json() {
  prune_sessions();
  prune_guest_visitors();
  std::string out = "[";
  for (size_t i = 0; i < sessions.size(); ++i) {
    if (i) out += ',';
    uint32_t remaining_ms = expired(sessions[i].expires) ? 0 : sessions[i].expires - millis32();
    uint32_t remaining_min = (remaining_ms + 59999) / 60000;
    out += "{\"username\":\"" + json_escape(sessions[i].username) + "\",\"role\":\"" + json_escape(sessions[i].role) + "\",\"remaining\":" + std::to_string(remaining_min) + "}";
  }
  if (public_access) {
    for (const GuestVisitor &visitor : guest_visitors) {
      if (out.size() > 1) out += ',';
      out += "{\"username\":\"Gast " + json_escape(visitor.ip) + "\",\"role\":\"public\",\"remaining\":1}";
    }
  }
  out += ']';
  return out;
}

static std::string active_users_json(bool include_users) {
  return std::string("{\"active_users\":") + (include_users ? active_users_array_json() : "null") + "}";
}

static std::string state_json(bool include_users) {
  prune_sessions();
  StateLock lock;  // geteilten Steuerzustand konsistent lesen; Kopie wird ohne Lock gesendet
  std::string out = "{\"relays\":[";
  for (size_t i = 0; i < relay_states.size(); ++i) {
    if (i) out += ',';
    out += relay_states[i] ? "true" : "false";
  }
  out += "],\"names\":[";
  for (size_t i = 0; i < relay_names.size(); ++i) {
    if (i) out += ',';
    out += '"' + json_escape(relay_names[i]) + '"';
  }
  out += "],\"title\":\"" + json_escape(site_title) + "\",\"subtitle\":\"" + json_escape(site_subtitle) + "\",\"public\":";
  out += public_access ? "true" : "false";
  out += ",\"scene_mode\":";
  out += scene_mode ? "true" : "false";
  out += ",\"active_scene\":";
  out += std::to_string(active_scene);
  out += ",\"scene_dirty\":";
  out += scene_dirty ? "true" : "false";
  out += ",\"feedback_errors\":[";
  for (size_t i = 0; i < feedback_error.size(); ++i) {
    if (i) out += ',';
    out += feedback_error[i] ? "true" : "false";
  }
  out += "],\"scene_errors\":[";
  for (size_t i = 0; i < scene_feedback_error.size(); ++i) {
    if (i) out += ',';
    out += scene_feedback_error[i] ? "true" : "false";
  }
  out += ']';
#ifdef INPUT_MODE_BUTTON
  out += ",\"buttons\":[";
  for (size_t i = 0; i < button_pressed.size(); ++i) {
    if (i) out += ',';
    out += button_pressed[i] ? "true" : "false";
  }
  out += ']';
#endif
  out += ",\"active_users\":";
  out += (include_users ? active_users_array_json() : "null");
  out += '}';
  return out;
}

static std::string scenes_config_json() {
  StateLock lock;
  std::string out = "[";
  for (uint8_t s = 0; s < cfg::SCENE_COUNT; ++s) {
    if (s) out += ',';
    out += "{\"en\":";
    out += scenes[s].enabled ? "true" : "false";
    out += ",\"name\":\"" + json_escape(scenes[s].name) + "\",\"act\":[";
    for (uint8_t r = 0; r < cfg::RELAY_COUNT; ++r) {
      if (r) out += ',';
      out += std::to_string(scenes[s].action[r]);
    }
    out += "]}";
  }
  out += "]";
  return out;
}

static bool check_api_key(const HttpRequest &req) {
  // Ohne konfigurierte Keys ist Key-Auth kein Zugriffsgrund: false, damit
  // die Entscheidung public_access/Session ueberlassen bleibt (sonst wuerde
  // eine leere Key-DB jeden Request in has_*_access() freigeben).
  if (api_keys_db.empty()) return false;
  std::string key;
  auto it = req.headers.find("x-api-key");
  if (it != req.headers.end()) key = it->second;
  if (key.empty()) key = query_param(req.query, "api_key");
  if (key.empty()) return false;
  return std::any_of(api_keys_db.begin(), api_keys_db.end(), [&](const ApiKeyEntry &e) { return e.key == key; });
}

static bool has_control_access(const HttpRequest &req) {
  return public_access || session_from_headers(req) || check_api_key(req);
}

static bool is_persistent_admin(const HttpRequest &req) {
  auto it = req.headers.find("cookie");
  if (it == req.headers.end()) return false;
  std::string sid = cookie_value(it->second, "sid");
  if (sid.empty()) return false;
  return std::find(persistent_admin_tokens.begin(), persistent_admin_tokens.end(), sid) != persistent_admin_tokens.end();
}

static bool has_relay_access(const HttpRequest &req) {
  // Regel 2: Public -> jeder darf schalten
  if (public_access) return true;
  // Regel 1: Admin darf immer schalten (auch nach Timeout)
  if (is_persistent_admin(req)) return true;
  // Regel 3: angemeldete User (aktive Session)
  if (session_from_headers(req)) return true;
  // API-Key (z.B. fuer externe Skripte)
  if (check_api_key(req)) return true;
  return false;
}

static bool send_all(uint8_t sn, const std::string &data) {
  size_t sent_total = 0;
  while (sent_total < data.size()) {
    uint16_t chunk = static_cast<uint16_t>(std::min<size_t>(data.size() - sent_total, 1400));
    // Zeitlich begrenzt auf freien TX-Puffer warten. Ohne diese Schranke wuerde
    // das WIZnet-send() bei Link-Verlust (TX-Puffer laeuft voll) sekundenlang
    // blockieren und die Hauptschleife (ESP-Link, Relais) aushungern.
    uint32_t start = millis32();
    while (getSn_TX_FSR(sn) < chunk) {
      if (getSn_SR(sn) != SOCK_ESTABLISHED) return false;
      if (millis32() - start > 150) return false;
    }
    int32_t sent = send(sn, reinterpret_cast<uint8_t *>(const_cast<char *>(data.data() + sent_total)), chunk);
    if (sent <= 0) return false;
    sent_total += static_cast<size_t>(sent);
  }
  return true;
}

static void close_socket(uint8_t sn) {
  sse_socket[sn] = false;
  sse_show_users[sn] = false;
  disconnect(sn);
  sleep_ms(1);
  close(sn);
}

// Sofortiges CLOSE ohne FIN-Handshake. disconnect() wuerde bei unerreichbarem
// Peer bis zum TCP-Timeout (Sekunden) blockieren und die Hauptschleife
// aushungern; das Hardware-CLOSE setzt den Socket direkt auf SOCK_CLOSED.
static void force_close_socket(uint8_t sn) {
  sse_socket[sn] = false;
  sse_show_users[sn] = false;
  close(sn);
}

static bool send_sse(uint8_t sn, const std::string &data) {
  if (!sse_socket[sn]) return false;
  if (getSn_SR(sn) != SOCK_ESTABLISHED) {
    force_close_socket(sn);
    return false;
  }
  if (!send_all(sn, data)) {
    force_close_socket(sn);
    return false;
  }
  return true;
}

static void prune_sse_sockets() {
  for (uint8_t sn = 0; sn < cfg::HTTP_SOCKET_COUNT; ++sn) {
    if (!sse_socket[sn]) continue;
    uint8_t status = getSn_SR(sn);
    if (status == SOCK_CLOSE_WAIT || status == SOCK_CLOSED || status == SOCK_FIN_WAIT || status == SOCK_TIME_WAIT || status == SOCK_LAST_ACK) {
      force_close_socket(sn);
    }
  }
}

static size_t active_sse_socket_count() {
  size_t count = 0;
  for (uint8_t sn = 0; sn < cfg::HTTP_SOCKET_COUNT; ++sn) {
    if (sse_socket[sn]) ++count;
  }
  return count;
}

static void send_response(uint8_t sn, const char *status, const char *content_type, const std::string &body, const std::string &extra_headers = "") {
  std::string response = "HTTP/1.1 ";
  response += status;
  response += "\r\nContent-Type: ";
  response += content_type;
  response += "\r\nContent-Length: ";
  response += std::to_string(body.size());
  response += "\r\nConnection: close\r\n";
  response += extra_headers;
  response += "\r\n";
  response += body;
  send_all(sn, response);
  close_socket(sn);
}

static void send_redirect(uint8_t sn, const std::string &location, const std::string &extra_headers = "") {
  std::string response = "HTTP/1.1 302 Found\r\nLocation: " + location + "\r\nContent-Length: 0\r\nConnection: close\r\n" + extra_headers + "\r\n";
  send_all(sn, response);
  close_socket(sn);
}

static bool parse_request(const std::string &raw, HttpRequest &req) {
  size_t first_line_end = raw.find("\r\n");
  if (first_line_end == std::string::npos) return false;
  std::string line = raw.substr(0, first_line_end);
  size_t first = line.find(' ');
  size_t second = first == std::string::npos ? std::string::npos : line.find(' ', first + 1);
  if (first == std::string::npos || second == std::string::npos) return false;
  req.method = line.substr(0, first);
  std::string full_path = line.substr(first + 1, second - first - 1);
  size_t q = full_path.find('?');
  req.path = q == std::string::npos ? full_path : full_path.substr(0, q);
  req.query = q == std::string::npos ? "" : full_path.substr(q + 1);

  size_t header_end = raw.find("\r\n\r\n");
  if (header_end == std::string::npos) return false;
  size_t pos = first_line_end + 2;
  while (pos < header_end) {
    size_t next = raw.find("\r\n", pos);
    if (next == std::string::npos || next > header_end) break;
    std::string header = raw.substr(pos, next - pos);
    size_t colon = header.find(':');
    if (colon != std::string::npos) req.headers[lower(trim(header.substr(0, colon)))] = trim(header.substr(colon + 1));
    pos = next + 2;
  }
  req.body = raw.substr(header_end + 4);
  return true;
}

static std::string socket_remote_ip(uint8_t sn) {
  uint8_t ip[4] = {};
  if (getsockopt(sn, SO_DESTIP, ip) != SOCK_OK) return "";
  return format_ipv4(ip);
}

static std::string build_login_html(const std::string &next, bool error, const Session *session, const std::string &prefill_username = "", const std::string &error_message = "");
static std::string build_password_html(const Session *session, const std::string &message, bool error);
static std::string page_header_css();
static std::string page_header_html(const Session *session, bool show_conn);
static std::string build_index_html(const Session *session, bool can_control);

static std::string build_index_html() {
  return R"HTML(<!DOCTYPE html><html lang="de"><head><meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Relais-Steuerung</title><style>*{box-sizing:border-box;margin:0;padding:0}body{font-family:'Segoe UI',system-ui,sans-serif;background:#0d1117;color:#c9d1d9;min-height:100vh;display:flex;flex-direction:column;align-items:center;padding:20px 16px 40px}.topbar{position:fixed;top:0;left:0;right:0;min-height:48px;background:#161b22;border-bottom:1px solid #30363d;display:flex;align-items:center;padding:6px 16px;gap:12px;z-index:100;flex-wrap:wrap}.topbar-title{font-size:1rem;font-weight:700;color:#58a6ff;flex:1;min-width:180px}.btn-cfg{padding:5px 12px;border:1px solid #30363d;background:#21262d;color:#8b949e;font-size:.78rem;font-weight:600;cursor:pointer}#active-users{font-size:.72rem;color:#c9d1d9;border:1px solid #30363d;background:#0d1117;padding:4px 10px;max-width:320px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}#conn{font-size:.72rem;font-weight:600;padding:4px 10px;border:1px solid #30363d;background:#161b22;color:#8b949e}#conn.ok{border-color:#238636;color:#3fb950}#conn.err{border-color:#da3633;color:#f85149}h1{margin:76px 0 24px;font-size:1.4rem;color:#e6edf3;font-weight:600;text-align:center}.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(220px,1fr));gap:12px;width:100%;max-width:800px}.card{background:#161b22;border:1px solid #30363d;padding:10px;display:flex;align-items:center}.card.on{border-color:#238636}.relay-row{display:flex;align-items:center;gap:10px;width:100%}.label{flex:1;font-size:.9rem;font-weight:700;color:#8b949e;text-transform:uppercase;text-align:left}.relay-num{flex:none;font-size:.65rem;color:#484f58;min-width:24px}.btn{flex:none;min-width:68px;height:34px;padding:0 12px;border:1px solid #30363d;cursor:pointer;font-size:.9rem;font-weight:700;background:#21262d;color:#8b949e}.btn.on{background:#1a4731;border-color:#238636;color:#3fb950}</style></head><body><div class="topbar"><span class="topbar-title" id="sitetitle">Relais-Steuerung</span><button class="btn-cfg" onclick="location.href='/config'">Konfig</button><span id="active-users">Benutzer: -</span><span id="conn">...</span></div><h1 id="subtitle">Relais-&Uuml;bersicht</h1><div class="grid" id="grid"></div><script>const grid=document.getElementById('grid');const conn=document.getElementById('conn');const active=document.getElementById('active-users');let names=Array.from({length:8},(_,i)=>'Relais '+(i+1));for(let i=0;i<8;i++){const card=document.createElement('div');card.className='card';card.id='c'+i;card.innerHTML=`<div class="relay-row"><span class="relay-num">#${i+1}</span><span class="label" id="l${i}">${names[i]}</span><button class="btn" id="b${i}" onclick="toggle(${i})">OFF</button></div>`;grid.appendChild(card)}function renderActiveUsers(users){if(!users||!users.length){active.textContent='Benutzer: -';active.title='Keine aktiven Anmeldungen';return}const text=users.map(u=>u.username+' ('+u.remaining+' min)').join(', ');active.textContent='Benutzer: '+text;active.title=text}function applyState(states,ns,users){if(ns)names=ns;if(states)states.forEach((on,i)=>{document.getElementById('l'+i).textContent=names[i];const b=document.getElementById('b'+i);const c=document.getElementById('c'+i);b.className='btn '+(on?'on':'');b.textContent=on?'ON':'OFF';c.className='card'+(on?' on':'')});renderActiveUsers(users)}function toggle(i){fetch('/relay/'+i+'/toggle',{method:'POST',credentials:'same-origin'}).then(r=>{if(r.status===401){location.href='/login?next=/';throw new Error('unauthorized')}if(!r.ok)throw new Error('HTTP '+r.status);return r.json()}).then(d=>applyState(d.relays,d.names,d.active_users)).catch(()=>{conn.textContent='Fehler';conn.className='err'})}let lastSseActivity=0;let lastConnectAttempt=0;let es=null;let reconnectTimer=0;function markConn(ok){conn.textContent=ok?'Verbunden':'Getrennt';conn.className=ok?'ok':'err'}function noteSseActivity(){lastSseActivity=Date.now();markConn(true)}function scheduleReconnect(delay=1000){if(reconnectTimer)return;reconnectTimer=setTimeout(()=>{reconnectTimer=0;connectEvents()},delay)}function forceReconnect(){if(es){es.close();es=null}scheduleReconnect(0)}function connectEvents(){if(es)es.close();lastConnectAttempt=Date.now();es=new EventSource('/events');es.onopen=()=>{noteSseActivity()};es.onerror=()=>{markConn(false);if(es){es.close();es=null}scheduleReconnect()};es.addEventListener('ping',()=>{noteSseActivity()});es.onmessage=e=>{noteSseActivity();try{const d=JSON.parse(e.data);applyState(d.relays,d.names,d.active_users);if(d.title){document.getElementById('sitetitle').textContent=d.title;document.title=d.title}if(d.subtitle)document.getElementById('subtitle').innerHTML=d.subtitle}catch(x){}}}connectEvents();window.addEventListener('pagehide',()=>{if(es)es.close()});setInterval(()=>{const now=Date.now();const activeLink=lastSseActivity&&now-lastSseActivity<2500;markConn(!!activeLink);if(!activeLink&&now-lastConnectAttempt>=2500&&!reconnectTimer)forceReconnect()},1000);</script></body></html>)HTML";
}

// Gemeinsames CSS/Markup fuer die "Auge"-Umschaltung der Passwortfelder.
static const char *pw_eye_css() {
  return ".pw-wrap{position:relative;display:block}.pw-wrap>input{width:100%!important;margin-top:0!important;padding-right:42px!important;box-sizing:border-box}.pw-eye{position:absolute!important;top:0;right:0;width:40px!important;height:100%;display:flex;align-items:center;justify-content:center;background:none!important;border:0!important;color:#9e9e9e!important;cursor:pointer;padding:0!important;margin:0!important;font-size:1.05rem;line-height:1;box-shadow:none!important}.pw-eye:hover{color:#4dabf7!important}.pw-eye.on{text-decoration:line-through}";
}
static std::string pw_eye_btn() {
  return "<button type=\"button\" class=\"pw-eye\" tabindex=\"-1\" aria-label=\"Passwort anzeigen\" onclick=\"var i=this.parentNode.querySelector('input');var s=i.type=='password';i.type=s?'text':'password';this.classList.toggle('on',s)\">\xF0\x9F\x91\x81</button>";
}

static std::string build_login_html(const std::string &next, bool error, const Session *session, const std::string &prefill_username, const std::string &error_message) {
  std::string html = "<!DOCTYPE html><html lang=\"de\"><head><meta charset=\"UTF-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><title>" + html_escape(site_title) + " - Anmelden</title><style>html,body{margin:0}body{box-sizing:border-box;font-family:'Segoe UI',sans-serif;background:#121212;color:#e0e0e0;min-height:100vh;padding:108px 24px 24px;display:flex;flex-direction:column;align-items:center;justify-content:flex-start}" + page_header_css() + ".card{background:#1e1e1e;padding:32px 28px;width:100%;max-width:360px;border:1px solid #2d2d2d}h1{font-size:1.3rem;color:#4dabf7;text-align:center}label{font-size:.8rem;font-weight:600;color:#9e9e9e;display:block;margin-top:12px}.card input,.card button{width:100%;padding:10px;margin-top:4px;box-sizing:border-box}.card input{background:#2a2a2a;color:#e0e0e0;border:1px solid #333}.card button{background:#1a3a5c;color:#4dabf7;border:1px solid #1e5a9e;font-weight:700}.actions{display:flex;gap:8px;margin-top:20px}.actions button{margin-top:0}.btn-secondary{background:#2a2a2a;color:#9e9e9e;border-color:#444}.err{color:#ff6b6b;text-align:center}" + std::string(pw_eye_css()) + "</style></head><body>" + page_header_html(session, true) + "<div class=\"card\"><h1>Admin-Anmeldung</h1>";
  if (error) html += "<p class=\"err\">" + (error_message.empty() ? std::string("Falscher Benutzername oder Passwort") : html_escape(error_message)) + "</p>";
  html += "<form method=\"POST\" action=\"/login\"><input type=\"hidden\" name=\"next\" value=\"" + html_escape(next) + "\"><label>Benutzername</label><input name=\"username\" value=\"" + html_escape(prefill_username) + "\"" + (error ? "" : " autofocus") + "><label>Passwort</label><span class=\"pw-wrap\"><input type=\"password\" name=\"password\" id=\"pw\"" + (error ? " autofocus" : "") + ">" + pw_eye_btn() + "</span><div class=\"actions\"><button type=\"submit\">Anmelden</button><button type=\"button\" class=\"btn-secondary\" onclick=\"location.href='/'\">Abbruch</button></div></form></div><script>const conn=document.getElementById('conn');let lastSseActivity=0;let lastConnectAttempt=0;let es=null;let reconnectTimer=0;function markConn(ok){conn.textContent=ok?'Verbunden':'Getrennt';conn.className=ok?'ok':'err'}function noteSseActivity(){lastSseActivity=Date.now();markConn(true)}function scheduleReconnect(delay=1000){if(reconnectTimer)return;reconnectTimer=setTimeout(()=>{reconnectTimer=0;connectEvents()},delay)}function forceReconnect(){if(es){es.close();es=null}scheduleReconnect(0)}function connectEvents(){if(es)es.close();lastConnectAttempt=Date.now();es=new EventSource('/events');es.onopen=()=>{noteSseActivity()};es.onerror=()=>{markConn(false);if(es){es.close();es=null}scheduleReconnect()};es.addEventListener('ping',()=>{noteSseActivity()});es.onmessage=()=>{noteSseActivity()}}connectEvents();window.addEventListener('pagehide',()=>{if(es)es.close()});setInterval(()=>{const now=Date.now();const activeLink=lastSseActivity&&now-lastSseActivity<2500;markConn(!!activeLink);if(!activeLink&&now-lastConnectAttempt>=2500&&!reconnectTimer)forceReconnect()},1000);</script></body></html>";
  return html;
}

static std::string build_password_html(const Session *session, const std::string &message, bool error) {
  std::string html = "<!DOCTYPE html><html lang=\"de\"><head><meta charset=\"UTF-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><title>" + html_escape(site_title) + " - Passwort &auml;ndern</title><style>html,body{margin:0}body{box-sizing:border-box;font-family:'Segoe UI',sans-serif;background:#121212;color:#e0e0e0;min-height:100vh;padding:108px 24px 24px;display:flex;flex-direction:column;align-items:center;justify-content:flex-start}" + page_header_css() + ".card{background:#1e1e1e;padding:32px 28px;width:100%;max-width:360px;border:1px solid #2d2d2d}h1{font-size:1.3rem;color:#4dabf7;text-align:center}label{font-size:.8rem;font-weight:600;color:#9e9e9e;display:block;margin-top:12px}.card input,.card button{width:100%;padding:10px;margin-top:4px;box-sizing:border-box}.card input{background:#2a2a2a;color:#e0e0e0;border:1px solid #333}.card button{background:#1a3a5c;color:#4dabf7;border:1px solid #1e5a9e;font-weight:700}.actions{display:flex;gap:8px;margin-top:20px}.actions button{margin-top:0}.btn-secondary{background:#2a2a2a;color:#9e9e9e;border-color:#444}.msg{text-align:center;margin-top:8px}.err{color:#ff6b6b}.ok{color:#51cf66}" + std::string(pw_eye_css()) + "</style></head><body>" + page_header_html(session, true) + "<div class=\"card\"><h1>Passwort &auml;ndern</h1><p style=\"text-align:center;color:#9e9e9e;font-size:.85rem\">Benutzer: " + html_escape(session ? session->username : "") + "</p>";
  if (!message.empty()) html += "<p class=\"msg " + std::string(error ? "err" : "ok") + "\">" + html_escape(message) + "</p>";
  html += "<form method=\"POST\" action=\"/password\"><label>Aktuelles Passwort</label><span class=\"pw-wrap\"><input type=\"password\" name=\"old\" autofocus>" + pw_eye_btn() + "</span><label>Neues Passwort</label><span class=\"pw-wrap\"><input type=\"password\" name=\"new1\">" + pw_eye_btn() + "</span><label>Neues Passwort (wiederholen)</label><span class=\"pw-wrap\"><input type=\"password\" name=\"new2\">" + pw_eye_btn() + "</span><div class=\"actions\"><button type=\"submit\">Speichern</button><button type=\"button\" class=\"btn-secondary\" onclick=\"if(history.length>1)history.back();else location.href='/'\">Abbruch</button></div></form></div><script>const conn=document.getElementById('conn');let lastSseActivity=0;let lastConnectAttempt=0;let es=null;let reconnectTimer=0;function markConn(ok){conn.textContent=ok?'Verbunden':'Getrennt';conn.className=ok?'ok':'err'}function noteSseActivity(){lastSseActivity=Date.now();markConn(true)}function scheduleReconnect(delay=1000){if(reconnectTimer)return;reconnectTimer=setTimeout(()=>{reconnectTimer=0;connectEvents()},delay)}function forceReconnect(){if(es){es.close();es=null}scheduleReconnect(0)}function connectEvents(){if(es)es.close();lastConnectAttempt=Date.now();es=new EventSource('/events');es.onopen=()=>{noteSseActivity()};es.onerror=()=>{markConn(false);if(es){es.close();es=null}scheduleReconnect()};es.addEventListener('ping',()=>{noteSseActivity()});es.onmessage=()=>{noteSseActivity()}}connectEvents();window.addEventListener('pagehide',()=>{if(es)es.close()});setInterval(()=>{const now=Date.now();const activeLink=lastSseActivity&&now-lastSseActivity<2500;markConn(!!activeLink);if(!activeLink&&now-lastConnectAttempt>=2500&&!reconnectTimer)forceReconnect()},1000);</script></body></html>";
  return html;
}

static std::string build_config_html(const Session *session) {
#ifdef INPUT_MODE_BUTTON
  const char *time_label = "Taster-Entprellzeit";
  const char *led_css = ".tinfo{color:#9e9e9e;white-space:nowrap}.tled{width:14px;height:14px;border-radius:50%;background:#444;border:1px solid #222;flex:none}.tled.on{background:#51cf66;box-shadow:0 0 6px #51cf66}";
  const char *led_js = "function refreshLeds(){fetch('/state').then(r=>r.json()).then(d=>{if(!d.buttons)return;document.querySelectorAll('.tled').forEach(e=>{e.classList.toggle('on',!!d.buttons[+e.dataset.led])})}).catch(()=>{})}setInterval(refreshLeds,400);refreshLeds();";
#else
  const char *time_label = "Rueckmeldezeit";
  const char *led_css = "";
  const char *led_js = "";
#endif
  const std::string time_min = std::to_string(cfg::MIN_INPUT_TIME_MS);
  const std::string time_max = std::to_string(cfg::MAX_INPUT_TIME_MS);
  const std::string time_def = std::to_string(cfg::DEFAULT_INPUT_TIME_MS);
  std::string rows;
  for (uint8_t i = 0; i < cfg::RELAY_COUNT; ++i) {
    const std::string index = std::to_string(i);
#ifdef INPUT_MODE_BUTTON
    rows += "<div class=\"row relay-config\"><label>Ausgang " + std::to_string(i + 1) + " (GP" + std::to_string(cfg::RELAY_PINS[i]) + ")</label><input maxlength=\"32\" value=\"" + html_escape(relay_names[i]) + "\" data-idx=\"" + index + "\"><label class=\"checklbl\" title=\"Relaisausgang LOW-aktiv\"><input type=\"checkbox\" data-low=\"" + index + "\"" + std::string(relay_active_low[i] ? " checked" : "") + "> Low aktiv</label><span class=\"tinfo\">Taster GP" + std::to_string(cfg::FEEDBACK_PINS[i]) + "</span><span class=\"tled\" data-led=\"" + index + "\" title=\"Tasterzustand\"></span></div>";
#else
    rows += "<div class=\"row relay-config\"><label>Ausgang " + std::to_string(i + 1) + " (GP" + std::to_string(cfg::RELAY_PINS[i]) + ")</label><input maxlength=\"32\" value=\"" + html_escape(relay_names[i]) + "\" data-idx=\"" + index + "\"><label class=\"checklbl\" title=\"Relaisausgang LOW-aktiv\"><input type=\"checkbox\" data-low=\"" + index + "\"" + std::string(relay_active_low[i] ? " checked" : "") + "> Low aktiv</label><label class=\"checklbl\" title=\"Physische Rueckmeldung ueber GP" + std::to_string(cfg::FEEDBACK_PINS[i]) + " pruefen\"><input type=\"checkbox\" data-fb=\"" + index + "\"" + std::string(feedback_enabled[i] ? " checked" : "") + "> Rueckmeldung GP" + std::to_string(cfg::FEEDBACK_PINS[i]) + "</label><label class=\"checklbl\" title=\"Rueckmeldepegel fuer EIN ist LOW\"><input type=\"checkbox\" data-fblow=\"" + index + "\"" + std::string(feedback_active_low[i] ? " checked" : "") + "> Rueckm. LOW</label></div>";
#endif
  }
  std::string html = "<!DOCTYPE html><html lang=\"de\"><head><meta charset=\"UTF-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><title>" + html_escape(site_title) + " - Konfiguration</title><style>html,body{margin:0}body{box-sizing:border-box;font-family:'Segoe UI',sans-serif;background:#121212;color:#e0e0e0;min-height:100vh;padding:108px 24px 24px}" + page_header_css() + "h1{color:#4dabf7}.card{background:#1e1e1e;padding:24px;max-width:980px;border:1px solid #2d2d2d}.row{display:flex;gap:12px;margin-bottom:12px;align-items:center}.row>label:first-child{width:160px;color:#9e9e9e;white-space:nowrap}.row>input{flex:1;min-width:180px;background:#2a2a2a;color:#e0e0e0;border:1px solid #333;padding:8px}.row .checklbl{flex:none;width:auto;display:flex;align-items:center;gap:4px;color:#9e9e9e;white-space:nowrap}.row .checklbl input{width:auto;margin:0}.relay-config{flex-wrap:wrap;border-top:1px solid #2d2d2d;padding-top:10px}.relay-config>input{flex:0 1 auto;min-width:90px;max-width:50%}.number-input{max-width:160px}" + std::string(led_css) + ".actions button{margin:0}button{padding:10px 14px;margin:4px;background:#1a3a5c;color:#4dabf7;border:1px solid #1e5a9e;font-weight:700}.save{background:#1b4332;color:#51cf66;border-color:#2d6a4f}.danger{background:#3d1515;color:#ff6b6b;border-color:#7a2020}.ok{color:#51cf66}.err{color:#ff6b6b}</style></head><body>" + page_header_html(session, true) + "<h1>Konfiguration</h1><div class=\"actions\"><button onclick=\"location.href='/'\">Zurueck</button><button class=\"save\" onclick=\"save()\">Speichern</button><button onclick=\"location.href='/admin'\">Benutzer/API</button><button onclick=\"location.href='/network'\">Network</button><button onclick=\"location.href='/scenes'\">Szenen</button><button class=\"danger\" onclick=\"location.href='/logout'\">Abmelden</button></div><div id=\"msg\"></div><div class=\"card\"><div class=\"row\"><label>Titel</label><input id=\"title\" maxlength=\"64\" value=\"" + html_escape(site_title) + "\"></div><div class=\"row\"><label>Ueberschrift</label><input id=\"subtitle\" maxlength=\"64\" value=\"" + html_escape(site_subtitle) + "\"></div><div class=\"row\"><label>Oeffentlich</label><input type=\"checkbox\" id=\"pub\" style=\"flex:none;width:auto;padding:0;margin:0;border:none;background:none\" " + std::string(public_access ? "checked" : "") + "></div><div class=\"row\"><label>" + std::string(time_label) + "</label><input class=\"number-input\" type=\"number\" id=\"fbtimeout\" min=\"" + time_min + "\" max=\"" + time_max + "\" value=\"" + std::to_string(feedback_timeout_ms) + "\"><span>ms</span></div>" + rows + "</div><script>const conn=document.getElementById('conn');const msg=document.getElementById('msg');let lastSseActivity=0;let lastConnectAttempt=0;let es=null;let reconnectTimer=0;function markConn(ok){conn.textContent=ok?'Verbunden':'Getrennt';conn.className=ok?'ok':'err'}function noteSseActivity(){lastSseActivity=Date.now();markConn(true)}function scheduleReconnect(delay=1000){if(reconnectTimer)return;reconnectTimer=setTimeout(()=>{reconnectTimer=0;connectEvents()},delay)}function forceReconnect(){if(es){es.close();es=null}scheduleReconnect(0)}function connectEvents(){if(es)es.close();lastConnectAttempt=Date.now();es=new EventSource('/events');es.onopen=()=>{noteSseActivity()};es.onerror=()=>{markConn(false);if(es){es.close();es=null}scheduleReconnect()};es.addEventListener('ping',()=>{noteSseActivity()});es.onmessage=()=>{noteSseActivity()}}connectEvents();window.addEventListener('pagehide',()=>{if(es)es.close()});setInterval(()=>{const now=Date.now();const activeLink=lastSseActivity&&now-lastSseActivity<2500;markConn(!!activeLink);if(!activeLink&&now-lastConnectAttempt>=2500&&!reconnectTimer)forceReconnect()},1000);function save(){const names=[...document.querySelectorAll('input[data-idx]')].map((e,i)=>e.value.trim()||('Relais '+(i+1)));const bools=a=>[...document.querySelectorAll(a)].map(e=>e.checked);const timeout=Math.max(" + time_min + ",Math.min(" + time_max + ",Number(document.getElementById('fbtimeout').value)||" + time_def + "));fetch('/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({names,act_low:bools('input[data-low]'),feedback_enabled:bools('input[data-fb]'),feedback_low:bools('input[data-fblow]'),feedback_timeout:timeout,title:document.getElementById('title').value,subtitle:document.getElementById('subtitle').value,public:document.getElementById('pub').checked})}).then(r=>r.json()).then(()=>{msg.textContent='OK gespeichert';msg.className='ok';const nt=document.getElementById('title').value.trim();if(nt){const st=document.getElementById('sitetitle');if(st)st.textContent=nt;document.title=nt+' - Konfiguration'}}).catch(()=>{msg.textContent='Fehler';msg.className='err'})}" + std::string(led_js) + "</script></body></html>";
  return html;
}

static std::string build_network_html(const Session *session) {
  const wiz_NetInfo net_info = current_net_info();
  std::string html = "<!DOCTYPE html><html lang=\"de\"><head><meta charset=\"UTF-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><title>" + html_escape(site_title) + " - Network</title><style>html,body{margin:0}body{box-sizing:border-box;font-family:'Segoe UI',sans-serif;background:#121212;color:#e0e0e0;min-height:100vh;padding:108px 24px 24px}" + page_header_css() + "h1{color:#4dabf7}.card{background:#1e1e1e;padding:24px;max-width:560px;border:1px solid #2d2d2d}.card+.card{margin-top:16px}.net-row,.form-row{display:flex;gap:12px;margin-bottom:10px;align-items:center}.net-label,.form-row label{width:120px;color:#9e9e9e;white-space:nowrap}.net-value{font-family:monospace;color:#e6edf3;word-break:break-word}.form-row input{flex:1;background:#2a2a2a;color:#e0e0e0;border:1px solid #333;padding:8px;font-family:monospace}.actions{display:flex;flex-wrap:wrap;gap:8px;margin:0 0 16px}.actions button{margin:0}button{padding:10px 14px;background:#1a3a5c;color:#4dabf7;border:1px solid #1e5a9e;font-weight:700}.save{background:#1b4332;color:#51cf66;border-color:#2d6a4f}.ok{color:#51cf66}.err{color:#ff6b6b}</style></head><body>" + page_header_html(session, true) + "<h1>Network</h1><div class=\"actions\"><button onclick=\"location.href='/config'\">Zurueck</button><button class=\"save\" onclick=\"saveNetwork()\">Speichern</button></div><div id=\"msg\"></div><div class=\"card\"><div class=\"net-row\"><div class=\"net-label\">Mode</div><div class=\"net-value\">" + network_mode_text(net_info) + "</div></div><div class=\"net-row\"><div class=\"net-label\">MAC</div><div class=\"net-value\">" + format_mac(net_info.mac) + "</div></div><div class=\"net-row\"><div class=\"net-label\">IP</div><div class=\"net-value\">" + format_ipv4(net_info.ip) + "</div></div><div class=\"net-row\"><div class=\"net-label\">Subnet Mask</div><div class=\"net-value\">" + format_ipv4(net_info.sn) + "</div></div><div class=\"net-row\"><div class=\"net-label\">Gateway</div><div class=\"net-value\">" + format_ipv4(net_info.gw) + "</div></div><div class=\"net-row\"><div class=\"net-label\">DNS</div><div class=\"net-value\">" + format_ipv4(net_info.dns) + "</div></div></div><div class=\"card\"><h2 style=\"color:#4dabf7;margin:0 0 16px\">Static</h2><div class=\"form-row\"><label>IP</label><input id=\"static-ip\" value=\"" + format_ipv4(static_ip.data()) + "\" inputmode=\"numeric\"></div><div class=\"form-row\"><label>Subnet Mask</label><input id=\"static-sn\" value=\"" + format_ipv4(static_sn.data()) + "\" inputmode=\"numeric\"></div><div class=\"form-row\"><label>Gateway</label><input id=\"static-gw\" value=\"" + format_ipv4(static_gw.data()) + "\" inputmode=\"numeric\"></div></div><script>const conn=document.getElementById('conn');const msg=document.getElementById('msg');let lastSseActivity=0;let lastConnectAttempt=0;let es=null;let reconnectTimer=0;function markConn(ok){conn.textContent=ok?'Verbunden':'Getrennt';conn.className=ok?'ok':'err'}function noteSseActivity(){lastSseActivity=Date.now();markConn(true)}function scheduleReconnect(delay=1000){if(reconnectTimer)return;reconnectTimer=setTimeout(()=>{reconnectTimer=0;connectEvents()},delay)}function forceReconnect(){if(es){es.close();es=null}scheduleReconnect(0)}function connectEvents(){if(es)es.close();lastConnectAttempt=Date.now();es=new EventSource('/events');es.onopen=()=>{noteSseActivity()};es.onerror=()=>{markConn(false);if(es){es.close();es=null}scheduleReconnect()};es.addEventListener('ping',()=>{noteSseActivity()});es.onmessage=()=>{noteSseActivity()}}connectEvents();window.addEventListener('pagehide',()=>{if(es)es.close()});setInterval(()=>{const now=Date.now();const activeLink=lastSseActivity&&now-lastSseActivity<2500;markConn(!!activeLink);if(!activeLink&&now-lastConnectAttempt>=2500&&!reconnectTimer)forceReconnect()},1000);function saveNetwork(){fetch('/network',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({ip:document.getElementById('static-ip').value,subnet:document.getElementById('static-sn').value,gateway:document.getElementById('static-gw').value})}).then(r=>r.json().then(d=>({ok:r.ok,d}))).then(x=>{if(!x.ok||!x.d.ok)throw new Error(x.d.error||'Fehler');msg.textContent='Gespeichert';msg.className='ok'}).catch(e=>{msg.textContent=e.message||'Fehler';msg.className='err'})}</script></body></html>";
  return html;
}

static std::string build_admin_html(const Session *current_session) {
  std::string rows;
  for (const auto &entry : users_db) {
    const bool is_admin = entry.second.role == "admin";
    const std::string name_arg = js_string_arg(entry.first);
    rows += "<tr><td>" + html_escape(entry.first) + "</td><td><select class=\"role\"><option value=\"user\"" + std::string(is_admin ? "" : " selected") + ">user</option><option value=\"admin\"" + std::string(is_admin ? " selected" : "") + ">admin</option></select></td><td><button class=\"user-action\" onclick=\"setRole(" + name_arg + ",this)\">Rolle speichern</button><button class=\"user-action\" onclick=\"setPwd(" + name_arg + ")\">Passwort</button><button class=\"user-action\" onclick=\"delUser(" + name_arg + ")\">Loeschen</button></td></tr>";
  }

  prune_sessions();
  std::string session_rows;
  for (const Session &session : sessions) {
    uint32_t remaining_ms = expired(session.expires) ? 0 : session.expires - millis32();
    uint32_t remaining_min = (remaining_ms + 59999) / 60000;
    session_rows += "<tr><td>" + html_escape(session.username) + "</td><td>" + html_escape(session.role) + "</td><td>" + std::to_string(remaining_min) + " min</td></tr>";
  }
  if (session_rows.empty()) session_rows = "<tr><td colspan=\"3\">Keine aktiven Anmeldungen</td></tr>";

  std::string key_rows;
  for (const ApiKeyEntry &ke : api_keys_db) {
    const std::string key_arg = js_string_arg(ke.key);
    key_rows += "<tr><td><code>" + html_escape(ke.key) + "</code></td><td><input maxlength=\"64\" value=\"" + html_escape(ke.comment) + "\" data-key=\"" + html_escape(ke.key) + "\" placeholder=\"Kommentar\"></td><td><button onclick=\"saveKeyComment(this)\">Speichern</button> <button onclick=\"delKey(" + key_arg + ")\">Loeschen</button></td></tr>";
  }
  if (key_rows.empty()) key_rows = "<tr><td colspan=\"3\">Keine API-Keys vorhanden</td></tr>";

  std::string html = "<!DOCTYPE html><html lang=\"de\"><head><meta charset=\"UTF-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><title>" + html_escape(site_title) + " - Admin</title><style>body{font-family:'Segoe UI',sans-serif;background:#121212;color:#e0e0e0;padding:108px 24px 24px}" + page_header_css() + ".card{background:#1e1e1e;padding:20px;max-width:820px;border:1px solid #2d2d2d;margin-bottom:16px}.actions{display:flex;flex-wrap:wrap;gap:8px;margin:0 0 16px}.actions button{margin:0}table{width:100%;border-collapse:collapse}td,th{border-bottom:1px solid #333;padding:8px;text-align:left;vertical-align:top}h2{font-size:1.05rem;color:#bdbdbd;margin:0 0 12px}input,select,button{padding:8px;margin:4px;background:#2a2a2a;color:#e0e0e0;border:1px solid #444}button{background:#1a3a5c;color:#4dabf7;border-color:#1e5a9e}.role{min-width:92px;font-weight:700}.user-action{min-width:92px}code{font-family:monospace;color:#8bd5ff}#keys-msg{margin-top:8px;color:#9ad77a}.modal-bg{display:none;position:fixed;inset:0;background:rgba(0,0,0,.7);z-index:500;align-items:center;justify-content:center}.modal-bg.show{display:flex}.modal{background:#1e1e1e;border:1px solid #2d2d2d;padding:28px 24px;min-width:300px;max-width:400px;width:100%}.modal h3{font-size:1rem;color:#4dabf7;margin:0 0 12px}.modal p{color:#bdbdbd;margin:0 0 16px;font-size:.9rem}.modal input{width:100%;box-sizing:border-box;background:#2a2a2a;color:#e0e0e0;border:1px solid #444;padding:9px;margin-bottom:12px}.modal-btns{display:flex;gap:8px;justify-content:flex-end}.modal-btns button{margin:0}.btn-ok{background:#1a3a5c;color:#4dabf7;border-color:#1e5a9e}.btn-cancel{background:#2a2a2a;color:#9e9e9e;border-color:#444}.btn-danger{background:#3d1515;color:#ff6b6b;border-color:#7a2020}" + std::string(pw_eye_css()) + "</style></head><body>" + page_header_html(current_session, true) + "<h1>Benutzerverwaltung</h1><div class=\"actions\"><button onclick=\"location.href='/config'\">Zurueck</button></div><div class=\"card\"><h2>Aktive Anmeldungen</h2><table><thead><tr><th>Benutzer</th><th>Rolle</th><th>Restzeit</th></tr></thead><tbody>" + session_rows + "</tbody></table></div><div class=\"card\"><h2>Benutzer</h2><table><thead><tr><th>Name</th><th>Rolle</th><th>Aktion</th></tr></thead><tbody>" + rows + "</tbody></table></div><div class=\"card\"><h2>Neuer Benutzer</h2><input id=\"u\" placeholder=\"Benutzer\"><span class=\"pw-wrap\"><input id=\"p\" type=\"password\" placeholder=\"Passwort\">" + pw_eye_btn() + "</span><select id=\"r\"><option value=\"admin\">admin</option><option value=\"user\">user</option></select><button onclick=\"addUser()\">Hinzufuegen</button></div><div class=\"card\"><h2>API-Keys</h2><div style=\"margin-bottom:12px\"><input id=\"key-comment\" maxlength=\"64\" placeholder=\"Kommentar fuer neuen Key\" style=\"width:260px\"> <button onclick=\"genKey()\">Neuen Key erzeugen</button></div><table><thead><tr><th>Key</th><th>Kommentar</th><th>Aktion</th></tr></thead><tbody>" + key_rows + "</tbody></table><div id=\"keys-msg\"></div></div>"
    "<div class=\"modal-bg\" id=\"mdl\"><div class=\"modal\"><h3 id=\"mdl-title\"></h3><p id=\"mdl-msg\" style=\"display:none\"></p><span class=\"pw-wrap\" id=\"mdl-input-wrap\" style=\"display:none\"><input id=\"mdl-input\" type=\"text\"><button type=\"button\" class=\"pw-eye\" id=\"mdl-eye\" tabindex=\"-1\" aria-label=\"Passwort anzeigen\" onclick=\"var i=this.parentNode.querySelector('input');var s=i.type=='password';i.type=s?'text':'password';this.classList.toggle('on',s)\">\xF0\x9F\x91\x81</button></span><div class=\"modal-btns\" id=\"mdl-btns\"></div></div></div>"
    "<script>"
    "const conn=document.getElementById('conn');let lastSseActivity=0;let lastConnectAttempt=0;let es=null;let reconnectTimer=0;"
    "function markConn(ok){conn.textContent=ok?'Verbunden':'Getrennt';conn.className=ok?'ok':'err'}"
    "function noteSseActivity(){lastSseActivity=Date.now();markConn(true)}"
    "function scheduleReconnect(delay=1000){if(reconnectTimer)return;reconnectTimer=setTimeout(()=>{reconnectTimer=0;connectEvents()},delay)}"
    "function forceReconnect(){if(es){es.close();es=null}scheduleReconnect(0)}"
    "function connectEvents(){if(es)es.close();lastConnectAttempt=Date.now();es=new EventSource('/events');es.onopen=()=>{noteSseActivity()};es.onerror=()=>{markConn(false);if(es){es.close();es=null}scheduleReconnect()};es.addEventListener('ping',()=>{noteSseActivity()});es.onmessage=()=>{noteSseActivity()}}"
    "connectEvents();window.addEventListener('pagehide',()=>{if(es)es.close()});setInterval(()=>{const now=Date.now();const activeLink=lastSseActivity&&now-lastSseActivity<2500;markConn(!!activeLink);if(!activeLink&&now-lastConnectAttempt>=2500&&!reconnectTimer)forceReconnect()},1000);"
    "function api(o){return fetch('/admin',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(o)}).then(r=>r.json())}"
    "function showModal(title,msg,inputType,buttons){"
      "document.getElementById('mdl-title').textContent=title;"
      "const mp=document.getElementById('mdl-msg');if(msg){mp.textContent=msg;mp.style.display=''}else mp.style.display='none';"
      "const mi=document.getElementById('mdl-input');const mw=document.getElementById('mdl-input-wrap');const me=document.getElementById('mdl-eye');if(inputType){mi.type=inputType;mi.value='';mw.style.display='block';me.style.display=inputType==='password'?'':'none';me.classList.remove('on');setTimeout(()=>mi.focus(),50)}else mw.style.display='none';"
      "const mb=document.getElementById('mdl-btns');mb.innerHTML='';"
      "buttons.forEach(b=>{const btn=document.createElement('button');btn.textContent=b.label;btn.className=b.cls||'btn-ok';btn.onclick=()=>{document.getElementById('mdl').classList.remove('show');b.cb(mi.value)};mb.appendChild(btn)});"
      "document.getElementById('mdl').classList.add('show');"
    "}"
    "function msgModal(title,msg){showModal(title,msg,null,[{label:'OK',cls:'btn-ok',cb:()=>{}}])}"
    "function addUser(){api({action:'add',username:u.value,password:p.value,role:r.value}).then(d=>{if(d.ok)location.reload();else msgModal('Fehler',d.error||'Unbekannter Fehler')})}"
    "function delUser(n){showModal('Benutzer l\\u00f6schen','Benutzer \"'+n+'\" wirklich l\\u00f6schen?',null,["
      "{label:'L\\u00f6schen',cls:'btn-danger',cb:()=>api({action:'delete',username:n}).then(d=>{if(d.ok)location.reload();else msgModal('Fehler',d.error||'Fehler')})},"
      "{label:'Abbrechen',cls:'btn-cancel',cb:()=>{}}"
    "])}"
    "function setPwd(n){showModal('Passwort setzen','Neues Passwort f\\u00fcr \"'+n+'\":','password',[{label:'Speichern',cls:'btn-ok',cb:pw=>{if(!pw)return;api({action:'set_password',username:n,password:pw}).then(d=>{if(!d.ok)msgModal('Fehler',d.error||'Fehler');else location.reload()})}},{label:'Abbrechen',cls:'btn-cancel',cb:()=>{}}])}"
    "function setRole(n,b){const role=b.closest('tr').querySelector('select').value;api({action:'set_role',username:n,role:role}).then(d=>{if(d.ok){msgModal('Rolle ge\\u00e4ndert','Rolle von \"'+n+'\" wurde auf \"'+role+'\" gesetzt.')}else msgModal('Fehler',d.error||'Fehler')})}"  
    "function genKey(){const c=document.getElementById('key-comment').value;api({action:'gen_key',comment:c}).then(d=>{if(d.ok)location.reload();else msgModal('Fehler',d.error||'Fehler')})}"
    "function delKey(k){showModal('API-Key l\\u00f6schen','Key \"'+k+'\" wirklich l\\u00f6schen?',null,["
      "{label:'L\\u00f6schen',cls:'btn-danger',cb:()=>api({action:'delete_key',key:k}).then(d=>{if(d.ok)location.reload();else msgModal('Fehler',d.error||'Fehler')})},"
      "{label:'Abbrechen',cls:'btn-cancel',cb:()=>{}}"
    "])}"
    "function saveKeyComment(btn){const row=btn.closest('tr');const inp=row.querySelector('input[data-key]');api({action:'set_key_comment',key:inp.dataset.key,comment:inp.value}).then(d=>{if(d.ok)document.getElementById('keys-msg').textContent='Gespeichert';else msgModal('Fehler',d.error||'Fehler')})}"
    "</script></body></html>";
  return html;
}

static std::string json_string_value(const std::string &body, const std::string &key) {
  std::string needle = '"' + key + '"';
  size_t pos = body.find(needle);
  if (pos == std::string::npos) return "";
  pos = body.find(':', pos);
  if (pos == std::string::npos) return "";
  pos = body.find('"', pos);
  if (pos == std::string::npos) return "";
  size_t end = body.find('"', pos + 1);
  if (end == std::string::npos) return "";
  return body.substr(pos + 1, end - pos - 1);
}

static bool json_bool_value(const std::string &body, const std::string &key, bool fallback) {
  std::string needle = '"' + key + '"';
  size_t pos = body.find(needle);
  if (pos == std::string::npos) return fallback;
  pos = body.find(':', pos);
  if (pos == std::string::npos) return fallback;
  while (pos + 1 < body.size() && std::isspace(static_cast<unsigned char>(body[pos + 1]))) ++pos;
  if (body.compare(pos + 1, 4, "true") == 0) return true;
  if (body.compare(pos + 1, 5, "false") == 0) return false;
  return fallback;
}

static void update_names_from_json(const std::string &body) {
  size_t pos = body.find("\"names\"");
  if (pos == std::string::npos) return;
  pos = body.find('[', pos);
  if (pos == std::string::npos) return;
  for (uint8_t i = 0; i < cfg::RELAY_COUNT; ++i) {
    pos = body.find('"', pos);
    if (pos == std::string::npos) return;
    size_t end = body.find('"', pos + 1);
    if (end == std::string::npos) return;
    relay_names[i] = body.substr(pos + 1, std::min<size_t>(32, end - pos - 1));
    pos = end + 1;
  }
}

// Liest das "act_low"-Boolean-Array (je Kanal true=LOW-aktiv) aus dem JSON-Body.
static void update_bool_array_from_json(const std::string &body, const char *name,
                                        std::array<bool, cfg::RELAY_COUNT> &values) {
  size_t key = body.find(std::string("\"") + name + "\"");
  if (key == std::string::npos) return;
  size_t start = body.find('[', key);
  if (start == std::string::npos) return;
  size_t end = body.find(']', start);
  if (end == std::string::npos) return;
  const std::string arr = body.substr(start + 1, end - start - 1);
  size_t p = 0;
  for (uint8_t i = 0; i < cfg::RELAY_COUNT; ++i) {
    size_t comma = arr.find(',', p);
    const std::string tok = arr.substr(p, comma == std::string::npos ? std::string::npos : comma - p);
    values[i] = tok.find("true") != std::string::npos;
    if (comma == std::string::npos) break;
    p = comma + 1;
  }
}

static uint32_t json_uint_value(const std::string &body, const char *name, uint32_t fallback) {
  size_t pos = body.find(std::string("\"") + name + "\"");
  if (pos == std::string::npos || (pos = body.find(':', pos)) == std::string::npos) return fallback;
  while (++pos < body.size() && std::isspace(static_cast<unsigned char>(body[pos]))) {}
  if (pos >= body.size() || !std::isdigit(static_cast<unsigned char>(body[pos]))) return fallback;
  uint32_t value = 0;
  while (pos < body.size() && std::isdigit(static_cast<unsigned char>(body[pos]))) {
    value = value * 10 + static_cast<uint32_t>(body[pos++] - '0');
  }
  return value;
}

static void configure_feedback_inputs() {
  for (uint8_t i = 0; i < cfg::RELAY_COUNT; ++i) {
    gpio_init(cfg::FEEDBACK_PINS[i]);
    gpio_set_dir(cfg::FEEDBACK_PINS[i], GPIO_IN);
#ifdef INPUT_MODE_BUTTON
    gpio_pull_up(cfg::FEEDBACK_PINS[i]);  // Taster schalten gegen GND
    button_pressed[i] = false;
    button_raw[i] = false;
    button_since[i] = millis32();
#else
    if (feedback_active_low[i]) gpio_pull_up(cfg::FEEDBACK_PINS[i]);
    else gpio_pull_down(cfg::FEEDBACK_PINS[i]);
#endif
    feedback_pending[i] = false;
    feedback_error[i] = false;
    feedback_source_scene[i] = -1;
  }
  update_scene_feedback_errors();
}

static void broadcast_state() {
  if (!lan_link_up) return;  // ohne LAN keine SSE-Empfaenger, blockierendes send() vermeiden
  prune_sse_sockets();
  for (uint8_t sn = 0; sn < cfg::HTTP_SOCKET_COUNT; ++sn) {
    if (sse_socket[sn]) send_sse(sn, "data: " + state_json(sse_show_users[sn]) + "\r\n\r\n");
  }
}

static void keepalive_sse() {
  if (!lan_link_up) return;
  if (millis32() - last_keepalive < cfg::SSE_KEEPALIVE_MS) return;
  last_keepalive = millis32();
  prune_sse_sockets();
  for (uint8_t sn = 0; sn < cfg::HTTP_SOCKET_COUNT; ++sn) {
    if (sse_socket[sn]) send_sse(sn, "event: ping\r\ndata: {}\r\n\r\n");
  }
}

static void handle_config_post(uint8_t sn, const HttpRequest &req) {
  {
    StateLock lock;
    update_names_from_json(req.body);
    update_bool_array_from_json(req.body, "act_low", relay_active_low);
    update_bool_array_from_json(req.body, "feedback_enabled", feedback_enabled);
    update_bool_array_from_json(req.body, "feedback_low", feedback_active_low);
    feedback_timeout_ms = std::clamp<uint32_t>(json_uint_value(req.body, "feedback_timeout", feedback_timeout_ms),
                                                cfg::MIN_INPUT_TIME_MS,
                                                cfg::MAX_INPUT_TIME_MS);
    std::string title = json_string_value(req.body, "title");
    std::string subtitle = json_string_value(req.body, "subtitle");
    if (!title.empty()) site_title = title.substr(0, 64);
    if (!subtitle.empty()) site_subtitle = subtitle.substr(0, 64);
    public_access = json_bool_value(req.body, "public", public_access);
    for (uint8_t i = 0; i < cfg::RELAY_COUNT; ++i) apply_relay(i);  // geaenderte Polaritaet sofort ausgeben
    configure_feedback_inputs();
  }
  save_config();
  broadcast_state();
  esp_link_display_dirty = true;
  send_response(sn, "200 OK", "application/json", "{\"ok\":true}");
}

static void handle_network_post(uint8_t sn, const HttpRequest &req) {
  std::array<uint8_t, 4> new_ip{};
  std::array<uint8_t, 4> new_sn{};
  std::array<uint8_t, 4> new_gw{};
  if (!parse_ipv4(trim(json_string_value(req.body, "ip")), new_ip) ||
      !parse_ipv4(trim(json_string_value(req.body, "subnet")), new_sn) ||
      !parse_ipv4(trim(json_string_value(req.body, "gateway")), new_gw)) {
    send_response(sn, "400 Bad Request", "application/json", "{\"ok\":false,\"error\":\"ungueltige IPv4-Adresse\"}");
    return;
  }

  static_ip = new_ip;
  static_sn = new_sn;
  static_gw = new_gw;
  save_config();
  send_response(sn, "200 OK", "application/json", "{\"ok\":true}");
}

static void handle_admin_post(uint8_t sn, const HttpRequest &req, const Session *session) {
  std::string action = json_string_value(req.body, "action");
  std::string username = normalize_username(json_string_value(req.body, "username"));
  if (action == "gen_key") {
    std::string comment = trim(json_string_value(req.body, "comment"));
    if (api_keys_db.size() >= cfg::MAX_API_KEYS) {
      send_response(sn, "400 Bad Request", "application/json", "{\"error\":\"maximale anzahl erreicht\"}");
      return;
    }
    std::string key = random_hex(8);
    api_keys_db.push_back({key, comment.substr(0, 64)});
    save_config();
    send_response(sn, "200 OK", "application/json", "{\"ok\":true,\"key\":\"" + key + "\"}");
  } else if (action == "delete_key") {
    std::string key = trim(json_string_value(req.body, "key"));
    auto it = std::remove_if(api_keys_db.begin(), api_keys_db.end(), [&](const ApiKeyEntry &e) { return e.key == key; });
    if (it == api_keys_db.end()) send_response(sn, "404 Not Found", "application/json", "{\"error\":\"key nicht gefunden\"}");
    else { api_keys_db.erase(it, api_keys_db.end()); save_config(); send_response(sn, "200 OK", "application/json", "{\"ok\":true}"); }
  } else if (action == "set_key_comment") {
    std::string key = trim(json_string_value(req.body, "key"));
    std::string comment = trim(json_string_value(req.body, "comment"));
    auto it = std::find_if(api_keys_db.begin(), api_keys_db.end(), [&](const ApiKeyEntry &e) { return e.key == key; });
    if (it == api_keys_db.end()) send_response(sn, "404 Not Found", "application/json", "{\"error\":\"key nicht gefunden\"}");
    else { it->comment = comment.substr(0, 64); save_config(); send_response(sn, "200 OK", "application/json", "{\"ok\":true}"); }
  } else if (action == "add") {
    std::string password = json_string_value(req.body, "password");
    std::string role = json_string_value(req.body, "role");
    if (username.empty() || password.size() < 4) send_response(sn, "400 Bad Request", "application/json", "{\"error\":\"ungueltige daten\"}");
    else {
      if (role != "admin" && role != "user") role = "user";
      users_db[username] = {sha256_hex(password), role};
      save_config();
      send_response(sn, "200 OK", "application/json", "{\"ok\":true}");
    }
  } else if (action == "delete") {
    if (session && username == normalize_username(session->username)) send_response(sn, "400 Bad Request", "application/json", "{\"error\":\"eigener account\"}");
    else {
      users_db.erase(username);
      save_config();
      send_response(sn, "200 OK", "application/json", "{\"ok\":true}");
    }
  } else if (action == "set_password") {
    std::string password = json_string_value(req.body, "password");
    auto it = users_db.find(username);
    if (it == users_db.end() || password.size() < 4) send_response(sn, "400 Bad Request", "application/json", "{\"error\":\"ungueltige daten\"}");
    else {
      it->second.hash = sha256_hex(password);
      save_config();
      send_response(sn, "200 OK", "application/json", "{\"ok\":true}");
    }
  } else if (action == "set_role") {
    std::string role = json_string_value(req.body, "role");
    auto it = users_db.find(username);
    if (it == users_db.end() || (role != "admin" && role != "user")) send_response(sn, "400 Bad Request", "application/json", "{\"error\":\"ungueltige daten\"}");
    else if (session && username == normalize_username(session->username) && role != "admin") send_response(sn, "400 Bad Request", "application/json", "{\"error\":\"eigene admin-rolle\"}");
    else if (it->second.role == "admin" && role != "admin" && admin_user_count() <= 1) send_response(sn, "400 Bad Request", "application/json", "{\"error\":\"letzter admin\"}");
    else {
      it->second.role = role;
      if (role != "admin") delete_sessions_for_user(username);
      save_config();
      send_response(sn, "200 OK", "application/json", "{\"ok\":true}");
    }
  } else {
    send_response(sn, "400 Bad Request", "application/json", "{\"error\":\"unbekannte aktion\"}");
  }
}

static std::string build_scenes_html(const Session *session) {
  std::string rows;
  for (uint8_t s = 0; s < cfg::SCENE_COUNT; ++s) {
    std::string cells;
    for (uint8_t r = 0; r < cfg::RELAY_COUNT; ++r) {
      const uint8_t a = scenes[s].action[r];
      const std::string label = relay_names[r].empty() ? ("R" + std::to_string(r + 1)) : relay_names[r];
      cells += "<div class=\"cell\"><span>" + html_escape(label) + "</span><select data-s=\"" + std::to_string(s) + "\" data-r=\"" + std::to_string(r) + "\">"
               "<option value=\"x\"" + std::string(a == 2 ? " selected" : "") + ">\xe2\x80\x94</option>"
               "<option value=\"0\"" + std::string(a == 0 ? " selected" : "") + ">Aus</option>"
               "<option value=\"1\"" + std::string(a == 1 ? " selected" : "") + ">Ein</option>"
               "</select></div>";
    }
    rows += "<div class=\"scene\"><div class=\"scene-head\"><label class=\"en\"><input type=\"checkbox\" class=\"s-en\" data-s=\"" + std::to_string(s) + "\"" + std::string(scenes[s].enabled ? " checked" : "") + "> aktiv</label>"
            "<input class=\"s-name\" data-s=\"" + std::to_string(s) + "\" maxlength=\"32\" placeholder=\"Szene " + std::to_string(s + 1) + "\" value=\"" + html_escape(scenes[s].name) + "\"></div>"
            "<div class=\"cells\">" + cells + "</div></div>";
  }
  std::string html = "<!DOCTYPE html><html lang=\"de\"><head><meta charset=\"UTF-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><title>" + html_escape(site_title) + " - Szenen</title><style>html,body{margin:0}body{box-sizing:border-box;font-family:'Segoe UI',sans-serif;background:#121212;color:#e0e0e0;min-height:100vh;padding:108px 24px 24px}" + page_header_css() +
      "h1{color:#4dabf7}.card{background:#1e1e1e;padding:24px;max-width:760px;border:1px solid #2d2d2d}.actions{display:flex;flex-wrap:wrap;gap:8px;margin:0 0 16px}.actions button{margin:0}button{padding:10px 14px;background:#1a3a5c;color:#4dabf7;border:1px solid #1e5a9e;font-weight:700}.save{background:#1b4332;color:#51cf66;border-color:#2d6a4f}.ok{color:#51cf66}.err{color:#ff6b6b}"
      ".mode-row{display:flex;align-items:center;gap:10px;margin-bottom:16px;font-weight:700;color:#9e9e9e}.scene{border:1px solid #2d2d2d;background:#181818;padding:12px;margin-bottom:12px}.scene-head{display:flex;gap:12px;align-items:center;margin-bottom:10px;flex-wrap:wrap}.scene-head .en{display:flex;align-items:center;gap:6px;color:#9e9e9e;white-space:nowrap}.s-name{flex:1;min-width:160px;background:#2a2a2a;color:#e0e0e0;border:1px solid #333;padding:8px}.cells{display:grid;grid-template-columns:repeat(auto-fill,minmax(150px,1fr));gap:8px}.cell{display:flex;flex-direction:column;gap:3px;font-size:.75rem;color:#9e9e9e}.cell select{background:#2a2a2a;color:#e0e0e0;border:1px solid #333;padding:6px}</style></head><body>" +
      page_header_html(session, true) +
      "<h1>Szenen</h1><div class=\"actions\"><button onclick=\"location.href='/config'\">Zurueck</button><button class=\"save\" onclick=\"save()\">Speichern</button></div><div id=\"msg\"></div><div class=\"card\">"
      "<div class=\"mode-row\"><input type=\"checkbox\" id=\"mode\"" + std::string(scene_mode ? " checked" : "") + "><label for=\"mode\">Szenen-Modus aktiv (Buttons aktivieren Szenen statt Relais)</label></div>" + rows + "</div>"
      "<script>const conn=document.getElementById('conn');let lastSseActivity=0;let lastConnectAttempt=0;let es=null;let reconnectTimer=0;function markConn(ok){if(!conn)return;conn.textContent=ok?'Verbunden':'Getrennt';conn.className=ok?'ok':'err'}function noteSseActivity(){lastSseActivity=Date.now();markConn(true)}function scheduleReconnect(delay=1000){if(reconnectTimer)return;reconnectTimer=setTimeout(()=>{reconnectTimer=0;connectEvents()},delay)}function forceReconnect(){if(es){es.close();es=null}scheduleReconnect(0)}function connectEvents(){if(es)es.close();lastConnectAttempt=Date.now();es=new EventSource('/events');es.onopen=()=>{noteSseActivity()};es.onerror=()=>{markConn(false);if(es){es.close();es=null}scheduleReconnect()};es.addEventListener('ping',()=>{noteSseActivity()});es.onmessage=()=>{noteSseActivity()}}connectEvents();window.addEventListener('pagehide',()=>{if(es)es.close()});setInterval(()=>{const now=Date.now();const activeLink=lastSseActivity&&now-lastSseActivity<2500;markConn(!!activeLink);if(!activeLink&&now-lastConnectAttempt>=2500&&!reconnectTimer)forceReconnect()},1000);"
      "function save(){const body={mode:document.getElementById('mode').checked};"
      "document.querySelectorAll('.s-en').forEach(e=>{body['s'+e.dataset.s+'_en']=e.checked});"
      "document.querySelectorAll('.s-name').forEach(e=>{body['s'+e.dataset.s+'_name']=e.value.replace(/[\"\\\\]/g,'')});"
      "for(let s=0;s<" + std::to_string(cfg::SCENE_COUNT) + ";s++){let act='';for(let r=0;r<" + std::to_string(cfg::RELAY_COUNT) + ";r++){const sel=document.querySelector('select[data-s=\"'+s+'\"][data-r=\"'+r+'\"]');act+=sel?sel.value:'x'}body['s'+s+'_act']=act}"
      "fetch('/scenes',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)}).then(r=>r.json()).then(d=>{const m=document.getElementById('msg');m.textContent=d.ok?'Gespeichert':'Fehler';m.className=d.ok?'ok':'err'}).catch(()=>{document.getElementById('msg').textContent='Fehler'})}"
      "</script></body></html>";
  return html;
}

static void handle_scenes_post(uint8_t sn, const HttpRequest &req) {
  {
    StateLock lock;
    scene_mode = json_bool_value(req.body, "mode", scene_mode);
    for (uint8_t s = 0; s < cfg::SCENE_COUNT; ++s) {
      const std::string p = "s" + std::to_string(s) + "_";
      scenes[s].enabled = json_bool_value(req.body, p + "en", scenes[s].enabled);
      scenes[s].name = json_string_value(req.body, p + "name").substr(0, 32);
      const std::string act = json_string_value(req.body, p + "act");
      for (uint8_t r = 0; r < cfg::RELAY_COUNT && r < act.size(); ++r) {
        const char c = act[r];
        scenes[s].action[r] = (c == '1') ? 1 : (c == '0') ? 0 : 2;
      }
    }
    if (active_scene >= 0 && (active_scene >= cfg::SCENE_COUNT || !scenes[active_scene].enabled)) active_scene = -1;
  }
  save_config();
  broadcast_state();
  esp_link_display_dirty = true;
  send_response(sn, "200 OK", "application/json", "{\"ok\":true}");
}

static void handle_http(uint8_t sn, const HttpRequest &req) {
  Session *current_session = session_from_headers(req);
  if (public_access && !current_session) mark_guest_visitor(req.client_ip);

  if (req.method == "GET" && (req.path == "/" || req.path == "/index.html")) {
    {
      Session *s = current_session;
      bool can_control = has_relay_access(req);
      send_response(sn, "200 OK", "text/html; charset=utf-8", build_index_html(s, can_control));
    }
  } else if (req.method == "GET" && req.path == "/login") {
    std::string next = query_param(req.query, "next");
    if (next.empty() || next[0] != '/' || next.find("//") != std::string::npos) next = "/config";
    send_response(sn, "200 OK", "text/html; charset=utf-8", build_login_html(next, false, session_from_headers(req)));
  } else if (req.method == "POST" && req.path == "/login") {
    auto form = parse_form(req.body);
    std::string next = form["next"];
    if (next.empty() || next[0] != '/' || next.find("//") != std::string::npos) next = "/config";
    auto it = users_db.find(normalize_username(form["username"]));
    const bool needs_admin = (next == "/config" || next == "/network" || next == "/scenes" || next == "/admin");
    if (it != users_db.end() && it->second.hash == sha256_hex(form["password"]) && needs_admin && it->second.role != "admin") {
      send_response(sn, "200 OK", "text/html; charset=utf-8", build_login_html(next, true, session_from_headers(req), form["username"], form["username"] + " hat keine Admin-Rechte"));
    } else if (it != users_db.end() && it->second.hash == sha256_hex(form["password"])) {
      std::string token = create_session(it->first, it->second.role);
      std::string cookie;
      if (it->second.role == "admin") {
        persistent_admin_tokens.push_back(token);
        // Admin-Cookie 1 Jahr gueltig, damit Admin auch nach Session-Timeout noch schalten kann.
        cookie = "Set-Cookie: sid=" + token + "; HttpOnly; SameSite=Strict; Path=/; Max-Age=31536000\r\n";
      } else {
        cookie = "Set-Cookie: sid=" + token + "; HttpOnly; SameSite=Strict; Path=/; Max-Age=1800\r\n";
      }
      send_redirect(sn, next, cookie);
    } else send_response(sn, "200 OK", "text/html; charset=utf-8", build_login_html(next, true, session_from_headers(req), form["username"]));
  } else if (req.method == "GET" && req.path == "/logout") {
    auto it = req.headers.find("cookie");
    if (it != req.headers.end()) {
      const std::string token = cookie_value(it->second, "sid");
      Session *session = get_session(token);
      if (session) {
        const std::string username = session->username;
        delete_sessions_for_user(username);
      } else {
        delete_session(token);
      }
      broadcast_state();
    }
    send_redirect(sn, "/", "Set-Cookie: sid=; HttpOnly; Path=/; Max-Age=0\r\n");
  } else if (req.path == "/password") {
    Session *session = session_from_headers(req);
    if (!session) {
      if (req.method == "GET") send_redirect(sn, "/login?next=/password");
      else send_response(sn, "401 Unauthorized", "application/json", "{\"error\":\"unauthorized\"}");
    } else if (req.method == "GET") {
      send_response(sn, "200 OK", "text/html; charset=utf-8", build_password_html(session, "", false));
    } else if (req.method == "POST") {
      auto form = parse_form(req.body);
      std::string old_pw = form["old"];
      std::string new1 = form["new1"];
      std::string new2 = form["new2"];
      auto it = users_db.find(normalize_username(session->username));
      if (it == users_db.end()) send_response(sn, "200 OK", "text/html; charset=utf-8", build_password_html(session, "Benutzer nicht gefunden", true));
      else if (it->second.hash != sha256_hex(old_pw)) send_response(sn, "200 OK", "text/html; charset=utf-8", build_password_html(session, "Aktuelles Passwort falsch", true));
      else if (new1.size() < 4) send_response(sn, "200 OK", "text/html; charset=utf-8", build_password_html(session, "Neues Passwort zu kurz (min. 4 Zeichen)", true));
      else if (new1 != new2) send_response(sn, "200 OK", "text/html; charset=utf-8", build_password_html(session, "Neue Passw\xc3\xb6" "rter stimmen nicht \xc3\xbc" "berein", true));
      else {
        it->second.hash = sha256_hex(new1);
        save_config();
        send_response(sn, "200 OK", "text/html; charset=utf-8", build_password_html(session, "Passwort ge\xc3\xa4" "ndert", false));
      }
    } else {
      send_response(sn, "405 Method Not Allowed", "text/plain", "Method not allowed");
    }
  } else if (req.path == "/config") {
    Session *session = session_from_headers(req);
    if (!session || session->role != "admin") {
      if (req.method == "GET") send_redirect(sn, "/login?next=/config");
      else send_response(sn, "403 Forbidden", "application/json", "{\"error\":\"forbidden\"}");
    } else if (req.method == "GET") send_response(sn, "200 OK", "text/html; charset=utf-8", build_config_html(session));
    else if (req.method == "POST") handle_config_post(sn, req);
    else send_response(sn, "405 Method Not Allowed", "text/plain", "Method not allowed");
  } else if (req.path == "/network") {
    Session *session = session_from_headers(req);
    if (!session || session->role != "admin") {
      if (req.method == "GET") send_redirect(sn, "/login?next=/network");
      else send_response(sn, "403 Forbidden", "application/json", "{\"error\":\"forbidden\"}");
    } else if (req.method == "GET") send_response(sn, "200 OK", "text/html; charset=utf-8", build_network_html(session));
    else if (req.method == "POST") handle_network_post(sn, req);
    else send_response(sn, "405 Method Not Allowed", "text/plain", "Method not allowed");
  } else if (req.path == "/scenes") {
    Session *session = session_from_headers(req);
    if (!session || session->role != "admin") {
      if (req.method == "GET") send_redirect(sn, "/login?next=/scenes");
      else send_response(sn, "403 Forbidden", "application/json", "{\"error\":\"forbidden\"}");
    } else if (req.method == "GET") send_response(sn, "200 OK", "text/html; charset=utf-8", build_scenes_html(session));
    else if (req.method == "POST") handle_scenes_post(sn, req);
    else send_response(sn, "405 Method Not Allowed", "text/plain", "Method not allowed");
  } else if (req.path == "/admin") {
    Session *session = session_from_headers(req);
    if (!session || session->role != "admin") {
      if (req.method == "GET") send_redirect(sn, "/login?next=/admin");
      else send_response(sn, "403 Forbidden", "application/json", "{\"error\":\"forbidden\"}");
    } else if (req.method == "GET") send_response(sn, "200 OK", "text/html; charset=utf-8", build_admin_html(session));
    else if (req.method == "POST") handle_admin_post(sn, req, session);
    else send_response(sn, "405 Method Not Allowed", "text/plain", "Method not allowed");
  } else if (req.method == "GET" && req.path == "/me") {
    Session *session = session_from_headers(req);
    if (session) send_response(sn, "200 OK", "application/json", "{\"username\":\"" + json_escape(session->username) + "\",\"role\":\"" + json_escape(session->role) + "\"}");
    else send_response(sn, "200 OK", "application/json", "{\"username\":null,\"role\":null}");
  } else if (req.method == "GET" && req.path == "/active_users") {
    send_response(sn, "200 OK", "application/json", active_users_json(session_from_headers(req) != nullptr || public_access));
  } else if (req.method == "GET" && req.path == "/state") {
    if (!has_control_access(req)) send_response(sn, "401 Unauthorized", "application/json", "{\"error\":\"unauthorized\"}");
    else send_response(sn, "200 OK", "application/json", state_json(session_from_headers(req) != nullptr || public_access));
  } else if (req.method == "GET" && req.path == "/events") {
    prune_sse_sockets();
    if (active_sse_socket_count() >= cfg::MAX_SSE_SOCKETS) {
      send_response(sn, "503 Service Unavailable", "text/plain", "SSE busy", "Retry-After: 1\r\n");
      return;
    }
    send_all(sn, "HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nCache-Control: no-cache\r\nConnection: keep-alive\r\nAccess-Control-Allow-Origin: *\r\n\r\n");
    sse_socket[sn] = true;
    sse_show_users[sn] = session_from_headers(req) != nullptr || public_access;
    send_all(sn, "data: " + state_json(sse_show_users[sn]) + "\r\n\r\n");
  } else if (req.method == "POST" && req.path.rfind("/relay/", 0) == 0) {
    if (!has_relay_access(req)) {
      send_response(sn, "401 Unauthorized", "application/json", "{\"error\":\"unauthorized\"}");
      return;
    }
    size_t first = req.path.find('/', 1);
    size_t second = first == std::string::npos ? std::string::npos : req.path.find('/', first + 1);
    if (first == std::string::npos || second == std::string::npos) {
      send_response(sn, "400 Bad Request", "text/plain", "");
      return;
    }
    int idx = std::atoi(req.path.substr(first + 1, second - first - 1).c_str());
    std::string action = req.path.substr(second + 1);
    if (idx < 0 || idx >= cfg::RELAY_COUNT) send_response(sn, "400 Bad Request", "text/plain", "Index 0-7");
    else if (action == "toggle" || action == "on" || action == "off") {
      if (action == "toggle") set_relay(static_cast<uint8_t>(idx), !relay_states[idx]);
      else set_relay(static_cast<uint8_t>(idx), action == "on");
      broadcast_state();
      send_response(sn, "200 OK", "application/json", state_json(session_from_headers(req) != nullptr || public_access));
    } else send_response(sn, "400 Bad Request", "text/plain", "");
  } else if (req.method == "POST" && req.path.rfind("/scene/", 0) == 0) {
    if (!has_relay_access(req)) {
      send_response(sn, "401 Unauthorized", "application/json", "{\"error\":\"unauthorized\"}");
      return;
    }
    size_t first = req.path.find('/', 1);
    size_t second = first == std::string::npos ? std::string::npos : req.path.find('/', first + 1);
    if (first == std::string::npos || second == std::string::npos) {
      send_response(sn, "400 Bad Request", "text/plain", "");
      return;
    }
    int idx = std::atoi(req.path.substr(first + 1, second - first - 1).c_str());
    std::string action = req.path.substr(second + 1);
    if (idx < 0 || idx >= cfg::SCENE_COUNT) send_response(sn, "400 Bad Request", "text/plain", "Index 0-7");
    else if (action == "activate") {
      if (!scenes[idx].enabled) send_response(sn, "409 Conflict", "application/json", "{\"error\":\"szene inaktiv\"}");
      else {
        activate_scene(static_cast<uint8_t>(idx));
        broadcast_state();
        send_response(sn, "200 OK", "application/json", state_json(session_from_headers(req) != nullptr || public_access));
      }
    } else send_response(sn, "400 Bad Request", "text/plain", "");
  } else {
    send_response(sn, "404 Not Found", "text/plain", "Nicht gefunden");
  }
}

static void service_socket(uint8_t sn) {
  uint8_t status = getSn_SR(sn);
  switch (status) {
    case SOCK_CLOSED:
      sse_socket[sn] = false;
      sse_show_users[sn] = false;
      socket(sn, Sn_MR_TCP, cfg::HTTP_PORT, 0x00);
      listen(sn);
      break;
    case SOCK_INIT:
      listen(sn);
      break;
    case SOCK_ESTABLISHED: {
      if (sse_socket[sn]) {
        if (getSn_RX_RSR(sn) > 0) force_close_socket(sn);
        break;
      }
      uint16_t available = getSn_RX_RSR(sn);
      if (!available) break;
      std::string raw;
      raw.resize(std::min<uint16_t>(available, 4096));
      int32_t received = recv(sn, reinterpret_cast<uint8_t *>(raw.data()), static_cast<uint16_t>(raw.size()));
      if (received <= 0) break;
      raw.resize(static_cast<size_t>(received));
      HttpRequest req;
      if (!parse_request(raw, req)) send_response(sn, "400 Bad Request", "text/plain", "Bad Request");
      else {
        req.client_ip = socket_remote_ip(sn);
        handle_http(sn, req);
      }
      break;
    }
    case SOCK_CLOSE_WAIT:
      force_close_socket(sn);
      break;
    default:
      break;
  }
}

static void init_relays() {
  for (uint8_t i = 0; i < cfg::RELAY_COUNT; ++i) {
    relay_names[i] = "Relais " + std::to_string(i + 1);
    gpio_init(cfg::RELAY_PINS[i]);
    gpio_set_dir(cfg::RELAY_PINS[i], GPIO_OUT);
    gpio_put(cfg::RELAY_PINS[i], relay_gpio_value(i, false));
  }
}

   
namespace esp_link {
static void service();  // Vorwaerts-Deklaration: waehrend DHCP-Wartephase bedienen
}

// Zuverlaessige PHY-Link-Erkennung (MII/MDIO), identisch zur port-internen Pruefung.
static bool phy_link_up() {
  return wizphy_getphylink() == PHY_LINK_ON;
}

// Wartet zeitbegrenzt auf den PHY-Link und bedient dabei das ESP-Display weiter.
static bool wait_for_phy_link(uint32_t timeout_ms) {
  uint32_t start = millis32();
  while (!phy_link_up()) {
    if (millis32() - start >= timeout_ms) return false;
    sleep_ms(10);
  }
  lan_link_up = true;
  return true;
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
  printf("DHCP-Adresse: %u.%u.%u.%u\n", g_net_info.ip[0], g_net_info.ip[1], g_net_info.ip[2], g_net_info.ip[3]);
  printf("DHCP-Lease: %lu Sekunden\n", static_cast<unsigned long>(getDHCPLeasetime()));
  dhcp_assigned = true;
}

static void dhcp_conflict_handler() {
  printf("DHCP-Konflikt: zugewiesene IP ist bereits belegt.\n");
  dhcp_conflict = true;
}

static bool acquire_dhcp_address() {
  printf("DHCP aktiv: ja\n");
  printf("DHCP-Client startet auf Socket %u ...\n", cfg::DHCP_SOCKET);
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
      ++retries;
      printf("DHCP-Timeout, Versuch %u/%u\n", retries, cfg::DHCP_RETRY_COUNT);
      if (retries >= cfg::DHCP_RETRY_COUNT) break;
    } else if (result == DHCP_STOPPED) {
      break;
    }
    sleep_ms(10);
  }

  DHCP_stop();
  return dhcp_assigned && !dhcp_conflict;
}

static std::string current_session_username(const Session *session) {
  return session ? session->username : "-";
}

static std::string other_active_users_text(const Session *current) {
  prune_sessions();
  std::string text;
  for (const Session &session : sessions) {
    if (current && session.token == current->token) continue;
    uint32_t remaining_ms = expired(session.expires) ? 0 : session.expires - millis32();
    uint32_t remaining_min = (remaining_ms + 59999) / 60000;
    if (!text.empty()) text += ", ";
    text += session.username + " (" + std::to_string(remaining_min) + " min)";
  }
  return text.empty() ? "-" : text;
}

static std::string page_header_css() {
  return ".page-header,.page-header *{box-sizing:border-box}.page-header{position:fixed;top:0;left:0;right:0;height:80px;background:#0d1117;z-index:100;border-bottom:1px solid #30363d;overflow:visible}.topbar{height:50px;background:#161b22;border-bottom:1px solid #30363d;display:flex;align-items:center;padding:6px 16px;gap:12px;flex-wrap:nowrap;overflow:visible}.topbar-title{display:block;margin:0;padding:0;border:0;background:transparent;font-family:'Segoe UI',system-ui,sans-serif;font-size:16px;font-weight:700;line-height:20px;color:#58a6ff;flex:1;min-width:80px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;text-decoration:none}.topbar-title:visited{color:#58a6ff}.topbar-title:hover{color:#8bd5ff}.topbar button{margin:0;line-height:1.2}.btn-cfg{padding:5px 12px;border:1px solid #30363d;background:#21262d;color:#8b949e;font-size:.78rem;font-weight:600;cursor:pointer}.user-menu{position:relative;display:inline-block}.current-user{font-size:.72rem;color:#c9d1d9;border:1px solid #30363d;background:#0d1117;padding:4px 10px;text-decoration:none;cursor:pointer;white-space:nowrap}.current-user:hover,.user-menu:hover .current-user{border-color:#58a6ff;color:#58a6ff}.user-menu-content{display:none;position:absolute;right:0;top:100%;background:#161b22;border:1px solid #30363d;min-width:170px;z-index:200;margin-top:2px}.user-menu:hover .user-menu-content{display:block}.user-menu-content a{display:block;padding:8px 12px;color:#c9d1d9;text-decoration:none;font-size:.78rem;white-space:nowrap;cursor:pointer;border-bottom:1px solid #30363d}.user-menu-content a:last-child{border-bottom:0}.user-menu-content a:hover{background:#21262d;color:#58a6ff}.activebar{height:30px;color:#8b949e;font-size:.72rem;line-height:16px;padding:7px 16px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}#conn{font-size:.72rem;font-weight:600;padding:4px 10px;border:1px solid #30363d;background:#161b22;color:#8b949e;white-space:nowrap}#conn.ok{border-color:#238636;color:#3fb950}#conn.err{border-color:#da3633;color:#f85149}";
}

static std::string page_header_html(const Session *session, bool show_conn) {
  std::string html = "<div class=\"page-header\"><div class=\"topbar\"><a class=\"topbar-title\" id=\"sitetitle\" href=\"/\" onclick=\"if(location.pathname==='/'&&location.search===''&&location.hash==='')return false;\">" + html_escape(site_title) + "</a><button class=\"btn-cfg\" onclick=\"location.href='/config'\">Konfig</button>";
  if (session) {
    html += "<div class=\"user-menu\"><span class=\"current-user\">" + html_escape(session->username) + " \xe2\x96\xbe</span><div class=\"user-menu-content\"><a href=\"/logout\">Abmelden</a><a href=\"/password\">Passwort \xc3\xa4ndern</a></div></div>";
  } else {
    html += "<button class=\"current-user\" onclick=\"location.href='/login'\">Login</button>";
  }
  if (show_conn) html += "<span id=\"conn\">...</span>";
  const bool show_user_list = session || public_access;
  html += "</div>";
  if (show_user_list) {
    html += "<div class=\"activebar\" id=\"other-users\">Andere Benutzer: " + html_escape(other_active_users_text(session)) + "</div>";
  }
  html += "</div>";
  if (show_user_list) {
    html += "<script>window.__currentUser=\"" + json_escape(current_session_username(session)) + "\";function __renderHeaderUsers(users){const el=document.getElementById('other-users');if(!el)return;if(users===null){el.style.display='none';return}el.style.display='';const cur=window.__currentUser||'-';const list=(users||[]).filter(u=>!cur||cur==='-'||u.username!==cur);if(!list.length){el.textContent='Andere Benutzer: -';el.title='Keine anderen aktiven Anmeldungen';return}const text=list.map(u=>u.username+' ('+u.remaining+' min)').join(', ');el.textContent='Andere Benutzer: '+text;el.title=text}function __pollHeaderUsers(){fetch('/active_users',{cache:'no-store'}).then(r=>r.ok?r.json():null).then(d=>{if(d)__renderHeaderUsers(d.active_users)}).catch(()=>{})}setInterval(__pollHeaderUsers,5000);__pollHeaderUsers();</script>";
  }
  html += "<div id=\"fwfoot\" style=\"width:100%;box-sizing:border-box;margin-top:auto;padding:16px 8px 4px;color:#484f58;font-size:.7rem;user-select:text;-webkit-user-select:text\">Designed and built by OE5RNL, OE5NVL and Claude &nbsp;&nbsp;Firmware pico: " FW_VERSION " &nbsp; esp32: " + html_escape(esp_fw_version) + "</div><script>document.addEventListener('DOMContentLoaded',function(){var f=document.getElementById('fwfoot');if(f)document.body.appendChild(f);});</script>";
  return html;
}

static std::string build_index_html(const Session *session, bool can_control) {
  std::string body_section;
  if (can_control) {
    if (scene_mode) {
      body_section =
          "<div style=\"width:100%;max-width:800px;margin:22px 0 8px;color:#8b949e;font-size:.8rem;font-weight:700;text-transform:uppercase\">Szenen</div>"
          "<div class=\"grid\" id=\"scenegrid\"></div>"
          "<div style=\"width:100%;max-width:800px;margin:22px 0 8px;color:#8b949e;font-size:.8rem;font-weight:700;text-transform:uppercase\">Relais</div>"
          "<div class=\"grid\" id=\"grid\"></div>"
          "<script>const sceneData=" + scenes_config_json() + ";"
          "function activateScene(i){fetch('/scene/'+i+'/activate',{method:'POST'}).catch(()=>{})}"
          "(function(){const sg=document.getElementById('scenegrid');if(!sg)return;const btns={};sceneData.forEach((s,i)=>{if(!s.en)return;const b=document.createElement('button');b.className='card';b.type='button';b.id='sc'+i;b.onclick=()=>activateScene(i);const row=document.createElement('div');row.className='relay-row';const lab=document.createElement('span');lab.className='label';lab.textContent=s.name||('Szene '+(i+1));const st=document.createElement('span');st.className='state';st.textContent='\\u25B6';row.appendChild(lab);row.appendChild(st);b.appendChild(row);sg.appendChild(b);btns[i]=b});window.applyScenes=function(as,errs=[],dirty=false){Object.keys(btns).forEach(k=>{btns[k].className='card'+(errs[k]?' error':((+k===as)?(' on'+(dirty?' dirty':'')):''))})}})();"
          "</script>";
    } else {
      body_section = "<div class=\"grid\" id=\"grid\"></div>";
    }
  } else {
    body_section = "<div class=\"notice\"><p>Bitte <a href=\"/login?next=/\">anmelden</a>, </p></div>";
  }
  std::string html = "<!DOCTYPE html><html lang=\"de\"><head><meta charset=\"UTF-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><title>" + html_escape(site_title) + "</title><style>*{box-sizing:border-box;margin:0;padding:0}body{font-family:'Segoe UI',system-ui,sans-serif;background:#0d1117;color:#c9d1d9;min-height:100vh;display:flex;flex-direction:column;align-items:center;padding:20px 16px 40px}" + page_header_css() + "h1{margin:92px 0 24px;font-size:1.4rem;color:#e6edf3;font-weight:600;text-align:center}.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(220px,1fr));gap:12px;width:100%;max-width:800px}.card{width:100%;background:#161b22;border:1px solid #30363d;padding:10px;display:flex;align-items:center;cursor:pointer;text-align:inherit;font:inherit;color:inherit}.card.on{border-color:#238636}.card.error{background:#5a1616;border-color:#f85149}.card.error .label,.card.error .state{color:#fff}.card.on.dirty{position:relative}.card.on.dirty::after{content:'';position:absolute;top:4px;right:4px;width:10px;height:10px;border-radius:50%;background:#f85149}.card:focus-visible{outline:2px solid #58a6ff;outline-offset:2px}.relay-row{display:flex;align-items:center;gap:10px;width:100%}.label{flex:1;font-size:.9rem;font-weight:700;color:#8b949e;text-transform:uppercase;text-align:left}.relay-num{flex:none;font-size:.65rem;color:#484f58;min-width:24px}.state{flex:none;min-width:38px;font-size:.9rem;font-weight:700;color:#8b949e;text-align:right}.card.on .state{color:#3fb950}.notice{background:#161b22;border:1px solid #30363d;padding:24px 28px;color:#c9d1d9;font-size:.95rem;text-align:center;max-width:560px}.notice a{color:#58a6ff;font-weight:700}</style></head><body data-user=\"" + html_escape(current_session_username(session)) + "\" data-can-control=\"" + (can_control ? "1" : "0") + "\">" + page_header_html(session, true) + "<h1 id=\"subtitle\">" + html_escape(site_subtitle) + "</h1>" + body_section + "<script>const initialState=" + state_json(session != nullptr || public_access) + ";const canControl=document.body.dataset.canControl==='1';const grid=document.getElementById('grid');const conn=document.getElementById('conn');const other=document.getElementById('other-users');const currentUser=document.body.dataset.user;let names=(initialState.names&&initialState.names.length)?initialState.names:Array.from({length:8},(_,i)=>'Relais '+(i+1));if(grid&&canControl){const initialRelays=initialState.relays||[];const initialErrors=initialState.feedback_errors||[];for(let i=0;i<8;i++){const on=!!initialRelays[i];const card=document.createElement('button');card.type='button';card.className='card'+(initialErrors[i]?' error':(on?' on':''));card.id='c'+i;card.onclick=()=>toggle(i);card.innerHTML=`<div class=\"relay-row\"><span class=\"relay-num\">#${i+1}</span><span class=\"label\" id=\"l${i}\">${names[i]}</span><span class=\"state\" id=\"s${i}\">${on?'ON':'OFF'}</span></div>`;grid.appendChild(card)}}function renderActiveUsers(users){if(!other)return;if(users===null){other.style.display='none';return}other.style.display='';const list=(users||[]).filter(u=>!currentUser||currentUser==='-'||u.username!==currentUser);if(!list.length){other.textContent='Andere Benutzer: -';other.title='Keine anderen aktiven Anmeldungen';return}const text=list.map(u=>u.username+' ('+u.remaining+' min)').join(', ');other.textContent='Andere Benutzer: '+text;other.title=text}function applyState(states,ns,users,errors=[]){if(ns)names=ns;if(canControl&&states)states.forEach((on,i)=>{const lbl=document.getElementById('l'+i);if(!lbl)return;lbl.textContent=names[i];const state=document.getElementById('s'+i);const c=document.getElementById('c'+i);state.textContent=on?'ON':'OFF';c.className='card'+(errors[i]?' error':(on?' on':''))});renderActiveUsers(users)}renderActiveUsers(initialState.active_users);if(window.applyScenes)applyScenes(initialState.active_scene,initialState.scene_errors,initialState.scene_dirty);function toggle(i){fetch('/relay/'+i+'/toggle',{method:'POST',credentials:'same-origin'}).then(r=>{if(r.status===401){location.href='/login?next=/';throw new Error('unauthorized')}if(!r.ok)throw new Error('HTTP '+r.status);return r.json()}).then(d=>{applyState(d.relays,d.names,d.active_users,d.feedback_errors);if(window.applyScenes)applyScenes(d.active_scene,d.scene_errors,d.scene_dirty)}).catch(()=>{conn.textContent='Fehler';conn.className='err'})}let lastSseActivity=0;let lastConnectAttempt=0;let es=null;let reconnectTimer=0;function markConn(ok){conn.textContent=ok?'Verbunden':'Getrennt';conn.className=ok?'ok':'err'}function noteSseActivity(){lastSseActivity=Date.now();markConn(true)}function scheduleReconnect(delay=1000){if(reconnectTimer)return;reconnectTimer=setTimeout(()=>{reconnectTimer=0;connectEvents()},delay)}function forceReconnect(){if(es){es.close();es=null}scheduleReconnect(0)}function connectEvents(){if(es)es.close();lastConnectAttempt=Date.now();es=new EventSource('/events');es.onopen=()=>{noteSseActivity()};es.onerror=()=>{markConn(false);if(es){es.close();es=null}scheduleReconnect()};es.addEventListener('ping',()=>{noteSseActivity()});es.onmessage=e=>{noteSseActivity();try{const d=JSON.parse(e.data);applyState(d.relays,d.names,d.active_users,d.feedback_errors);if(window.applyScenes)applyScenes(d.active_scene,d.scene_errors,d.scene_dirty);if(d.title){document.getElementById('sitetitle').textContent=d.title;document.title=d.title}if(d.subtitle)document.getElementById('subtitle').innerHTML=d.subtitle}catch(x){}}}connectEvents();window.addEventListener('pagehide',()=>{if(es)es.close()});setInterval(()=>{const now=Date.now();const activeLink=lastSseActivity&&now-lastSseActivity<2500;markConn(!!activeLink);if(!activeLink&&now-lastConnectAttempt>=2500&&!reconnectTimer)forceReconnect()},1000);</script></body></html>";
  return html;
}
static void init_users() {
  if (users_db.empty()) users_db[cfg::DEFAULT_ADMIN_USER] = {cfg::DEFAULT_ADMIN_HASH, "admin"};
  auto admin = users_db.find(cfg::DEFAULT_ADMIN_USER);
  if (admin != users_db.end() && admin->second.hash == cfg::OLD_ADMIN_HASH) {
    admin->second.hash = cfg::DEFAULT_ADMIN_HASH;
    admin->second.role = "admin";
  }
}

// Eigene WIZchip-Init (nur W6300/QSPI-PIO), entspricht wizchip_initialize() aus
// dem WIZnet-Port, laesst aber dessen blockierendes Warten auf den PHY-Link weg
// (haengt sonst ohne LAN-Kabel den Boot). Der zeitbegrenzte Link-Check erfolgt
// danach in wait_for_phy_link(); der Fremdcode bleibt dadurch unveraendert.
static void wizchip_init_no_phy_wait() {
  (*spi_handle)->frame_end();
  reg_wizchip_qspi_cbfunc((*spi_handle)->read_byte, (*spi_handle)->write_byte);
  reg_wizchip_cs_cbfunc((*spi_handle)->frame_start, (*spi_handle)->frame_end);
  uint8_t memsize[2][8] = {{4, 4, 4, 4, 4, 4, 4, 4}, {4, 4, 4, 4, 4, 4, 4, 4}};
  if (ctlwizchip(CW_INIT_WIZCHIP, (void *)memsize) == -1) {
    printf(" W6x00 initialized fail\n");
  }
}

static void init_network() {
  printf("W6300 init mit WIZnet-PICO-C PIO/QSPI Quad Mode ...\n");
  wizchip_spi_initialize();
  wizchip_cris_initialize();
  wizchip_reset();
  wizchip_init_no_phy_wait();
  wizchip_check();
  printf("PHY-Link: %s\n", phy_link_up() ? "UP" : "DOWN");
  if (g_use_dhcp) {
    // Ohne LAN-Link wuerde der erste DHCP-DISCOVER in sendto() endlos auf
    // Sn_IR_SENDOK warten und den Boot (inkl. ESP-Link) blockieren. Daher zuerst
    // zeitbegrenzt auf den PHY-Link warten und dabei das ESP-Display bedienen.
    if (wait_for_phy_link(cfg::PHY_LINK_WAIT_MS)) {
      if (acquire_dhcp_address()) {
        printf("DHCP erfolgreich: HTTP-Server verwendet %u.%u.%u.%u\n", g_net_info.ip[0], g_net_info.ip[1], g_net_info.ip[2], g_net_info.ip[3]);
        return;
      }
      printf("DHCP fehlgeschlagen, Fallback auf statische IP.\n");
    } else {
      printf("Kein LAN-Link erkannt, DHCP uebersprungen, Fallback auf statische IP.\n");
    }
  }
  apply_static_network_to_runtime();
  network_initialize(g_net_info);
  print_network_information(g_net_info);
}

// ---- ESP-Display Link auf UART0 (GP0=TX, GP1=RX) -------------------------
namespace esp_link {
static uart_inst_t *const UART = uart0;
constexpr uint UART_TX_PIN = 0;
constexpr uint UART_RX_PIN = 1;
constexpr uint UART_BAUD = 115200;
static char rx_buf[96];
static size_t rx_len = 0;

static void send_line(const char *prefix, const std::string &value) {
  uart_puts(UART, prefix);
  uart_puts(UART, value.c_str());
  uart_puts(UART, "\n");
}

static void send_states() {
  StateLock lock;
  for (uint8_t i = 0; i < cfg::RELAY_COUNT; ++i) {
    uart_puts(UART, ("STATE" + std::to_string(i + 1) + ":").c_str());
    uart_puts(UART, relay_states[i] ? "ON\n" : "OFF\n");
    uart_puts(UART, ("ERROR" + std::to_string(i + 1) + ":").c_str());
    uart_puts(UART, feedback_error[i] ? "ON\n" : "OFF\n");
    uart_puts(UART, ("SERROR" + std::to_string(i + 1) + ":").c_str());
    uart_puts(UART, scene_feedback_error[i] ? "ON\n" : "OFF\n");
  }
  uart_puts(UART, ("ASCENE:" + std::to_string(active_scene + 1) + "\n").c_str());
  uart_puts(UART, scene_dirty ? "SDIRTY:ON\n" : "SDIRTY:OFF\n");
  uart_puts(UART, "END STATES\n");
}

static void send_display_config() {
  StateLock lock;
  send_line("TITLE:", site_title);
  uart_puts(UART, scene_mode ? "MODE:SCENE\n" : "MODE:RELAY\n");
  uart_puts(UART, ("IP:" + ip_status_text() + "\n").c_str());
  for (uint8_t i = 0; i < cfg::RELAY_COUNT; ++i) {
    send_line(("NAME" + std::to_string(i + 1) + ":").c_str(), relay_names[i]);
  }
  for (uint8_t i = 0; i < cfg::SCENE_COUNT; ++i) {
    send_line(("SCENE" + std::to_string(i + 1) + ":").c_str(), scenes[i].enabled ? scenes[i].name : std::string());
  }
  send_states();
  uart_puts(UART, "END DISPLAY\n");
}

static bool handle_scene_command(const char *line) {
  if (std::strncmp(line, "SCENE", 5) != 0) return false;
  const char *separator = std::strchr(line, ':');
  if (!separator) return false;
  int scene_number = std::atoi(line + 5);
  if (scene_number < 1 || scene_number > cfg::SCENE_COUNT) return false;
  if (std::strcmp(separator + 1, "GO") != 0) return false;
  if (!scenes[scene_number - 1].enabled) return true;
  activate_scene(static_cast<uint8_t>(scene_number - 1));
  g_sse_dirty = true;  // SSE-Broadcast von core1 anfordern (W6300 nur dort)
  return true;
}

static bool handle_switch_command(const char *line) {
  if (std::strncmp(line, "SW", 2) != 0) return false;

  const char *separator = std::strchr(line, ':');
  if (!separator) return false;

  int switch_number = std::atoi(line + 2);
  if (switch_number < 1 || switch_number > cfg::RELAY_COUNT) return false;

  const char *state = separator + 1;
  if (std::strcmp(state, "ON") == 0) {
    set_relay(static_cast<uint8_t>(switch_number - 1), true);
  } else if (std::strcmp(state, "OFF") == 0) {
    set_relay(static_cast<uint8_t>(switch_number - 1), false);
  } else {
    return false;
  }

  g_sse_dirty = true;  // SSE-Broadcast von core1 anfordern (W6300 nur dort)
  return true;
}

static void handle_line(const char *line) {
  printf("ESP-UART RX: %s\n", line);
  if (std::strcmp(line, "GET DISPLAY") == 0) {
    send_display_config();
  } else if (std::strcmp(line, "GET TITLE") == 0) {
    send_line("TITLE:", site_title);
  } else if (std::strcmp(line, "GET NAMES") == 0) {
    for (uint8_t i = 0; i < cfg::RELAY_COUNT; ++i) {
      send_line(("NAME" + std::to_string(i + 1) + ":").c_str(), relay_names[i]);
    }
    uart_puts(UART, "END NAMES\n");
  } else if (std::strcmp(line, "GET STATES") == 0) {
    send_states();
  } else if (std::strcmp(line, "GET SUBTITLE") == 0) {
    send_line("SUBTITLE:", site_subtitle);
  } else if (std::strcmp(line, "PING") == 0) {
    uart_puts(UART, "PONG\n");
  } else if (std::strncmp(line, "VER:", 4) == 0) {
    esp_fw_version = line + 4;
  } else if (handle_scene_command(line)) {
    printf("ESP-UART: Szene aktiviert\n");
  } else if (handle_switch_command(line)) {
    printf("ESP-UART: Schalter gesetzt\n");
  } else {
    printf("ESP-UART: unbekanntes Kommando '%s'\n", line);
  }
}

static void init() {
  uart_init(UART, UART_BAUD);
  gpio_set_function(UART_TX_PIN, GPIO_FUNC_UART);
  gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);
  uart_set_hw_flow(UART, false, false);
  uart_set_format(UART, 8, 1, UART_PARITY_NONE);
  uart_set_fifo_enabled(UART, true);
}

static void flush_rx() {
  while (uart_is_readable(UART)) (void)uart_getc(UART);
  rx_len = 0;
}

static void service() {
  while (uart_is_readable(UART)) {
    char c = uart_getc(UART);
    if (c == '\r') continue;
    if (c == '\n') {
      rx_buf[rx_len] = '\0';
      if (rx_len > 0) handle_line(rx_buf);
      rx_len = 0;
      continue;
    }
    if (rx_len < sizeof(rx_buf) - 1) {
      rx_buf[rx_len++] = c;
    } else {
      rx_len = 0;  // overflow -> drop
    }
  }

  if (esp_link_state_dirty) {
    esp_link_state_dirty = false;
    send_states();
  }

  if (esp_link_display_dirty) {
    esp_link_display_dirty = false;
    esp_link_state_dirty = false;
    send_display_config();
  }
}

// Sendet die aktuelle IP-/Link-Statuszeile ans ESP-Display.
static void notify_ip_status() {
  uart_puts(UART, ("IP:" + ip_status_text() + "\n").c_str());
}
}  // namespace esp_link

static void open_http_sockets() {
  for (uint8_t sn = 0; sn < cfg::HTTP_SOCKET_COUNT; ++sn) {
    force_close_socket(sn);  // nicht-blockierend: kein Warten auf FIN/TCP-Timeout
    socket(sn, Sn_MR_TCP, cfg::HTTP_PORT, 0x00);
    listen(sn);
  }
}

// Nach LAN-Wiederkehr Netzwerk und HTTP-Sockets neu aufsetzen.
static void reconnect_network() {
  printf("LAN-Link wieder da: Netzwerk neu initialisieren ...\n");
  if (g_use_dhcp) acquire_dhcp_address();  // neue Lease/IP holen, bedient dabei das ESP-Display
  else network_initialize(g_net_info);
  open_http_sockets();
}

// Prueft periodisch den PHY-Link, meldet Aenderungen ans Display und reconnectet.
static void service_network_link() {
  uint32_t now = millis32();
  if (now - last_link_check < 1000) return;
  last_link_check = now;
  bool up = phy_link_up();
  if (up == lan_link_up) return;
  lan_link_up = up;
  if (up) {
    if (g_use_dhcp) dhcp_assigned = false;  // bis zur neuen Lease "connecting LAN" anzeigen
    g_ip_status_dirty = true;               // Zwischenstand ans Display (core0 sendet)
    reconnect_network();                    // neue DHCP-Lease/Sockets holen
    g_ip_status_dirty = true;               // finale IP nach Reconnect ans Display pushen
  } else {
    g_ip_status_dirty = true;               // "LAN connection lost" (core0 sendet)
    for (uint8_t sn = 0; sn < cfg::HTTP_SOCKET_COUNT; ++sn) force_close_socket(sn);  // haengende Verbindungen sofort verwerfen (ohne Blockieren)
  }
}


// ===========================================================================
// core1: Netzwerk (W6300, HTTP, DHCP, SSE) + Flash-Schreiben.
// Laeuft voellig entkoppelt vom Relais-/ESP-Steuerpfad auf core0. Blockierende
// WIZnet-Aufrufe hier koennen das Schalten der Relais niemals verzoegern.
// WICHTIG: core1 fasst NIE die ESP-UART an (uart0 gehoert core0); IP-Status
// wird ueber g_ip_status_dirty an core0 delegiert.
// ===========================================================================
static void net_core_main() {
  init_network();
  esp_link_display_dirty = true;  // ESP nach DHCP mit korrekter IP versorgen (core0 sendet)
  g_ip_status_dirty = true;
  open_http_sockets();
  printf("HTTP-Server: http://%u.%u.%u.%u/\n", g_net_info.ip[0], g_net_info.ip[1], g_net_info.ip[2], g_net_info.ip[3]);
  while (true) {
    for (uint8_t sn = 0; sn < cfg::HTTP_SOCKET_COUNT; ++sn) service_socket(sn);
    keepalive_sse();
    service_network_link();
    if (g_persist_dirty) {  // Flash-Schreiben nur hier (core0 ist Lockout-Victim)
      g_persist_dirty = false;
      save_config();
    }
    if (g_sse_dirty) {  // SSE-Broadcast nur hier (W6300 nur auf core1)
      g_sse_dirty = false;
      broadcast_state();
    }
    sleep_ms(1);
  }
}

int main() {
  esp_link::init();  // UART sofort auf Idle-High treiben, bevor der ESP booten kann
  stdio_init_all();
  printf("OE5RNL> W6300 Relay Webserver startet\n");
  random_state ^= time_us_32();
  init_relays();
#ifdef PERSIST_WIPE
  wipe_persist_slots();  // Einmal-Wipe (mit -DPERSIST_WIPE=ON gebaut): danach ohne Flag neu flashen
#endif
  ConfigLoadResult config_load_result = load_config();
  configure_feedback_inputs();
  init_users();
  if (config_load_result == ConfigLoadResult::Empty) {
    printf("Keine gespeicherte Konfiguration gefunden, Defaults nur im RAM aktiv.\n");
  } else if (config_load_result == ConfigLoadResult::LayoutChanged) {
    persist_write_locked = true;
    printf("Persistenz-Datenstruktur geaendert oder ungueltig, Flash-Daten werden nicht ueberschrieben.\n");
  }
  for (uint8_t i = 0; i < cfg::RELAY_COUNT; ++i) apply_relay(i);
  g_use_dhcp = dhcp_requested_at_boot();  // Netzwerkmodus frueh bestimmen (fuer IP-Anzeige am ESP)
  // Ab hier stehen Titel/Namen/Zustaende fest: ESP-Display sofort bedienen (vor DHCP)
  esp_link::flush_rx();

  // Synchronisation + Flash-Schutz vorbereiten, dann core1 (Netzwerk) starten.
  recursive_mutex_init(&g_state_mtx);
  flash_safe_execute_core_init();  // core0 als Lockout-Victim registrieren (core1 schreibt Flash)
  g_core1_started = true;
  multicore_launch_core1(net_core_main);

  // core0: reiner Steuer-Loop. Beruehrt niemals W6300/Flash -> nie blockiert.
  // Das Schalten der Relais funktioniert damit unabhaengig vom LAN-Zustand.
  while (true) {
    esp_link::service();
    if (g_ip_status_dirty) {  // IP-/Link-Status von core1 gemeldet -> ans Display senden
      g_ip_status_dirty = false;
      esp_link::notify_ip_status();
    }
#ifdef INPUT_MODE_BUTTON
    if (service_buttons()) {
      g_sse_dirty = true;
    }
#else
    if (service_relay_feedback()) {
      esp_link_state_dirty = true;
      g_sse_dirty = true;
    }
#endif
    sleep_ms(1);
  }
}
