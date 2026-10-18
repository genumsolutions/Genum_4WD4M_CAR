#pragma once

// =====================================================================
// Genum_4WD4M_CAR — multimode 4WD4M car firmware (ESP32 + L298N)
// File: WebServerComm.h — WiFi web + WebSocket server transport: command
//       queue bridge, JSON status broadcast, WIFICFG provisioning
// Property of GENUM SOLUTIONS PVT. LTD.
// =====================================================================
#include <Arduino.h>
#include "CommInterface.h"
#include "ModeManager.h"
#include "Config.h"
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <Preferences.h>
#include <vector>

#define CMD_QUEUE_LEN 8
#define CMD_BUF_LEN 128
#define WS_PORT 81
#define WIFI_RETRY_INTERVAL_MS 10000ul  // F-20a: WiFi retry backoff
// FIN-40/D2 (T-71a FAIL, 2026-09-18 device gate): CAPPED STA retries. The
// ESP32's default auto-reconnect (and any disconnect/begin churn) drives STA
// scans that blank the softAP beacon, so an unreachable saved router must not
// be retried forever. Auto-reconnect is OFF (beginStation/rejoinNetwork) and
// only STA_MAX_RETRIES explicit attempts (spaced WIFI_RETRY_INTERVAL_MS) run;
// after that the car settles AP-ONLY so 4WD4MCar_WiFi stays rock-solid for
// phone joins. A later ROUTERS;USE / WIFICFG / mode re-entry starts STA fresh.
// R-16 (2026-09-18) budget note, REWRITTEN 2026-10-11 (U-98): the budget now
// covers BOTH the explicit user join (ROUTERS;USE / WIFICFG) AND the boot
// auto-join — they are one policy: join whatever router the owner last chose,
// keep the own hotspot up throughout, and settle AP-only once the capped budget
// is spent (the beacon never blanks, FIN-40/D2).
#define STA_MAX_RETRIES 6
// P7 / U-115 (2026-10-16): BOOT REACHABILITY SCAN — the boot auto-join SCANS
// FIRST.
//
// The boot auto-join used to hand the whole retry budget to a router that may
// not even be switched on. While that budget runs, the softAP FOLLOWS the STA
// channel (`startOwnAP()`'s staSharing branch → apChannel 0, because the STA is
// turning the rejoin step machine), and every `WiFi.begin()` retry makes the STA
// scan. So an absent router parked the own hotspot on a MOVING channel for the
// whole budget: the phone SAW 4WD4MCar_WiFi and could not complete the join —
// the owner's field report 2026-10-16 ("the phone's wifi doesn't connect to the
// car's wifi even though the car's own wifi is detected … it only connects once
// bluetooth is off"), which is exactly the ~STA_MAX_RETRIES × WIFI_RETRY_INTERVAL
// window elapsing, not a Bluetooth effect.
//
// The owner's own rule already asks for this (U-98/U-71): "the car should always
// go to the last switched router … if it is not available it should show its own
// router". So the boot path now runs ONE non-blocking scan before it commits to
// the rejoin: the stored router present → rejoin exactly as before; absent →
// STAY AP-ONLY, own hotspot rock-solid from the first second. An EXPLICIT switch
// (ROUTERS;USE / WIFICFG) is untouched — it keeps the full budget, because a
// person who just chose a network deserves every retry.
//
// Floor: the scan is driven without the collect vector (results are read
// directly and freed with scanDelete()), so it needs far less than the full
// SCAN_HEAP_FLOOR_BYTES. The measured cold-boot heap after the Bluetooth stack
// is ~23.2 kB (E1 waterfall, 2026-10-15), so a 20 kB floor lets the boot scan
// actually run while still refusing on a genuinely starved heap — and a scan
// that cannot complete is treated as INCONCLUSIVE and falls back to the plain
// rejoin, so this can never cost the car its router.
#define BOOT_SCAN_HEAP_FLOOR_BYTES 20000ul
// FIN-40/D1: retry cadence for a softAP() that failed to start (radio busy).
#define AP_RETRY_INTERVAL_MS 2000ul
// U-111: how long the radio stays at the low boot-window power (see
// startOwnAP()) before handle() restores a normal level once.
#define WIFI_BOOT_SETTLE_MS 15000ul
// D3 (U-68): the car-side nearby-network scan. SCAN_SETTLE_MS is the pause
// between "the command asked" and "the radio may be asked" — the radio can
// still be finishing an association, and scanning into that returns nothing.
// SCAN_TIMEOUT_MS bounds the wait so a radio that never finishes reports
// honestly instead of leaving the app waiting. SCAN_MAX_RESULTS caps both the
// kept list and the JSON fragment so the status buffer stays bounded.
// U-71 (2026-10-02) budget note, REWRITTEN 2026-10-11 (U-98), CORRECTED
// 2026-10-16 (P7/U-115): an EXPLICIT switch (ROUTERS;USE / WIFICFG) uses the
// FULL budget (WIFI_STA_TIMEOUT_MS / STA_MAX_RETRIES above). The BOOT
// auto-join uses the same full budget ONLY for a router the boot scan has
// already confirmed is in range (BOOT_SCAN_HEAP_FLOOR_BYTES note above) —
// scanning first is what keeps the own hotspot rock-solid for a phone join,
// because the rejoin's channel-follow is what made a stored-but-absent router
// hold the hotspot hostage. The old claim that "the own hotspot beacon stays UP
// throughout the whole retry" was true of the BEACON but not of its CHANNEL:
// the beacon was broadcast, on a channel that kept moving. Owner rule
// (2026-10-11): a power-up always returns to the last-switched router, and only
// settles on the own hotspot when that router is gone.
// U-76: how often a DEFERRED mDNS publication is retried once the STA settles.
#define MDNS_RETRY_INTERVAL_MS 2000ul
// U-77 (2026-10-03): stalled-client guards — the last two ways loop() could
// still be blocked past the 5 s task watchdog. The WS status broadcast runs
// EVERY SECOND to every client; a phone that stalls (screen off, app killed
// without a close, or its link orphaned by a channel hop) stops draining TCP
// and the next broadcasts fill the socket buffer until a write BLOCKS —
// inside WebSocketsServer, with no breadcrumb, past 5 s, = reboot. The
// heartbeat makes the server PING each client and drop it after two missed
// pongs, so a staller is gone long before its buffers can wedge a write.
// The HTTP guard bounds each write (SO_SNDTIMEO) and the whole root-page
// body by a total budget, so even a request from a slow-but-moving client
// (weak signal right after a network switch) can never occupy loop() for 5 s.
#define WS_HEARTBEAT_PING_MS 3000ul
#define WS_HEARTBEAT_PONG_TIMEOUT_MS 1000ul
#define WS_HEARTBEAT_DROP_AFTER 2
#define HTTP_WRITE_TIMEOUT_MS 500ul

// U-95/F-77: the TOTAL body budget must be strictly BELOW the task watchdog.
// It used to be 6000 ms against a 5000 ms watchdog, so the "mitigation" was
// longer than the thing it was mitigating: a client that stopped reading
// deterministically reset the car, every time. It read like a safeguard because
// it is a number with "budget" in its name, and the comment above it named the
// per-write cap and the yield() as what keeps loop() safe - which is precisely
// the pair that was the defect.
//
// 3500 ms is chosen with the arithmetic visible rather than by feel. Worst case
// is N × (per-write cap) + N × (spin interval), which for a 28 kB page in 512 B
// chunks is 56 × (500 + 10) = 28.6 s of blocked time, so the budget is what
// actually ends the loop, and it must end it before the watchdog (10 s since
// U-95 Phase 1b). 3.5 s leaves 6.5 s of headroom for the HTTP parser's own
// work plus the rest of the loop pass — and the budget did NOT grow when the
// watchdog did; only the untunable library operations forced that move.
// Below the watchdog, with margin - not merely under it.
#define HTTP_BODY_BUDGET_MS 3500ul
// The gap between a zero-byte write and the next attempt. This exists so a
// stalled client cannot turn into a busy spin, and it is the term the budget
// arithmetic above multiplies by the chunk count.
#define HTTP_WRITE_RETRY_GAP_MS 10ul

// The watchdog timeout, in ONE place, so a gate can compare against it. The
// sketch owns the real configuration (esp_task_wdt_config_t in the .ino); this
// is the number the contract gate reads and the reason the two must agree.
//
// U-95 Phase 1b: 5000 was a TIE with the one operation in loop() that nobody
// can shorten. WebSockets 2.7.2 waits WEBSOCKETS_TCP_TIMEOUT = 5000 ms for a
// partial frame inside the library's own translation unit (WebSockets.cpp
// 610-661), and a write to a client that stopped draining TCP retried for 5 s
// of no progress with each inner tcp->write() blocked up to the core's 3 s
// socket send timeout — the U-77 measurement, 5-8 s total. Against a 5000 ms
// watchdog fed once per pass those are not budgets, they are stopwatches
// racing the reset; and the library's timeout cannot be tuned from the sketch
// (#define in our TU is a no-op — read in its own; the owner's Arduino IDE
// passes no build flags; see the armed-handshake rule for the same reasoning).
// So the watchdog moves ABOVE the library's worst single operation: 10000 ms
// sits 5000 ms over the read and 2000 ms over the worst write, and both
// operations are fed immediately before they run in WebServerComm.cpp, so
// neither depends on what the rest of the pass already spent. The cost: a
// genuine hang resets in 10 s instead of 5 s. The benefit: a phone sending a
// half-frame can no longer reboot the car. The gate rule is `ws-budget`.
#define LOOP_TASK_WDT_TIMEOUT_MS 10000ul

// U-112 (2026-10-14). TWO guards for the owner's post-1.3.9 report ("reset when
// I connect BT + own hotspot together" · "freezes using the car's own page
// while the app is connected on Wi-Fi"). Both symptoms have one measured shape
// behind them: the field record shows free_heap at 13-18 kB on every boot
// (device_crash_reports, boot 1026-1029) - the WS-status statics alone are
// 7.2 kB (2 x 3584 B JSON + 512 B page chunk) on top of the BT + Wi-Fi + OLED
// stacks, and the WS coexistence scan allocates the biggest transient block of
// the whole system. Working near the floor, the failures are allocation
// failures: 1) a refused allocation inside buildStatusJson/serveRoot degrades
// silently (snprintf truncation, empty fragments) and, per the read loop below,
// a client that gets a short response keeps asking; 2) a scan whose kick never
// lands lands 8 s later on a SILENTLY EMPTY router list (collectScanIfDone
// clears busy on timeout, so it recovers - but the owner sees "no networks
// found" with no reason, which reads as the list being broken).
//
// BROADCAST_FLOOR_BYTES: below this free heap the status broadcast is
// DEFERRED - but only up to STATUS_HEAP_DEFER_MAX_MS, after which one attempt
// is allowed through regardless. The window is the safety valve: the field
// heap sits in the 13-18 kB zone at connect time, and an UNTIMED defer at a
// chronically low floor would black-hole telemetry forever, which is worse
// than a rare allocation attempt. Effect: under pressure the cadence degrades
// from 1 Hz to one attempt per 4 s (4x less allocation pressure) and recovers
// to 1 Hz the pass the heap is back over the floor.
// 49152 = ~6x the 7268 B the status path provably needs (2 x 3584 B JSON +
// 512 B page chunk), refusing to spend the last kilobytes the run-time needs.
#define STATUS_HEAP_FLOOR_BYTES 20000ul
// How long a below-floor broadcast may be deferred before one attempt is
// allowed through anyway. Telemetry can never be silent longer than this, and
// the gate pins it <= 10000 ms so it can never become an untimed defer.
#define STATUS_HEAP_DEFER_MAX_MS 4000ul
// SCAN_FLOOR_BYTES: the WS coexistence scan is the largest transient alloc of
// the system (driver buffers + the vector reallocation in collectScanIfDone()).
// Below this floor a scan request is REFUSED up front with ROUTERS;SCAN;BUSY
// - an honest, retryable answer the app already renders - instead of kicking
// the radio and landing 8 s later on a silently empty list when the
// allocation never happened. Must stay ABOVE the status floor: the bigger
// transient needs the bigger cushion (gate rule `heap-guard`).
#define SCAN_HEAP_FLOOR_BYTES 65536ul
// P4 (2026-10-17, owner item F / issue 6): LINK ADMISSION.
//
// The owner's report: raising BOTH links at once can FREEZE/reset the car
// because the ESP32 runs near its allocation floor (the field record shows
// free_heap 13-18 kB at the multi-connect resets). Telemetry and scans already
// REFUSE below a floor; a LINK never did, so the second transport came up into
// a starved heap and the run-time starved with it.
//
// `admitSecondLink()` is that missing refusal. It gates only the SECOND link —
// the FIRST is never gated, because a car with no way in is worse than a car
// with one. When the second link is refused the incumbent link is left
// untouched and the refusal is REPORTED (`;LINKCAP=0` on the STATE line,
// `"link_cap":0` in /status) so the app can say "multi-connect is not possible
// right now" instead of drawing a second link that is not there.
//
// The floor sits ABOVE the 20000 B status floor (a second link costs more than
// the status path) and BELOW the 65536 B scan floor (a scan is a one-shot
// transient; a link is a standing cost). It is a BENCH-TUNED value: P0 evidence
// (the owner's multi-connect soak) is owed and may move it — the point of the
// rule is that the refusal EXISTS and is honest, not that this exact number is
// final. The `link-admission` gate rule pins floor > STATUS_HEAP_FLOOR_BYTES.
#define LINK_ADMISSION_FLOOR_BYTES 32000ul
// The root-page send loop: this many CONSECUTIVE zero-byte writes (one retry
// gap each) ends the transfer for THIS pass. A client that moves resets the
// count on every byte, so it keeps the full HTTP_BODY_BUDGET_MS; a reader that
// makes no progress at all used to cost the whole budget on every pass -
// 3500 ms of loop() not running (no ws.loop(), no BT poll: the owner's
// "freezes using the car's own page while the app is connected").
#define HTTP_STALL_ABORT_AFTER 2

#define SCAN_SETTLE_MS 400ul
#define SCAN_TIMEOUT_MS 8000ul
// U-95 Phase 2: the rejoin MODE drop is asynchronous — WiFi.mode() returns
// once the set is ACCEPTED, not once the core's cached mode agrees with it.
// The REJOIN_STEP_MODE_WAIT step polls WiFi.getMode() (which does no radio
// work) for this long before counting the drop as not-taken, then re-issues
// the drop at most REJOIN_MODE_DROP_ATTEMPTS times total; after that the
// machine proceeds with a breadcrumb (`rejoin/mode-AP-not-taken`) rather
// than wedging — honest degradation, never a stuck switch.
#define REJOIN_MODE_SETTLE_MS 500ul
#define REJOIN_MODE_DROP_ATTEMPTS 2

// U-105 (1.3.5): the old beacon PARKING (U-86/U-100, `AP_PARK_GRACE_MS` /
// `AP_PARK_HTTP_MS`) was removed with the power-save switch (owner ruling).
// The own-hotspot beacon is never parked — it is up whenever the Wi-Fi
// transport is enabled, full stop. The only remaining power gate is the
// Wi-Fi/BT transport toggles themselves, which stop the whole layer.
#define SCAN_MAX_RESULTS 12
// T-48 (round-5): saved-router registry — the car is the source of truth.
// Max 6 routers (ssid ≤32 / pass ≤64, no ';' in either — split-rule parity
// with WIFICFG). Names sync OUT only; passwords never leave the car's NVS.
// T-62 (round-9): ROUTERS;CLEAR wipes the whole user registry + stored pair.
// T-66 (round-9): the car's OWN network (WIFI_AP_NAME = 4WD4MCar_WiFi) is a
// protected virtual entry — always FIRST in ROUTERS/NETW/JSON lists while in
// webserver mode, never stored, never deletable; DEL/CLEAR refuse it and
// USE on it reverts the car to its own AP as the default network.
#define MAX_SAVED_ROUTERS 6
#define ROUTER_SSID_LEN 33   // 32 + NUL
#define ROUTER_PASS_LEN 65   // 64 + NUL

class WebServerComm : public CommInterface {
public:
  WebServerComm();
  ~WebServerComm();

  void init();
  bool begin();
  bool beginStation();
  // v2 (owner plan 2026-09-28, Phase 1): ALWAYS-ON web layer — bring up the
  // own AP + HTTP/WS servers once at boot and leave them running for the
  // whole uptime. The donor tied this lifecycle to ESP_SER (SERVER) mode;
  // v2 has no mode-gated transport at all.
  //
  // CORRECTION: this comment used to say stop() is "never called from loop()".
  // That was false. `loop()` DOES call stop() — in the U-79 transport power
  // arbitration, whenever a Bluetooth client takes over and the Wi-Fi layer is
  // allowed to power down — and beginAlwaysOn() re-arms it. Anyone reasoning
  // about the web layer's lifetime from the old text would have concluded it can
  // never be torn down mid-session. It can, by design.
  void beginAlwaysOn();
  // T-34: force a disconnect + reconnect to the CURRENT stored credentials
  // (new WIFICFG pair). Fixes re-provisioning while already in ESP_SER: the
  // rejoin previously sat behind the edge-triggered mode-change check + the
  // WL_CONNECTED retry guard, so the new network only applied on the NEXT
  // mode cycle (owner report 2026-09-15). Non-blocking like beginStation();
  // handle() resolves the pending STA connect (success or AP fallback).
  void rejoinNetwork();
  void stop();
  void handle();
  void setModeManager(ModeManager* mm);

  // ---- WiFi provisioning (v1.4.0: app → BT → car) ----
  // The app sends `WIFICFG;<ssid>;<password>` over the Bluetooth SPP link
  // while the car is in BT mode. The car stores the pair in Preferences
  // (namespace "botcfg") then rejoins the router. `WIFICFG?` asks the car for
  // the stored SSID (never the password) for the app's pre-fill display.
  //
  // CORRECTION: this used to say the car "switches itself to ESP_SER to join
  // the router and host its web page". That stopped being true in v2 — WIFICFG
  // stores and rejoins WITHOUT switching mode (see handleWifiConfigCommand in
  // the .cpp, which says so 55 lines below this header). ESP_SER was removed
  // from the mode registry entirely in U-79; there is no mode to switch to.
  bool handleWifiConfigCommand(const char* cmd);
  // U-105 (1.3.5, owner 2026-10-13): two transport ON/OFF toggles REPLACE the
  // old parallel-links + power-save (APSAVE) switches entirely — their command
  // branches, NVS keys and app settings are all gone (removed as "residues").
  // NVS `botcfg/t_wifi` / `botcfg/t_bt`, set via `WIFICFG;WIFI;ON|OFF` /
  // `WIFICFG;BT;ON|OFF`. Both default ON so a first-flash car is open on both
  // transports at once; the car never runs with BOTH off (the command path and
  // the U-79 loop clamp). `bootAutojoin_` (NVS `botcfg/boot_autojoin`) gates
  // the boot auto-join of the last-switched router (U-98) and ships ON.
  bool wifiEnabled() const { return wifiEnabled_; }
  bool btEnabled() const { return btEnabled_; }
  bool bootAutojoin() const { return bootAutojoin_; }
  // P4 (2026-10-17): link admission — may a SECOND transport be raised right
  // now? Called by the sketch's transport-power loop ONLY when one link is
  // already up (the FIRST link is never gated). Returns false and latches the
  // refusal when the free heap cannot carry a second link; the incumbent link
  // is left up. The verdict is reported via linkCapCode().
  bool admitSecondLink();
  // The LINKCAP report: 1 = a second link is (or could be) admitted; 0 = the car
  // REFUSED it. Only meaningful while BOTH transports are enabled — with one
  // off there is no second link to deny, so the honest answer is 1. Absent on
  // pre-1.3.13 firmware: the app must read ABSENCE as "capable" (P5/F-88), not
  // as a denial.
  int linkCapCode() const;
  // Clear the latched refusal once both links are actually up (or the owner
  // turned a transport off) — a stale refusal must not outlive its cause.
  void clearLinkCapRefusal() { linkCapRefused_ = false; }
  // T-48 (round-5): saved-router registry commands — ROUTERS;LIST /
  // ROUTERS;ADD;<ssid>;<pass> / ROUTERS;USE;<ssid> / ROUTERS;DEL;<ssid>.
  // True when the line was a ROUTERS command (consumed). USE stores the pair
  // as the ACTIVE credentials + rejoinNetwork() (non-blocking, servers survive).
  bool handleRouterCommand(const char* cmd);
  // Load stored credentials (falling back to the compile-time constants)
  // into the module-level WIFI_SSID/WIFI_PASSWORD buffers.
  void loadStoredWifi();
  // T-48: load the saved-router NVS registry at boot (contiguous keys
  // rnet0s/rnet0p …); promotes the stored active pair (wifi_ssid) when it is
  // not already present so the live network always shows in the list.
  void loadSavedRouters();
  // U-98 (2026-10-11): *"the car should be clean and fresh always — clear all
  // the other saved router datas each time i flash."* Arduino flashing does not
  // erase NVS, so saved routers/credentials/last-good survive every re-flash
  // and a boot silently re-joins an outside router the owner had deleted.
  // `botcfg/fw_mark` stores the FW_VERSION; a mismatch (absent on a virgin
  // board, different on a re-install) erases every router-related key and
  // writes the new mark. Power cycles with the SAME firmware keep the saved
  // routers (U-84). Call in setup() BEFORE loadStoredWifi()/loadSavedRouters().
  bool wipeRoutersOnFreshFirmware();
  // T-49: broadcast the registry names-only. SPP line: "NETW;<s1>;<s2>;…"
  // (fed into the remote's saved-networks menu). WS JSON: networks:[...].
  void buildNetworksLine(char* buf, size_t bufLen);
  void buildNetworksJson(char* buf, size_t bufLen);
  /**
   * F-68 (U-68): the acknowledgement form of a command line, with every
   * credential stripped. `ROUTERS;ADD;<ssid>;<pass>` -> `ROUTERS;ADD;<ssid>`.
   * An ack confirms the request; it must never replay it, because the raw line
   * travels back over WebSocket (and over HTTP, where it is also broadcast to
   * every WS client) and would otherwise sit in any proxy log in plain text.
   */
  static void redactForAck(const char* cmd, char* out, size_t outLen);

  /**
   * D3 (U-68, 2026-10-02): the car-side nearby-network scan.
   *
   * `ROUTERS;SCAN` existed in the app since the Connections-Hub round and the
   * car answered `ROUTERS;ERROR;Syntax` every time — there was no "networks
   * near the car" list anywhere in the system. This is it.
   *
    * ASYNCHRONOUS ON PURPOSE: `WiFi.scanNetworks()` blocks for seconds. Called
    * from the command path it would starve loop() and trip the task watchdog
    * (F-24), rebooting the car — the exact failure F-60 was written to stop.
    * So the command only ARMS the scan; handle() runs and collects it.
    * U-95 Phase 2: answers. false = refused (a scan already in flight, or the
    * STA is mid-join), so the caller replies `ROUTERS;SCAN;BUSY` instead of
    * claiming STARTED for a scan it silently dropped. Gate: `scan-honest`.
    */
  bool startScan();
  void runScanIfDue();
  void collectScanIfDone();
  /**
   * P7 / U-115: the boot auto-join's "is the stored router actually in range?"
   * probe. `startBootReachabilityScan()` ARMS it (false when the heap is below
   * BOOT_SCAN_HEAP_FLOOR_BYTES or another scan is in flight, so the caller can
   * fall back to a plain rejoin); `serviceBootReachabilityScan()` kicks the
   * non-blocking pass and, when the radio reports, either hands off to
   * `rejoinNetwork()` (present, or the result was inconclusive) or leaves the
   * car AP-only (absent). One scan, never a retry loop on the command path.
   */
  bool startBootReachabilityScan();
  void serviceBootReachabilityScan();
  /** SPP form: `SCAN;<ssid>,<rssi>,<1|0>;…` or `SCAN;NONE`. */
  void buildScanLine(char* buf, size_t bufLen) const;
  /** WS JSON form: `[{"ssid":…,"rssi":…,"open":true|false},…]`. */
  size_t buildScanJson(char* buf, size_t bufLen) const;
  int savedRouterCount() const { return savedRouterCount_; }
  const char* savedRouterSsid(int i) const {
    return (i >= 0 && i < MAX_SAVED_ROUTERS) ? savedRouterSsid_[i] : "";
  }
  const char* savedRouterPass(int i) const {
    return (i >= 0 && i < MAX_SAVED_ROUTERS) ? savedRouterPass_[i] : "";
  }
  // True when runtime credentials have been stored via WIFICFG.
  bool hasStoredWifi() const { return hasStoredWifi_; }
  // SSID currently stored ("" when none) — safe to show in the app.
  //
  // U-70 (2026-10-02): THIS IS THE ROUTER WE ASKED FOR, NOT THE ONE WE ARE ON.
  // Reporting it as "the network the car is on" is a lie the moment a join
  // fails - exactly what the owner saw: the car displayed the new router's name
  // while it never left its own hotspot. Anything that claims to describe the
  // CURRENT network must use joinedSsid() instead (U-70 rule 1).
  //
  // The two are different questions and must stay separate: "which router did
  // the user ask for" vs "which router are we on".
  const char* storedSsid() const { return storedSsid_; }
  // The network the car is ACTUALLY on: the joined STA SSID when associated,
  // the car's own broadcast id on its hotspot, "" when neither. The only value
  // that may be presented as the current network.
  String joinedSsid() const;

  // ---- U-71 (2026-10-02): the deferred rejoin state machine ------------
  // (U-70 deferred only the `WiFi.begin()` via a `staBeginPending_` flag; U-71
  // replaces that entirely, because deferring one call still left `WiFi.mode()`
  // and `softAP()` running inside the command path.)
  // rejoinNetwork() does NO radio work; it only arms this, and handle()
  // advances it with ONE radio call per loop pass. That is what stops a router
  // switch from occupying loop() long enough to trip the 5 s task watchdog —
  // and a WDT trip is a reboot (F-24), which is the most plausible cause of
  // "the car restarted as soon as I tried to access the webpage".
  // U-95 Phase 2 (2026-10-07): drop -> wait -> restore -> raise -> begin.
  // U-90's step ran mode(WIFI_AP_STA) and claimed a driver restart, but the
  // installed core (WiFiGeneric.cpp:602) returns EARLY when the requested
  // mode equals the current one — and after the first switch of the car's
  // life the mode IS already WIFI_AP_STA, so every later switch performed no
  // teardown at all. The machine now DROPS the mode (WIFI_AP is always
  // unequal to AP_STA, so the call always does real work), polls the core
  // until it agrees (0 radio calls), restores the hotspot while the STA is
  // down (the one window staBusy() cannot refuse), raises AP_STA (also
  // always unequal), and only then begins. One radio call per pass (U-71)
  // is preserved. Gate rule: `switch-teardown`.
  enum RejoinStep : uint8_t {
    REJOIN_STEP_IDLE = 0,
    REJOIN_STEP_MODE,       // WiFi.mode(WIFI_AP) — drop the stale STA association
    REJOIN_STEP_MODE_WAIT,  // poll WiFi.getMode() until the drop took (no radio)
    REJOIN_STEP_AP,         // startOwnAP() — while the STA is down
    REJOIN_STEP_RAISE,      // WiFi.mode(WIFI_AP_STA) — STA returns on a clean slate
    REJOIN_STEP_BEGIN,      // WiFi.begin(ssid, pass)
  };

  // U-86 (owner): "the esp's own router seems to be active for the connection
  // even when the car is working with another external route, is that necessary
  // or is it impossible to [stop]? if not, fix this and save power and energy
  // as much as you can."
  //
  // Answer: NOT necessary, and it is a real cost. One radio serves both, but a
  // live softAP still beacons, keeps up to 4 associated stations, and holds the
  // driver out of low-power states. Measured on an ESP32 the beacon alone is
  // tens of mA.
  //
  // U-87 CORRECTION, found on the bench. The first version parked the beacon
  // whenever the STA was connected, and that made the car UNREACHABLE: it joined
  // a router the laptop was not on, the STA came up, the beacon went down, and
  // there was no way in at all. "The car is on a router" does not mean "you can
  // reach that router".
  //
  // So the beacon is parked ONLY while a client that does NOT need it is using
  // the car - a phone on the ROUTER - and it comes straight back when that
  // client leaves:
  //
  //   in use -> parked  (power saved, when it is safe to)
  //   idle   -> up      (always reachable, which is the safety property)
  //
  // It is a beacon, not the web server: HTTP :80 and WS :81 stay bound and the
  // page keeps working on the router's address throughout.
  //
  // CORRECTION (2026-10-10, owner: "the car is not connected through its own
  // hotspot for the phone after some period of time"). A client count alone
  // cannot say WHICH side of the car a client arrived on, so a phone sitting on
  // 4WD4MCar_WiFi counted as a reason to PARK 4WD4MCar_WiFi - roughly 20 s after it
  // opened the page the car dropped the very link it was being driven over. A
  // client on the AP is proof the beacon is REQUIRED, never that it is spare.
  // U-105 (1.3.5): parking was REMOVED entirely (owner ruled the power-save
  // button and its residues gone), so updateApPowerPolicy() only ever GUARANTEES
  // the beacon - it never drops it. The own hotspot is always up.
  void updateApPowerPolicy();
  uint32_t staConnectedAtMs_;   // when the current association came up
  // U-105 (1.3.5): the beacon is never parked (the parking feature and the
  // `apParked_` flag were removed), so `staConnectedAtMs_` is the only beacon-
  // side state that remains. `apRunning_` is declared below with the radio
  // stack state it actually belongs to.
  // U-105 (1.3.5, owner 2026-10-13): the own-hotspot beacon is NEVER parked.
  // The old power-save (U-100 `ap_psave`) and parallel-links (U-100 Phase 4
  // `allow_parallel`) switches + NVS keys are gone; updateApPowerPolicy() only
  // restores the beacon when the STA is gone. Defaults ON so a first-flash car
  // is open on both transports at once (see the getters above).
  bool wifiEnabled_ = true;
  bool btEnabled_ = true;
  bool bootAutojoin_ = true;
  // P4 (2026-10-17): latched when admitSecondLink() refuses a second transport.
  // Cleared the moment both links are up (or a transport is toggled off). Only
  // reported while both transports are enabled (see linkCapCode()).
  bool linkCapRefused_ = false;
void advanceRejoinStep();
  uint8_t rejoinStep_;
  // U-95 Phase 2: MODE_WAIT bookkeeping. modeWaitStartMs_ is the base of the
  // current drop's settle window; modeDropAttempts_ bounds the re-issues so a
  // core that never confirms the drop cannot hold the machine forever.
  unsigned long modeWaitStartMs_;
  uint8_t modeDropAttempts_;

  // U-75 (2026-10-02): OWNER HARD EVIDENCE - the device finally told us why.
  //
  //   [BOOT] reset reason: 6 (TASK WATCHDOG (loop hung >5s))
  //
  // That is ESP_RST_TASK_WDT: `loop()` was blocked for more than 5 s, so the
  // watchdog fired and its action IS a reboot. It is not a crash and not a
  // brownout — which is exactly what had been suspected for weeks. The fault is
  // a BLOCKING CALL IN LOOP.
  //
  // `advanceRejoinStep()` above already solved this for the switch path: one
  // radio call per pass, loop() returns between steps. The one place that still
  // violated it was the GIVE-UP fallback, which ran, in a single pass:
  //
  //     WiFi.disconnect(); WiFi.mode(WIFI_AP); startOwnAP();
  //
  // i.e. a teardown plus a full WiFi driver RESTART plus a softAP bring-up,
  // back to back. `WiFi.mode()` is the expensive one — it stops and restarts the
  // driver — and doing that while an association is being torn down is the most
  // expensive case there is.
  //
  // U-71 made this path far more likely to be taken: boot auto-join gives every
  // power-up ONE short attempt at the last-good router, so an unreachable router
  // now reaches the give-up branch on EVERY boot rather than only after a user
  // asked for a switch. The hazard and the new exposure arrived together.
  //
  // So the same treatment: one radio call per pass, loop() fed in between.
  enum GiveUpStep : uint8_t {
    GIVEUP_IDLE = 0,
    GIVEUP_DISCONNECT,  // WiFi.disconnect()
    GIVEUP_MODE,        // WiFi.mode(WIFI_AP) — the driver restart, on its own
    GIVEUP_AP,          // startOwnAP() — softAPConfig() + softAP()
  };
  void giveUpStepMachine();
  uint8_t giveUpStep_;

  // U-75: name the blocking step BEFORE it runs. A hang produces no serial
  // output of its own, so the last breadcrumb printed is the step that hung —
  // which turns the next occurrence from a hunt into a diagnosis. This is the
  // cheap half of the fix; splitting the passes above is the real one.
  void noteRadioStep(const char* step);
  // U-76: STA busy = connecting OR connected (DHCP included). While true, no
  // driver/netif config set is legal - see startOwnAP().
  bool staBusy() const;
  // U-76: mDNS deferred until the STA settles, then retried by handle().
  bool mdnsPending_;
  unsigned long lastMdnsRetryMs_;

  // Remember the router that ACTUALLY associated, so a boot can retry it and a
  // failed switch cannot overwrite a working router with a broken one.
  void saveLastGoodRouter();
  void armBootAutoJoin();
  char lastGoodSsid_[ROUTER_SSID_LEN];
  char lastGoodPass_[ROUTER_PASS_LEN];

  // Per-attempt association window and the retry cap. U-98 (2026-10-11): the
  // boot auto-join and an explicit user switch now share the SAME full budget —
  // a power-up returns to the last-switched router, and an unreachable one ends
  // in the give-up path, which restores the own hotspot (up the whole time).
  unsigned long staTimeoutMs_;
  uint8_t staRetryCap_;
  // R-12: the AP fallback broadcast id (4WD4MCar_WiFi, T-56) once started;
  // empty before.
  const char* apBroadcastId() const { return apName_.length() > 0 ? apName_.c_str() : ""; }
  // R-13 (owner matrix 2026-09-15): the car's current reachable IP — the
  // joined station IP when WiFi is up, else the AP fallback (192.168.245.1
  // since FIN-48 — previously the SDK default 192.168.4.1).
  // Empties buf when neither is active. Rides `;IP=` on STATE lines and the
  // status JSON `ip` for the OLED / hand-held remote / app to display.
  void currentIp(char* buf, size_t bufLen);
  // R-12: ModeManager::sendState() reads WiFi truth (SSID / AP id) for STATE lines.
  static WebServerComm* instance() { return instanceForWs; }
  // Connections-Hub (2026-09-28): the car's stable BOARD identity — the last
  // 6 hex digits of ESP.getEfuseMac() (uppercase), e.g. "1A2B3C". This is the
  // per-device profile key (`fw:<id>`) the app uses for WiFi-only cars. Rides
  // `;ID=` on STATE lines and the status JSON `id`. Stable across renames.
  static void boardIdHex(char* out, size_t outLen);
  // Reply produced by the last consumed WIFICFG line ("" when none).
  const String& lastWifiReply() const { return lastWifiReply_; }
  void clearLastWifiReply() { lastWifiReply_ = ""; }

  // ---- U-108 Phase 7 (owner 2026-10-13): fresh start + timestamps ----------
  // The car has no RTC/NTP of its own, so the APP is the clock: on every connect
  // it sends `TIME;<unixEpoch>`, and the car derives the current epoch from that
  // base + its own millis(). Every config write then stamps `botcfg/config_epoch`
  // so a stale record is provable, and `FACTORY;RESET` wipes the whole `botcfg`
  // namespace for a clean slate. See TRACKS/RECORDS-INVENTORY.md.
  //
  // `FACTORY;RESET` clears EVERY `botcfg` key (routers, credentials, toggles,
  // mode/speed, crash counters), restores the RAM defaults and
  // replies `FACTORY;CLEARED`. The next power cycle then boots a genuinely fresh
  // car (there is deliberately NO mid-command reboot: the reply must reach the
  // app first, and a reboot would drop the very link that carries it).
  bool handleFactoryCommand(const char* cmd);

  // P6 (2026-10-17, owner item D): the car's EVENT-LOG verbs (system-hook
  // commands, every mode, every transport; replies ride STATE as REPLY=…):
  //
  //   LOG / LOG?          `LOG;COUNT=<n>;CAP=12` — how many events the ring
  //                       holds and its hard cap.
  //   LOG;GET;<n>         newest-first window starting at entry n, three at a
  //                       time (the REPLY= slot holds ~190 bytes; bounded so
  //                       one reply can never overflow it): `LOG;GET;<n>;<n>
  //                       =<total>;K,<boot>,<ms>,<heap>,<reason>,<phase>;…`.
  //                       Each event is SIX comma fields (kind number, boot
  //                       counter, millis, free heap, reset reason, phase).
  //                       A car with NO events answers an empty `LOG;GET;
  //                       0;<n>=0;` — honest, parseable, never a guess.
  //   LOG;CLEAR           drop every stored event → `LOG;CLEARED`.
  //
  // Contract rule `event-log` pins the shapes; the app half is P7.
  bool handleLogCommand(const char* cmd);
  // Adopt the app-provided epoch (seconds). 0 = unsynced.
  void syncEpoch(uint32_t epochSeconds);
  // Current epoch (seconds), or 0 when the app has never synced this session.
  uint32_t nowEpoch() const;
  // The epoch stamped on the last config write (0 = never stamped).
  uint32_t configEpoch() const { return configEpoch_; }

  bool isRunning() const;
  // v1.0.1 (owner OLED directive 2026-09-28): differentiated link truth for
  // the dashboard's Link/Type rows. hasWsClients() = a controller is linked
  // over the WebSocket (the ACTIVE control path); isStaJoined() = the STA
  // side joined a router (server reachable on the LAN even with no client).
  // (Non-const: WebSocketsServer::connectedClients() is not const-safe.)
  bool hasWsClients();
  // U-112: true while the status broadcast is currently below the heap floor
  // and inside its defer window (STATUS_HEAP_FLOOR_BYTES / _DEFER_MAX_MS).
  bool broadcastDeferred();
  bool isStaJoined() const;

  // CommInterface overrides
  size_t available() override;
  int read() override;
  size_t readBytes(char* buf, size_t len, unsigned long timeoutMs = 0) override;
  int peek() override;
  size_t write(const char* buf, size_t len) override;
  bool send(const char* msg) override;
  void flush() override;
  bool connected() override;

  // Queue helpers
  bool queuePush(const char* s);
  bool queuePopChar(char* out);
  void queueClear();

  // Public wrapper to refresh extra info immediately
  void refreshExtraInfo();

  // ---- U-74 (2026-10-02): mDNS advertisement --------------------------
  // The car advertises itself as `genum-car.local` on whatever network it is
  // on, re-publishing on every network change. This exists because of a
  // STRUCTURAL consequence of AP_STA, not a UI bug: the softAP and the STA
  // share one radio and therefore one channel, so when the car joins a router
  // its hotspot MOVES to that router's channel and the phone's hotspot link
  // drops. With no link the app has no address for the car, so the drive deck
  // cannot open. mDNS is what makes the car findable BY NAME once the phone is
  // on the router - the alternative being the user reading an IP off the OLED
  // and typing it.
  //
  // NOTE for the app side: React Native cannot resolve a `.local` name without a
  // NATIVE MODULE, and a native module cannot be delivered by an OTA - it needs
  // a new APK. The advertisement is therefore the prerequisite half; the
  // resolver is queued behind that APK (see the app ledger). Until then the
  // phone can still reach the car by the address the OLED shows.
  static constexpr const char* MDNS_HOSTNAME = "genum-car";
  void startMdns();
  void stopMdns();
  bool mdnsRunning_;

private:
  void handleRoot();
  void handleMove(const char* cmd);
  void handleSpeed();
  void handleMode();
  void handleStatus();
  void handleNotFound();
  void buildStatusJson(char* buf, size_t bufLen);

  // U-108 Phase 7: stamp the app-provided epoch onto an ALREADY-OPEN writable
  // "botcfg" handle. NVS shares one handle per namespace, so the stamp must ride
  // the caller's open handle rather than opening a second one (U-77). No-op when
  // the app has not synced (nowEpoch()==0) — an unsynced car stays unstamped.
  void stampConfigWrite(Preferences& p);

  // U-77: bounded HTTP serving. armHttpGuard() caps every write of the
  // CURRENT request's socket (SO_SNDTIMEO); serveRootBounded() additionally
  // caps the TOTAL time spent writing the ~18 kB page body, so a stalled or  // crawling client can never hold loop() past the task watchdog. On a
  // hopeless link the page arrives truncated — the client is dead anyway,
  // and a truncated page beats a rebooted car.

  void armHttpGuard();
  void serveRootBounded();

  void setupWebSocket();
  void handleWebSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length);
  void broadcastStatus();

  char cmdQueue[CMD_QUEUE_LEN][CMD_BUF_LEN];
  size_t cmdHead;
  size_t cmdTail;
  size_t cmdCount;
  size_t readPos;

  // F-20a: non-blocking STA connect + guarded retry. connectingPending_ =
  // a rejoinNetwork() is in flight (resolved by handle() on connect, or on
  // staTimeoutMs_ → AP fallback).
  //
  // CORRECTION: this said "begin()/rejoinNetwork()" and "WIFI_STA_TIMEOUT_MS".
  // Both were wrong. `begin()` is a no-op stub ({ return true; }) that exists
  // only to satisfy the pure virtual — nothing calls it, the real entry is
  // beginAlwaysOn(). And handle() resolves on `staTimeoutMs_`, which on the boot
  // path now uses the SAME full budget as an explicit switch (U-98, 2026-10-11).
  // Reading the old text would have meant budgeting the wrong number.
  //
  // pendingRejoin_ (T-34) forces reconnection to the NEW credentials even
  // while WiFi still reports WL_CONNECTED to the OLD network.
  bool connectingPending_;
  bool pendingRejoin_;
  // U-95 Phase 2 (retry split): the retry's WiFi.disconnect() and
  // WiFi.begin() no longer share ONE handle() pass. The disconnect arms
  // this; the begin lands on the NEXT pass, with its own breadcrumb, so the
  // two radio calls never run back to back inside one loop iteration — the
  // U-75 shape that rebooted the car on the give-up path — and the begin
  // cannot race a driver that is still tearing the old association down.
  // Cleared by rejoinNetwork()/beginStation()/stop(): a deferred begin must
  // never outlive the request that armed it (the U-75 pattern).
  bool retryBeginPending_;
  unsigned long connectStartMs_;
  unsigned long lastWifiRetryMs_;
  // FIN-40/D2: capped-STA-retry bookkeeping (see STA_MAX_RETRIES above).
  uint8_t staRetryCount_;
  bool staGiveUpLogged_;
  // FIN-40/D1: retry cadence state for a softAP() that failed to start.
  unsigned long lastApRetryMs_;

  // U-116: page-server power + heap gate. See handleRoot() and the
  // contract rules (ws-page-gate / page-serve-heap) in the track files for
  // the owner's post-1.3.9 multi-connect report.
  //
  // pageOff_         - the webpage server (:80/:81) is OFF while true.
  //                    Loaded from NVS `botcfg` on boot via loadStoredWifi(),
  //                    toggled by the app during a live session, persisted
  //                    in NVS so a power restart keeps it.
  //
  // pageOffAtBoot_   - set true on boot if NVS `page_off` was true. It is
  //                    then cleared and only re-asserted by a live
  //                    `WIFICFG;WIFI;OFF` command, so a power restart does
  //                    not leak into later command calls.
  //
  // pageOffHeld_     - true when pageOff_ is false but the heap gate is
  //                    holding the page server down to protect the app's
  //                    run-time (multi-connect heap freeze). The page is
  //                    never served while this is true.
  //
  // serveGhostPage() - the tiny 200 response that answers a client that
  //                    asked for / while the page server is OFF, so the
  //                    transport layer stays up and the HTTP/WS stack is
  //                    not killed, but no page bytes flow.
  //
  // serveRootBounded() is the bounded page send (F-77 budget). It is called
  // only when pageOff_ == false; when the page server is OFF the client
  // receives this ghost page instead of connecting to / and blocking
  // loop() for the full HTTP_BODY_BUDGET_MS, which is what strangles the BT
  // stack, the WS and the mode ticker during a page load (the multi-connect
  // car heap freeze).
  //
  // heapReserve_     - the page-serve budget is the LAST thing the app may
  //                    lose on a heap-constrained car. It is the spare
  //                    above the BT-engaged floor: with BT on, free heap
  //                    collapses ~110 kB -> ~10 kB, so a gate that keys
  //                    only on the total is too coarse. This is the margin
  //                    the page serve needs above what the run-time still
  //                    has; the page is kept UP for the app while BT is
  //                    busy, and dropped only once that reserve is truly
  //                    gone.
  //
  // pageServerIsOn() - true when the page server is allowed to serve.
  // pageServerIsHeld() - true when the page server is waiting to be
  //                      enabled (not yet allowed by the heap gate).
  // pageServerMustBeHeld() - true when free heap (with BT engaged) is below
  //                          the reserve the page serve needs. This is what
  //                          stops a multi-connect session from blocking
  //                          loop() long enough to strangle the BT stack
  //                          and the WS.
  // setPageOff(bool) - toggle the page-server power and persist the choice
  //                    to NVS so a power restart keeps it. `on = true`
  //                    means the page server is allowed; false means the
  //                    page server is OFF while the transport stays up.
  //
  // The page server is the app's last-resort heap consumer. While BT is
  // up and free heap is near its floor, the run-time needs every spare
  // kilobyte. Holding the page server (ghost page instead of a real
  // transfer) keeps loop() free to feed BT, WS and the mode ticker, which
  // is the exact mechanism that fixed the owner's "reset when BT + own
  // hotspot together" / "freezes using the page while the app is on Wi-Fi"
  // report.
  bool pageOff_;
  bool pageOffAtBoot_;
  bool pageOffHeld_;

  // Public: is the webpage server currently allowed to serve?
  bool pageServerIsOn() const { return !pageOff_; }

  // Public: is the page server currently waiting to be enabled (not yet
  // allowed by the heap gate)?
  bool pageServerIsHeld() const { return pageOffHeld_; }

  // Heap gate: should the page server be held back to protect the app's
  // run-time? True when free heap (with BT engaged) is below the reserve
  // the page serve needs. This is what stops a multi-connect session from
  // blocking loop() long enough to strangle the BT stack and the WS.
  bool pageServerMustBeHeld() const;

  // Toggle the page-server power, and persist the choice to NVS so a power
  // restart keeps it. `on` true = page server allowed; false = page server
  // off (transport stays up).
  void setPageOff(bool on);

  // Tiny 200 response for a client that asked for / while the page server
  // is OFF. Keeps the transport layer and HTTP/WS stack alive without
  // sending page bytes, so loop() is never blocked for the full page
  // budget while the page is muted.
  void serveGhostPage();
  WebSocketsServer ws{WS_PORT};  // static object
  bool serverRunning;
  bool startedAsAP;
// T-65 (round-9) said the car's OWN WiFi stays broadcast for the WHOLE
  // webserver session, even while a router is joined, "so the hotspot is never
  // hidden". U-86 later allowed parking it for power; U-105 (1.3.5) removed
  // that entirely per the owner — the own hotspot is broadcast whenever the
  // Wi-Fi transport is enabled, never "pure cost", always a way in.
  //
  // `apRunning_` = the softAP interface is up (the CLAIM, forced false in a few
  // places by FIN-40/D1). The old park flag that lived beside it was removed.
  bool apRunning_;
  // U-111: true while the AP radio is still on the low boot-window power
  // (startOwnAP() sets it; handle() raises to full power once, after the settle
  // window — re-armed by every bring-up so a late softAP() also ends at full
  // power).
  bool apTxPowerLow_;

  ModeManager* modeManager;

  // ---- WiFi provisioning state ----
  bool hasStoredWifi_;
  char storedSsid_[33];   // WiFi SSID max = 32 bytes + NUL
  char storedPass_[65];   // WiFi WPA2 password max = 64 bytes + NUL
  // T-48: saved-router registry (the car is the source of truth; passwords stay
  // in NVS `botcfg` and are never printed / sent off-device).
  char savedRouterSsid_[MAX_SAVED_ROUTERS][ROUTER_SSID_LEN];
  char savedRouterPass_[MAX_SAVED_ROUTERS][ROUTER_PASS_LEN];
  int savedRouterCount_;
  // Last WIFICFG reply, consumed by ModeManager's system-command hook so the
  // answer reaches the app over whichever comm is active.
  String lastWifiReply_;

  // U-108 Phase 7: the app-provided clock. `epochBase_` is the last Unix epoch
  // the app sent; `epochSetMs_` is millis() at that moment, so the live epoch is
  // `epochBase_ + (millis() - epochSetMs_)/1000`. `epochSynced_` guards a 0
  // epoch. `configEpoch_` mirrors the last value stamped into NVS so the app can
  // read it back without touching flash.
  bool epochSynced_;
  uint32_t epochBase_;
  unsigned long epochSetMs_;
  uint32_t configEpoch_;

  // D3 (U-68): the nearby-network scan. One pass at a time, armed by the
  // command and driven by handle(); scanNetworks_ survives until the next scan
  // so the app can read it at leisure (and so a passive status broadcast keeps
  // carrying it). scanCount_ is the raw pass count (diagnostics only).
  struct ScannedNetwork {
    char ssid[33];
    // U-95 Phase 2: dedupe key — one row per RADIO (BSSID), not per NAME.
    // Internal only: deliberately NOT in the SPP line or the status JSON —
    // no consumer needs the radio's MAC; the table is name/rssi/channel truth.
    char bssid[18];  // "AA:BB:CC:DD:EE:FF"
    int32_t rssi;
    // U-95 Phase 3: the channel this result arrived on — a table column the
    // car can answer and a client cannot. WiFi.channel(i) at collect time,
    // while the scan results are still live in the driver.
    uint8_t channel;
    bool open;
  };
  bool scanPending_;
  // U-112 (in-class initialisers: these are read from the first handle() pass,
  // before begin() could set them - a bare member would be indeterminate).
  // `statusDeferStreak_` marks an ACTIVE defer (logged once on each edge, so
  // the serial capture shows the defer instead of a dashboard that quietly
  // went quiet); `statusDeferSinceMs_` is when the current below-floor window
  // opened (the untimed-defer safety valve); `scanFloorRefusals_` counts
  // scans refused for low heap so the refusal is visible in the log, not
  // silent.
  bool statusDeferStreak_ = false;
  unsigned long statusDeferSinceMs_ = 0;
  uint32_t scanFloorRefusals_ = 0;
  bool scanBusy_;
  unsigned long scanStartMs_;
  unsigned long scanWaitMs_;
  int scanCount_;
  std::vector<ScannedNetwork> scanNetworks_;
  // P7 / U-115: the boot auto-join's reachability probe. `bootScanArmed_` is set
  // by armBootAutoJoin() and cleared when the probe resolves; `bootScanKicked_`
  // marks that the async pass has been issued (so the arm/kick/evaluate phases
  // never repeat); `bootScanWaitMs_` bounds the wait exactly like
  // scanWaitMs_ does for a user scan. Read from the first handle() pass, so the
  // in-class initialisers keep them deterministic before begin().
  bool bootScanArmed_ = false;
  bool bootScanKicked_ = false;
  unsigned long bootScanWaitMs_ = 0;
  // AP fallback broadcast id ("4WD4MCar_WiFi", T-56), shown on the OLED and
  // in the status JSON ("ap") so the app/website can display it.
  String apName_;

  char pendingModeBuf[32];
  unsigned long pendingModeAtMs;

  void updateExtraInfo();

  // T-65 (round-9) + v2: bring the car's own hotspot (4WD4MCar_WiFi) up and
  // keep apRunning_/apName_ consistent. Idempotent; retried by handle() on a
  // softAP failure (FIN-40/D1) — the AP is up for the WHOLE uptime in v2.
  void startOwnAP();
  // T-66 (round-9) + v2: the reserved own-AP entry is ALWAYS listed — the
  // web layer (and its routers list) is always-on, no longer SERVER-scoped.
  bool showReservedEntry() const;

  static WebServerComm* instanceForWs;
};