// =====================================================================
// Genum_4WD4M_CAR — multimode 4WD4M car firmware (ESP32 + L298N)
// File: RelayComm.cpp — U-94 Phase 2: the Internet-method transport.
//
// Contract implemented here, from the relay side (website, supabase/functions/
// car-relay/relay-core.ts). Everything below was read off that file rather
// than assumed, because the three defects found while writing this file were
// all assumptions that had never met the other end:
//
//   * the relay caps car->controller at 448 (the car's own STATE buffer) and
//     controller->car at 128 (handleCommand's buffer), so kLineLen is 128;
//   * boardIdHex is SIX uppercase hex, `%06X` of efuseMac & 0xFFFFFF - the same
//     string that goes in `STATE;...;ID=`;
//   * the token is 32 LOWERCASE hex (the regex is lowercase-only);
//   * the relay originates `hello-required` and `paired`, and forwards the
//     latency pair. Everything else in the control plane is refused BY NAME,
//     which is why an unknown `ev` here is logged loudly rather than ignored.
//
// TRUST: beginSslWithCA with the ISRG Root X1 bundle, never setInsecure.
// `setInsecure()` on a car that accepts drive commands over the public
// Internet is a MITM invitation with wheels. The full Mozilla store would be
// ~150 KB of .rodata for no benefit: Supabase serves a chain to ISRG Root X1,
// and one verified anchor is the entire trust decision here.
//
// THE TOKEN IS A CREDENTIAL. It is written to NVS, sent once in the hello, and
// never printed - not in a log line, not in a STATE, not in REPLY=, not in an
// error message. Every acknowledgement below is a fixed literal for that
// reason; none of them interpolate anything. tools/check-relay-contract.mjs
// fails CI if a printf-family call ever mentions the token, or if a reply stops
// being a plain literal.
// Property of GENUM SOLUTIONS PVT. LTD.
// =====================================================================
#include "RelayComm.h"

#include "Config.h"
#include "WebServerComm.h"
#include <WiFi.h>

namespace {

// ISRG Root X1 - the Let's Encrypt root. Self-signed, SHA-256, RSA 4096,
// valid to 2035-06-04. Extracted from Node's trust store, not retyped.
const char kIsrgRootX1[] PROGMEM = R"(
-----BEGIN CERTIFICATE-----
MIIFazCCA1OgAwIBAgIRAIIQz7DSQONZRGPgu2OCiwAwDQYJKoZIhvcNAQELBQAw
TzELMAkGA1UEBhMCVVMxKTAnBgNVBAoTIEludGVybmV0IFNlY3VyaXR5IFJlc2Vh
cmNoIEdyb3VwMRUwEwYDVQQDEwxJU1JHIFJvb3QgWDEwHhcNMTUwNjA0MTEwNDM4
WhcNMzUwNjA0MTEwNDM4WjBPMQswCQYDVQQGEwJVUzEpMCcGA1UEChMgSW50ZXJu
ZXQgU2VjdXJpdHkgUmVzZWFyY2ggR3JvdXAxFTATBgNVBAMTDElTUkcgUm9vdCBY
MTCCAiIwDQYJKoZIhvcNAQEBBQADggIPADCCAgoCggIBAK3oJHP0FDfzm54rVygc
h77ct984kIxuPOZXoHj3dcKi/vVqbvYATyjb3miGbESTtrFj/RQSa78f0uoxmyF+
0TM8ukj13Xnfs7j/EvEhmkvBioZxaUpmZmyPfjxwv60pIgbz5MDmgK7iS4+3mX6U
A5/TR5d8mUgjU+g4rk8Kb4Mu0UlXjIB0ttov0DiNewNwIRt18jA8+o+u3dpjq+sW
T8KOEUt+zwvo/7V3LvSye0rgTBIlDHCNAymg4VMk7BPZ7hm/ELNKjD+Jo2FR3qyH
B5T0Y3HsLuJvW5iB4YlcNHlsdu87kGJ55tukmi8mxdAQ4Q7e2RCOFvu396j3x+UC
B5iPNgiV5+I3lg02dZ77DnKxHZu8A/lJBdiB3QW0KtZB6awBdpUKD9jf1b0SHzUv
KBds0pjBqAlkd25HN7rOrFleaJ1/ctaJxQZBKT5ZPt0m9STJEadao0xAH0ahmbWn
OlFuhjuefXKnEgV4We0+UXgVCwOPjdAvBbI+e0ocS3MFEvzG6uBQE3xDk3SzynTn
jh8BCNAw1FtxNrQHusEwMFxIt4I7mKZ9YIqioymCzLq9gwQbooMDQaHWBfEbwrbw
qHyGO0aoSCqI3Haadr8faqU9GY/rOPNk3sgrDQoo//fb4hVC1CLQJ13hef4Y53CI
rU7m2Ys6xt0nUW7/vGT1M0NPAgMBAAGjQjBAMA4GA1UdDwEB/wQEAwIBBjAPBgNV
HRMBAf8EBTADAQH/MB0GA1UdDgQWBBR5tFnme7bl5AFzgAiIyBpY9umbbjANBgkq
hkiG9w0BAQsFAAOCAgEAVR9YqbyyqFDQDLHYGmkgJykIrGF1XIpu+ILlaS/V9lZL
ubhzEFnTIZd+50xx+7LSYK05qAvqFyFWhfFQDlnrzuBZ6brJFe+GnY+EgPbk6ZGQ
3BebYhtF8GaV0nxvwuo77x/Py9auJ/GpsMiu/X1+mvoiBOv/2X/qkSsisRcOj/KK
NFtY2PwByVS5uCbMiogziUwthDyC3+6WVwW6LLv3xLfHTjuCvjHIInNzktHCgKQ5
ORAzI4JMPJ+GslWYHb4phowim57iaztXOoJwTdwJx4nLCgdNbOhdjsnvzqvHu7Ur
TkXWStAmzOVyyghqpZXjFaH3pO3JLF+l+/+sKAIuvtd7u+Nxe5AW0wdeRlN8NwdC
jNPElpzVmbUq4JUagEiuTDkHzsxHpFKVK7q4+63SM1N95R1NbdWhscdCb+ZAJzVc
oyi3B43njTOQ5yOf+1CceWxG1bQVs5ZufpsMljq4Ui0/1lvh+wjChP4kqKOJ2qxq
4RgqsahDYVvTH9w7jXbyLeiNdd8XM2w9U/t7y0Ff/9yi0GE44Za4rF2LN9d11TPA
mRGunUHBcnWEvgJBQl9nJEiU0Zsnvgc/ubhPgXRR4Xq37Z0j4r7g1SgEEzwxA57d
emyPxgcYxn/eR44/KJ4EBs+lVDR3veyJm+kXQ99b21/+jh5Xos1AnX5iItreGCc=
-----END CERTIFICATE-----
)";

// The relay's own cap for what the CAR may send (maxBytesCarToController).
// If the firmware's STATE buffer ever shrinks below this, the relay will
// happily forward frames the car then truncates mid-field - the exact class of
// bug U-94 Phase 1 shipped with. Asserted against CMD_BUF_LEN by the gate.
static_assert(RELAY_MAX_OUT_BYTES >= 448,
              "relay admits 448 bytes from the car; STATE is built into 448");

// The two numbers that must agree across the wire, asserted here so they cannot
// drift in this repo: the relay admits 128 bytes controller->car, and
// handleCommand() copies into CMD_BUF_LEN. If the relay's cap ever rose above
// this, it would deliver commands the car truncates mid-token — a command that
// changes meaning when it is cut. The relay's own number is checked in ITS repo;
// this side can only guarantee it never grows.
static_assert(RelayComm::kLineLen == CMD_BUF_LEN,
              "relay's inbound cap (128) is CMD_BUF_LEN; if this changes, change the relay too");
static_assert(RELAY_MAX_OUT_BYTES >= 448, "STATE is built into a 448-byte buffer");

// 2 s heartbeat, 5 s pong timeout, give up after 2 misses. This is not
// politeness: without it a half-open TCP connection (phone in a lift) leaves
// `connected()` true, and a link that claims to be connected is a link that
// has switched the 2 s safe-stop OFF. Liveness has to be measured, not assumed.
constexpr unsigned long kHeartbeatMs = 2000;
constexpr unsigned long kPongTimeoutMs = 5000;
constexpr uint8_t kMaxPongMisses = 2;

// Redial backoff. The relay is optional, so this must not be aggressive: a car
// on its own AP with no token, or a router that is simply not there yet, would
// otherwise be dialling every second for the life of the battery.
constexpr unsigned long kRedialMs = 15000;

// How long to let a socket finish closing before re-arming with new credentials
// (RELAYCFG). Long enough for the close handshake, short enough that the owner
// has not yet reached for the reset button.
constexpr unsigned long kRestartDelayMs = 500;

}  // namespace

// The library's event callback is a plain function pointer, so the instance has
// to be reachable statically. One RelayComm exists; the gate asserts there is
// exactly one.
static RelayComm* sInstance = nullptr;

void RelayComm::onEventTrampoline(WStype_t type, uint8_t* payload, size_t length) {
  if (sInstance) sInstance->handleEvent(type, payload, length);
}

// ---- lifecycle --------------------------------------------------------

bool RelayComm::begin() {
  if (started_) return true;

  // No token means no dial. Not "dial and get refused": an unauthenticated car
  // is a car that appears to want the Internet on the owner's bench and cannot
  // explain why it never connects.
  loadToken();
  if (!hasToken()) {
    FW_LOG("[RELAY] no token stored - send RELAYCFG;<32 hex> over BT or the page");
    return false;
  }

  WebServerComm::boardIdHex(boardId_, sizeof(boardId_));
  if (boardId_[0] == '\0') {
    FW_LOG("[RELAY] could not read the board id");
    return false;
  }

  sInstance = this;
  client_.beginSslWithCA(RELAY_HOST, RELAY_PORT, RELAY_PATH, kIsrgRootX1);
  client_.setReconnectInterval(kRedialMs);
  client_.enableHeartbeat(kHeartbeatMs, kPongTimeoutMs, kMaxPongMisses);
  client_.onEvent(onEventTrampoline);

  started_ = true;
  wantsLink_ = true;
  restartAtMs_ = 0;
  FW_LOG("[RELAY] dialling %s:%d/%s as %s", RELAY_HOST, RELAY_PORT, RELAY_PATH, boardId_);
  return true;
}

void RelayComm::restart() {
  if (!started_) {
    begin();
    return;
  }
  // The disconnect is finished by loop(), so the redial waits a beat and the
  // pump keeps running in the meantime. Without the delay this would call
  // begin() on a client that is still closing its socket, which is the kind of
  // thing that works once on the bench and never again.
  restartAtMs_ = millis() + kRestartDelayMs;
  enrolled_ = false;
  sentHello_ = false;
  queueClear();
}

void RelayComm::handle() {
  // The library reconnects from loop(). It also does all socket I/O here, so
  // NOT calling this is the same as not having a relay.
  if (restartAtMs_ != 0 && static_cast<long>(millis() - restartAtMs_) >= 0) {
    restartAtMs_ = 0;
    client_.disconnect();
    started_ = false;
    begin();
    return;
  }
  if (!started_) return;
  client_.loop();
}

void RelayComm::stop() {
  if (!started_ && restartAtMs_ == 0) return;
  restartAtMs_ = 0;
  client_.disconnect();
  sInstance = nullptr;
  started_ = false;
  wantsLink_ = false;
  enrolled_ = false;
  sentHello_ = false;
  queueClear();
}

void RelayComm::noteDisconnect() {
  if (enrolled_) FW_LOG("[RELAY] link lost");
  enrolled_ = false;
  sentHello_ = false;
  // Anything still queued was meant for a link that no longer exists. A command
  // that arrives after the safe-stop is worse than one that never arrives.
  queueClear();
}

// ---- events -----------------------------------------------------------

void RelayComm::handleEvent(WStype_t type, uint8_t* payload, size_t length) {
  switch (type) {
    case WStype_CONNECTED:
      FW_LOG("[RELAY] socket up, saying hello");
      sendHello();
      break;

    case WStype_TEXT:
      // Not NUL-terminated: the library gives a length. The grammar only ever
      // sees NUL-terminated lines (queuePush copies and terminates).
      if (!payload || length == 0) break;
      handleTextFrame(reinterpret_cast<const char*>(payload), length);
      break;

    case WStype_DISCONNECTED:
      // No close code available: this version of the library does not surface
      // the peer's code to the client. Rather than invent one, the useful
      // diagnostic is the `refused` control frame, which arrives first and says
      // WHY - "token does not match", "car already connected", and so on.
      FW_LOG("[RELAY] socket down");
      noteDisconnect();
      break;

    default:
      break;
  }
}

void RelayComm::handleTextFrame(const char* frame, size_t len) {
  // THE GRAMMAR LOCK, enforced by the byte value rather than by care.
  // handleCommand() reads a C string, and '\0' cannot appear inside one, so a
  // control frame is structurally incapable of being parsed as a car line -
  // the check below is the ONLY thing standing between the relay's control
  // plane and the drive-command parser, so it happens first, before any
  // inspection of contents.
  if (isControlFrame(frame, len)) {
    if (controlEventIs(frame, "paired")) {
      enrolled_ = true;
      FW_LOG("[RELAY] paired - link is live");
    } else if (controlEventIs(frame, "hello-required")) {
      // The relay asks for the hello explicitly on open. Sending ours on
      // connect as well is deliberate: whichever arrives second is a no-op,
      // and a car that waited for a prompt would never hello a relay that
      // changed its mind about prompts.
      sendHello();
    } else if (controlEventIs(frame, "ping")) {
      answerPing(frame);
    } else if (controlEventIs(frame, "link-down")) {
      FW_LOG("[RELAY] relay says the controller is gone");
      // Not a transport failure - the socket is fine. But the phone has gone,
      // so the car is no longer commanded, and the safe-stop must not be
      // suppressed by a link that carries nothing.
      enrolled_ = false;
      queueClear();
    } else if (controlEventIs(frame, "lagging")) {
      // The relay is dropping our frames because the phone is not keeping up.
      // Saying so is the point: silence here would look like a dead link.
      FW_LOG("[RELAY] phone is behind - car frames are being dropped");
    } else if (controlEventIs(frame, "refused")) {
      // Refused BY NAME, so this is the diagnostic worth having. The detail
      // carries no credential (the relay never echoes the token).
      FW_LOG("[RELAY] refused by relay: control frame not accepted");
    } else {
      // Loud, because the relay refuses unknown events by name too. A version
      // skew between car and relay should be obvious here, not diagnosed later.
      FW_LOG("[RELAY] unknown control frame ignored");
    }
    return;
  }

  // A car line. Queue it; do not parse it, do not act on it, do not reorder it.
  if (len > kLineLen) {
    FW_LOG("[RELAY] dropped inbound line of %u bytes (max %u)", static_cast<unsigned int>(len),
           static_cast<unsigned int>(kLineLen));
    return;
  }
  if (!queuePush(frame)) FW_LOG("[RELAY] inbound queue full - line dropped");
}

void RelayComm::sendHello() {
  if (!client_.isConnected() || !hasToken()) return;
  char hello[192];
  // v is the relay's RELAY_PROTOCOL_VERSION; a mismatch is refused with a name.
  int n = snprintf(hello, sizeof(hello),
                   "{\"v\":1,\"role\":\"car\",\"boardIdHex\":\"%s\",\"token\":\"%s\","
                   "\"fwVersion\":\"%s\"}",
                   boardId_, token_, FW_VERSION);
  if (n <= 0 || n >= static_cast<int>(sizeof(hello))) {
    FW_LOG("[RELAY] hello did not fit - refusing to send a truncated one");
    return;
  }
  client_.sendTXT(hello);
  sentHello_ = true;
}

void RelayComm::answerPing(const char* frame) {
  if (!client_.isConnected()) return;
  // The relay FORWARDS the phone's ping so the phone measures the phone's
  // round trip to the car. It never answered on the phone's behalf - that is
  // what made the latency number meaningless. So the echo carries the
  // timestamp the phone chose, untouched.
  unsigned long t = controlNumber(frame, "\"t\"");
  char echo[128];
  int n = snprintf(echo, sizeof(echo), "%s{\"ev\":\"ping-echo\",\"boardIdHex\":\"%s\",\"t\":%lu}",
                   kControlPrefix, boardId_, t);
  if (n > 0 && n < static_cast<int>(sizeof(echo))) client_.sendTXT(echo);
}

// ---- outbound ---------------------------------------------------------

size_t RelayComm::write(const char* buf, size_t len) {
  if (!buf || len == 0) return 0;
  if (!connected()) return 0;

  // One line per frame. ModeManager fans out whole STATE/NETW/SCAN lines
  // ending in '\n'; splitting on it here means the phone sees lines, not one
  // growing blob, and a partial line can never be mistaken for a complete one.
  size_t sent = 0;
  size_t start = 0;
  for (size_t i = 0; i < len; i++) {
    if (buf[i] != '\n') continue;
    size_t n = i - start + 1;
    if (i > start && n < RELAY_MAX_OUT_BYTES) {
      if (client_.sendTXT(buf + start, n)) sent += n;
    }
    start = i + 1;
  }
  // A tail with no newline: forwarded, because dropping the last line of a
  // state burst would be a silent lie about the car's state.
  if (start < len && len - start < RELAY_MAX_OUT_BYTES) {
    if (client_.sendTXT(buf + start, len - start)) sent += len - start;
  }
  return sent;
}

bool RelayComm::send(const char* msg) {
  if (!msg || !connected()) return false;
  return client_.sendTXT(msg);
}

void RelayComm::flush() {
  // Nothing to flush: sendTXT hands the frame to the library, which writes it
  // from handle(). Claiming otherwise here would be inventing a guarantee.
}

bool RelayComm::connected() {
  // Socket AND enrolled. A car refused with close 4401 is not "connected" -
  // and if it read as connected, it would switch the safe-stop off.
  return started_ && client_.isConnected() && enrolled_;
}

// ---- inbound queue (identical shape to the web transport's) -----------

bool RelayComm::queuePush(const char* line) {
  if (count_ >= kQueueLen) {
    // Drop the OLDEST, not the newest: a command queue is a stream, and the
    // newest line is the one the owner most recently asked for.
    head_ = (head_ + 1) % kQueueLen;
    count_--;
  }
  strncpy(queue_[tail_], line, kLineLen - 1);
  queue_[tail_][kLineLen - 1] = '\0';
  tail_ = (tail_ + 1) % kQueueLen;
  count_++;
  return true;
}

size_t RelayComm::available() {
  if (count_ == 0) return 0;
  size_t cur = strlen(queue_[head_]) - readPos_;
  if (count_ > 1) cur += 1;  // the '\n' between lines
  return cur;
}

int RelayComm::read() {
  if (count_ == 0) return -1;
  char c = queue_[head_][readPos_];
  if (c == '\0') {
    head_ = (head_ + 1) % kQueueLen;
    count_--;
    readPos_ = 0;
    return '\n';
  }
  readPos_++;
  return c;
}

int RelayComm::peek() {
  if (count_ == 0) return -1;
  char c = queue_[head_][readPos_];
  return c == '\0' ? '\n' : c;
}

size_t RelayComm::readBytes(char* buf, size_t len, unsigned long timeoutMs) {
  // Single-shot: the car drains this transport from ModeManager::run(), which
  // already has its own 20 ms budget on readLineFromComm(). Blocking here for
  // a timeout would stall the whole loop - including the socket pump that keeps
  // this transport alive - so the timeout is accepted for interface parity and
  // only ever waited on when nothing at all was available.
  size_t n = 0;
  const unsigned long start = millis();
  do {
    while (n < len && available() > 0) {
      int c = read();
      if (c < 0) break;
      buf[n++] = static_cast<char>(c);
    }
    if (n > 0 || timeoutMs == 0) break;
    delay(1);
  } while (millis() - start < timeoutMs);

  if (n < len) buf[n] = '\0';
  return n;
}

void RelayComm::queueClear() {
  head_ = tail_ = 0;
  count_ = 0;
  readPos_ = 0;
}

// ---- control-frame helpers -------------------------------------------

bool RelayComm::isControlFrame(const char* frame, size_t len) {
  if (!frame || len < kControlPrefixLen) return false;
  // memcmp, not strncmp: the frame is NOT NUL-terminated, and the prefix
  // contains a NUL, so every string function is wrong here by construction.
  return memcmp(frame, kControlPrefix, kControlPrefixLen) == 0;
}

bool RelayComm::controlEventIs(const char* frame, const char* ev) {
  // Deliberately not a JSON parser. There are six event names, they are
  // produced by one function on the other end, and a dependency plus a heap
  // allocation to read `{"ev":"paired"}` would be a poor trade on a car. An
  // unrecognised frame falls to the loud branch in handleTextFrame.
  char needle[24];
  int n = snprintf(needle, sizeof(needle), "\"ev\":\"%s\"", ev);
  if (n <= 0 || n >= static_cast<int>(sizeof(needle))) return false;
  return strstr(frame, needle) != nullptr;
}

unsigned long RelayComm::controlNumber(const char* frame, const char* key) {
  const char* p = strstr(frame, key);
  if (!p) return 0;
  p = strchr(p, ':');
  if (!p) return 0;
  return strtoul(p + 1, nullptr, 10);
}

// ---- provisioning -----------------------------------------------------

bool RelayComm::isTokenShape(const char* s) {
  if (!s) return false;
  size_t n = strlen(s);
  if (n != kTokenLen) return false;
  for (size_t i = 0; i < n; i++) {
    char c = s[i];
    // Lowercase hex only, because the relay's regex is lowercase-only. An
    // uppercase paste is rejected HERE rather than being quietly down-cased:
    // silently accepting it would let the owner believe they had pasted the
    // token they were given when in fact they pasted a different spelling of
    // it, and then be baffled by a 4401.
    bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    if (!ok) return false;
  }
  return true;
}

void RelayComm::loadToken() {
  token_[0] = '\0';
  Preferences prefs;
  if (!prefs.begin("botcfg", true)) return;
  prefs.getString("relay_token", token_, kTokenLen + 1);
  prefs.end();
  if (!isTokenShape(token_)) {
    // A token of the wrong shape is treated as no token at all: an NVS value
    // written by an older build, or a half-finished write, must not turn into
    // an authentication attempt that is guaranteed to fail.
    token_[0] = '\0';
  }
}

bool RelayComm::storeToken(const char* token) {
  Preferences prefs;
  if (!prefs.begin("botcfg", false)) return false;
  size_t n = prefs.putString("relay_token", token);
  prefs.end();
  return n == kTokenLen;
}

bool RelayComm::handleRelayConfigCommand(const char* cmd) {
  if (!cmd) return false;

  static const char kCmd[] = "RELAYCFG";
  size_t n = strlen(cmd);
  if (n < sizeof(kCmd) - 1) return false;
  for (size_t i = 0; i < sizeof(kCmd) - 1; i++) {
    if (toupper(static_cast<unsigned char>(cmd[i])) != kCmd[i]) return false;
  }
  if (n > sizeof(kCmd) - 1 && cmd[sizeof(kCmd) - 1] != ';') return false;

  const char* arg = (n > sizeof(kCmd) - 1) ? cmd + sizeof(kCmd) : "";  // after ';'
  lastReply_[0] = '\0';

  // Every reply below is a FIXED STRING. None of them contains a %s, and none
  // of them is built from the argument. `RELAYCFG;SHOW` says whether a token is
  // stored and never what it is - a provisioning command whose acknowledgement
  // contains the secret is a command that prints the secret to any serial log
  // in the room, including the phone's.
  if (arg[0] == '\0') {
    reply("RELAYCFG;USAGE;RELAYCFG;<32 hex>|RELAYCFG;RESET|RELAYCFG;SHOW");
    return true;
  }

  if (strcasecmp(arg, "RESET") == 0) {
    Preferences prefs;
    if (prefs.begin("botcfg", false)) {
      prefs.remove("relay_token");
      prefs.end();
    }
    stop();
    loadToken();
    reply("RELAYCFG;CLEARED");
    return true;
  }

  if (strcasecmp(arg, "SHOW") == 0) {
    loadToken();
    reply(hasToken() ? "RELAYCFG;SET" : "RELAYCFG;NOT_SET");
    return true;
  }

  if (!isTokenShape(arg)) {
    reply("RELAYCFG;ERROR;token must be exactly 32 lowercase hex characters");
    return true;
  }
  if (!storeToken(arg)) {
    reply("RELAYCFG;ERROR;could not write NVS");
    return true;
  }
  strncpy(token_, arg, kTokenLen);
  token_[kTokenLen] = '\0';
  // Redial now rather than at the next boot: the owner just gave us a token
  // and is sitting in front of the car.
  restart();
  FW_LOG("[RELAY] token stored for %s", boardId_[0] ? boardId_ : "this car");
  reply("RELAYCFG;STORED");
  return true;
}

void RelayComm::reply(const char* text) {
  strncpy(lastReply_, text, sizeof(lastReply_) - 1);
  lastReply_[sizeof(lastReply_) - 1] = '\0';
}
