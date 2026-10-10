#pragma once

// =====================================================================
// Genum_4WD4M_CAR — multimode 4WD4M car firmware (ESP32 + L298N)
// File: RelayComm.h — U-94 Phase 2: the Internet-method transport.
//
// WHAT THIS IS: a WebSocket CLIENT that dials OUT to the cloud relay, over
// the STA the car already joins (U-71 auto-join), authenticates with the
// board id + the per-car token, and then behaves like any other transport:
// lines in, lines out, nothing else.
//
// WHAT THIS IS NOT, and the mistake that keeps getting made in this project:
// it is not a second command grammar. It does not know what `F`, `SPD180`,
// `REQ_STATE` or `STATE;` mean. It queues whatever arrives and lets
// ModeManager::run() parse it through the ONE handleCommand() there is
// (T-1). The relay's own control frames are NUL-prefixed and are consumed
// HERE, never forwarded into the grammar.
//
// WHY A THIRD TRANSPORT SLOT EXISTS AT ALL: the car already has two
// (comm_ = web, auxComm_ = Bluetooth) and ModeManager is what makes a
// transport real. A transport that is not in all four of those places is a
// transport that does not drain, does not receive STATE, does not count as
// connected — and a link that does not count as connected SUPPRESSES THE
// 2 s SAFE-STOP. See tools/check-relay-contract.mjs, which fails CI if a
// CommInterface subclass is missing from any of them.
//
// U-94 PHASE 2 — THE DIAL LIVES ON ITS OWN TASK. This is the F-84 fix, and it
// is the reason this file grows a FreeRTOS task. On the loop task, a relay
// host that completed TCP and then stalled TLS held WebSocketsClient::loop()
// for up to NetworkClientSecure's 120000 ms handshake_timeout, against a
// loop-task watchdog measured in wall-clock milliseconds — so the car reset
// every time the relay was slow to answer. WebSocketsClient keeps its
// WiFiClientSecure private with no setter and deletes/recreates it on every
// retry, so the timeout cannot be shortened from here. The only fix available
// to this firmware is to run the dial (and every client_.loop()) on a task
// that is NOT subscribed to that watchdog, so the loop task keeps feeding its
// own watchdog and keeps running motor safety while the handshake waits.
//
// That moves real ownership, not just a call: `client_` is touched ONLY by
// the relay task. The loop task talks to it through two lock-guarded queues
// (lines in, lines out) and one published scalar (`socketUp_`, filled by the
// task from client_.isConnected() right after every loop()). `connected()`
// therefore gates on the PUBLISHED socket flag and on enrolment; the gate's
// `enrolment` rule was changed deliberately to check both halves of that fact
// (the flag is fed from the live socket) rather than let a second source of
// truth drift from the socket it describes (F-82).
//
// NO STATE, NO QUEUE-FOR-RETRY: a frame the relay could not deliver is
// dropped there, not parked here. Replaying a stale `F` is worse than never
// sending it.
//
// 4WD4M TESTBED ONLY (plan §4 Phase 2): donor cars are on the 86% partition
// and have no room for a TLS stack.
// Property of GENUM SOLUTIONS PVT. LTD.
// =====================================================================
#include "CommInterface.h"
#include <Arduino.h>
#include <Preferences.h>
#include <WebSocketsClient.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

class RelayComm : public CommInterface {
public:
  // One line per frame, and a line is what handleCommand() reads: this must
  // stay equal to the firmware's command buffer (CMD_BUF_LEN, 128) or the
  // relay will happily deliver a command the car silently truncates. The gate
  // asserts the two are equal so they cannot drift.
  static constexpr size_t kQueueLen = 8;
  static constexpr size_t kLineLen = 128;

  // The outbound queue holds WHOLE frames, and a frame can be a full STATE
  // line, which the relay admits up to RELAY_MAX_OUT_BYTES (448). The .cpp
  // static_asserts this equals RELAY_MAX_OUT_BYTES rather than trusting the
  // number here, so the two cannot drift.
  static constexpr size_t kOutQueueLen = 8;
  static constexpr size_t kOutLineLen = 448;

  /** The relay's control plane: NUL + "relay". A NUL byte cannot start a car
   *  line (handleCommand reads a C string), so a control frame and a car line
   *  can never be confused — the same property the relay relies on, and the
   *  grammar lock is structural rather than a matter of looking carefully. */
  static constexpr char kControlPrefix[] = "\x00" "relay";
  static constexpr size_t kControlPrefixLen = 6;

  /** Token shape, fixed by the relay: 32 lowercase hex (16 bytes). A truncated
   *  paste must fail loudly at provisioning time, not silently at handshake. */
  static constexpr size_t kTokenLen = 32;

  RelayComm() = default;
  ~RelayComm() override = default;

  RelayComm(const RelayComm&) = delete;
  RelayComm& operator=(const RelayComm&) = delete;

  // ---- CommInterface -------------------------------------------------
  bool begin() override;
  size_t available() override;
  int read() override;
  size_t readBytes(char* buf, size_t len, unsigned long timeoutMs = 0) override;
  int peek() override;
  size_t write(const char* buf, size_t len) override;
  bool send(const char* msg) override;
  void flush() override;
  bool connected() override;

  // ---- lifecycle -----------------------------------------------------
  /** Supervisor, called every loop. The relay task does its own I/O and
   *  reconnection from its own loop; this no longer pumps the client. Its job
   *  now is to watch the task's heartbeat and drop the link flags if the task
   *  stops ticking, so a relay that has wedged can never keep reading as a
   *  live link (and so can never suppress the 2 s safe-stop). */
  void handle();
  /** Disconnect and forget nothing. Idempotent. The task itself closes the
   *  socket; this only clears the want-a-link state. */
  void stop();
  /** Re-arm with the current credentials. The task owns the disconnect, so
   *  this only requests it — the reconnect happens on the task, not here. */
  void restart();

  /** True when the socket is up AND the relay has accepted us (`paired`).
   *  Deliberately stricter than "the TCP socket is open": a car refused with
   *  close 4401 must never read as connected, because a link that claims to be
   *  connected switches the safe-stop off. */
  bool enrolled() const { return enrolled_; }
  /** Attempting or established — used by the transport power policy, which
   *  must keep the radio up while the relay needs it. Reads the socket flag
   *  the relay task publishes; the loop task must not touch client_ itself. */
  bool isDialing() { return socketUp_ || wantsLink_; }

  // ---- provisioning (the WIFICFG/ROUTERS command family) -------------
  /** `RELAYCFG;<32 hex>` stores the token, `RELAYCFG;RESET` forgets it,
   *  `RELAYCFG;SHOW` says whether one is stored and NEVER what it is.
   *  Returns true when the line was a RELAYCFG line at all.
   *
   *  The token is a credential: it is never echoed, never logged, and never
   *  appears in STATE, REPLY= or any control frame except the hello. */
  bool handleRelayConfigCommand(const char* cmd);
  const char* lastReply() const { return lastReply_; }
  void clearLastReply() { lastReply_[0] = '\0'; }

  bool hasToken() const { return token_[0] != '\0'; }

private:
  // ---- the relay task (U-94 Phase 2) ---------------------------------
  static void taskTrampoline(void* arg);
  void taskLoop();
  void configureClient();

  static void onEventTrampoline(WStype_t type, uint8_t* payload, size_t length);
  void handleEvent(WStype_t type, uint8_t* payload, size_t length);
  void handleTextFrame(const char* frame, size_t len);
  void sendHello();
  void answerPing(const char* frame);
  void noteDisconnect();

  bool queuePush(const char* line);
  // NOTE: `queuePopChar` used to be declared here and defined NOWHERE. It was a
  // leftover from copying WebServerComm's queue, whose RelayComm twin does not
  // have — RelayComm::read() indexes the queue directly. Benign only because
  // nothing called it; the first call site would have been an undefined
  // reference at link time. Removed rather than left as a trap.
  void queueClear();

  bool outPush(const char* line, size_t len);
  bool outPop(char* dst, size_t cap);
  void outClear();

  static bool isControlFrame(const char* frame, size_t len);
  static bool controlEventIs(const char* frame, const char* ev);
  static unsigned long controlNumber(const char* frame, const char* key);
  static bool isTokenShape(const char* s);
  void loadToken();
  bool storeToken(const char* token);
  void reply(const char* text);

  WebSocketsClient client_;

  // Inbound lines, drained by ModeManager::run() — exactly like the web
  // transport's queue, which is what keeps this on the one command path.
  // Filled by the relay task, drained by the loop task: guarded by lock_.
  char queue_[kQueueLen][kLineLen];
  size_t head_ = 0;
  size_t tail_ = 0;
  size_t count_ = 0;
  size_t readPos_ = 0;

  // Outbound lines, filled by the loop task, drained by the relay task.
  // Guarded by lock_.
  char outQ_[kOutQueueLen][kOutLineLen];
  size_t outHead_ = 0;
  size_t outTail_ = 0;
  size_t outCount_ = 0;

  char token_[kTokenLen + 1] = "";
  char boardId_[16] = "";
  char lastReply_[96] = "";

  // ---- task + shared state (U-94 Phase 2) -----------------------------
  // Every member below is read or written by BOTH tasks, so each is volatile
  // and every multi-word structure is guarded by lock_. client_ is the one
  // member that is NOT shared: it belongs to the relay task alone.
  TaskHandle_t task_ = nullptr;
  SemaphoreHandle_t lock_ = nullptr;  // guards queue_/outQ_ and the flags below

  volatile bool started_ = false;     // the relay task exists
  volatile bool enabled_ = false;     // a live link is wanted
  volatile bool configured_ = false;  // client_ has been configured on the task
  volatile bool wantsLink_ = false;   // the power policy wants the radio up
  volatile bool enrolled_ = false;    // the relay accepted us (paired)
  volatile bool sentHello_ = false;
  volatile bool socketUp_ = false;    // published from client_.isConnected() by the task
  volatile bool restartRequested_ = false;
  volatile unsigned long taskTickMs_ = 0;  // the task's heartbeat, for the supervisor
};
