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
constexpr uint8_t MAX_RELAIS = 8;    // konfigurierbare Relais (je Typ einfach/4-fach)
constexpr uint8_t MAX_BUTTONS = 8;   // logische Bedien-Buttons (Web + ESP-Display)
constexpr uint8_t SCENE_COUNT = 8;
constexpr uint8_t MAX_OUTPUTS = 4;   // Ausgaenge je Relais (einfach nutzt 1, 4-fach 4)
// Ausgangs-Pool (frei je Relais-Ausgang waehlbar) und fest gepaarter Eingangs-Pool
// (Rueckmeldung ODER Taster): OUTPUT_PINS[i] <-> INPUT_PINS[i].
constexpr std::array<uint8_t, 8> OUTPUT_PINS = {2, 3, 4, 5, 6, 7, 8, 9};
constexpr std::array<uint8_t, 8> INPUT_PINS = {10, 11, 12, 13, 14, 26, 27, 28};
constexpr uint32_t DEFAULT_FEEDBACK_TIMEOUT_MS = 500;
constexpr uint32_t MIN_FEEDBACK_TIMEOUT_MS = 10;
constexpr uint32_t MAX_FEEDBACK_TIMEOUT_MS = 10000;
constexpr uint32_t DEFAULT_DEBOUNCE_MS = 25;   // Taster-Entprellzeit
constexpr uint32_t MIN_DEBOUNCE_MS = 5;
constexpr uint32_t MAX_DEBOUNCE_MS = 2000;
constexpr uint16_t DEFAULT_IMPULSE_MS = 300;
constexpr uint16_t MIN_IMPULSE_MS = 100;
constexpr uint16_t MAX_IMPULSE_MS = 2000;
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
constexpr uint32_t PERSIST_VERSION = 9;
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

// Ein Relais im Flash. Je Ausgang k: gewaehlte Ausgangs-GPIO (out_gpio, -1=frei),
// Eingangs-Rolle (in_role: 0=keine, 1=Rueckmeldung, 2=Taster) und Rueckmelde-
// Polaritaet. Der Eingangs-GPIO wird beim Laden aus out_gpio abgeleitet.
struct PersistedRelais {
  uint8_t enabled;
  uint8_t type;          // 0=einfach, 1=4-fach
  char name[33];
  uint8_t active_low;    // Ausgangspolaritaet
  uint8_t impulse;
  uint16_t impulse_ms;
  int8_t out_gpio[cfg::MAX_OUTPUTS];
  uint8_t in_role[cfg::MAX_OUTPUTS];
  uint8_t in_active_low[cfg::MAX_OUTPUTS];
  uint8_t active_output; // 0=aus/keiner, 1..4=aktiver Ausgang
};

struct PersistedButton {
  uint8_t enabled;
  char name[33];
  int8_t relais_idx;     // -1 = keine Bindung
  uint8_t input_idx;     // 0 (einfach) bzw. 0..3 (4-fach)
};

struct PersistedConfig {
  uint32_t magic;
  uint32_t version;
  uint8_t public_access;
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
  uint8_t scene_action[cfg::SCENE_COUNT][cfg::MAX_BUTTONS];  // je Button 0=aus,1=ein,2=unveraendert
  uint8_t active_scene;
  uint32_t feedback_timeout_ms;
  uint32_t taster_debounce_ms;
  PersistedRelais relais[cfg::MAX_RELAIS];
  PersistedButton buttons[cfg::MAX_BUTTONS];
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

// Szene: pro Button 0=aus, 1=ein, 2=unveraendert. Nur aktivierte Szenen sind bedienbar.
struct Scene {
  bool enabled = false;
  std::string name;
  std::array<uint8_t, cfg::MAX_BUTTONS> action = {2, 2, 2, 2, 2, 2, 2, 2};
};

enum class RelayType : uint8_t { Simple = 0, Quad = 1, Dual = 2 };
enum class InRole : uint8_t { None = 0, Feedback = 1, Taster = 2 };

// Physisches Relais. out_gpio (vom Nutzer gewaehlt) wird persistiert; in_gpio wird
// daraus via input_for_output() abgeleitet. active_output ist der (gelatchte)
// Anzeige-/Steuerzustand: 0=aus/keiner, 1..4=aktiver Ausgang.
struct Relais {
  bool enabled = false;
  RelayType type = RelayType::Simple;
  std::string name;
  bool impulse = false;
  uint16_t impulse_ms = cfg::DEFAULT_IMPULSE_MS;
  bool active_low = false;
  int8_t out_gpio[cfg::MAX_OUTPUTS] = {-1, -1, -1, -1};
  uint8_t in_role[cfg::MAX_OUTPUTS] = {0, 0, 0, 0};       // InRole
  bool in_active_low[cfg::MAX_OUTPUTS] = {false, false, false, false};
  // abgeleitet / Laufzeit (nicht persistiert):
  int8_t in_gpio[cfg::MAX_OUTPUTS] = {-1, -1, -1, -1};
  bool valid = false;                                      // GPIOs aufgeloest, keine Doppelbelegung
  uint8_t active_output = 0;
  bool imp_active[cfg::MAX_OUTPUTS] = {};
  uint32_t imp_deadline[cfg::MAX_OUTPUTS] = {};
  bool fb_expected[cfg::MAX_OUTPUTS] = {};                 // erwarteter Rueckmeldezustand je Ausgang
  bool fb_pending[cfg::MAX_OUTPUTS] = {};
  bool fb_error[cfg::MAX_OUTPUTS] = {};
  uint32_t fb_deadline[cfg::MAX_OUTPUTS] = {};
  int8_t fb_source_scene[cfg::MAX_OUTPUTS] = {-1, -1, -1, -1};
  bool btn_raw[cfg::MAX_OUTPUTS] = {};                     // Taster-Entprellung je Ausgang
  bool btn_pressed[cfg::MAX_OUTPUTS] = {};
  uint32_t btn_since[cfg::MAX_OUTPUTS] = {};
};

// Logischer Bedien-Button (Web + ESP-Display). Verweist auf einen Relais-Eingang.
struct Button {
  bool enabled = false;
  std::string name;
  int8_t relais_idx = -1;
  uint8_t input_idx = 0;
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

static uint8_t outputs_count(const Relais &rl);  // Vorwaerts-Deklaration (load_config nutzt sie frueh)

static std::array<Relais, cfg::MAX_RELAIS> relais{};
static std::array<Button, cfg::MAX_BUTTONS> buttons{};
static uint32_t feedback_timeout_ms = cfg::DEFAULT_FEEDBACK_TIMEOUT_MS;
static uint32_t taster_debounce_ms = cfg::DEFAULT_DEBOUNCE_MS;
static std::array<bool, cfg::SCENE_COUNT> scene_feedback_error{};
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
  config.feedback_timeout_ms = feedback_timeout_ms;
  config.taster_debounce_ms = taster_debounce_ms;
  config.public_access = public_access ? 1 : 0;
  copy_cstr(config.site_title, sizeof(config.site_title), site_title);
  copy_cstr(config.site_subtitle, sizeof(config.site_subtitle), site_subtitle);
  for (uint8_t r = 0; r < cfg::MAX_RELAIS; ++r) {
    PersistedRelais &pr = config.relais[r];
    const Relais &rl = relais[r];
    pr.enabled = rl.enabled ? 1 : 0;
    pr.type = static_cast<uint8_t>(rl.type);
    copy_cstr(pr.name, sizeof(pr.name), rl.name);
    pr.active_low = rl.active_low ? 1 : 0;
    pr.impulse = rl.impulse ? 1 : 0;
    pr.impulse_ms = rl.impulse_ms;
    pr.active_output = rl.active_output;
    for (uint8_t k = 0; k < cfg::MAX_OUTPUTS; ++k) {
      pr.out_gpio[k] = rl.out_gpio[k];
      pr.in_role[k] = rl.in_role[k];
      pr.in_active_low[k] = rl.in_active_low[k] ? 1 : 0;
    }
  }
  for (uint8_t b = 0; b < cfg::MAX_BUTTONS; ++b) {
    PersistedButton &pb = config.buttons[b];
    const Button &bt = buttons[b];
    pb.enabled = bt.enabled ? 1 : 0;
    copy_cstr(pb.name, sizeof(pb.name), bt.name);
    pb.relais_idx = bt.relais_idx;
    pb.input_idx = bt.input_idx;
  }

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
    for (uint8_t b = 0; b < cfg::MAX_BUTTONS; ++b) config.scene_action[s][b] = scenes[s].action[b];
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
  for (uintptr_t offset : persist_flash_offsets()) {
    if (!persist_flash_offset_usable(offset)) continue;
    const PersistedConfig *config = persisted_config_ptr(offset);
    if (is_erased_flash(config, sizeof(PersistedConfig))) continue;
    if (config->magic != cfg::PERSIST_MAGIC || config->version != cfg::PERSIST_VERSION) {
      // Kein passendes Layout: bewusst KEINE Migration (Werksreset per -w vorgesehen).
      saw_invalid = true;
      continue;
    }
    printf("Konfiguration aus Persistenz-Slot 0x%08lx geladen.\n", static_cast<unsigned long>(offset));
    for (uint8_t r = 0; r < cfg::MAX_RELAIS; ++r) {
      const PersistedRelais &pr = config->relais[r];
      Relais &rl = relais[r];
      rl.enabled = pr.enabled != 0;
      rl.type = pr.type == static_cast<uint8_t>(RelayType::Quad) ? RelayType::Quad
                : pr.type == static_cast<uint8_t>(RelayType::Dual) ? RelayType::Dual
                : RelayType::Simple;
      if (pr.name[0]) rl.name = pr.name;
      rl.active_low = pr.active_low != 0;
      rl.impulse = pr.impulse != 0;
      rl.impulse_ms = std::clamp<uint16_t>(pr.impulse_ms, cfg::MIN_IMPULSE_MS, cfg::MAX_IMPULSE_MS);
      rl.active_output = pr.active_output <= outputs_count(rl) ? pr.active_output : 0;
      for (uint8_t k = 0; k < cfg::MAX_OUTPUTS; ++k) {
        rl.out_gpio[k] = pr.out_gpio[k];
        rl.in_role[k] = pr.in_role[k] <= static_cast<uint8_t>(InRole::Taster) ? pr.in_role[k] : 0;
        rl.in_active_low[k] = pr.in_active_low[k] != 0;
      }
    }
    for (uint8_t b = 0; b < cfg::MAX_BUTTONS; ++b) {
      const PersistedButton &pb = config->buttons[b];
      Button &bt = buttons[b];
      bt.enabled = pb.enabled != 0;
      if (pb.name[0]) bt.name = pb.name;
      bt.relais_idx = (pb.relais_idx >= 0 && pb.relais_idx < cfg::MAX_RELAIS) ? pb.relais_idx : -1;
      bt.input_idx = pb.input_idx < cfg::MAX_OUTPUTS ? pb.input_idx : 0;
    }
    feedback_timeout_ms = std::clamp<uint32_t>(config->feedback_timeout_ms,
                                                cfg::MIN_FEEDBACK_TIMEOUT_MS, cfg::MAX_FEEDBACK_TIMEOUT_MS);
    taster_debounce_ms = std::clamp<uint32_t>(config->taster_debounce_ms,
                                              cfg::MIN_DEBOUNCE_MS, cfg::MAX_DEBOUNCE_MS);
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
      for (uint8_t b = 0; b < cfg::MAX_BUTTONS; ++b) {
        uint8_t a = config->scene_action[s][b];
        scenes[s].action[b] = (a <= 2) ? a : 2;
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

// Anzahl genutzter Ausgaenge/Eingaenge eines Relais (einfach=1, 2-fach=2, 4-fach=4).
static uint8_t outputs_count(const Relais &rl) {
  switch (rl.type) {
    case RelayType::Quad: return 4;
    case RelayType::Dual: return 2;
    default: return 1;
  }
}

// Index einer Ausgangs-GPIO im OUTPUT_PINS-Pool (-1 = kein Pool-Pin).
static int output_pool_index(int gpio) {
  if (gpio < 0) return -1;
  for (size_t i = 0; i < cfg::OUTPUT_PINS.size(); ++i)
    if (cfg::OUTPUT_PINS[i] == static_cast<uint8_t>(gpio)) return static_cast<int>(i);
  return -1;
}

// Fest gepaarter Eingangs-GPIO zu einer Ausgangs-GPIO (-1 = keiner).
static int input_for_output(int out_gpio) {
  const int idx = output_pool_index(out_gpio);
  return idx < 0 ? -1 : static_cast<int>(cfg::INPUT_PINS[idx]);
}

// Ausgangspegel je Polaritaet.
static uint8_t output_gpio_value(const Relais &rl, bool on) {
  if (rl.active_low) return on ? 0 : 1;
  return on ? 1 : 0;
}

static void apply_output(uint8_t r, uint8_t k, bool on) {
  if (relais[r].out_gpio[k] < 0) return;
  gpio_put(static_cast<uint>(relais[r].out_gpio[k]), output_gpio_value(relais[r], on));
}

// Ist der von Button b referenzierte Relais-Eingang aktiv?
static bool button_is_on(uint8_t b) {
  const Button &bt = buttons[b];
  if (!bt.enabled || bt.relais_idx < 0 || bt.relais_idx >= cfg::MAX_RELAIS) return false;
  const Relais &rl = relais[bt.relais_idx];
  return rl.active_output == static_cast<uint8_t>(bt.input_idx + 1);
}

// Rueckmeldung eines Ausgangs (nur wenn Rolle==Feedback) gegen den erwarteten Zustand.
static bool feedback_matches(uint8_t r, uint8_t k) {
  const Relais &rl = relais[r];
  if (rl.in_gpio[k] < 0) return true;
  const bool input_high = gpio_get(static_cast<uint>(rl.in_gpio[k])) != 0;
  const bool fb_on = rl.in_active_low[k] ? !input_high : input_high;
  return fb_on == rl.fb_expected[k];
}

static void update_scene_feedback_errors() {
  scene_feedback_error.fill(false);
  for (uint8_t r = 0; r < cfg::MAX_RELAIS; ++r) {
    for (uint8_t k = 0; k < cfg::MAX_OUTPUTS; ++k) {
      const int8_t scene = relais[r].fb_source_scene[k];
      if (relais[r].fb_error[k] && scene >= 0 && scene < cfg::SCENE_COUNT) scene_feedback_error[scene] = true;
    }
  }
}

// Startet die Rueckmeldepruefung eines Ausgangs gegen expected_on.
static void start_feedback_check(uint8_t r, uint8_t k, bool expected_on, int8_t source_scene) {
  Relais &rl = relais[r];
  rl.fb_expected[k] = expected_on;
  rl.fb_source_scene[k] = source_scene;
  rl.fb_error[k] = false;
  if (rl.in_role[k] != static_cast<uint8_t>(InRole::Feedback) || feedback_matches(r, k)) {
    rl.fb_pending[k] = false;
    rl.fb_source_scene[k] = -1;
  } else {
    rl.fb_pending[k] = true;
    rl.fb_deadline[k] = millis32() + feedback_timeout_ms;
  }
  update_scene_feedback_errors();
}

// Setzt Ausgang k statisch oder startet (on && Impuls) einen Impuls. Unter StateLock.
static void drive_output_locked(uint8_t r, uint8_t k, bool on, int8_t source_scene) {
  Relais &rl = relais[r];
  if (on && rl.impulse) {
    apply_output(r, k, true);
    rl.imp_active[k] = true;
    rl.imp_deadline[k] = millis32() + rl.impulse_ms;
    rl.fb_pending[k] = false;  // waehrend des Impulses keine Rueckmeldepruefung
    rl.fb_error[k] = false;
    rl.fb_expected[k] = true;  // Latch bleibt "ein"
    rl.fb_source_scene[k] = -1;
    update_scene_feedback_errors();
  } else {
    apply_output(r, k, on);
    rl.imp_active[k] = false;  // laufenden Impuls bei explizitem Setzen abbrechen
    start_feedback_check(r, k, on, source_scene);
  }
}

// einfach: Ausgang 0 statisch/gepulst auf on setzen; active_output latchen.
static void relais_set_simple(uint8_t r, bool on, int8_t source_scene) {
  relais[r].active_output = on ? 1 : 0;
  drive_output_locked(r, 0, on, source_scene);
}

// 4-fach: Ausgang k anwaehlen (nur diesen schalten, Geschwister nur logisch aus).
// Rueckmelde-/Impuls-Laufzeit der Geschwister wird zurueckgesetzt (nur der aktive
// Ausgang wird ueberwacht).
static void relais_select_quad(uint8_t r, uint8_t k, int8_t source_scene) {
  Relais &rl = relais[r];
  for (uint8_t j = 0; j < outputs_count(rl); ++j) {
    if (j == k) continue;
    rl.imp_active[j] = false;
    rl.fb_pending[j] = false;
    rl.fb_error[j] = false;
    rl.fb_source_scene[j] = -1;
  }
  rl.active_output = static_cast<uint8_t>(k + 1);
  drive_output_locked(r, k, true, source_scene);
}

static bool scene_state_matches(uint8_t idx);

// Fuehrt eine Button-Aktion aus (0=aus, 1=ein, 2=umschalten). Unter StateLock.
static void apply_button_locked(uint8_t b, int action) {
  Button &bt = buttons[b];
  if (!bt.enabled || bt.relais_idx < 0 || bt.relais_idx >= cfg::MAX_RELAIS) return;
  Relais &rl = relais[bt.relais_idx];
  if (!rl.valid || !rl.enabled) return;
  const uint8_t k = bt.input_idx;
  if (k >= outputs_count(rl)) return;
  if (rl.type == RelayType::Simple) {
    const bool on = action == 2 ? (rl.active_output != 1) : (action == 1);
    relais_set_simple(bt.relais_idx, on, -1);
  } else {
    const bool currently = (rl.active_output == static_cast<uint8_t>(k + 1));
    const bool on = action == 2 ? !currently : (action == 1);
    if (on) {
      relais_select_quad(bt.relais_idx, k, -1);
    } else if (currently) {
      rl.active_output = 0;
      drive_output_locked(bt.relais_idx, k, false, -1);
    }
  }
}

// Zentrale Button-Aktion (Web /relay, ESP SWn). Setzt Dirty-Flags fuer core1.
static void button_command(uint8_t b, int action) {
  {
    StateLock lock;
    apply_button_locked(b, action);
    if (scene_mode && active_scene >= 0) scene_dirty = !scene_state_matches(active_scene);
    else active_scene = -1;
  }
  g_persist_dirty = true;
  esp_link_state_dirty = true;
  g_sse_dirty = true;
}

static void press_button(uint8_t b) { button_command(b, 2); }

static bool service_relay_feedback() {
  StateLock lock;
  bool changed = false;
  const uint32_t now = millis32();
  for (uint8_t r = 0; r < cfg::MAX_RELAIS; ++r) {
    Relais &rl = relais[r];
    if (!rl.valid || !rl.enabled) continue;
    for (uint8_t k = 0; k < outputs_count(rl); ++k) {
      if (rl.in_role[k] != static_cast<uint8_t>(InRole::Feedback)) continue;
      // 2-/4-fach: nur den aktiven Ausgang ueberwachen (Geschwister sind bistabil/unverwaltet).
      if (rl.type != RelayType::Simple && rl.active_output != static_cast<uint8_t>(k + 1)) continue;
      if (feedback_matches(r, k)) {
        if (rl.fb_pending[k] || rl.fb_error[k]) changed = true;
        rl.fb_pending[k] = false;
        rl.fb_error[k] = false;
        rl.fb_source_scene[k] = -1;
      } else if (rl.fb_pending[k] && static_cast<int32_t>(now - rl.fb_deadline[k]) >= 0) {
        rl.fb_pending[k] = false;
        rl.fb_error[k] = true;
        changed = true;
      }
    }
  }
  if (changed) update_scene_feedback_errors();
  return changed;
}

// Prueft, ob die aktuellen Button-Zustaende der Szenenvorgabe entsprechen
// ("unveraendert"/Aktion 2 wird ignoriert).
static bool scene_state_matches(uint8_t idx) {
  for (uint8_t b = 0; b < cfg::MAX_BUTTONS; ++b) {
    const uint8_t a = scenes[idx].action[b];
    if (a == 2) continue;
    if (button_is_on(b) != (a == 1)) return false;
  }
  return true;
}

// Laeuft auf core0: beendet abgelaufene Impulse. Der Ausgang geht auf Idle, die
// Anzeige (active_output) bleibt gelatcht. Rueckgabe: Zustand geaendert.
static bool service_impulses() {
  StateLock lock;
  bool changed = false;
  const uint32_t now = millis32();
  for (uint8_t r = 0; r < cfg::MAX_RELAIS; ++r) {
    Relais &rl = relais[r];
    if (!rl.valid || !rl.enabled) continue;
    for (uint8_t k = 0; k < outputs_count(rl); ++k) {
      if (rl.imp_active[k] && static_cast<int32_t>(now - rl.imp_deadline[k]) >= 0) {
        rl.imp_active[k] = false;
        apply_output(r, k, false);  // Impuls-GPIO auf Idle, Latch bleibt
        const bool latched_on = (rl.active_output == static_cast<uint8_t>(k + 1));
        start_feedback_check(r, k, latched_on, -1);
        changed = true;
      }
    }
  }
  return changed;
}

// Wendet eine (aktivierte) Szene momentan an. 1-fach-Relais werden getoggelt
// (jede in der Szene enthaltene Aktion != "unveraendert" = ein Tastendruck),
// 2-/4-fach waehlen bei "ein" ihren Ausgang; "unveraendert" wird uebersprungen.
static void activate_scene(uint8_t idx) {
  bool persist = false;
  {
    StateLock lock;
    if (idx >= cfg::SCENE_COUNT || !scenes[idx].enabled) return;
    const int prev_scene = active_scene;
    bool changed = false;
    for (uint8_t b = 0; b < cfg::MAX_BUTTONS; ++b) {
      const uint8_t a = scenes[idx].action[b];
      if (a == 2) continue;
      Button &bt = buttons[b];
      if (!bt.enabled || bt.relais_idx < 0 || bt.relais_idx >= cfg::MAX_RELAIS) continue;
      Relais &rl = relais[bt.relais_idx];
      if (!rl.valid || !rl.enabled) continue;
      const uint8_t k = bt.input_idx;
      if (k >= outputs_count(rl)) continue;
      const bool on = (a == 1);
      if (rl.type == RelayType::Simple) {  // Toggle wie ein Tastendruck (Aus/Ein egal)
        relais_set_simple(bt.relais_idx, rl.active_output != 1, static_cast<int8_t>(idx));
        changed = true;
      } else if (on) {  // 2-/4-fach: nur "ein" waehlt einen Ausgang, "aus" ist bedeutungslos
        relais_select_quad(bt.relais_idx, k, static_cast<int8_t>(idx));
        changed = true;
      }
    }
    // Szene nur als "aktiv" markieren, wenn der Zustand jetzt ihrer Vorgabe entspricht.
    // So folgt die Button-Anzeige einem 1-fach-Toggle (nach AUS nicht mehr grün).
    active_scene = scene_state_matches(idx) ? static_cast<int>(idx) : -1;
    scene_dirty = false;
    persist = changed || (active_scene != prev_scene);
  }
  if (persist) g_persist_dirty = true;
  esp_link_state_dirty = true;
  g_sse_dirty = true;
}

// Laeuft auf core0: liest je Ausgang mit Rolle==Taster den Eingang (Pull-up,
// gedrueckt=LOW), entprellt und loest bei steigender Flanke den Relais-Eingang aus.
// Rueckgabe: Anzeige-/Zustandsaenderung (fuer SSE).
static bool service_tasters() {
  StateLock lock;
  const uint32_t now = millis32();
  const uint32_t debounce = taster_debounce_ms;
  bool changed = false;
  for (uint8_t r = 0; r < cfg::MAX_RELAIS; ++r) {
    Relais &rl = relais[r];
    if (!rl.valid || !rl.enabled) continue;
    for (uint8_t k = 0; k < outputs_count(rl); ++k) {
      if (rl.in_role[k] != static_cast<uint8_t>(InRole::Taster) || rl.in_gpio[k] < 0) continue;
      const bool raw = gpio_get(static_cast<uint>(rl.in_gpio[k])) == 0;  // Pull-up: gedrueckt = LOW
      if (raw != rl.btn_raw[k]) {
        rl.btn_raw[k] = raw;
        rl.btn_since[k] = now;
      } else if (raw != rl.btn_pressed[k] &&
                 static_cast<int32_t>(now - rl.btn_since[k]) >= static_cast<int32_t>(debounce)) {
        rl.btn_pressed[k] = raw;
        changed = true;
        if (raw) {  // steigende Flanke: gleiche Aktion wie der logische Button
          if (rl.type == RelayType::Simple) relais_set_simple(r, rl.active_output != 1, -1);
          else relais_select_quad(r, k, -1);
          if (scene_mode && active_scene >= 0) scene_dirty = !scene_state_matches(active_scene);
          else active_scene = -1;
          esp_link_state_dirty = true;
          g_persist_dirty = true;
        }
      }
    }
  }
  return changed;
}

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

// Zustand des von Button b referenzierten Ausgangs: Rueckmeldefehler bzw. Tasterdruck.
static bool button_feedback_error(uint8_t b) {
  const Button &bt = buttons[b];
  if (!bt.enabled || bt.relais_idx < 0 || bt.relais_idx >= cfg::MAX_RELAIS) return false;
  return relais[bt.relais_idx].fb_error[bt.input_idx];
}
static bool button_taster_pressed(uint8_t b) {
  const Button &bt = buttons[b];
  if (!bt.enabled || bt.relais_idx < 0 || bt.relais_idx >= cfg::MAX_RELAIS) return false;
  return relais[bt.relais_idx].btn_pressed[bt.input_idx];
}

static std::string state_json(bool include_users) {
  prune_sessions();
  StateLock lock;  // geteilten Steuerzustand konsistent lesen; Kopie wird ohne Lock gesendet
  std::string out = "{\"relays\":[";
  for (uint8_t b = 0; b < cfg::MAX_BUTTONS; ++b) {
    if (b) out += ',';
    out += button_is_on(b) ? "true" : "false";
  }
  out += "],\"names\":[";
  for (uint8_t b = 0; b < cfg::MAX_BUTTONS; ++b) {
    if (b) out += ',';
    out += '"' + json_escape(buttons[b].name) + '"';
  }
  out += "],\"btn_en\":[";
  for (uint8_t b = 0; b < cfg::MAX_BUTTONS; ++b) {
    if (b) out += ',';
    out += buttons[b].enabled ? "true" : "false";
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
  for (uint8_t b = 0; b < cfg::MAX_BUTTONS; ++b) {
    if (b) out += ',';
    out += button_feedback_error(b) ? "true" : "false";
  }
  out += "],\"scene_errors\":[";
  for (size_t i = 0; i < scene_feedback_error.size(); ++i) {
    if (i) out += ',';
    out += scene_feedback_error[i] ? "true" : "false";
  }
  out += "],\"buttons\":[";
  for (uint8_t b = 0; b < cfg::MAX_BUTTONS; ++b) {
    if (b) out += ',';
    out += button_taster_pressed(b) ? "true" : "false";
  }
  out += ']';
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
    for (uint8_t b = 0; b < cfg::MAX_BUTTONS; ++b) {
      if (b) out += ',';
      out += std::to_string(scenes[s].action[b]);
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

// Einheitliche horizontale Menueleiste (auf allen Admin-Seiten identisch). Der
// Speichern-Button ruft die seiteneigene save()-Funktion auf; der Button der
// aktuellen Seite (active = "config"/"relais"/"scenes") wird rot hervorgehoben.
static std::string page_nav_actions(const char *active) {
  auto red = [&](const char *key) {
    return std::string(active && std::strcmp(active, key) == 0 ? " style=\"color:#ff6b6b\"" : "");
  };
  return "<div class=\"actions\">"
         "<button onclick=\"location.href='/'\">Uebersicht</button>"
         "<button class=\"save\" onclick=\"save()\">Speichern</button>"
         "<button" + red("config") + " onclick=\"location.href='/config'\">Buttons</button>"
         "<button" + red("relais") + " onclick=\"location.href='/relais'\">Relais</button>"
         "<button" + red("scenes") + " onclick=\"location.href='/scenes'\">Szenen</button>"
         "<button onclick=\"location.href='/admin'\">Benutzer/API</button>"
         "<button onclick=\"location.href='/network'\">Network</button>"
         "<button class=\"danger\" onclick=\"location.href='/logout'\">Abmelden</button>"
         "</div>";
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

// Gemeinsames SSE-Verbindungsskript (Conn-Anzeige) fuer die Konfig-Seiten.
static const char *sse_conn_script() {
  return "const conn=document.getElementById('conn');let lastSseActivity=0;let lastConnectAttempt=0;let es=null;let reconnectTimer=0;function markConn(ok){if(!conn)return;conn.textContent=ok?'Verbunden':'Getrennt';conn.className=ok?'ok':'err'}function noteSseActivity(){lastSseActivity=Date.now();markConn(true)}function scheduleReconnect(delay=1000){if(reconnectTimer)return;reconnectTimer=setTimeout(()=>{reconnectTimer=0;connectEvents()},delay)}function forceReconnect(){if(es){es.close();es=null}scheduleReconnect(0)}function connectEvents(){if(es)es.close();lastConnectAttempt=Date.now();es=new EventSource('/events');es.onopen=()=>{noteSseActivity()};es.onerror=()=>{markConn(false);if(es){es.close();es=null}scheduleReconnect()};es.addEventListener('ping',()=>{noteSseActivity()});es.onmessage=()=>{noteSseActivity()}}connectEvents();window.addEventListener('pagehide',()=>{if(es)es.close()});setInterval(()=>{const now=Date.now();const activeLink=lastSseActivity&&now-lastSseActivity<2500;markConn(!!activeLink);if(!activeLink&&now-lastConnectAttempt>=2500&&!reconnectTimer)forceReconnect()},1000);";
}

static void resolve_gpios();
static void configure_inputs();
static void apply_all_outputs();
static void broadcast_state();
static std::vector<std::string> json_string_list(const std::string &body, const char *name, size_t maxlen);
static std::vector<bool> json_bool_list(const std::string &body, const char *name);
static std::vector<int> json_int_list(const std::string &body, const char *name);
static uint32_t json_uint_value(const std::string &body, const char *name, uint32_t fallback);

// JSON-Daten der zuweisbaren Relais-Eingaenge (nur aktive/gueltige Relais).
static std::string relais_options_json() {
  std::string out = "[";
  bool first = true;
  for (uint8_t r = 0; r < cfg::MAX_RELAIS; ++r) {
    const Relais &rl = relais[r];
    if (!rl.enabled || !rl.valid) continue;
    if (!first) out += ',';
    first = false;
    out += "{\"i\":" + std::to_string(r) + ",\"name\":\"" + json_escape(rl.name) + "\",\"n\":" + std::to_string(outputs_count(rl)) + "}";
  }
  out += "]";
  return out;
}

// Ausgaenge-Seite: legt die 1-8 Buttons fest (Aktiv, Name, zugewiesener Relais-Eingang).
static std::string build_config_html(const Session *session) {
  std::string btnData = "[";
  for (uint8_t b = 0; b < cfg::MAX_BUTTONS; ++b) {
    if (b) btnData += ',';
    const Button &bt = buttons[b];
    btnData += "{\"en\":" + std::string(bt.enabled ? "true" : "false") + ",\"name\":\"" + json_escape(bt.name) +
               "\",\"rel\":" + std::to_string(bt.relais_idx) + ",\"in\":" + std::to_string(bt.input_idx) + "}";
  }
  btnData += "]";
  std::string html = "<!DOCTYPE html><html lang=\"de\"><head><meta charset=\"UTF-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><title>" + html_escape(site_title) + " - Buttons</title><style>html,body{margin:0}body{box-sizing:border-box;font-family:'Segoe UI',sans-serif;background:#121212;color:#e0e0e0;min-height:100vh;padding:108px 24px 24px}" + page_header_css() + "h1{color:#4dabf7}.card{background:#1e1e1e;padding:24px;max-width:980px;border:1px solid #2d2d2d}.row{display:flex;gap:12px;margin-bottom:12px;align-items:center}.row>label:first-child{width:160px;color:#9e9e9e;white-space:nowrap}.row>input{flex:1;min-width:180px;background:#2a2a2a;color:#e0e0e0;border:1px solid #333;padding:8px}.brow{display:flex;flex-wrap:wrap;gap:10px;align-items:center;border-top:1px solid #2d2d2d;padding-top:10px;margin-bottom:6px}.brow .bn{flex:none;width:80px;color:#9e9e9e}.brow .checklbl{flex:none;display:flex;align-items:center;gap:4px;color:#9e9e9e}.brow input[type=text]{flex:0 1 auto;min-width:120px;background:#2a2a2a;color:#e0e0e0;border:1px solid #333;padding:8px}.brow select{background:#2a2a2a;color:#e0e0e0;border:1px solid #333;padding:8px;min-width:200px}.actions button{margin:0}button{padding:10px 14px;margin:4px;background:#1a3a5c;color:#4dabf7;border:1px solid #1e5a9e;font-weight:700}.save{background:#1b4332;color:#51cf66;border-color:#2d6a4f}.danger{background:#3d1515;color:#ff6b6b;border-color:#7a2020}.ok{color:#51cf66}.err{color:#ff6b6b}</style></head><body>" + page_header_html(session, true) + "<h1>Buttons</h1>" + page_nav_actions("config") + "<div id=\"msg\"></div><div class=\"card\"><div class=\"row\"><label>Titel</label><input id=\"title\" maxlength=\"64\" value=\"" + html_escape(site_title) + "\"></div><div class=\"row\"><label>Ueberschrift</label><input id=\"subtitle\" maxlength=\"64\" value=\"" + html_escape(site_subtitle) + "\"></div><div class=\"row\"><label>Oeffentlich</label><input type=\"checkbox\" id=\"pub\" style=\"flex:none;width:auto;padding:0;margin:0;border:none;background:none\" " + std::string(public_access ? "checked" : "") + "></div><div id=\"btns\"></div></div><script>const relData=" + relais_options_json() + ";const btnData=" + btnData + ";const msg=document.getElementById('msg');" + std::string(sse_conn_script()) +
    "function optLabel(r,k){const base='Relais '+(r.i+1)+(r.name?' ('+r.name+')':'');return r.n>1?base+' \\u2013 Ausgang '+(k+1):base}"
    "function buildSelect(b){let s='<select data-b=\"'+b+'\"><option value=\"-1_0\">\\u2013 keine \\u2013</option>';relData.forEach(r=>{for(let k=0;k<r.n;k++){const v=r.i+'_'+k;const sel=(btnData[b].rel===r.i&&btnData[b].in===k)?' selected':'';s+='<option value=\"'+v+'\"'+sel+'>'+optLabel(r,k)+'</option>'}});return s+'</select>'}"
    "const wrap=document.getElementById('btns');let h='';for(let b=0;b<" + std::to_string(cfg::MAX_BUTTONS) + ";b++){h+='<div class=\"brow\"><span class=\"bn\">Button '+(b+1)+'</span><label class=\"checklbl\"><input type=\"checkbox\" data-en=\"'+b+'\"'+(btnData[b].en?' checked':'')+'> aktiv</label><input type=\"text\" maxlength=\"32\" data-name=\"'+b+'\" placeholder=\"Name\" value=\"\">'+buildSelect(b)+'</div>'}wrap.innerHTML=h;for(let b=0;b<" + std::to_string(cfg::MAX_BUTTONS) + ";b++){document.querySelector('input[data-name=\"'+b+'\"]').value=btnData[b].name}"
    "function save(){const en=[],names=[],rel=[],inp=[];for(let b=0;b<" + std::to_string(cfg::MAX_BUTTONS) + ";b++){en.push(document.querySelector('input[data-en=\"'+b+'\"]').checked);names.push(document.querySelector('input[data-name=\"'+b+'\"]').value.trim());const v=document.querySelector('select[data-b=\"'+b+'\"]').value.split('_');rel.push(+v[0]);inp.push(+v[1])}fetch('/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({btn_enabled:en,btn_names:names,btn_relais:rel,btn_input:inp,title:document.getElementById('title').value,subtitle:document.getElementById('subtitle').value,public:document.getElementById('pub').checked})}).then(r=>r.json()).then(()=>{msg.textContent='OK gespeichert';msg.className='ok';setTimeout(()=>{msg.textContent='';msg.className=''},2000);const nt=document.getElementById('title').value.trim();if(nt){const st=document.getElementById('sitetitle');if(st)st.textContent=nt;document.title=nt+' - Buttons'}}).catch(()=>{msg.textContent='Fehler';msg.className='err'})}"
    "</script></body></html>";
  return html;
}

// Relais-Seite: definiert die 1-8 Relais (Typ, Name, Impuls, je Ausgang GPIO + Eingangsrolle).
static std::string build_relais_html(const Session *session) {
  const std::string imp_min = std::to_string(cfg::MIN_IMPULSE_MS);
  const std::string imp_max = std::to_string(cfg::MAX_IMPULSE_MS);
  std::string outPins = "[";
  for (size_t i = 0; i < cfg::OUTPUT_PINS.size(); ++i) { if (i) outPins += ','; outPins += std::to_string(cfg::OUTPUT_PINS[i]); }
  outPins += "]";
  std::string inPins = "[";
  for (size_t i = 0; i < cfg::INPUT_PINS.size(); ++i) { if (i) inPins += ','; inPins += std::to_string(cfg::INPUT_PINS[i]); }
  inPins += "]";
  std::string relData = "[";
  for (uint8_t r = 0; r < cfg::MAX_RELAIS; ++r) {
    if (r) relData += ',';
    const Relais &rl = relais[r];
    relData += "{\"en\":" + std::string(rl.enabled ? "true" : "false") + ",\"type\":" + std::to_string(static_cast<int>(rl.type)) +
               ",\"name\":\"" + json_escape(rl.name) + "\",\"low\":" + std::string(rl.active_low ? "true" : "false") +
               ",\"imp\":" + std::string(rl.impulse ? "true" : "false") + ",\"impms\":" + std::to_string(rl.impulse_ms) + ",\"out\":[";
    for (uint8_t k = 0; k < cfg::MAX_OUTPUTS; ++k) { if (k) relData += ','; relData += std::to_string(rl.out_gpio[k]); }
    relData += "],\"role\":[";
    for (uint8_t k = 0; k < cfg::MAX_OUTPUTS; ++k) { if (k) relData += ','; relData += std::to_string(rl.in_role[k]); }
    relData += "],\"rlow\":[";
    for (uint8_t k = 0; k < cfg::MAX_OUTPUTS; ++k) { if (k) relData += ','; relData += (rl.in_active_low[k] ? "true" : "false"); }
    relData += "]}";
  }
  relData += "]";
  const std::string tmin = std::to_string(cfg::MIN_FEEDBACK_TIMEOUT_MS);
  const std::string tmax = std::to_string(cfg::MAX_FEEDBACK_TIMEOUT_MS);
  const std::string dmin = std::to_string(cfg::MIN_DEBOUNCE_MS);
  const std::string dmax = std::to_string(cfg::MAX_DEBOUNCE_MS);
  std::string html = "<!DOCTYPE html><html lang=\"de\"><head><meta charset=\"UTF-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><title>" + html_escape(site_title) + " - Relais</title><style>html,body{margin:0}body{box-sizing:border-box;font-family:'Segoe UI',sans-serif;background:#121212;color:#e0e0e0;min-height:100vh;padding:108px 24px 24px}" + page_header_css() + "h1{color:#4dabf7}.card{background:#1e1e1e;padding:20px;max-width:1040px;border:1px solid #2d2d2d}.row{display:flex;gap:12px;margin-bottom:12px;align-items:center}.row>label:first-child{width:180px;color:#9e9e9e;white-space:nowrap}.number-input{max-width:120px;background:#2a2a2a;color:#e0e0e0;border:1px solid #333;padding:8px}.rel{border-top:1px solid #2d2d2d;padding:10px 0}.rel.collapsed .outs{display:none}.caret{flex:none;background:none;border:0;color:#4dabf7;font-size:1rem;cursor:pointer;padding:0 6px;margin:0;line-height:1}.rhead{display:flex;flex-wrap:wrap;gap:10px;align-items:center}.rhead .rn{flex:none;width:70px;color:#4dabf7;font-weight:700}.rhead input[type=text]{flex:0 1 auto;min-width:120px;background:#2a2a2a;color:#e0e0e0;border:1px solid #333;padding:8px}.checklbl{display:flex;align-items:center;gap:4px;color:#9e9e9e;white-space:nowrap}select{background:#2a2a2a;color:#e0e0e0;border:1px solid #333;padding:7px}.outs{margin:8px 0 0 70px}.orow{display:flex;flex-wrap:wrap;gap:8px;align-items:center;margin-bottom:6px;color:#9e9e9e}.orow .ol{flex:none;width:80px}.inpin{font-family:monospace;color:#8bd5ff;min-width:64px}.actions{margin:0 0 16px}.actions button{margin:0}button{padding:10px 14px;margin:4px;background:#1a3a5c;color:#4dabf7;border:1px solid #1e5a9e;font-weight:700}.save{background:#1b4332;color:#51cf66;border-color:#2d6a4f}.danger{background:#3d1515;color:#ff6b6b;border-color:#7a2020}.ok{color:#51cf66}.err{color:#ff6b6b}</style></head><body>" + page_header_html(session, true) + "<h1>Relais</h1>" + page_nav_actions("relais") + "<div id=\"msg\"></div><div class=\"card\"><div class=\"row\"><label>Rueckmeldezeit</label><input class=\"number-input\" type=\"number\" id=\"fbtimeout\" min=\"" + tmin + "\" max=\"" + tmax + "\" value=\"" + std::to_string(feedback_timeout_ms) + "\"><span>ms</span></div><div class=\"row\"><label>Taster-Entprellzeit</label><input class=\"number-input\" type=\"number\" id=\"debounce\" min=\"" + dmin + "\" max=\"" + dmax + "\" value=\"" + std::to_string(taster_debounce_ms) + "\"><span>ms</span></div><div id=\"rels\"></div></div><script>const relData=" + relData + ";const OUT=" + outPins + ";const IN=" + inPins + ";const IMPMIN=" + imp_min + ",IMPMAX=" + imp_max + ";const msg=document.getElementById('msg');" + std::string(sse_conn_script()) +
    "function inFor(g){const i=OUT.indexOf(+g);return i<0?'-':('GP'+IN[i])}"
    "function outSelect(r,k){let s='<select class=\"osel\" data-r=\"'+r+'\" data-k=\"'+k+'\" onchange=\"upd()\"><option value=\"-1\">\\u2013</option>';OUT.forEach(p=>{const sel=(relData[r].out[k]===p)?' selected':'';s+='<option value=\"'+p+'\"'+sel+'>GP'+p+'</option>'});return s+'</select>'}"
    "function roleSelect(r,k){const cur=relData[r].role[k];let s='<select class=\"rsel\" data-r=\"'+r+'\" data-k=\"'+k+'\"><option value=\"0\"'+(cur===0?' selected':'')+'>keine</option><option value=\"1\"'+(cur===1?' selected':'')+'>Rueckmeldung</option><option value=\"2\"'+(cur===2?' selected':'')+'>Taster</option></select>';return s}"
    "let collapsed=Array(" + std::to_string(cfg::MAX_RELAIS) + ").fill(true);"
    "function toggleRel(r){collapsed[r]=!collapsed[r];upd()}"
    "function render(){let h='';for(let r=0;r<" + std::to_string(cfg::MAX_RELAIS) + ";r++){const d=relData[r];const n=d.type===1?4:(d.type===2?2:1);h+='<div class=\"rel'+(collapsed[r]?' collapsed':'')+'\"><div class=\"rhead\"><button type=\"button\" class=\"caret\" onclick=\"toggleRel('+r+')\">'+(collapsed[r]?'\\u25B8':'\\u25BE')+'</button><span class=\"rn\">Relais '+(r+1)+'</span>'+'<label class=\"checklbl\"><input type=\"checkbox\" class=\"ren\" data-r=\"'+r+'\"'+(d.en?' checked':'')+'> aktiv</label>'+'<input type=\"text\" maxlength=\"32\" class=\"rname\" data-r=\"'+r+'\" placeholder=\"Name\">'+'<select class=\"rtype\" data-r=\"'+r+'\" onchange=\"upd()\"><option value=\"0\"'+(d.type===0?' selected':'')+'>1-fach</option><option value=\"2\"'+(d.type===2?' selected':'')+'>2-fach</option><option value=\"1\"'+(d.type===1?' selected':'')+'>4-fach</option></select>'+'<label class=\"checklbl\"><input type=\"checkbox\" class=\"rlow\" data-r=\"'+r+'\"'+(d.low?' checked':'')+'> Low aktiv</label>'+'<label class=\"checklbl\"><input type=\"checkbox\" class=\"rimp\" data-r=\"'+r+'\"'+(d.imp?' checked':'')+'> Impuls</label>'+'<input class=\"number-input rimpms\" data-r=\"'+r+'\" type=\"number\" min=\"'+IMPMIN+'\" max=\"'+IMPMAX+'\" value=\"'+d.impms+'\"><span>ms</span></div><div class=\"outs\">';for(let k=0;k<n;k++){h+='<div class=\"orow\"><span class=\"ol\">Ausgang '+(k+1)+'</span>'+outSelect(r,k)+'<span>Eingang</span><span class=\"inpin\" id=\"in'+r+'_'+k+'\">'+inFor(d.out[k])+'</span>'+roleSelect(r,k)+'<label class=\"checklbl\"><input type=\"checkbox\" class=\"rrlow\" data-r=\"'+r+'\" data-k=\"'+k+'\"'+(d.rlow[k]?' checked':'')+'> LOW</label></div>'}h+='</div></div>'}document.getElementById('rels').innerHTML=h;for(let r=0;r<" + std::to_string(cfg::MAX_RELAIS) + ";r++)document.querySelector('input.rname[data-r=\"'+r+'\"]').value=relData[r].name}"
    "function upd(){for(let r=0;r<" + std::to_string(cfg::MAX_RELAIS) + ";r++){relData[r].type=+document.querySelector('select.rtype[data-r=\"'+r+'\"]').value;relData[r].name=document.querySelector('input.rname[data-r=\"'+r+'\"]').value;relData[r].en=document.querySelector('input.ren[data-r=\"'+r+'\"]').checked;relData[r].low=document.querySelector('input.rlow[data-r=\"'+r+'\"]').checked;relData[r].imp=document.querySelector('input.rimp[data-r=\"'+r+'\"]').checked;relData[r].impms=+document.querySelector('input.rimpms[data-r=\"'+r+'\"]').value}document.querySelectorAll('select.osel').forEach(s=>{relData[+s.dataset.r].out[+s.dataset.k]=+s.value});document.querySelectorAll('select.rsel').forEach(s=>{relData[+s.dataset.r].role[+s.dataset.k]=+s.value});document.querySelectorAll('input.rrlow').forEach(c=>{relData[+c.dataset.r].rlow[+c.dataset.k]=c.checked});render()}"
    "render();"
    "function save(){upd();const en=[],type=[],names=[],low=[],imp=[],impms=[],out=[],role=[],rlow=[];for(let r=0;r<" + std::to_string(cfg::MAX_RELAIS) + ";r++){const d=relData[r];en.push(d.en);type.push(d.type);names.push((d.name||'').trim());low.push(d.low);imp.push(d.imp);impms.push(Math.max(IMPMIN,Math.min(IMPMAX,+d.impms||300)));for(let k=0;k<4;k++){out.push(d.out[k]);role.push(d.role[k]);rlow.push(d.rlow[k])}}const ft=Math.max(" + tmin + ",Math.min(" + tmax + ",+document.getElementById('fbtimeout').value||500));const db=Math.max(" + dmin + ",Math.min(" + dmax + ",+document.getElementById('debounce').value||25));fetch('/relais',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({r_enabled:en,r_type:type,r_names:names,r_low:low,r_imp:imp,r_impms:impms,r_out:out,r_role:role,r_rlow:rlow,feedback_timeout:ft,taster_debounce:db})}).then(r=>r.json()).then(d=>{msg.textContent=d.ok?'Gespeichert':'Fehler';msg.className=d.ok?'ok':'err';if(d.ok)setTimeout(()=>location.reload(),700)}).catch(()=>{msg.textContent='Fehler';msg.className='err'})}"
    "</script></body></html>";
  return html;
}

// Relais-Seite POST: uebernimmt Relaisdefinitionen, loest GPIOs auf und wendet sie an.
static void handle_relais_post(uint8_t sn, const HttpRequest &req) {
  {
    StateLock lock;
    const std::vector<bool> en = json_bool_list(req.body, "r_enabled");
    const std::vector<int> type = json_int_list(req.body, "r_type");
    const std::vector<std::string> names = json_string_list(req.body, "r_names", 32);
    const std::vector<bool> low = json_bool_list(req.body, "r_low");
    const std::vector<bool> imp = json_bool_list(req.body, "r_imp");
    const std::vector<int> impms = json_int_list(req.body, "r_impms");
    const std::vector<int> out = json_int_list(req.body, "r_out");    // flach r*4+k
    const std::vector<int> role = json_int_list(req.body, "r_role");  // flach r*4+k
    const std::vector<bool> rlow = json_bool_list(req.body, "r_rlow"); // flach r*4+k
    for (uint8_t r = 0; r < cfg::MAX_RELAIS; ++r) {
      Relais &rl = relais[r];
      if (r < en.size()) rl.enabled = en[r];
      if (r < type.size()) rl.type = (type[r] == 1) ? RelayType::Quad : (type[r] == 2) ? RelayType::Dual : RelayType::Simple;
      if (r < names.size()) rl.name = names[r];
      if (r < low.size()) rl.active_low = low[r];
      if (r < imp.size()) rl.impulse = imp[r];
      if (r < impms.size()) rl.impulse_ms = std::clamp<uint16_t>(static_cast<uint16_t>(impms[r]), cfg::MIN_IMPULSE_MS, cfg::MAX_IMPULSE_MS);
      for (uint8_t k = 0; k < cfg::MAX_OUTPUTS; ++k) {
        const size_t idx = static_cast<size_t>(r) * cfg::MAX_OUTPUTS + k;
        if (idx < out.size()) rl.out_gpio[k] = (output_pool_index(out[idx]) >= 0) ? static_cast<int8_t>(out[idx]) : -1;
        if (idx < role.size()) rl.in_role[k] = (role[idx] >= 0 && role[idx] <= 2) ? static_cast<uint8_t>(role[idx]) : 0;
        if (idx < rlow.size()) rl.in_active_low[k] = rlow[idx];
      }
      if (rl.active_output > outputs_count(rl)) rl.active_output = 0;
    }
    feedback_timeout_ms = std::clamp<uint32_t>(json_uint_value(req.body, "feedback_timeout", feedback_timeout_ms),
                                               cfg::MIN_FEEDBACK_TIMEOUT_MS, cfg::MAX_FEEDBACK_TIMEOUT_MS);
    taster_debounce_ms = std::clamp<uint32_t>(json_uint_value(req.body, "taster_debounce", taster_debounce_ms),
                                              cfg::MIN_DEBOUNCE_MS, cfg::MAX_DEBOUNCE_MS);
    resolve_gpios();
    apply_all_outputs();
    configure_inputs();
  }
  save_config();
  broadcast_state();
  esp_link_display_dirty = true;
  send_response(sn, "200 OK", "application/json", "{\"ok\":true}");
}

static std::string build_network_html(const Session *session) {
  const wiz_NetInfo net_info = current_net_info();
  std::string html = "<!DOCTYPE html><html lang=\"de\"><head><meta charset=\"UTF-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><title>" + html_escape(site_title) + " - Network</title><style>html,body{margin:0}body{box-sizing:border-box;font-family:'Segoe UI',sans-serif;background:#121212;color:#e0e0e0;min-height:100vh;padding:108px 24px 24px}" + page_header_css() + "h1{color:#4dabf7}.card{background:#1e1e1e;padding:24px;max-width:560px;border:1px solid #2d2d2d}.card+.card{margin-top:16px}.net-row,.form-row{display:flex;gap:12px;margin-bottom:10px;align-items:center}.net-label,.form-row label{width:120px;color:#9e9e9e;white-space:nowrap}.net-value{font-family:monospace;color:#e6edf3;word-break:break-word}.form-row input{flex:1;background:#2a2a2a;color:#e0e0e0;border:1px solid #333;padding:8px;font-family:monospace}.actions{margin:0 0 16px}.actions button{margin:0}button{padding:10px 14px;background:#1a3a5c;color:#4dabf7;border:1px solid #1e5a9e;font-weight:700}.save{background:#1b4332;color:#51cf66;border-color:#2d6a4f}.ok{color:#51cf66}.err{color:#ff6b6b}</style></head><body>" + page_header_html(session, true) + "<h1>Network</h1><div class=\"actions\"><button onclick=\"location.href='/config'\">Zurueck</button><button class=\"save\" onclick=\"saveNetwork()\">Speichern</button></div><div id=\"msg\"></div><div class=\"card\"><div class=\"net-row\"><div class=\"net-label\">Mode</div><div class=\"net-value\">" + network_mode_text(net_info) + "</div></div><div class=\"net-row\"><div class=\"net-label\">MAC</div><div class=\"net-value\">" + format_mac(net_info.mac) + "</div></div><div class=\"net-row\"><div class=\"net-label\">IP</div><div class=\"net-value\">" + format_ipv4(net_info.ip) + "</div></div><div class=\"net-row\"><div class=\"net-label\">Subnet Mask</div><div class=\"net-value\">" + format_ipv4(net_info.sn) + "</div></div><div class=\"net-row\"><div class=\"net-label\">Gateway</div><div class=\"net-value\">" + format_ipv4(net_info.gw) + "</div></div><div class=\"net-row\"><div class=\"net-label\">DNS</div><div class=\"net-value\">" + format_ipv4(net_info.dns) + "</div></div></div><div class=\"card\"><h2 style=\"color:#4dabf7;margin:0 0 16px\">Static</h2><div class=\"form-row\"><label>IP</label><input id=\"static-ip\" value=\"" + format_ipv4(static_ip.data()) + "\" inputmode=\"numeric\"></div><div class=\"form-row\"><label>Subnet Mask</label><input id=\"static-sn\" value=\"" + format_ipv4(static_sn.data()) + "\" inputmode=\"numeric\"></div><div class=\"form-row\"><label>Gateway</label><input id=\"static-gw\" value=\"" + format_ipv4(static_gw.data()) + "\" inputmode=\"numeric\"></div></div><script>const conn=document.getElementById('conn');const msg=document.getElementById('msg');let lastSseActivity=0;let lastConnectAttempt=0;let es=null;let reconnectTimer=0;function markConn(ok){conn.textContent=ok?'Verbunden':'Getrennt';conn.className=ok?'ok':'err'}function noteSseActivity(){lastSseActivity=Date.now();markConn(true)}function scheduleReconnect(delay=1000){if(reconnectTimer)return;reconnectTimer=setTimeout(()=>{reconnectTimer=0;connectEvents()},delay)}function forceReconnect(){if(es){es.close();es=null}scheduleReconnect(0)}function connectEvents(){if(es)es.close();lastConnectAttempt=Date.now();es=new EventSource('/events');es.onopen=()=>{noteSseActivity()};es.onerror=()=>{markConn(false);if(es){es.close();es=null}scheduleReconnect()};es.addEventListener('ping',()=>{noteSseActivity()});es.onmessage=()=>{noteSseActivity()}}connectEvents();window.addEventListener('pagehide',()=>{if(es)es.close()});setInterval(()=>{const now=Date.now();const activeLink=lastSseActivity&&now-lastSseActivity<2500;markConn(!!activeLink);if(!activeLink&&now-lastConnectAttempt>=2500&&!reconnectTimer)forceReconnect()},1000);function saveNetwork(){fetch('/network',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({ip:document.getElementById('static-ip').value,subnet:document.getElementById('static-sn').value,gateway:document.getElementById('static-gw').value})}).then(r=>r.json().then(d=>({ok:r.ok,d}))).then(x=>{if(!x.ok||!x.d.ok)throw new Error(x.d.error||'Fehler');msg.textContent='Gespeichert';msg.className='ok';setTimeout(()=>{msg.textContent='';msg.className=''},2000)}).catch(e=>{msg.textContent=e.message||'Fehler';msg.className='err'})}</script></body></html>";
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

  std::string html = "<!DOCTYPE html><html lang=\"de\"><head><meta charset=\"UTF-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><title>" + html_escape(site_title) + " - Admin</title><style>body{font-family:'Segoe UI',sans-serif;background:#121212;color:#e0e0e0;padding:108px 24px 24px}" + page_header_css() + ".card{background:#1e1e1e;padding:20px;max-width:820px;border:1px solid #2d2d2d;margin-bottom:16px}.actions{margin:0 0 16px}.actions button{margin:0}table{width:100%;border-collapse:collapse}td,th{border-bottom:1px solid #333;padding:8px;text-align:left;vertical-align:top}h2{font-size:1.05rem;color:#bdbdbd;margin:0 0 12px}input,select,button{padding:8px;margin:4px;background:#2a2a2a;color:#e0e0e0;border:1px solid #444}button{background:#1a3a5c;color:#4dabf7;border-color:#1e5a9e}.role{min-width:92px;font-weight:700}.user-action{min-width:92px}code{font-family:monospace;color:#8bd5ff}#keys-msg{margin-top:8px;color:#9ad77a}.modal-bg{display:none;position:fixed;inset:0;background:rgba(0,0,0,.7);z-index:500;align-items:center;justify-content:center}.modal-bg.show{display:flex}.modal{background:#1e1e1e;border:1px solid #2d2d2d;padding:28px 24px;min-width:300px;max-width:400px;width:100%}.modal h3{font-size:1rem;color:#4dabf7;margin:0 0 12px}.modal p{color:#bdbdbd;margin:0 0 16px;font-size:.9rem}.modal input{width:100%;box-sizing:border-box;background:#2a2a2a;color:#e0e0e0;border:1px solid #444;padding:9px;margin-bottom:12px}.modal-btns{display:flex;gap:8px;justify-content:flex-end}.modal-btns button{margin:0}.btn-ok{background:#1a3a5c;color:#4dabf7;border-color:#1e5a9e}.btn-cancel{background:#2a2a2a;color:#9e9e9e;border-color:#444}.btn-danger{background:#3d1515;color:#ff6b6b;border-color:#7a2020}" + std::string(pw_eye_css()) + "</style></head><body>" + page_header_html(current_session, true) + "<h1>Benutzerverwaltung</h1><div class=\"actions\"><button onclick=\"location.href='/config'\">Zurueck</button></div><div class=\"card\"><h2>Aktive Anmeldungen</h2><table><thead><tr><th>Benutzer</th><th>Rolle</th><th>Restzeit</th></tr></thead><tbody>" + session_rows + "</tbody></table></div><div class=\"card\"><h2>Benutzer</h2><table><thead><tr><th>Name</th><th>Rolle</th><th>Aktion</th></tr></thead><tbody>" + rows + "</tbody></table></div><div class=\"card\"><h2>Neuer Benutzer</h2><input id=\"u\" placeholder=\"Benutzer\"><span class=\"pw-wrap\"><input id=\"p\" type=\"password\" placeholder=\"Passwort\">" + pw_eye_btn() + "</span><select id=\"r\"><option value=\"admin\">admin</option><option value=\"user\">user</option></select><button onclick=\"addUser()\">Hinzufuegen</button></div><div class=\"card\"><h2>API-Keys</h2><div style=\"margin-bottom:12px\"><input id=\"key-comment\" maxlength=\"64\" placeholder=\"Kommentar fuer neuen Key\" style=\"width:260px\"> <button onclick=\"genKey()\">Neuen Key erzeugen</button></div><table><thead><tr><th>Key</th><th>Kommentar</th><th>Aktion</th></tr></thead><tbody>" + key_rows + "</tbody></table><div id=\"keys-msg\"></div></div>"
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
    "function saveKeyComment(btn){const row=btn.closest('tr');const inp=row.querySelector('input[data-key]');api({action:'set_key_comment',key:inp.dataset.key,comment:inp.value}).then(d=>{if(d.ok){const km=document.getElementById('keys-msg');km.textContent='Gespeichert';setTimeout(()=>{km.textContent=''},2000);}else msgModal('Fehler',d.error||'Fehler')})}"
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

// Extrahiert den Inhalt eines JSON-Arrays "key":[ ... ] (ohne Klammern).
static bool json_array_body(const std::string &body, const char *name, std::string &out) {
  size_t key = body.find(std::string("\"") + name + "\"");
  if (key == std::string::npos) return false;
  size_t start = body.find('[', key);
  if (start == std::string::npos) return false;
  size_t end = body.find(']', start);
  if (end == std::string::npos) return false;
  out = body.substr(start + 1, end - start - 1);
  return true;
}

// Liest ein String-Array "key":["a","b",...] in eine Liste (je Element auf maxlen begrenzt).
static std::vector<std::string> json_string_list(const std::string &body, const char *name, size_t maxlen) {
  std::vector<std::string> out;
  std::string arr;
  if (!json_array_body(body, name, arr)) return out;
  size_t p = 0;
  while (true) {
    size_t q = arr.find('"', p);
    if (q == std::string::npos) break;
    size_t e = arr.find('"', q + 1);
    if (e == std::string::npos) break;
    out.push_back(arr.substr(q + 1, std::min<size_t>(maxlen, e - q - 1)));
    p = e + 1;
  }
  return out;
}

// Liest ein Boolean-Array "key":[true,false,...] in eine Liste.
static std::vector<bool> json_bool_list(const std::string &body, const char *name) {
  std::vector<bool> out;
  std::string arr;
  if (!json_array_body(body, name, arr)) return out;
  size_t p = 0;
  while (p < arr.size()) {
    size_t comma = arr.find(',', p);
    const std::string tok = arr.substr(p, comma == std::string::npos ? std::string::npos : comma - p);
    if (tok.find_first_not_of(" \t\r\n") != std::string::npos)
      out.push_back(tok.find("true") != std::string::npos);
    if (comma == std::string::npos) break;
    p = comma + 1;
  }
  return out;
}

// Liest ein Integer-Array "key":[1,-1,2,...] in eine Liste (signed).
static std::vector<int> json_int_list(const std::string &body, const char *name) {
  std::vector<int> out;
  std::string arr;
  if (!json_array_body(body, name, arr)) return out;
  size_t p = 0;
  while (p < arr.size()) {
    while (p < arr.size() && arr[p] != '-' && !std::isdigit(static_cast<unsigned char>(arr[p]))) ++p;
    if (p >= arr.size()) break;
    bool neg = false;
    if (arr[p] == '-') { neg = true; ++p; }
    if (p >= arr.size() || !std::isdigit(static_cast<unsigned char>(arr[p]))) break;
    int v = 0;
    while (p < arr.size() && std::isdigit(static_cast<unsigned char>(arr[p]))) v = v * 10 + (arr[p++] - '0');
    out.push_back(neg ? -v : v);
  }
  return out;
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

// Leitet je aktivem Relais-Ausgang den Eingangs-GPIO aus dem gewaehlten Ausgangs-GPIO
// ab und validiert (kein Ausgangs-Pin doppelt belegt). Ungueltige Relais bleiben aus.
static void resolve_gpios() {
  std::array<bool, 8> out_used{};  // Pool-Index belegt?
  for (uint8_t r = 0; r < cfg::MAX_RELAIS; ++r) {
    Relais &rl = relais[r];
    for (uint8_t k = 0; k < cfg::MAX_OUTPUTS; ++k) rl.in_gpio[k] = -1;
    rl.valid = false;
    if (!rl.enabled) continue;
    const uint8_t n = outputs_count(rl);
    bool ok = true;
    for (uint8_t k = 0; k < n && ok; ++k) {
      const int oi = output_pool_index(rl.out_gpio[k]);
      if (oi < 0 || out_used[oi]) ok = false;
    }
    if (!ok) { printf("Relais %u: GPIO-Konflikt/ungueltig, deaktiviert.\n", r + 1); continue; }
    for (uint8_t k = 0; k < n; ++k) {
      const int oi = output_pool_index(rl.out_gpio[k]);
      out_used[oi] = true;
      rl.in_gpio[k] = static_cast<int8_t>(cfg::INPUT_PINS[oi]);
    }
    rl.valid = true;
  }
}

// Konfiguriert die Eingangs-GPIOs je nach Rolle (Rueckmeldung/Taster) und setzt
// die Laufzeitzustaende zurueck. Alle Pool-Eingaenge werden vorab als Input mit
// Pull-up initialisiert (unbenutzte bleiben so definiert).
static void configure_inputs() {
  for (uint8_t pin : cfg::INPUT_PINS) {
    gpio_init(pin);
    gpio_set_dir(pin, GPIO_IN);
    gpio_pull_up(pin);
  }
  for (uint8_t r = 0; r < cfg::MAX_RELAIS; ++r) {
    Relais &rl = relais[r];
    for (uint8_t k = 0; k < cfg::MAX_OUTPUTS; ++k) {
      rl.fb_pending[k] = false;
      rl.fb_error[k] = false;
      rl.fb_source_scene[k] = -1;
      rl.btn_pressed[k] = false;
      rl.btn_raw[k] = false;
      rl.btn_since[k] = millis32();
    }
    if (!rl.valid || !rl.enabled) continue;
    for (uint8_t k = 0; k < outputs_count(rl); ++k) {
      if (rl.in_gpio[k] < 0) continue;
      const uint pin = static_cast<uint>(rl.in_gpio[k]);
      if (rl.in_role[k] == static_cast<uint8_t>(InRole::Taster)) {
        gpio_pull_up(pin);  // Taster schaltet gegen GND
      } else if (rl.in_role[k] == static_cast<uint8_t>(InRole::Feedback)) {
        if (rl.in_active_low[k]) gpio_pull_up(pin); else gpio_pull_down(pin);
      }
    }
  }
  update_scene_feedback_errors();
}

// Initialisiert alle Ausgangs-Pins als Ausgang (Idle) und gibt den gelatchten Zustand aus.
static void apply_all_outputs() {
  for (uint8_t pin : cfg::OUTPUT_PINS) {
    gpio_init(pin);
    gpio_set_dir(pin, GPIO_OUT);
  }
  for (uint8_t r = 0; r < cfg::MAX_RELAIS; ++r) {
    Relais &rl = relais[r];
    if (!rl.valid || !rl.enabled) continue;
    for (uint8_t k = 0; k < outputs_count(rl); ++k) {
      const bool on = (rl.active_output == static_cast<uint8_t>(k + 1));
      apply_output(r, k, on);
    }
  }
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

// Ausgaenge-Seite: definiert die logischen Buttons (Aktiv, Name, Relais-Eingang).
static void handle_config_post(uint8_t sn, const HttpRequest &req) {
  {
    StateLock lock;
    const std::vector<bool> en = json_bool_list(req.body, "btn_enabled");
    const std::vector<std::string> names = json_string_list(req.body, "btn_names", 32);
    const std::vector<int> rels = json_int_list(req.body, "btn_relais");
    const std::vector<int> ins = json_int_list(req.body, "btn_input");
    for (uint8_t b = 0; b < cfg::MAX_BUTTONS; ++b) {
      Button &bt = buttons[b];
      if (b < en.size()) bt.enabled = en[b];
      if (b < names.size()) bt.name = names[b];
      if (b < rels.size()) {
        const int r = rels[b];
        bt.relais_idx = (r >= 0 && r < cfg::MAX_RELAIS) ? static_cast<int8_t>(r) : -1;
      }
      if (b < ins.size()) {
        const int k = ins[b];
        bt.input_idx = (k >= 0 && k < cfg::MAX_OUTPUTS) ? static_cast<uint8_t>(k) : 0;
      }
    }
    std::string title = json_string_value(req.body, "title");
    std::string subtitle = json_string_value(req.body, "subtitle");
    if (!title.empty()) site_title = title.substr(0, 64);
    if (!subtitle.empty()) site_subtitle = subtitle.substr(0, 64);
    public_access = json_bool_value(req.body, "public", public_access);
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
    for (uint8_t r = 0; r < cfg::MAX_BUTTONS; ++r) {
      if (!buttons[r].enabled) continue;  // nur aktive Buttons sind in Szenen steuerbar
      const uint8_t a = scenes[s].action[r];
      const std::string label = buttons[r].name.empty() ? ("Button " + std::to_string(r + 1)) : buttons[r].name;
      const bool is_simple = buttons[r].relais_idx >= 0 && buttons[r].relais_idx < cfg::MAX_RELAIS &&
                             relais[buttons[r].relais_idx].type == RelayType::Simple;
      cells += "<div class=\"cell\"><span>" + html_escape(label) + "</span><select data-s=\"" + std::to_string(s) + "\" data-r=\"" + std::to_string(r) + "\">"
               "<option value=\"x\"" + std::string(a == 2 ? " selected" : "") + ">\xe2\x80\x94</option>";
      if (is_simple) {  // 1-fach: Toggle statt absolutem Aus/Ein
        cells += "<option value=\"1\"" + std::string(a != 2 ? " selected" : "") + ">Umschalten</option>";
      } else {
        cells += "<option value=\"0\"" + std::string(a == 0 ? " selected" : "") + ">Aus</option>"
                 "<option value=\"1\"" + std::string(a == 1 ? " selected" : "") + ">Ein</option>";
      }
      cells += "</select></div>";
    }
    rows += "<div class=\"scene collapsed\"><div class=\"scene-head\"><button type=\"button\" class=\"caret\" onclick=\"toggleScene(this)\">\xe2\x96\xb8</button><span class=\"s-num\">" + std::to_string(s + 1) + "</span><label class=\"en\"><input type=\"checkbox\" class=\"s-en\" data-s=\"" + std::to_string(s) + "\"" + std::string(scenes[s].enabled ? " checked" : "") + "> aktiv</label>"
            "<input class=\"s-name\" data-s=\"" + std::to_string(s) + "\" maxlength=\"32\" placeholder=\"Szene " + std::to_string(s + 1) + "\" value=\"" + html_escape(scenes[s].name) + "\"></div>"
            "<div class=\"cells\">" + cells + "</div></div>";
  }
  std::string html = "<!DOCTYPE html><html lang=\"de\"><head><meta charset=\"UTF-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><title>" + html_escape(site_title) + " - Szenen</title><style>html,body{margin:0}body{box-sizing:border-box;font-family:'Segoe UI',sans-serif;background:#121212;color:#e0e0e0;min-height:100vh;padding:108px 24px 24px}" + page_header_css() +
      "h1{color:#4dabf7}.card{background:#1e1e1e;padding:24px;max-width:760px;border:1px solid #2d2d2d}.actions{margin:0 0 16px}.actions button{margin:0}button{padding:10px 14px;background:#1a3a5c;color:#4dabf7;border:1px solid #1e5a9e;font-weight:700}.save{background:#1b4332;color:#51cf66;border-color:#2d6a4f}.danger{background:#3d1515;color:#ff6b6b;border-color:#7a2020}.ok{color:#51cf66}.err{color:#ff6b6b}"
      ".mode-row{display:flex;align-items:center;gap:10px;margin-bottom:16px;font-weight:700;color:#9e9e9e}.scene{border:1px solid #2d2d2d;background:#181818;padding:12px;margin-bottom:8px}.scene.collapsed{padding:8px 12px}.scene.collapsed .cells{display:none}.scene.collapsed .scene-head{margin-bottom:0}.caret{flex:none;background:none;border:0;color:#4dabf7;font-size:1rem;cursor:pointer;padding:0 6px;margin:0;line-height:1}.scene-head{display:flex;gap:12px;align-items:center;margin-bottom:10px;flex-wrap:wrap}.scene-head .s-num{flex:none;display:inline-flex;align-items:center;justify-content:center;min-width:26px;height:26px;padding:0 6px;border-radius:13px;background:#1a3a5c;color:#4dabf7;border:1px solid #1e5a9e;font-weight:700;font-size:.85rem}.scene-head .en{display:flex;align-items:center;gap:6px;color:#9e9e9e;white-space:nowrap}.s-name{flex:1;min-width:160px;background:#2a2a2a;color:#e0e0e0;border:1px solid #333;padding:8px}.cells{display:grid;grid-template-columns:repeat(auto-fill,minmax(150px,1fr));gap:8px}.cell{display:flex;flex-direction:column;gap:3px;font-size:.75rem;color:#9e9e9e}.cell select{background:#2a2a2a;color:#e0e0e0;border:1px solid #333;padding:6px}</style></head><body>" +
      page_header_html(session, true) +
      "<h1>Szenen</h1>" + page_nav_actions("scenes") + "<div id=\"msg\"></div><div class=\"card\">"
      "<div class=\"mode-row\"><input type=\"checkbox\" id=\"mode\"" + std::string(scene_mode ? " checked" : "") + "><label for=\"mode\">Szenen-Modus aktiv (Buttons aktivieren Szenen statt Relais)</label></div>" + rows + "</div>"
      "<script>const conn=document.getElementById('conn');let lastSseActivity=0;let lastConnectAttempt=0;let es=null;let reconnectTimer=0;function markConn(ok){if(!conn)return;conn.textContent=ok?'Verbunden':'Getrennt';conn.className=ok?'ok':'err'}function noteSseActivity(){lastSseActivity=Date.now();markConn(true)}function scheduleReconnect(delay=1000){if(reconnectTimer)return;reconnectTimer=setTimeout(()=>{reconnectTimer=0;connectEvents()},delay)}function forceReconnect(){if(es){es.close();es=null}scheduleReconnect(0)}function connectEvents(){if(es)es.close();lastConnectAttempt=Date.now();es=new EventSource('/events');es.onopen=()=>{noteSseActivity()};es.onerror=()=>{markConn(false);if(es){es.close();es=null}scheduleReconnect()};es.addEventListener('ping',()=>{noteSseActivity()});es.onmessage=()=>{noteSseActivity()}}connectEvents();window.addEventListener('pagehide',()=>{if(es)es.close()});setInterval(()=>{const now=Date.now();const activeLink=lastSseActivity&&now-lastSseActivity<2500;markConn(!!activeLink);if(!activeLink&&now-lastConnectAttempt>=2500&&!reconnectTimer)forceReconnect()},1000);"
      "function toggleScene(btn){const sc=btn.closest('.scene');sc.classList.toggle('collapsed');btn.textContent=sc.classList.contains('collapsed')?'\\u25B8':'\\u25BE'}"
      "function save(){const body={mode:document.getElementById('mode').checked};"
      "document.querySelectorAll('.s-en').forEach(e=>{body['s'+e.dataset.s+'_en']=e.checked});"
      "document.querySelectorAll('.s-name').forEach(e=>{body['s'+e.dataset.s+'_name']=e.value.replace(/[\"\\\\]/g,'')});"
      "for(let s=0;s<" + std::to_string(cfg::SCENE_COUNT) + ";s++){let act='';for(let r=0;r<" + std::to_string(cfg::MAX_BUTTONS) + ";r++){const sel=document.querySelector('select[data-s=\"'+s+'\"][data-r=\"'+r+'\"]');act+=sel?sel.value:'x'}body['s'+s+'_act']=act}"
      "fetch('/scenes',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)}).then(r=>r.json()).then(d=>{const m=document.getElementById('msg');m.textContent=d.ok?'Gespeichert':'Fehler';m.className=d.ok?'ok':'err';if(d.ok)setTimeout(()=>{m.textContent='';m.className=''},2000)}).catch(()=>{document.getElementById('msg').textContent='Fehler'})}"
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
      for (uint8_t r = 0; r < cfg::MAX_BUTTONS && r < act.size(); ++r) {
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
    const bool needs_admin = (next == "/config" || next == "/relais" || next == "/network" || next == "/scenes" || next == "/admin");
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
  } else if (req.path == "/relais") {
    Session *session = session_from_headers(req);
    if (!session || session->role != "admin") {
      if (req.method == "GET") send_redirect(sn, "/login?next=/relais");
      else send_response(sn, "403 Forbidden", "application/json", "{\"error\":\"forbidden\"}");
    } else if (req.method == "GET") send_response(sn, "200 OK", "text/html; charset=utf-8", build_relais_html(session));
    else if (req.method == "POST") handle_relais_post(sn, req);
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
    if (idx < 0 || idx >= cfg::MAX_BUTTONS) send_response(sn, "400 Bad Request", "text/plain", "Index 0-7");
    else if (action == "toggle" || action == "on" || action == "off") {
      button_command(static_cast<uint8_t>(idx), action == "toggle" ? 2 : (action == "on" ? 1 : 0));
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

// Standardwerte (nur als RAM-Defaults, bevor eine Konfig geladen wird) und alle
// Ausgangs-Pins auf Idle. Die GPIO-Aufloesung erfolgt spaeter in resolve_gpios().
static void init_relays() {
  for (uint8_t r = 0; r < cfg::MAX_RELAIS; ++r) relais[r].name = "Relais " + std::to_string(r + 1);
  for (uint8_t b = 0; b < cfg::MAX_BUTTONS; ++b) buttons[b].name = "Button " + std::to_string(b + 1);
  for (uint8_t pin : cfg::OUTPUT_PINS) {
    gpio_init(pin);
    gpio_set_dir(pin, GPIO_OUT);
    gpio_put(pin, 0);
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
  std::string html = "<!DOCTYPE html><html lang=\"de\"><head><meta charset=\"UTF-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><title>" + html_escape(site_title) + "</title><style>*{box-sizing:border-box;margin:0;padding:0}body{font-family:'Segoe UI',system-ui,sans-serif;background:#0d1117;color:#c9d1d9;min-height:100vh;display:flex;flex-direction:column;align-items:center;padding:20px 16px 40px}" + page_header_css() + "h1{margin:92px 0 24px;font-size:1.4rem;color:#e6edf3;font-weight:600;text-align:center}.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(220px,1fr));gap:12px;width:100%;max-width:800px}.card{width:100%;background:#161b22;border:1px solid #30363d;padding:10px;display:flex;align-items:center;cursor:pointer;text-align:inherit;font:inherit;color:inherit}.card.on{border-color:#238636}.card.error{background:#5a1616;border-color:#f85149}.card.error .label,.card.error .state{color:#fff}.card.on.dirty{position:relative}.card.on.dirty::after{content:'';position:absolute;top:4px;right:4px;width:10px;height:10px;border-radius:50%;background:#f85149}.card:focus-visible{outline:2px solid #58a6ff;outline-offset:2px}.relay-row{display:flex;align-items:center;gap:10px;width:100%}.label{flex:1;font-size:.9rem;font-weight:700;color:#8b949e;text-transform:uppercase;text-align:left}.relay-num{flex:none;font-size:.65rem;color:#484f58;min-width:24px}.state{flex:none;min-width:38px;font-size:.9rem;font-weight:700;color:#8b949e;text-align:right}.card.on .state{color:#3fb950}.notice{background:#161b22;border:1px solid #30363d;padding:24px 28px;color:#c9d1d9;font-size:.95rem;text-align:center;max-width:560px}.notice a{color:#58a6ff;font-weight:700}</style></head><body data-user=\"" + html_escape(current_session_username(session)) + "\" data-can-control=\"" + (can_control ? "1" : "0") + "\">" + page_header_html(session, true) + "<h1 id=\"subtitle\">" + html_escape(site_subtitle) + "</h1>" + body_section + "<script>const initialState=" + state_json(session != nullptr || public_access) + ";const canControl=document.body.dataset.canControl==='1';const grid=document.getElementById('grid');const conn=document.getElementById('conn');const other=document.getElementById('other-users');const currentUser=document.body.dataset.user;let names=(initialState.names&&initialState.names.length)?initialState.names:Array.from({length:8},(_,i)=>'Relais '+(i+1));if(grid&&canControl){const initialRelays=initialState.relays||[];const initialErrors=initialState.feedback_errors||[];const btnEn=initialState.btn_en||[];for(let i=0;i<8;i++){if(btnEn[i]===false)continue;const on=!!initialRelays[i];const card=document.createElement('button');card.type='button';card.className='card'+(initialErrors[i]?' error':(on?' on':''));card.id='c'+i;card.onclick=()=>toggle(i);card.innerHTML=`<div class=\"relay-row\"><span class=\"relay-num\">#${i+1}</span><span class=\"label\" id=\"l${i}\">${names[i]}</span><span class=\"state\" id=\"s${i}\">${on?'ON':'OFF'}</span></div>`;grid.appendChild(card)}}function renderActiveUsers(users){if(!other)return;if(users===null){other.style.display='none';return}other.style.display='';const list=(users||[]).filter(u=>!currentUser||currentUser==='-'||u.username!==currentUser);if(!list.length){other.textContent='Andere Benutzer: -';other.title='Keine anderen aktiven Anmeldungen';return}const text=list.map(u=>u.username+' ('+u.remaining+' min)').join(', ');other.textContent='Andere Benutzer: '+text;other.title=text}function applyState(states,ns,users,errors=[]){if(ns)names=ns;if(canControl&&states)states.forEach((on,i)=>{const lbl=document.getElementById('l'+i);if(!lbl)return;lbl.textContent=names[i];const state=document.getElementById('s'+i);const c=document.getElementById('c'+i);state.textContent=on?'ON':'OFF';c.className='card'+(errors[i]?' error':(on?' on':''))});renderActiveUsers(users)}renderActiveUsers(initialState.active_users);if(window.applyScenes)applyScenes(initialState.active_scene,initialState.scene_errors,initialState.scene_dirty);function toggle(i){fetch('/relay/'+i+'/toggle',{method:'POST',credentials:'same-origin'}).then(r=>{if(r.status===401){location.href='/login?next=/';throw new Error('unauthorized')}if(!r.ok)throw new Error('HTTP '+r.status);return r.json()}).then(d=>{applyState(d.relays,d.names,d.active_users,d.feedback_errors);if(window.applyScenes)applyScenes(d.active_scene,d.scene_errors,d.scene_dirty)}).catch(()=>{conn.textContent='Fehler';conn.className='err'})}let lastSseActivity=0;let lastConnectAttempt=0;let es=null;let reconnectTimer=0;function markConn(ok){conn.textContent=ok?'Verbunden':'Getrennt';conn.className=ok?'ok':'err'}function noteSseActivity(){lastSseActivity=Date.now();markConn(true)}function scheduleReconnect(delay=1000){if(reconnectTimer)return;reconnectTimer=setTimeout(()=>{reconnectTimer=0;connectEvents()},delay)}function forceReconnect(){if(es){es.close();es=null}scheduleReconnect(0)}function connectEvents(){if(es)es.close();lastConnectAttempt=Date.now();es=new EventSource('/events');es.onopen=()=>{noteSseActivity()};es.onerror=()=>{markConn(false);if(es){es.close();es=null}scheduleReconnect()};es.addEventListener('ping',()=>{noteSseActivity()});es.onmessage=e=>{noteSseActivity();try{const d=JSON.parse(e.data);applyState(d.relays,d.names,d.active_users,d.feedback_errors);if(window.applyScenes)applyScenes(d.active_scene,d.scene_errors,d.scene_dirty);if(d.title){document.getElementById('sitetitle').textContent=d.title;document.title=d.title}if(d.subtitle)document.getElementById('subtitle').innerHTML=d.subtitle}catch(x){}}}connectEvents();window.addEventListener('pagehide',()=>{if(es)es.close()});setInterval(()=>{const now=Date.now();const activeLink=lastSseActivity&&now-lastSseActivity<2500;markConn(!!activeLink);if(!activeLink&&now-lastConnectAttempt>=2500&&!reconnectTimer)forceReconnect()},1000);</script></body></html>";
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
  for (uint8_t b = 0; b < cfg::MAX_BUTTONS; ++b) {
    uart_puts(UART, ("STATE" + std::to_string(b + 1) + ":").c_str());
    uart_puts(UART, button_is_on(b) ? "ON\n" : "OFF\n");
    uart_puts(UART, ("ERROR" + std::to_string(b + 1) + ":").c_str());
    uart_puts(UART, button_feedback_error(b) ? "ON\n" : "OFF\n");
    uart_puts(UART, ("SERROR" + std::to_string(b + 1) + ":").c_str());
    uart_puts(UART, scene_feedback_error[b] ? "ON\n" : "OFF\n");
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
  for (uint8_t b = 0; b < cfg::MAX_BUTTONS; ++b) {
    send_line(("NAME" + std::to_string(b + 1) + ":").c_str(), buttons[b].enabled ? buttons[b].name : std::string());
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
  if (switch_number < 1 || switch_number > cfg::MAX_BUTTONS) return false;

  const char *state = separator + 1;
  if (std::strcmp(state, "ON") == 0) {
    button_command(static_cast<uint8_t>(switch_number - 1), 1);
  } else if (std::strcmp(state, "OFF") == 0) {
    button_command(static_cast<uint8_t>(switch_number - 1), 0);
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
    for (uint8_t b = 0; b < cfg::MAX_BUTTONS; ++b) {
      send_line(("NAME" + std::to_string(b + 1) + ":").c_str(), buttons[b].enabled ? buttons[b].name : std::string());
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
  resolve_gpios();
  configure_inputs();
  init_users();
  if (config_load_result == ConfigLoadResult::Empty) {
    printf("Keine gespeicherte Konfiguration gefunden, Defaults nur im RAM aktiv.\n");
  } else if (config_load_result == ConfigLoadResult::LayoutChanged) {
    persist_write_locked = true;
    printf("Persistenz-Datenstruktur geaendert oder ungueltig, Flash-Daten werden nicht ueberschrieben.\n");
  }
  apply_all_outputs();
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
    if (service_tasters()) {  // physische Taster (je Ausgang mit Rolle==Taster)
      g_sse_dirty = true;
    }
    if (service_relay_feedback()) {  // Rueckmeldungen (je Ausgang mit Rolle==Feedback)
      esp_link_state_dirty = true;
      g_sse_dirty = true;
    }
    if (service_impulses()) {  // abgelaufene Impulse beenden
      esp_link_state_dirty = true;
      g_sse_dirty = true;
    }
    sleep_ms(1);
  }
}
