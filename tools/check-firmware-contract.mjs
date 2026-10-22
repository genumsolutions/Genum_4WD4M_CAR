// U-111: the firmware contract gate. This file is `check-relay-contract.mjs`
// with the U-94 Phase 2 RELAY rules removed (RelayComm.cpp/.h are gone with the
// Internet method): control-frame, TLS, token, armed-handshake, enrolment,
// inert-by-default, caps/inbound-fit and singleton are all dropped with the
// transport they described.
//
// EVERY OTHER RULE SURVIVES HERE: the transport seams + grammar lock, the
// restart record (F-79), the watchdog & budget gates (F-60/F-77), the
// WebSocket pump / guard / budget gates (F-75/F-76), the OLED extra-row
// decoupling (U-98 P2), the router-switch teardown + NVS-persistence + retry
// split (U-95 Phase 2), the scan honesty/quality rules, the mode-cycle walk
// and the status network fields (U-95 Phase 3), and the board-id consumer
// comparison.
//
// They are here, not "merged into mutate-gates.mjs": the mutation harness runs
// this gate as `gateContract` and asserts one mutant per rule, so a rule that
// leaves this file does not retire - it goes UNDETECTED (measured: 35
// undetected mutants the round this file was cut down to the relay rules).
//
// HOW THIS FILE READS SOURCE (F-83): structural rules run against `code()` -
// comments and string/char literals blanked - so prose can never satisfy a
// rule. Rules that must see LITERALS (a reply string, a strcmp token) run
// against `stripComments()`, which keeps string literals and blanks only the
// comments - `code()` blanks strings, raw text lets a comment that NAMES the
// token pass for code. Every rule states which view it reads, and every rule
// is mutation-proven BOTH directions by tools/mutate-gates.mjs.

import { existsSync, readFileSync } from "node:fs";
import { createHash, X509Certificate } from "node:crypto";
import { rootCertificates } from "node:tls";
import { fileURLToPath } from "node:url";
import { dirname, join } from "node:path";

const root = join(dirname(fileURLToPath(import.meta.url)), "..");
const raw = (p) => readFileSync(join(root, p), "utf8");
const read = (p) => (existsSync(join(root, p)) ? raw(p) : "");

/** Source with comments and string/char literals blanked out, so rules match
 *  code and never the prose around it. */
const code = (text) =>
  text
    .replace(/\/\*[\s\S]*?\*\//g, (m) => m.replace(/[^\n]/g, " "))
    .replace(/\/\/[^\n]*/g, (m) => " ".repeat(m.length))
    .replace(/"(?:\\.|[^"\\])*"/g, (m) => '"' + " ".repeat(Math.max(0, m.length - 2)) + '"')
    .replace(/'(?:\\.|[^'\\])*'/g, (m) => "'" + " ".repeat(Math.max(0, m.length - 2)) + "'");

/** Comments blanked, string literals KEPT. The only view that is both: raw
 *  text lets a comment naming a token pass for code; `code()` blanks the
 *  literal the rule exists to check. */
const stripComments = (text) =>
  text
    .replace(/\/\*[\s\S]*?\*\//g, (m) => m.replace(/[^\n]/g, " "))
    .replace(/\/\/[^\n]*/g, (m) => " ".repeat(m.length));

/** Extract a function body by brace matching, so "mentions it somewhere in the
 *  file" cannot pass for "mentions it in this function". */
function body(text, signature) {
  const at = text.indexOf(signature);
  if (at < 0) return null;
  const open = text.indexOf("{", at);
  if (open < 0) return null;
  let depth = 0;
  for (let i = open; i < text.length; i++) {
    if (text[i] === "{") depth++;
    else if (text[i] === "}" && --depth === 0) return text.slice(open, i + 1);
  }
  return null;
}

/** Extract the braced block that starts at `from`, by brace matching. Used where
 *  a fixed-size character window would be wrong: comments and formatting decide
 *  how much text sits between two lines of code, and a window that happens to be
 *  long enough today silently stops covering the code after a reflow. */
function braced(text, from) {
  const open = text.indexOf("{", from);
  if (from < 0 || open < 0) return null;
  let depth = 0;
  for (let i = open; i < text.length; i++) {
    if (text[i] === "{") depth++;
    else if (text[i] === "}" && --depth === 0) return text.slice(open, i + 1);
  }
  return null;
}

let failures = 0;
const fail = (rule, detail) => {
  failures++;
  console.error(`  FAIL  ${rule}\n        ${detail}`);
};
const pass = (rule, detail) => console.log(`  ok    ${rule} - ${detail}`);

// ---- 1. the transport seams --------------------------------------------
// Each CommInterface subclass must appear in all four ModeManager functions
// plus the sketch's pump. The class list is derived from the headers, so a
// SECOND transport added later is covered by this rule for free.

const mmCode = code(raw("ModeManager.cpp"));
const inoCode = code(raw("Genum_4WD4M_CAR.ino"));
const cfg = raw("Config.h");
const wsc = raw("WebServerComm.cpp");
const wsh = raw("WebServerComm.h");
const wscCode = code(wsc);

const headers = ["BluetoothComm.h", "WebServerComm.h", "CommInterface.h"];
const transports = headers
  .flatMap((h) => [...raw(h).matchAll(/class (\w+)\s*:\s*public CommInterface/g)].map((m) => m[1]))
  .filter((v, i, a) => a.indexOf(v) === i);

if (transports.length < 2) {
  fail("transport-seam", `only found ${transports.length} CommInterface subclasses: ${transports}`);
} else {
  pass("transport-seam", `${transports.length} transports: ${transports.join(", ")}`);
}

const memberFor = { BluetoothComm: "auxComm_", WebServerComm: "comm_", CommInterface: "g_2wd1mHandler" };
const alsoAccepts = { WebServerComm: "WebServerComm", BluetoothComm: "BluetoothComm" };

for (const t of transports) {
  const member = memberFor[t];
  if (!member) {
    fail("member-known", `${t} has no member name here - add it, or its seam checks are a no-op`);
    continue;
  }
  for (const [label, sig, allowClassName] of [
    ["ModeManager::run() drains it", "void ModeManager::run()", false],
    ["ModeManager::sendState() fans out to it", "void ModeManager::sendState()", false],
    ["ModeManager::isConnectedNow() counts it", "bool ModeManager::isConnectedNow()", false],
    ["ModeManager::currentLink() reports it", "ModeManager::LinkInfo ModeManager::currentLink()", true],
  ]) {
    const fnBody = body(mmCode, sig);
    const alt = alsoAccepts[t];
    const hit = fnBody?.includes(member) || (allowClassName && fnBody?.includes(alt));
    if (!fnBody) fail("seam-present", `${label}: could not locate the function`);
    else if (!hit) fail("seam-present", `${label}: neither "${member}" nor "${alt}" appears in it`);
    else pass("seam-present", `${label} (${member})`);
  }
}

// ---- 2. the grammar lock -----------------------------------------------
// The single parse site in ModeManager::run() must use the ONE grammar entry
// point (handleCommand with CmdSource::WIRE). No transport may have its own
// command path.

const runBody = body(mmCode, "void ModeManager::run()") ?? "";

// TWO assertions were in the old gate (M17 and M18); keep the first one.
if (!/relayComm_/i.test(runBody)) {
  pass("grammar-lock", "ModeManager::run() has no relay transport reference");
} else {
  fail("grammar-lock", "ModeManager::run() still references a relay CommInterface - remove the reference");
}

// The run() body must parse every queued line through the single parser.
if (!/handleCommand\s*\(\s*cmdBuf\s*,\s*CmdSource::WIRE\s*\)/.test(runBody)) {
  fail("grammar-lock", "ModeManager::run() does not drain via handleCommand(..., CmdSource::WIRE)");
} else {
  pass("grammar-lock", "ModeManager::run() drains via handleCommand(..., CmdSource::WIRE)");
}

// ---- 3. TLS ------------------------------------------------------------
// The old TLS rules (setInsecure, beginSslWithCA, tls-anchor) were tightly
// bound to the relay certificate. The current firmware has no TLS on the
// car's own transports (web + BT are plain), so these rules are omitted.
// If TLS is re-added, add the rules back verbatim.

// ---- 5. the token is a credential --------------------------------------
// No relay token exists in the firmware. Any reference to a credential in the
// codebase is handled through the general botcfg rules (see section 8).

// ---- 6. the restart record (U-95/F-79) ---------------------------------
// Everything above was green while the car rebooted in a loop for a week. These
// rules exist because of that, and each one is a FACT that used to be a COMMENT.

// (a) trigger_panic must be FALSE.
const panicOn = /trigger_panic\s*=\s*true/.test(inoCode);
if (panicOn) {
  fail("reset-truth", "the task watchdog still escalates to panic, so TASK_WDT is unreachable and a hung loop() reads as a crash");
} else if (!/trigger_panic\s*=\s*false/.test(inoCode)) {
  fail("reset-truth", "trigger_panic is neither true nor false - the watchdog escalation path is unstated");
} else {
  pass("reset-truth", "trigger_panic = false, so a hung loop() reports TASK_WDT rather than PANIC");
}

// (b) The record must be taken before anything else allocates.
const setupAt = inoCode.indexOf("void setup(");
const beginAt = inoCode.indexOf("crashLogBegin()");
const firstAllocAt = inoCode.search(/\b(motor|btComm|oled)\s*\.\s*begin\s*\(/);
if (beginAt < 0) {
  fail("crash-record", "setup() never calls crashLogBegin() - the restart reason is not recorded");
} else if (setupAt < 0 || beginAt < setupAt) {
  fail("crash-record", "crashLogBegin() is not inside setup()");
} else if (firstAllocAt >= 0 && beginAt > firstAllocAt) {
  fail("crash-record", "crashLogBegin() runs after the first begin() - the recorded heap is post-allocation, not the boot heap");
} else {
  pass("crash-record", "crashLogBegin() runs before any hardware bring-up allocates");
}

// (c) An UNKNOWN reset must count as a fault.
const clCode = code(read("CrashLog.cpp"));
if (!clCode) {
  fail("crash-record", "CrashLog.cpp is missing");
} else {
  const isFault = body(clCode, "bool isFaultReset") ?? "";
  if (!/default\s*:\s*return\s+true/.test(isFault)) {
    fail("crash-record", "isFaultReset() does not default to 'a fault' - an unknown reset would be silently dismissed");
  } else {
    pass("crash-record", "isFaultReset() counts an unknown reset as a fault, not as fine");
  }
  if (!/ESP_RST_POWERON[\s\S]{0,200}ESP_RST_DEEPSLEEP[\s\S]{0,200}default\s*:\s*\n\s*return\s+true/.test(isFault)) {
    fail("crash-record", "isFaultReset() no longer lists POWERON/EXT/DEEPSLEEP as non-faults before its default");
  } else {
    pass("crash-record", "only POWERON/EXT/DEEPSLEEP/SW are treated as clean");
  }
  const phaseFnRaw = body(read("CrashLog.cpp"), "void crashLogPhase") ?? "";
  if (!phaseFnRaw) {
    fail("crash-record", "crashLogPhase() not found in CrashLog.cpp");
  } else {
    const floorLine = phaseFnRaw.split("\n").find((l) => l.includes("CRASHLOG_PHASE_MIN_MS") && l.includes("return")) ?? "";
    if (!floorLine) {
      fail("crash-record", "crashLogPhase() no longer rate-limits phase writes - check whether flash wear is now unbounded");
    } else if (!/next\s*\[\s*0\s*\]\s*!=\s*'\\0'/.test(floorLine)) {
      fail("crash-record", "the phase-write floor applies to the idle transition as well as real phases - a crash while parked will be attributed to whatever the car last did, which is the one wrong answer this record must not give");
    } else {
      pass("crash-record", "the idle transition bypasses the write floor, so a parked-car crash is not blamed on the last real operation");
    }
  }
}

// (d) Both status-JSON buffers must be the SAME size.
const bufSizes = [...wsc.matchAll(/static\s+char\s+buf\[(\d+)\]\s*;\s*\n\s*buildStatusJson/gm)].map((m) => Number(m[1]));
if (bufSizes.length < 2) {
  fail("status-buffer", `found ${bufSizes.length} status-JSON buffer size(s) - expected two STATIC char buf[N] (HTTP + WS copies; static keeps ~3.5 KB off the 8 KB loopTask stack)`);
} else if (new Set(bufSizes).size !== 1) {
  fail("status-buffer", `the two status-JSON buffers differ (${bufSizes.join(" vs ")}) - one copy truncates differently`);
} else {
  // And it must actually be big enough: the fragments buildStatusJson writes
  // into it, plus room for the fixed literal fields between them.
  const frag = (re) => Number(wsc.match(re)?.[1] ?? 0);
  const need =
    frag(/char\s+networks\[(\d+)\]/) + frag(/char\s+scanJson\[(\d+)\]/) +
    frag(/char\s+caps\[(\d+)\]/) + frag(/char\s+statusWord\[(\d+)\]/) +
    frag(/char\s+boardId\[(\d+)\]/) + 520;  // ssid/ap/ip/4 IPs + every literal field
  if (bufSizes[0] < need) {
    fail("status-buffer", `buffer is ${bufSizes[0]} but the fragments alone need ~${need} - snprintf will truncate into invalid JSON`);
  } else {
    pass("status-buffer", `both copies are ${bufSizes[0]} bytes, above the ~${need} the fragments need`);
  }
}

// (e) The crash fields must be in the status JSON, and the page must gate them
// on crash_count > 0 rather than on the field merely existing.
const stJson = body(wsc, "void WebServerComm::buildStatusJson") ?? "";
const fields = ["reset_reason", "boot_count", "crash_count", "last_crash_phase"];
const missing = fields.filter((f) => !stJson.includes(f));
if (missing.length) {
  fail("crash-visible", `status JSON is missing ${missing.join(", ")}`);
} else {
  pass("crash-visible", "status JSON carries the reason, both counters and the phase");
}
const page = raw("WebPage.h");
if (!/crash_count[\s\S]{0,400}crashCount\s*>\s*0/.test(page) && !/cc\s*>\s*0[\s\S]{0,300}reset_reason/.test(page)) {
  fail("crash-visible", "the page does not gate the restart row on crash_count > 0 - an absent field would render as a false zero");
} else {
  pass("crash-visible", "the page shows the restart row only when crash_count > 0");
}

// (f) U-96i: the firmware's own version must be on the wire, fed from FW_VERSION
if (!/\\"fw\\":\\"%s\\"/.test(stJson)) {
  fail("status-fw", "status JSON has no fw:%s slot - the app cannot learn the firmware version");
} else if (!/boardId\s*,\s*FW_VERSION\s*,\s*networks/.test(code(stJson))) {
  fail("status-fw", "the fw slot is not fed by FW_VERSION in the position of its %s - snprintf order is the contract");
} else {
  pass("status-fw", "status JSON carries fw, fed from FW_VERSION in its argument position");
}

// (f2) U-114: the telemetry-parameter surface. It is read in TWO directions by
// one line of Config.h — the TSH gate parses it as TEXT, the C++ assigns it to
// a `const char*` — so the shape of that line, the slot it feeds on /status and
// the ARGUMENT POSITION of that slot are all one contract. Commit 79e6d26 broke
// all three at once (unquoted macro = no build; `strchr(x,',')+1` = UB; the two
// buffers appended after bootAutojoin_ = wifi_enabled/bt_enabled/boot_autojoin
// fed by the wrong argument, which the app ADOPTS).
const teleMacroLine = cfg.match(/^\s*#define\s+CONFIG_TELEMETRY_PARAMETERS\s+(\S.*)$/m)?.[1]?.trim() ?? "";
if (!/^"[\s\S]*"$/.test(teleMacroLine)) {
  fail("tele-params", `the config surface is not one string literal (got: ${teleMacroLine || "<missing>"}) - it is assigned to a const char*, so the bare token list does not compile`);
} else if (!/\\"tele_params\\":\\"freq=%s, vyse=%s\\"/.test(stJson)) {
  fail("tele-params", "status JSON has no tele_params freq=%s, vyse=%s slot - the surface exists but nothing reports it");
} else if (!/scanJson\s*,\s*freqBuf\s*,\s*vyseBuf\s*,\s*wifiEnabled_/.test(code(stJson))) {
  fail("tele-params", "freqBuf/vyseBuf are not the arguments at the tele_params slot position - snprintf order is the contract, so every field after them reports a neighbour's value");
} else if (!/teleSurfaceField\(\s*CONFIG_TELEMETRY_PARAMETERS\s*,\s*TELE_FREQ_FIELD\s*,\s*freqBuf/.test(code(wsc)) ||
           !/teleSurfaceField\(\s*CONFIG_TELEMETRY_PARAMETERS\s*,\s*TELE_VYSE_FIELD\s*,\s*vyseBuf/.test(code(wsc))) {
  fail("tele-params", "the surface is not read through teleSurfaceField() with both field indices - a direct parse brings the NULL-unsafe chain back");
} else if (/strchr\s*\([^)]*\)\s*\+\s*1/.test(code(wsc))) {
  fail("tele-params", "a `strchr(...) + 1` chain is back in WebServerComm.cpp - strchr returns NULL when a field is missing, so +1 is undefined behaviour");
} else if (!/if\s*\(\s*!p\s*\)\s*return;/.test(code(body(wsc, "static void teleSurfaceField") ?? ""))) {
  fail("tele-params", "teleSurfaceField does not stop on a missing field - the bounds guard on the helper is gone, so a shortened surface reads past its own end");
} else {
  pass("tele-params", "surface is one literal, tele_params is fed in slot order, and the parser is the NULL-safe bounded helper");
}

// (f3) P4 (issue 6, 2026-10-15): the E1 heap waterfall. Issue 6 is fixed by
// MEASUREMENT, so the tree must emit one breadcrumb per radio lifecycle stage
// before anyone guesses at the diet. This rule pins BOTH halves: the helper's
// wire format (free AND max, so fragmentation is visible) and one call site per
// stage, in the file that owns that stage. A breadcrumb that stops being
// emitted, or that loses its maxAllocHeap, silently turns the E1 capture into a
// flat line where the ~100 KB BT collapse should be.
const hwFmt = /\[HEAPW\]\s+%s free=%u max=%u\\n/.test(cfg);
const hwMax = /getMaxAllocHeap\s*\(\s*\)/.test(cfg);
const hwSites = [
  ["BOOT", inoCode, "the sketch's setup()"],
  ["BT", code(raw("BluetoothComm.cpp")), "BluetoothComm::begin"],
  ["SPP", inoCode, "the sketch's SPP-connect edge"],
  ["AP", wscCode, "WebServerComm::startOwnAP"],
  ["PAGE", wscCode, "WebServerComm::handleRoot"],
  ["BOTH", inoCode, "the sketch's both-transports-up branch"],
];
const hwMissing = hwSites
  .filter(([tok, src]) => !new RegExp(`heapWaterfall\\(\\s*HEAPW_STAGE_${tok}\\b`).test(src))
  .map(([tok, , where]) => `${tok} (${where})`);
if (!hwFmt) {
  fail("heap-waterfall", "Config.h has no `[HEAPW] <stage> free=%u max=%u` format - the E1 capture has nothing to key on");
} else if (!hwMax) {
  fail("heap-waterfall", "the breadcrumb prints free heap but NOT getMaxAllocHeap() - BT-restart fragmentation (largest block) is invisible in the total (P4 suspect (c))");
} else if (hwMissing.length) {
  fail("heap-waterfall", `no breadcrumb emitted at ${hwMissing.join(", ")} - the waterfall has a hole exactly where the heap story is`);
} else {
  pass("heap-waterfall", "all 6 stages emit one [HEAPW] free+max breadcrumb (boot/bt/spp/ap/page/both)");
}

// (f4) P4 (issue 6, 2026-10-15): the diet itself. Issue 6's largest single cause
// was the radio being brought up in BTDM mode: BluetoothSerial::begin()'s
// disableBLE argument defaults to FALSE (BluetoothSerial.h:43), so the bare
// begin(name) allocated BLE controller + host memory this SPP-only car never
// uses (the app's BLE path is an explicit placeholder). disableBLE=true asks for
// ESP_BT_MODE_CLASSIC_BT, and core 3.3.12's btStartMode() releases the BLE half
// for us (esp32-hal-bt.c btMemRelease(BT_MODE_BLE)). Drop the third argument —
// or flip it to false — and the ~100 KB collapse comes back with every gate
// still green, which is why the THIRD argument is the contract here.
if (!/SerialBT\.begin\(\s*[^,)]+\s*,\s*[^,)]+\s*,\s*true\s*\)/.test(code(raw("BluetoothComm.cpp")))) {
  fail("bt-heap", "BluetoothComm::begin() does not ask for classic-BT-only (SerialBT.begin(name, false, true)) - the default is BTDM, which allocates the BLE controller+host memory this SPP-only car never uses (issue 6)");
} else {
  pass("bt-heap", "the radio comes up ESP_BT_MODE_CLASSIC_BT, so core 3.3.12 releases the unused BLE memory before the controller starts");
}

// ---- 7. the page-send loop cannot starve the watchdog (U-95/F-77) -------
// The sketch's REAL watchdog config must be the same number the header declares.

const inoWdt = inoCode.match(/esp_task_wdt_config_t\s+\w+\s*=\s*\{([^}]*)\}/)?.[1] ?? "";
const inoTimeoutRaw = inoWdt.match(/timeout_ms\s*=\s*([^,}]+)/)?.[1]?.trim() ?? "";
const budget = Number(wsh.match(/#define\s+HTTP_BODY_BUDGET_MS\s+(\d+)ul/)?.[1] ?? 0);
const wdt = Number(wsh.match(/#define\s+LOOP_TASK_WDT_TIMEOUT_MS\s+(\d+)ul/)?.[1] ?? 0);

if (!budget || !wdt) {
  fail("wdt-budget", `could not read the numbers (budget=${budget}, watchdog=${wdt}) - one of the defines is gone or unparseable`);
} else if (budget >= wdt) {
  fail("wdt-budget", `HTTP_BODY_BUDGET_MS (${budget}) is NOT below the watchdog (${wdt}) - a slow client resets the car instead of getting a truncated page`);
} else if (wdt - budget < 1000) {
  fail("wdt-budget", `only ${wdt - budget}ms between the budget and the watchdog - the HTTP parser and the rest of loop() have no room`);
} else if (!/static_assert\s*\(\s*HTTP_BODY_BUDGET_MS\s*<\s*LOOP_TASK_WDT_TIMEOUT_MS/.test(code(wsc))) {
  fail("wdt-budget", "no static_assert ties the budget to the watchdog - the invariant is a comment again");
} else if (inoTimeoutRaw !== "LOOP_TASK_WDT_TIMEOUT_MS") {
  fail("wdt-budget", `the sketch configures the watchdog with \`${inoTimeoutRaw || "nothing"}\` instead of LOOP_TASK_WDT_TIMEOUT_MS - a literal here is a second copy of the number, and the static_assert would be guarding a value the hardware never used`);
} else {
  pass("wdt-budget", `${budget}ms budget under a ${wdt}ms watchdog (${wdt - budget}ms margin), tied by static_assert and used by the sketch`);
}

// The LOOP itself.
const serveRoot = body(wscCode, "void WebServerComm::serveRootBounded") ?? "";
const loopAt = serveRoot.indexOf("while (off < pageLen)");
const loopBody = loopAt >= 0 ? serveRoot.slice(loopAt) : "";
if (!loopBody) {
  fail("wdt-feed", "could not find the page-send while loop in serveRootBounded()");
} else {
  const stallAt = loopBody.indexOf("if (w == 0)");
  const stallBody = braced(loopBody, stallAt) ?? "";
  const feeds = (loopBody.match(/esp_task_wdt_reset\s*\(\s*\)/g) ?? []).length;
  if (!stallBody) {
    fail("wdt-feed", "the send loop has no zero-write branch - check this rule still matches the code");
  } else if (!/esp_task_wdt_reset\s*\(\s*\)/.test(stallBody)) {
    fail("wdt-feed", "the zero-write branch does not feed the watchdog - this is the F-77 spin, restored");
  } else if (!/delay\s*\(\s*1\s*\)|HTTP_WRITE_RETRY_GAP_MS/.test(stallBody)) {
    fail("wdt-feed", "the zero-write branch has no real gap - a bare yield() returns to the same instruction and busy-spins");
  } else if (feeds < 2) {
    fail("wdt-feed", `only ${feeds} watchdog feed(s) in the send loop - both the stall path and the progress path need one`);
  } else {
    pass("wdt-feed", `the zero-write branch feeds the watchdog AND waits a real gap; ${feeds} feeds across both paths`);
  }

  // The budget must be checked BEFORE the write, not after.
  const budgetAt = loopBody.search(/millis\(\)\s*-\s*start\s*>\s*HTTP_BODY_BUDGET_MS/);
  const writeAt = loopBody.search(/c\.write\s*\(/);
  if (budgetAt < 0) {
    fail("wdt-budget", "the send loop no longer checks HTTP_BODY_BUDGET_MS at all");
  } else if (writeAt >= 0 && budgetAt > writeAt) {
    fail("wdt-budget", "the budget is checked AFTER the write - an iteration can begin out of time");
  } else {
    pass("wdt-budget", "the budget is checked before every write");
  }
}

// ---- 8. the WebSocket pump cannot race the watchdog (U-95 1b/1c) -------
// ws.loop() contains a read the sketch cannot shorten: WebSockets 2.7.2 waits
// WEBSOCKETS_TCP_TIMEOUT = 5000 ms for a partial frame, inside the library's
// own translation unit. broadcastStatus() contains a write that measured 5-8 s
// to a client that stopped draining TCP. Both run on the loop task, both used
// to draw on whatever was left of the pass's window, and against a 5000 ms
// watchdog fed once per pass both were stopwatches racing the reset.

const handleBody = body(wscCode, "void WebServerComm::handle()") ?? "";
const broadcastBody = body(wscCode, "void WebServerComm::broadcastStatus") ?? "";

if (!handleBody) {
  fail("ws-loop-feed", "could not locate WebServerComm::handle() - check this rule still matches the code");
} else if (!/esp_task_wdt_reset\s*\(\s*\)/.test(handleBody)) {
  fail("ws-loop-feed", "WebServerComm::handle() does not feed the watchdog");
} else {
  pass("ws-loop-feed", "WebServerComm::handle() feeds the watchdog");
}

if (!broadcastBody) {
  fail("ws-broadcast-feed", "could not locate WebServerComm::broadcastStatus() - check this rule still matches the code");
} else if (!/esp_task_wdt_reset\s*\(\s*\)/.test(broadcastBody)) {
  fail("ws-broadcast-feed", "WebServerComm::broadcastStatus() does not feed the watchdog");
} else {
  pass("ws-broadcast-feed", "WebServerComm::broadcastStatus() feeds the watchdog");
}

// Note: broadcastStatus() uses a single ws.broadcastTXT() call (not a chunked loop
// like serveRootBounded), so it doesn't need a total budget check — the watchdog
// feed before the call covers the worst-case write. The per-write timeout is
// handled by the WebSocket library's internal timeout.

// ---- 9. KEEP the old board-id consumer gate (website-side) ------------
// Cross-repo: the website's claim loader asserts the relay hello's BOARD_ID_RE
// and CLAIM_UNIQUE_ID_RE against the car's WebServerComm::boardIdHex() output.
// This checkout may or may not have the sibling repo; CI's checkout never does.
// When it is here, compare the literal patterns; when it is not, say `skip`.

const consumerRelay = join(root, "..", "genumsolutions-website", "supabase", "functions", "car-relay", "relay-core.ts");
if (!existsSync(consumerRelay)) {
  console.log("  skip  board-id-consumer - sibling genumsolutions-website absent (CI checkout); run this gate inside the workspace for the cross-repo comparison");
} else {
  const consumerSrc = readFileSync(consumerRelay, "utf8");
  const rhsOf = (name) => {
    const line = consumerSrc.split("\n").find((l) => l.includes(`const ${name}`));
    return line ? line.slice(line.indexOf("=") + 1).trim().replace(/;.*$/, "") : null;
  };
  const want = {
    BOARD_ID_RE: "/^[0-9A-F]{6}$/",
    CLAIM_UNIQUE_ID_RE: "/^fw:([0-9A-F]{6})$/",
  };
  const wrong = Object.keys(want).filter((k) => rhsOf(k) !== want[k]);
  if (wrong.length) {
    fail(
      "board-id-consumer",
      `relay-core ${wrong.map((k) => `${k} = ${rhsOf(k)}`).join("; ")} — this car prints 6 uppercase hex ` +
        "(WebServerComm::boardIdHex). If the website side changed ON PURPOSE, change the car and " +
        "both gates in the same commit — a lone change here means every real car is refused"
    );
  } else {
    pass("board-id-consumer", "website BOARD_ID_RE + CLAIM_UNIQUE_ID_RE are exactly the 6-hex forms this car produces");
  }
}

// ---- 10. the broadcast cannot walk the sockets with nobody listening ----
// U-95 1c: broadcastStatus() measured 5-8 s writing to a client that stopped
// draining TCP, and ran unguarded on the 1 s ticker - a 1536-byte JSON built
// and every socket walked whether or not anyone was listening. The guard must
// be INSIDE broadcastStatus and must run BEFORE the JSON build; a guard
// anywhere else (or after it) is that case still. Comment-stripped view: the
// prose above the guard explains it and would otherwise satisfy the rule.
{
  const guardAt = broadcastBody.search(/hasWsClients\s*\(\s*\)|connectedClients\s*\(\s*\)/);
  const bcastAt = broadcastBody.indexOf("broadcastTXT");
  if (!broadcastBody) {
    fail("ws-guard", "could not locate broadcastStatus() - check this rule still matches the code");
  } else if (guardAt < 0) {
    fail("ws-guard", "broadcastStatus() has no connectedClients() guard - the 1 s ticker builds a 1536-byte JSON and walks every socket with nobody listening (U-95 Phase 1c)");
  } else if (bcastAt >= 0 && guardAt > bcastAt) {
    fail("ws-guard", "the client guard runs AFTER broadcastTXT() - it must return before the JSON is built and the sockets are walked");
  } else {
    pass("ws-guard", "broadcastStatus() is client-guarded, before the JSON build and the write");
  }
}

// The watchdog must sit STRICTLY above the library's worst single operation:
// 8000 ms is the U-77 write measurement (5000 ms no-progress retry + a 3000 ms
// inner socket block); the partial-frame read is 5000 ms. A watchdog EQUAL to
// either is the F-77 tie again - both clocks expire together and correctness
// is whichever the scheduler reaches first.
if (!wdt) {
  fail("ws-budget", "LOOP_TASK_WDT_TIMEOUT_MS is gone or unparseable - check this rule still matches the code");
} else if (wdt <= 8000) {
  fail("ws-budget", `the watchdog is ${wdt} ms - it does not exceed the WebSockets library's worst single operation (8000 ms measured: 5000 ms no-progress retry + 3000 ms socket send timeout); the stall ties the watchdog instead of fitting under it`);
} else {
  pass("ws-budget", `the watchdog is ${wdt} ms - ${wdt - 8000} ms above the library's worst operation (read 5000, write ~8000), each fed immediately before it runs`);
}

// ---- 11. the OLED extra row ticks apart from the WS broadcast (U-98 P2) --
// The OLED's network/extra row and the WebSocket status JSON used to share ONE
// `if (now - lastExtraMs >= EXTRA_INTERVAL)` block, so the OLED refresh rate
// could not move without changing how often every client is broadcast to. The
// owner asked for the OLED to be "pronto" (U-98); the honest fix is to raise
// the extra row's OWN cadence and leave the broadcast alone - NOT to double the
// status JSON to every socket. This rule pins the two ticks APART and ordered:
// each call sits under its own interval constant (a shared constant is the
// re-coupling back, dressed differently), and the extra row refreshes strictly
// FASTER than the broadcast (a slower OLED tick is the same defect in reverse).
{
  const guards = [];
  const gre = /(?:now|millis\s*\(\s*\))\s*-\s*(last\w*Ms)\s*>=\s*(\w+)/g;
  let gm;
  while ((gm = gre.exec(handleBody)) !== null) guards.push({ at: gm.index, interval: gm[2] });

  const intervals = {};
  const ire = /const\s+unsigned\s+long\s+(\w+)\s*=\s*(\d+)\s*;/g;
  let im;
  while ((im = ire.exec(handleBody)) !== null) intervals[im[1]] = Number(im[2]);

  // Map each call to the LAST tick guard that precedes it (the calls appear
  // several times elsewhere in handle(), each under its own event branch, so an
  // indexOf would grab the wrong one - the last guard wins). The tick block is
  // the last timer in handle(), so this lands on the right pair.
  const guardFor = (callRe) => {
    let hit = null;
    for (let i = 0; i < guards.length; i++) {
      const start = guards[i].at;
      const end = i + 1 < guards.length ? guards[i + 1].at : handleBody.length;
      if (callRe.test(handleBody.slice(start, end))) hit = guards[i];
    }
    return hit;
  };
  const eg = guardFor(/updateExtraInfo\s*\(/);
  const bg = guardFor(/broadcastStatus\s*\(/);

  if (!eg || !bg) {
    fail("extra-status-decoupled", "could not map updateExtraInfo()/broadcastStatus() to a `now - last…Ms >= …` tick in handle() - check this rule still matches the code");
  } else if (eg.interval === bg.interval) {
    fail("extra-status-decoupled", `updateExtraInfo() and broadcastStatus() share the tick ${eg.interval} - the OLED cadence is bolted to the client broadcast again (U-98 Phase 2)`);
  } else if (!(eg.interval in intervals) || !(bg.interval in intervals)) {
    fail("extra-status-decoupled", `one of the tick constants (${eg.interval}, ${bg.interval}) has no literal value - the rule cannot compare the two cadences`);
  } else if (!(intervals[eg.interval] < intervals[bg.interval])) {
    fail("extra-status-decoupled", `the extra row (${eg.interval}=${intervals[eg.interval]} ms) is not faster than the broadcast (${bg.interval}=${intervals[bg.interval]} ms) - the "pronto" refresh is not actually sooner`);
  } else {
    pass("extra-status-decoupled", `OLED extra row refreshes every ${intervals[eg.interval]} ms (${eg.interval}); the WS broadcast stays at ${intervals[bg.interval]} ms (${bg.interval})`);
  }
}

// ---- 12. a router switch must be a real teardown (U-95 Phase 2) ----------
// U-90 shipped the claim this rule enforces as a COMMENT: the MODE step's
// prose said "WiFi.mode(WIFI_AP_STA) restarts the WiFi driver, the stale STA
// state is torn down". The installed core does not restart anything when the
// requested mode equals the current one (WiFiGeneric.cpp:602 early-returns),
// and the current mode IS WIFI_AP_STA after the first switch - so the
// teardown did not exist on any switch after the first, the stale
// association survived, and every gate in this file stayed green through it.
// Prose certified by plausibility, F-82 in one line.
//
// So the sequence is asserted, IN ORDER, against the comment-stripped body:
//   drop (mode WIFI_AP, always unequal -> always real work)
//   -> poll (getMode, confirms the drop took, 0 radio calls)
//   -> restore (startOwnAP while the STA is down - the one window
//      startOwnAP()'s staBusy() guard cannot refuse, so the phone keeps a
//      way in for the WHOLE switch: T-65's "reachable throughout")
//   -> raise (mode WIFI_AP_STA, always unequal)
//   -> begin (WiFi.begin on a clean slate)
// A missing step or a reordered one is the U-90 defect again: a begin that
// races the old link, or a switch that leaves the car without its hotspot.
const rejoinBody = body(wscCode, "void WebServerComm::advanceRejoinStep") ?? "";
if (!rejoinBody) {
  fail("switch-teardown", "could not locate advanceRejoinStep() - check this rule still matches the code");
} else {
  const dropAt = rejoinBody.search(/WiFi\s*\.\s*mode\s*\(\s*WIFI_AP\s*\)/);
  const pollAt = rejoinBody.search(/WiFi\s*\.\s*getMode\s*\(\s*\)/);
  const restoreAt = rejoinBody.search(/startOwnAP\s*\(\s*\)/);
  const raiseAt = rejoinBody.search(/WiFi\s*\.\s*mode\s*\(\s*WIFI_AP_STA\s*\)/);
  const beginAt = rejoinBody.search(/WiFi\s*\.\s*begin\s*\(/);
  if (dropAt < 0) {
    fail("switch-teardown", "the MODE step no longer DROPS the mode (WiFi.mode(WIFI_AP)) - mode(WIFI_AP_STA) on an already-AP_STA driver is the WiFiGeneric.cpp:602 early return: no teardown on any switch after the first (the U-90 defect)");
  } else if (pollAt < 0) {
    fail("switch-teardown", "the drop is never confirmed (no WiFi.getMode() poll) - mode() returns once the set is ACCEPTED, not once it took; proceeding blind is racing the driver");
  } else if (restoreAt < 0) {
    fail("switch-teardown", "the rejoin path never restores the hotspot (startOwnAP()) - the phone loses its way in during the switch (T-65)");
  } else if (raiseAt < 0) {
    fail("switch-teardown", "the STA side is never re-raised (no WiFi.mode(WIFI_AP_STA)) - the car would stay AP-only after a switch");
  } else if (beginAt < 0) {
    fail("switch-teardown", "the rejoin path never reaches WiFi.begin() - nothing ever joins the router");
  } else if (!(dropAt < pollAt && pollAt < restoreAt && restoreAt < raiseAt && raiseAt < beginAt)) {
    fail("switch-teardown", `the steps are out of order (drop=${dropAt}, poll=${pollAt}, restore=${restoreAt}, raise=${raiseAt}, begin=${beginAt}) - the sequence must be drop -> confirm -> restore -> raise -> begin`);
  } else {
    pass("switch-teardown", "drop -> confirm -> restore -> raise -> begin, in order, one radio call per pass");
  }
}

// NVS wear (U-95 Phase 2). Arduino-ESP32 defaults to persistent(true), which
// routes every esp_wifi_set_config through FLASH: each WiFi.begin() rewrites
// the station credentials and each softAP() rewrites the AP config. A router
// switch multiplies that by the retry budget (STA_MAX_RETRIES), the boot
// auto-join adds more, and none of it is data this firmware reads back - its
// credentials live in its own `botcfg` Preferences. The guard must exist AND
// sit in the boot bring-up (beginAlwaysOn, or the top of beginStation before
// it configures the radio): placed anywhere else in the file it runs after
// the very first config write it exists to prevent.
const abBody = body(wscCode, "void WebServerComm::beginAlwaysOn") ?? "";
const bsBody = body(wscCode, "bool WebServerComm::beginStation") ?? "";
const persistRe = /WiFi\s*\.\s*persistent\s*\(\s*false\s*\)/;
const pAb = abBody ? abBody.search(persistRe) : -1;
const pBs = bsBody ? bsBody.search(persistRe) : -1;
const bsFirstRadio = bsBody ? bsBody.search(/WiFi\s*\.\s*(?:mode|softAP|begin)\s*\(/) : -1;
if (pAb < 0 && pBs < 0) {
  fail("wifi-persistent", "WiFi.persistent(false) is not in the boot bring-up (beginAlwaysOn/beginStation) - every WiFi.begin()/softAP() rewrites flash NVS against ~100k erase cycles (the Arduino default is persistent=true)");
} else if (pAb < 0 && bsFirstRadio >= 0 && pBs > bsFirstRadio) {
  fail("wifi-persistent", "WiFi.persistent(false) runs AFTER the first mode/softAP/begin inside beginStation() - the first config write still reaches flash, so the guard is decorative");
} else {
  pass("wifi-persistent", "WiFi.persistent(false) is set in the boot bring-up, before the radio is configured");
}

// The retry's disconnect and its begin must NOT share one handle() pass
// (U-95 Phase 2). Same shape as the give-up path that rebooted the car (U-75):
// two radio calls back to back inside one loop iteration, with the begin
// racing a driver that is still tearing the old association down - the plan's
// "separate the disconnect/begin across passes". Three structural facts, then
// the two ways a deferred begin can still leak (a rejoin must cancel it, and
// must re-stamp the backoff or the retry fires on pass 1, mid-teardown).
const rejoinNetBody = body(wscCode, "void WebServerComm::rejoinNetwork()") ?? "";
const rbArmAt = handleBody.search(/retryBeginPending_\s*=\s*true/);
const rbGuardAt = handleBody.search(/if\s*\(\s*retryBeginPending_\s*\)/);
const rbGuardBody = rbGuardAt >= 0 ? braced(handleBody, rbGuardAt) ?? "" : "";
const samePassBegin = /WiFi\.disconnect\s*\(\s*\)\s*;\s*(?:noteRadioStep\s*\(\s*"[^"]*"\s*\)\s*;\s*)?WiFi\.begin\s*\(/.test(handleBody);
if (!handleBody) {
  fail("retry-split", "could not locate WebServerComm::handle() - check this rule still matches the code");
} else if (rbArmAt < 0) {
  fail("retry-split", "the retry branch never arms retryBeginPending_ - the deferred begin has no producer");
} else if (rbGuardAt < 0) {
  fail("retry-split", "no if (retryBeginPending_) guard - the deferred begin is never issued (the flag is armed and forgotten)");
} else if (!/WiFi\.begin\s*\(/.test(rbGuardBody)) {
  fail("retry-split", "the retryBeginPending_ guard does not contain WiFi.begin - the arm has no consumer, so a disconnect with no begin leaves the STA down until the give-up path settles");
} else if (samePassBegin) {
  fail("retry-split", "a WiFi.disconnect() still runs directly into a WiFi.begin() in handle() - the same-pass race the split removes (the U-75 shape: two radio calls in one loop pass)");
} else if (!rejoinNetBody) {
  fail("retry-split", "could not locate rejoinNetwork() - check this rule still matches the code");
} else if (!/retryBeginPending_\s*=\s*false/.test(rejoinNetBody)) {
  fail("retry-split", "rejoinNetwork() does not cancel an in-flight retry begin - a stale deferred begin lands after the user asked for a different network (the U-75 pattern)");
} else if (!/lastWifiRetryMs_\s*=\s*millis\s*\(\s*\)/.test(rejoinNetBody)) {
  fail("retry-split", "rejoinNetwork() does not re-stamp the retry backoff - the 10 s stamp is stale after any successful session, so the retry branch fires on pass 1 of the step machine and disconnects the teardown that is mid-walk");
} else {
  pass("retry-split", "disconnect and begin are on separate passes, the deferred begin has a guard and a producer, and rejoinNetwork() cancels + re-stamps");
}

// U-95 Phase 2C: the SCAN answer must be TRUE or BUSY - never an unconditional
// STARTED for a scan that was silently dropped - and startScan() must refuse
// BOTH while a scan is in flight and while the STA is mid-join. Literals are
// read from the comment-stripped view (F-83: stripComments KEEPS string
// literals, so `"ROUTERS;SCAN;BUSY"` is a real reply only if the source
// carries it; the string is written in code only, never quoted in a comment).
const scanLiterals = stripComments(wsc);
const scanStartBody = body(wscCode, "bool WebServerComm::startScan") ?? "";
const routerCmdBody = body(wscCode, "bool WebServerComm::handleRouterCommand") ?? "";
if (!scanStartBody) {
  fail("scan-honest", "startScan() is no longer `bool WebServerComm::startScan` - void startScan() is the pre-U-95 shape: it refuses silently and the handler cannot answer BUSY");
} else if ((scanStartBody.match(/return false/g) || []).length < 2) {
  fail("scan-honest", "startScan() does not refuse in both cases - it must reject an in-flight scan AND a scan kicked while the STA is associating (mid-join scans race the driver and return garbage)");
} else if (!/if\s*\(\s*scanPending_\s*\|\|\s*scanBusy_\s*\)\s*return\s+false/.test(scanStartBody)) {
  // The COUNT check above is no longer sufficient on its own: the U-112 heap
  // floor added a third `return false`, so removing the in-flight refusal
  // still leaves two returns and the count stays green (found by mutation M38
  // going undetected - a rule weakened by an unrelated addition is the F-83
  // shape again). The in-flight refusal is now pinned BY TEXT.
  fail("scan-honest", "startScan() no longer refuses while a scan is armed or in flight - a double-tap arms a second scan over one already running, and the reply lies about both");
} else if (!/connectingPending_/.test(scanStartBody)) {
  fail("scan-honest", "startScan() no longer gates on connectingPending_ - a scan is started between WiFi.begin() and association, racing a driver that is mid-join");
} else if (!routerCmdBody) {
  fail("scan-honest", "could not locate handleRouterCommand() - check this rule still matches the code");
} else if (!/if\s*\(\s*startScan\s*\(\s*\)\s*\)/.test(routerCmdBody)) {
  fail("scan-honest", "the SCAN handler does not key the reply on startScan()'s result - ROUTERS;SCAN;STARTED is claimed unconditionally, so a dropped scan still reports success and the app waits forever");
} else if (!scanLiterals.includes('\"ROUTERS;SCAN;BUSY\"')) {
  fail("scan-honest", "the source never answers ROUTERS;SCAN;BUSY - a refusal has no honest form");
} else if (!scanLiterals.includes('\"ROUTERS;SCAN;STARTED\"')) {
  fail("scan-honest", "the source never answers ROUTERS;SCAN;STARTED - a scan that DID start has no acknowledgement");
} else {
  pass("scan-honest", "startScan() answers (false = BUSY, gated on in-flight + associating) and the handler keys the reply on it");
}

// U-95 Phase 2C: the collected list must be ranked by STRENGTH, deduped per
// RADIO (BSSID, not name), with exactly ONE cap applied AFTER the sort. The
// old shape capped before the loop and at insert, never sorted, and deduped
// on names - so it kept the first N results in radio-order, dropped strong
// networks, and silently merged two different routers broadcasting one SSID.
const collectBody = body(wscCode, "void WebServerComm::collectScanIfDone") ?? "";
const sortAt = collectBody.search(/scanNetworks_\[\s*b\s*-\s*1\s*\]\s*\.\s*rssi\s*<\s*key\s*\.\s*rssi/);
const capAt = collectBody.search(/scanNetworks_\s*\.\s*resize\s*\(\s*SCAN_MAX_RESULTS/);
if (!collectBody) {
  fail("scan-quality", "could not locate collectScanIfDone() - check this rule still matches the code");
} else if (!/char\s+bssid\s*\[/.test(wsh)) {
  fail("scan-quality", "ScannedNetwork has no BSSID field - there is no per-RADIO dedupe key, so the list can only dedupe on names (merging different routers that share an SSID)");
} else if (!/WiFi\.BSSIDstr\s*\(/.test(collectBody)) {
  fail("scan-quality", "collectScanIfDone() never reads the BSSID - the dedupe key is declared but never captured");
} else if (!/strcasecmp\s*\(\s*scanNetworks_\[\s*\w+\s*\]\s*\.\s*bssid/.test(collectBody)) {
  fail("scan-quality", "the dedupe no longer compares BSSIDs - name-dedupe silently merges two different routers broadcasting one SSID and hides a multi-AP router's second radio");
} else if (/if\s*\(\s*n\s*>\s*SCAN_MAX_RESULTS/.test(collectBody)) {
  fail("scan-quality", "the list is still pre-truncated before the loop - the cap runs before the sort, keeping radio-report order instead of strength");
} else if (/scanNetworks_\s*\.\s*size\s*\(\s*\)\s*<\s*SCAN_MAX_RESULTS/.test(collectBody)) {
  fail("scan-quality", "the list is still capped at INSERT time - the first N results in radio-order survive, dropping the strong networks");
} else if (sortAt < 0) {
  fail("scan-quality", "no strongest-first sort (descending RSSI comparison) before the cap - the list is unordered, so which rows survive the cap is radio-report luck");
} else if (capAt < 0) {
  fail("scan-quality", "the SCAN_MAX_RESULTS cap is gone entirely - an unbounded list bloats the status broadcast and the app's table");
} else if (sortAt > capAt) {
  fail("scan-quality", "the cap runs BEFORE the sort - truncation must happen last, or the rows dropped are chosen before strength is known");
} else {
  pass("scan-quality", "dedupe on BSSID, sort strongest-first (hand-rolled), single cap applied last");
}

// U-95 Phase 3: MODECYCLE/MODEPREV/MODENEXT must be ONE walk (cycleMode)
// behind three tokens. The walk existed only on the GPIO switch, so without
// tokens the page and the app would each grow their own - three
// implementations, three wrap rules, three drift stories. The rule checks the
// tokens reach cycleMode with the right SIGN (prev must not walk forward),
// that cycleMode wraps both ways (a missing negative wrap hands setMode() an
// out-of-range enum), and that the GPIO still uses the same walk.
// The token checks run on stripComments(mmHandle), NOT mmHandle: code() BLANKS
// string literals, so a strcmp against `"MODECYCLE"` reads as strcmp against
// nothing there - while raw would let the comment above the tokens (which
// NAMES them) pass for code. stripComments is the only view that is both.
const mmHandle = stripComments(body(raw("ModeManager.cpp"), "void ModeManager::handleCommand") ?? "");
const mmCycleBody = body(mmCode, "void ModeManager::cycleMode") ?? "";
const inoSrc = raw("Genum_4WD4M_CAR.ino");
if (!mmHandle) {
  fail("mode-cycle", "could not locate ModeManager::handleCommand - check this rule still matches the code");
} else if (!/strcmp\s*\(\s*cmdBuf\s*,\s*\"MODECYCLE\"\s*\)/.test(mmHandle)) {
  fail("mode-cycle", "no MODECYCLE token - the walk is GPIO-only, so page and app must each invent their own and drift apart");
} else if (!/strcmp\s*\(\s*cmdBuf\s*,\s*\"MODEPREV\"\s*\)/.test(mmHandle)) {
  fail("mode-cycle", "no MODEPREV token - there is no way to walk backwards");
} else if (!/strcmp\s*\(\s*cmdBuf\s*,\s*\"MODENEXT\"\s*\)/.test(mmHandle)) {
  fail("mode-cycle", "no MODENEXT token - the forward step has no explicit alias for consumers that mean 'next' rather than 'cycle'");
} else if (!/cycleMode\s*\(\s*1\s*\)/.test(mmHandle)) {
  fail("mode-cycle", "the forward tokens do not call cycleMode(1) - the shared walk is bypassed");
} else if (!/cycleMode\s*\(\s*-1\s*\)/.test(mmHandle)) {
  fail("mode-cycle", "MODEPREV does not call cycleMode(-1) - 'prev' walks forwards");
} else if (!mmCycleBody) {
  fail("mode-cycle", "could not locate cycleMode() - check this rule still matches the code");
} else if (!/next\s*>=\s*MODE_COUNT/.test(mmCycleBody)) {
  fail("mode-cycle", "cycleMode() never wraps past the end of the registry");
} else if (!/next\s*<\s*0\s*\)\s*next\s*=\s*MODE_COUNT\s*-\s*1/.test(mmCycleBody)) {
  fail("mode-cycle", "cycleMode() never wraps below 0 - MODEPREV from the first mode hands setMode() an out-of-range enum");
} else if (!/cycleMode\s*\(\s*\)/.test(inoSrc)) {
  fail("mode-cycle", "the GPIO switch no longer calls cycleMode() - two implementations of the walk now exist and will drift");
} else {
  pass("mode-cycle", "MODECYCLE/MODENEXT -> cycleMode(1), MODEPREV -> cycleMode(-1), wraps both ways, GPIO uses the same walk");
}

// U-95 Phase 3: the status data must carry the network facts a client cannot
// compute itself - per-scan-row channel/own/saved/current (the page's table,
// and the car is the only place the saved list and the association live) and
// the four IP fields (station/ap/broadcast/subnet; softAPBroadcastIP() was
// never called anywhere in this repository). Literals run on stripComments
// views (code() blanks strings); structure on code views. Rule:
// `status-net-fields`.
const scanJsonBody = body(wscCode, "size_t WebServerComm::buildScanJson") ?? "";
const scanJsonLits = stripComments(body(raw("WebServerComm.cpp"), "size_t WebServerComm::buildScanJson") ?? "");
const statusLits = scanLiterals; // stripComments of the whole cpp - literals kept
const statusBody = body(wscCode, "void WebServerComm::buildStatusJson") ?? "";
// The format strings contain ESCAPED quotes (`\"station_ip\":\"%s\"`), so a
// plain substring of `"station_ip":"%s"` never matches the source characters -
// match the backslash-quote shape instead.
const ipFieldRe = [
  /\\+"station_ip\\+":\\+"%s/,
  /\\+"ap_ip\\+":\\+"%s/,
  /\\+"broadcast_ip\\+":\\+"%s/,
  /\\+"subnet\\+":\\+"%s/,
];
const missingIp = ["station_ip", "ap_ip", "broadcast_ip", "subnet"].filter((_, i) => !ipFieldRe[i].test(statusLits));
if (!scanJsonBody) {
  fail("status-net-fields", "could not locate buildScanJson() - check this rule still matches the code");
} else if (!/\\+"channel\\+"/.test(scanJsonLits)) {
  fail("status-net-fields", "scan rows no longer carry channel - a table column only the radio can answer is gone");
} else if (!/\\+"own\\+"/.test(scanJsonLits) || !/\\+"saved\\+"/.test(scanJsonLits) || !/\\+"current\\+"/.test(scanJsonLits)) {
  fail("status-net-fields", "scan rows no longer carry own/saved/current - the page would have to guess which row is the car's own network, from three app modules that already disagree");
} else if (!/WiFi\.channel\s*\(/.test(collectBody)) {
  fail("status-net-fields", "collectScanIfDone() never reads WiFi.channel(i) - the channel field is declared but never captured from the radio at collect time, while the driver still holds it");
} else if (!/savedRouterSsid_/.test(scanJsonBody)) {
  fail("status-net-fields", "buildScanJson() no longer consults the saved-router list - `saved` is guessed, not car truth");
} else if (!/WiFi\.status\s*\(\s*\)\s*==\s*WL_CONNECTED/.test(scanJsonBody)) {
  fail("status-net-fields", "buildScanJson() no longer checks the association - `current` cannot distinguish an associated STA from a dropdown guess");
} else if (!statusBody) {
  fail("status-net-fields", "could not locate buildStatusJson() - check this rule still matches the code");
} else if (missingIp.length > 0) {
  fail("status-net-fields", `status JSON is missing ${missingIp.join(", ")} - the single legacy ip slot cannot say which interface is up, and the directed-broadcast path has no address to use`);
} else if (!/WiFi\.softAPBroadcastIP\s*\(/.test(statusBody)) {
  fail("status-net-fields", "WiFi.softAPBroadcastIP() is never called - the broadcast address was, until Phase 3, unreachable from anywhere in this repository");
} else if (!/WiFi\.softAPSubnetMask\s*\(/.test(statusBody)) {
  fail("status-net-fields", "the subnet mask is never read - broadcast without mask is an arithmetic guess");
} else {
  pass("status-net-fields", "scan rows carry channel/own/saved/current from car truth; station_ip/ap_ip/broadcast_ip/subnet are all computed");
}


// ---- U-112: the heap floor and the page stall-abort ----------------------
// The owner's post-1.3.9 report: the car RESET while connecting Bluetooth +
// the own hotspot together, and FREEZES when the car's own page is opened
// while the app holds a Wi-Fi link. The field record (device_crash_reports,
// boots 1026-1029, fw 1.3.9) shows free_heap at 13-18 kB and PANIC resets
// with an empty phase - the car lives at the allocation floor, where every
// big transient is a coin flip. Three guards exist for that, each with a rule:
//   1. broadcastStatus() checks STATUS_HEAP_FLOOR_BYTES for EVERY caller and
//      the defer is TIME-BOXED (an untimed defer at a chronically low heap is
//      a permanent telemetry black hole - a worse failure than the one it
//      prevents);
//   2. startScan() refuses below SCAN_HEAP_FLOOR_BYTES with the honest BUSY
//      the handler already answers, instead of kicking the radio into an
//      allocation that lands 8 s later on a silently EMPTY router list;
//   3. the root-page send loop aborts a reader that makes no progress after
//      HTTP_STALL_ABORT_AFTER attempts, because a full 3500 ms budget per
//      pass means loop() is not running - no ws.loop() for the app, no BT
//      poll - which is what "freezes while the page is open" IS. The counter
//      resets on progress, so slow-but-alive readers keep the whole budget.
// Reads: the header raw (defines are preprocessor, they survive \b word
// boundaries only as text), structure on code() views as always (F-83).
{
  const num = (name) => Number(String(wsh.match(new RegExp("#define\\s+" + name + "\\s+(\\d+)"))?.[1] ?? 0));
  const statusFloor = num("STATUS_HEAP_FLOOR_BYTES");
  const deferMax = num("STATUS_HEAP_DEFER_MAX_MS");
  const scanFloor = num("SCAN_HEAP_FLOOR_BYTES");
  const stallAfter = num("HTTP_STALL_ABORT_AFTER");
  if (!statusFloor || !scanFloor || !deferMax || !stallAfter) {
    fail("heap-guard", `one of the U-112 defines is missing or unparseable (status=${statusFloor}, defer=${deferMax}, scan=${scanFloor}, stall=${stallAfter})`);
  } else {
    if (scanFloor <= statusFloor) {
      fail("heap-guard", `SCAN_HEAP_FLOOR_BYTES (${scanFloor}) must sit ABOVE STATUS_HEAP_FLOOR_BYTES (${statusFloor}) - the bigger transient needs the bigger cushion`);
    } else if (statusFloor < 30000 && scanFloor < statusFloor * 2) {
      fail("heap-guard", `SCAN_HEAP_FLOOR_BYTES (${scanFloor}) is too close to STATUS_HEAP_FLOOR_BYTES (${statusFloor}) - the bigger transient needs a bigger cushion for reliability when the status floor is low`);
    } else {
      pass("heap-guard", `floors: status ${statusFloor} B < scan ${scanFloor} B; page stall-abort after ${stallAfter} zero-writes`);
    }
    if (deferMax > 10000) {
      fail("heap-guard", `STATUS_HEAP_DEFER_MAX_MS is ${deferMax} ms - the below-floor defer must be time-boxed (<= 10000 ms) or a chronically low heap black-holes telemetry forever`);
    } else {
      pass("heap-guard", `the below-floor defer is time-boxed at ${deferMax} ms - telemetry can never be silent longer than that`);
    }
  }

  const bcBody = body(wscCode, "void WebServerComm::broadcastStatus()") ?? "";
  if (!bcBody) {
    fail("heap-guard", "could not locate broadcastStatus() - check this rule still matches the code");
  } else if (!/if\s*\(\s*broadcastDeferred\(\)\s*\)/.test(bcBody)) {
    fail("heap-guard", "broadcastStatus() no longer checks the heap floor - the REQ_STATE/REQ_NETW paths would allocate against the last kilobytes again");
  } else if (!/STATUS_HEAP_DEFER_MAX_MS/.test(bcBody)) {
    fail("heap-guard", "broadcastStatus()'s defer has no time box - a chronically low heap would defer every broadcast forever (the untimed-defer failure mode)");
  } else {
    pass("heap-guard", "broadcastStatus() checks the floor for every caller, and the defer is time-boxed");
  }

  const scanBody = body(wscCode, "bool WebServerComm::startScan") ?? "";
  if (!scanBody || !/ESP\s*\.\s*getFreeHeap\s*\(\s*\)\s*<\s*SCAN_HEAP_FLOOR_BYTES/.test(scanBody)) {
    fail("heap-guard", "startScan() no longer refuses below SCAN_HEAP_FLOOR_BYTES - a low-heap scan lands 8 s later on a silently empty router list instead of an honest BUSY");
  } else {
    pass("heap-guard", "startScan() refuses below the scan floor (the handler answers ROUTERS;SCAN;BUSY)");
  }

  const rootBody = body(wscCode, "void WebServerComm::serveRootBounded") ?? "";
  const sendAt = rootBody.indexOf("while (off < pageLen)");
  const sendLoop = sendAt >= 0 ? rootBody.slice(sendAt) : "";
  if (!sendLoop) {
    fail("heap-guard", "could not find the page-send loop - check this rule still matches the code");
  } else if (!/stallRetries\s*>=\s*HTTP_STALL_ABORT_AFTER/.test(sendLoop)) {
    fail("heap-guard", "the page-send loop has no stall abort - a non-draining reader still costs HTTP_BODY_BUDGET_MS of loop() per pass, freezing ws.loop()/BT while a page loads (the owner's report)");
  } else if (!/stallRetries\s*=\s*0\s*;/.test(sendLoop)) {
    fail("heap-guard", "the stall counter is never reset on progress - ONE slow moment would end every later page pass, breaking slow-but-alive readers");
  } else {
    pass("heap-guard", "the page-send loop aborts a non-draining reader after the attempt cap and resets the counter on progress");
  }
}

// ---- P5 / U-113 issues 7 + 9 (2026-10-15): the Wi-Fi transport switch ----
//
// ISSUE 7 ("Wi-Fi OFF must hide everything - the phone still sees the hotspot
// AND its webpage") had a source-visible cause, so E3 was only needed to pick
// WHICH of the three candidate failures it was. The one that is in the code:
//
//   the own-hotspot bring-up was NEVER gated on the Wi-Fi transport switch.
//     - stop() took the beacon down only `if (apRunning_)`, and apRunning_ is a
//       BELIEVED flag the code itself force-writes false across an AP_STA<->AP
//       mode bounce (GIVEUP_AP / REJOIN_STEP_AP: "forced false, not trusted"),
//       so a stale value silently skipped softAPdisconnect();
//     - handle()'s FIN-40/D1 retry then re-created the hotspot ~2 s
//       (AP_RETRY_INTERVAL_MS) after `WIFICFG;WIFI;OFF`, because it tested
//       `!apRunning_` and nothing else, and stop() had just cleared that flag.
//     - stop() is reached only while `webUp` is true, and the U-79 loop clears
//       webUp as it calls it - so the re-created beacon was NEVER taken down
//       again. The switch said hidden; the radio said broadcasting.
//
// The rule pins the whole shape: the teardown is UNCONDITIONAL and NO bring-up
// path (boot, retry, rejoin, give-up) raises the beacon while the transport is
// off. The device half (does the SSID actually vanish, does the page refuse) is
// E3 + U-113 rows on the owner's bench (F-61).
const p5StopBody = body(wscCode, "void WebServerComm::stop()") ?? "";
const p5ApBody = body(wscCode, "void WebServerComm::startOwnAP()") ?? "";
const p5HandleBody = body(wscCode, "void WebServerComm::handle()") ?? "";
const p5ApDisconnectAt = p5StopBody.search(/WiFi\.softAPdisconnect\s*\(/);
const p5ApGatedOnFlag = /if\s*\(\s*apRunning_\s*\)[^\n]*softAPdisconnect\s*\(/.test(p5StopBody);
if (!p5StopBody) {
  fail("wifi-off-hides-ap", "could not locate WebServerComm::stop() - check this rule still matches the code");
} else if (p5ApDisconnectAt < 0) {
  fail("wifi-off-hides-ap", "stop() never calls WiFi.softAPdisconnect() - turning Wi-Fi OFF leaves the own beacon broadcasting (issue 7)");
} else if (p5ApGatedOnFlag) {
  fail("wifi-off-hides-ap", "stop() still gates the beacon teardown on `apRunning_` - that flag is force-written false across AP_STA<->AP mode bounces, so a stale value silently skips softAPdisconnect() and the hotspot survives the switch (issue 7)");
} else if (!p5ApBody) {
  fail("wifi-off-hides-ap", "could not locate WebServerComm::startOwnAP() - check this rule still matches the code");
} else if (!/if\s*\(\s*!\s*wifiEnabled_\s*\)\s*return\s*;/.test(p5ApBody)) {
  fail("wifi-off-hides-ap", "startOwnAP() does not refuse while the Wi-Fi transport is OFF - the boot bring-up, the rejoin and the give-up machines can all raise a beacon the owner switched off (issue 7)");
} else if (!p5HandleBody) {
  fail("wifi-off-hides-ap", "could not locate WebServerComm::handle() - check this rule still matches the code");
} else if (!/if\s*\(\s*wifiEnabled_\s*&&\s*!\s*apRunning_/.test(p5HandleBody)) {
  fail("wifi-off-hides-ap", "handle()'s own-hotspot retry is not gated on wifiEnabled_ - stop() clears apRunning_, so the next pass re-creates the beacon ~2 s after Wi-Fi OFF (the owner's 'the phone still sees it', issue 7)");
} else if (!/server\.stop\s*\(\s*\)/.test(p5StopBody) || !/serverRunning\s*=\s*false/.test(p5StopBody)) {
  fail("wifi-off-hides-ap", "stop() no longer stops the HTTP server (server.stop() + serverRunning=false) - the page would still be served after Wi-Fi OFF (issue 7)");
} else {
  pass("wifi-off-hides-ap", "stop() drops the beacon unconditionally; startOwnAP() AND the handle() retry refuse while the Wi-Fi transport is OFF; the HTTP server is stopped, so the page refuses");
}

// ISSUE 9 ("Page always up when Wi-Fi is on - never wait for the app/remote to
// connect first") is the same beacon seen from the other end: while the Wi-Fi
// transport is ON, the own hotspot and its page must be a way in with NOBODY
// attached. There is no new fix here - U-105 removed the power-save PARK - so
// this rule exists to make the property regress-proof, and E4 is the device
// half (does the page actually render with no app connected).
const p5RootBody = body(wscCode, "void WebServerComm::handleRoot()") ?? "";
const p5ServeBody = body(wscCode, "void WebServerComm::serveRootBounded()") ?? "";
const p5PowerBody = body(wscCode, "void WebServerComm::updateApPowerPolicy()") ?? "";
if (!p5RootBody) {
  fail("page-always-on", "could not locate WebServerComm::handleRoot() - check this rule still matches the code");
} else if (!/serveRootBounded\s*\(\s*\)/.test(p5RootBody)) {
  fail("page-always-on", "handleRoot() no longer sends the page via serveRootBounded() - the root route lost its body");
} else if (/hasWsClients|connectedClients/.test(p5RootBody)) {
  fail("page-always-on", "handleRoot() is gated on a connected client - the page would wait for the app to attach first (issue 9: 'never wait for the app/remote to connect first')");
} else if (!p5ServeBody) {
  fail("page-always-on", "could not locate WebServerComm::serveRootBounded() - check this rule still matches the code");
} else if (/hasWsClients|connectedClients/.test(p5ServeBody)) {
  fail("page-always-on", "serveRootBounded() is gated on a connected client - a phone that browses the car's own AP before the app connects gets no page (issue 9)");
} else if (!p5PowerBody) {
  fail("page-always-on", "could not locate WebServerComm::updateApPowerPolicy() - check this rule still matches the code");
} else if (/softAPdisconnect|softAP\s*\(\s*\)/.test(p5PowerBody)) {
  fail("page-always-on", "updateApPowerPolicy() tears the beacon down - the own hotspot must never be PARKED while Wi-Fi is ON (U-105: the beacon is the setup path, issue 9)");
} else if (!/if\s*\(\s*!\s*wifiEnabled_\s*\)\s*return\s*;/.test(p5PowerBody)) {
  fail("page-always-on", "updateApPowerPolicy() no longer returns on a disabled Wi-Fi transport - it would fight the owner's own switch (issue 7)");
} else {
  pass("page-always-on", "the root page is served with no client gate, and the own beacon is never parked while Wi-Fi is on");
}

// P7 / U-115 (2026-10-16): BOOT AUTO-JOIN SCANS FIRST.
//
// The boot auto-join handed the FULL retry budget to a router that may simply
// be switched off. While that budget ran, startOwnAP()'s staSharing branch set
// the AP channel to FOLLOW the STA (apChannel 0) and every WiFi.begin() retry
// made the STA scan — so the own hotspot broadcast on a MOVING channel: a phone
// SAW `4WD4MCar_WiFi` and could not complete the join (owner field report
// 2026-10-16, "the phone's wifi doesn't connect … even though the car's own
// wifi is detected … it only connects once bluetooth is off"). One non-blocking
// reachability scan before the rejoin is armed fixes it: router present → rejoin;
// absent → stay AP-only, own hotspot stable from the first second.
//
// This rule pins the whole shape, so a refactor cannot quietly re-introduce the
// blind rejoin: the boot path must reach the probe, the probe must be a real
// scan, and — the load-bearing half — it must FALL BACK to rejoinNetwork() when
// the scan cannot answer, so the car can never lose its router to a probe it
// could not run. The device half (a phone joins the own hotspot within seconds
// of a boot with the saved router absent) is the owner's bench (F-61).
const bootArmBody = body(wscCode, "void WebServerComm::armBootAutoJoin()") ?? "";
const bootProbeBody = body(wscCode, "bool WebServerComm::startBootReachabilityScan()") ?? "";
const bootSvcBody = body(wscCode, "void WebServerComm::serviceBootReachabilityScan()") ?? "";
const bootRejoinBody = body(wscCode, "void WebServerComm::rejoinNetwork()") ?? "";
const bootStopBody = body(wscCode, "void WebServerComm::stop()") ?? "";
if (!bootArmBody) {
  fail("boot-scan-first", "could not locate WebServerComm::armBootAutoJoin() - check this rule still matches the code");
} else if (!/startBootReachabilityScan\s*\(\s*\)/.test(bootArmBody)) {
  fail("boot-scan-first", "armBootAutoJoin() no longer asks the air before it joins - a stored-but-absent router holds the own hotspot on a moving channel for the whole retry budget (the owner's 'detected but won't connect')");
} else if (!bootProbeBody) {
  fail("boot-scan-first", "WebServerComm::startBootReachabilityScan() is gone - the boot probe has no producer");
} else if (!/BOOT_SCAN_HEAP_FLOOR_BYTES/.test(bootProbeBody)) {
  fail("boot-scan-first", "startBootReachabilityScan() no longer floors on the boot heap - the scan can be issued into a starved heap and land silently empty");
} else if (!bootSvcBody) {
  fail("boot-scan-first", "WebServerComm::serviceBootReachabilityScan() is gone - the boot probe has no consumer");
} else if (!/WiFi\s*\.\s*scanNetworks\s*\(/.test(bootSvcBody) || !/WiFi\s*\.\s*scanComplete\s*\(\s*\)/.test(bootSvcBody)) {
  fail("boot-scan-first", "the boot probe does not actually run a non-blocking scan (scanNetworks/scanComplete)");
} else if (!/storedSsid_/.test(bootSvcBody)) {
  fail("boot-scan-first", "the boot probe never compares a result against the stored router name - it cannot tell present from absent");
} else if (!/WiFi\s*\.\s*scanDelete\s*\(\s*\)/.test(bootSvcBody)) {
  fail("boot-scan-first", "the boot probe never frees the driver's scan results (scanDelete()) - the boot heap is the tightest of the uptime");
} else if (!/rejoinNetwork\s*\(\s*\)/.test(bootSvcBody)) {
  fail("boot-scan-first", "the boot probe has no fallback to rejoinNetwork() - a probe that cannot answer would strand the car without its router");
} else if (!/if\s*\(\s*inRange\s*\|\|\s*!\s*conclusive\s*\)/.test(bootSvcBody)) {
  fail("boot-scan-first", "the boot probe's join decision is not `inRange || !conclusive` - an inconclusive scan must fall back to the rejoin, and only a conclusive ABSENT result may stay AP-only");
} else if (!p5HandleBody || !/serviceBootReachabilityScan\s*\(\s*\)/.test(p5HandleBody)) {
  fail("boot-scan-first", "handle() no longer drags serviceBootReachabilityScan() - the probe would arm and never run");
} else if (!/bootScanArmed_\s*=\s*false/.test(bootRejoinBody) || !/bootScanArmed_\s*=\s*false/.test(bootStopBody)) {
  fail("boot-scan-first", "an explicit switch (rejoinNetwork) or a Wi-Fi OFF (stop) no longer clears an armed boot probe - a stale probe could fire a second rejoin after the owner chose a network");
} else {
  pass("boot-scan-first", "the boot auto-join probes the air first; an absent router settles AP-only with no rejoin churn, an inconclusive scan falls back to the rejoin, and an explicit switch or Wi-Fi OFF clears a stale probe");
}

// P6 / issue 1 (2026-10-16): BOTH TRANSPORTS STAY UP AT THE SAME TIME.
//
// Issue 1 asks for the app and the car connected over Bluetooth AND Wi-Fi
// simultaneously. The firmware already does this — the U-79 "both transports
// on" branch only starts a transport that is down, it never stops one — but
// until this rule that was a fact of one `if` nobody could see. The old
// "one client on one transport powers the OTHER down" teardown was retired
// with U-105, and the app-side half (ConnectionSection's shared `linkLive`
// gate hiding the BT scan while Wi-Fi was live) landed the same day, so a
// regression on EITHER side would silently put the fleet back to one link.
//
// This rule pins the firmware side: the both-enabled branch must exist, must
// start whichever link is down, and must contain no `stop()` — a stop there
// would drop the second link the moment it came up, which is exactly the
// defect P6 exists to remove. The device half (both links live at once,
// `BT+WiFi` on the OLED, each disconnecting independently) is rows U-113-12..14
// on the owner's bench (F-61).
const dualAt = inoCode.indexOf("if (wifiEn && btEn)");
const dualOn = dualAt < 0 ? null : braced(inoCode, dualAt);
if (!dualOn) {
  fail("dual-link-kept", "could not locate the U-79 'both transports on' branch - check this rule still matches the code");
} else if (/\.stop\s*\(/.test(dualOn)) {
  fail("dual-link-kept", "the both-enabled branch STOPS a transport - a stop() here drops the second link as soon as it comes up, so the car is back to one active link (issue 1)");
} else if (!/ensureBtAdvertising\s*\(/.test(dualOn)) {
  fail("dual-link-kept", "the both-enabled branch never raises Bluetooth - with Bluetooth down and Wi-Fi up the second link can never appear, so a reboot leaves a one-link car");
} else if (!/beginAlwaysOn\s*\(/.test(dualOn)) {
  fail("dual-link-kept", "the both-enabled branch never raises the web layer - with Bluetooth up and Wi-Fi down the own hotspot and page never come up, so the second link can never appear");
} else {
  pass("dual-link-kept", "the both-enabled branch starts whichever link is down and stops neither - BT + own hotspot + page stay up together (issue 1)");
}

// P4 / item F (2026-10-17): THE CAR CAN REFUSE A SECOND LINK, HONESTLY.
//
// The owner's report (item 6): connecting BOTH links can FREEZE/reset the car
// because the ESP32 runs near its allocation floor. Telemetry and scans already
// REFUSE below a floor; a LINK never did — the second transport came up into a
// starved heap and the run-time starved with it. P4 adds the missing refusal:
// `admitSecondLink()` gates ONLY the second link (the first is never gated — a
// car with no way in is worse than a car with one), the incumbent link is left
// untouched, and the verdict is REPORTED on BOTH surfaces — `;LINKCAP=` on the
// STATE line a Bluetooth-only app reads, `"link_cap"` in the /status JSON the
// web transport reads.
//
// This rule pins all of it. The app half (F-88: an ABSENT field means CAPABLE,
// never denied) is `linkCap.ts` + its tests in genumsolutions-app.
const linkFloorM = wsh.match(/#define\s+LINK_ADMISSION_FLOOR_BYTES\s+(\d+)ul/);
const linkFloor = linkFloorM ? Number(linkFloorM[1]) : 0;
const statusFloor = Number(wsh.match(/#define\s+STATUS_HEAP_FLOOR_BYTES\s+(\d+)ul/)?.[1] ?? 0);
const admitBody = body(wscCode, "bool WebServerComm::admitSecondLink") ?? "";
const capBody = body(wscCode, "int WebServerComm::linkCapCode") ?? "";
const linkBranch = dualOn ?? "";
const btAdmitAt = linkBranch.indexOf("if (shouldStartBt && webWasUp)");
const webAdmitAt = linkBranch.indexOf("if (shouldStartWeb && btUp)");
const btAdmit = btAdmitAt >= 0 ? braced(linkBranch, btAdmitAt) : null;
const webAdmit = webAdmitAt >= 0 ? braced(linkBranch, webAdmitAt) : null;
const linkStateLits = stripComments(raw("ModeManager.cpp"));
if (!linkFloor) {
  fail("link-admission", "LINK_ADMISSION_FLOOR_BYTES is not defined in WebServerComm.h - the car has no second-link budget to refuse against");
} else if (linkFloor <= statusFloor) {
  fail("link-admission", `LINK_ADMISSION_FLOOR_BYTES (${linkFloor}) is not ABOVE STATUS_HEAP_FLOOR_BYTES (${statusFloor}) - a standing second link costs MORE than the status path, so its cushion must be bigger`);
} else if (!/ESP\.getFreeHeap\(\)/.test(admitBody) || !/<\s*LINK_ADMISSION_FLOOR_BYTES/.test(admitBody)) {
  fail("link-admission", "admitSecondLink() does not compare the free heap against LINK_ADMISSION_FLOOR_BYTES - the refusal no longer exists");
} else if (!/linkCapRefused_\s*=\s*true/.test(admitBody)) {
  fail("link-admission", "admitSecondLink() does not LATCH the refusal - the app cannot tell a refusal from a link still coming up");
} else if (!/return\s+false/.test(admitBody) || !/return\s+true/.test(admitBody)) {
  fail("link-admission", "admitSecondLink() does not return both a refusal (false) and an admit (true)");
} else if (!/(wifiEnabled_\s*&&\s*btEnabled_)/.test(capBody)) {
  fail("link-admission", "linkCapCode() does not fold the transport toggles - with one transport off there is no second link to deny, so it must read 1");
} else if (!btAdmit || !/admitSecondLink\s*\(/.test(btAdmit)) {
  fail("link-admission", "the both-enabled branch does not gate the Bluetooth raise on admitSecondLink() - with Wi-Fi already up the second link is raised unconditionally again (the freeze, item 6)");
} else if (!webAdmit || !/admitSecondLink\s*\(/.test(webAdmit)) {
  fail("link-admission", "the both-enabled branch does not gate the Wi-Fi raise on admitSecondLink() - with Bluetooth already up the second link is raised unconditionally again (the freeze, item 6)");
} else if (/\.stop\s*\(/.test(linkBranch)) {
  fail("link-admission", "the both-enabled branch STOPS a transport - a refusal must keep the incumbent link, never tear it down");
} else if (!/webUp\s*=\s*false/.test(linkBranch)) {
  fail("link-admission", "a refused Wi-Fi raise does not revert the optimistic webUp mark - the loop would believe Wi-Fi is up and never retry it once the heap recovers");
} else if (!linkStateLits.includes(";LINKCAP=%d")) {
  fail("link-admission", "the STATE line does not carry ;LINKCAP=%d - a Bluetooth-only app cannot learn the car's link verdict");
} else if (!stripComments(body(wsc, "void WebServerComm::buildStatusJson") ?? "").includes("link_cap")) {
  fail("link-admission", "/status JSON does not carry link_cap - the web transport cannot learn the car's link verdict");
} else {
  pass("link-admission", "the car refuses a SECOND link below LINK_ADMISSION_FLOOR_BYTES, keeps the incumbent, and reports the verdict on the STATE line and in /status");
}

// U-117/U-118 (2026-10-20): THE CAR'S PAGE STATE IS PUBLISHED, AND THE
// CHOICE SURVIVES A POWER CYCLE.
//
// Owner (2026-10-20): "the webpage toggle off button is not working". The car
// already HAD the toggle (WIFICFG;WIFI;PAGE), the persistence (NVS
// `page_off`) and the serve gate (handleRoot reads pageOff_) — but the STATE
// line, the only surface a BLUETOOTH-only app reads, carried nothing, and
// `/status` needs the web transport. So the app could never learn the truth
// and painted an ASSUMED off after every (re)connect: a switch that reads
// wrong reads broken.
//
// A wire field with no consumer is noise, and a consumer with no field is a
// guess — hence the same-commit pair: `;PAGE=%s` here, and pageToggle.test.ts
// (genumsolutions-app) parsing `;PAGE=` / `page_on` in the same round.
//
// Views matter (see the stripComments/code() note at the top): the NVS load
// check needs comments blanked AND strings KEPT, so it reads stripComments of
// the RAW source — `code()` blanks `"page_off"` and would blind the rule.
const mmStateLits = stripComments(raw("ModeManager.cpp"));
const statusPsLits = stripComments(body(wsc, "void WebServerComm::buildStatusJson") ?? "");
const wscPlain = stripComments(wsc);
if (!mmStateLits.includes(";PAGE=%s")) {
  fail("page-state", "the STATE line does not carry ;PAGE=%s - a Bluetooth-only app can never read the car's page-server state, so its switch can only guess");
} else if (!/pageServerIsOn\(\)/.test(mmStateLits)) {
  fail("page-state", "the STATE PAGE key is not fed by pageServerIsOn() - the line would publish something other than the serve truth");
} else if (!statusPsLits.includes("page_on")) {
  fail("page-state", "/status JSON does not carry page_on - the web transport cannot read the car's page-server state");
} else if (!wscPlain.includes('getBool("page_off"')) {
  fail("page-state", "the boot load no longer reads NVS page_off - a turned-OFF page turns itself back ON at the next power cycle, which is exactly the toggle 'not working'");
} else if (!/pageOff_\s*\|\|\s*pageServerMustBeHeld\(\)/.test(wscPlain)) {
  fail("page-state", "the serve path no longer gates on pageOff_ - WIFICFG;WIFI;PAGE would answer while the page keeps being served");
} else {
  pass("page-state", "the page-server state rides STATE (;PAGE=) and /status (page_on), survives a reboot (NVS page_off), and the serve path honours it");
}

// P6 / item D (2026-10-17): THE CAR HANDS OVER A BOUNDED EVENT LOG.
//
// The owner's report (item 4): the remote needs to SEE and ACT ON the
// bug/reset/freeze history **tied to the car's unique profile**. P6 is the
// car half: a HARD-BOUNDED ring (F-89 — cap first, then read) of boots,
// faults, low-heap crossings, link refusals and link up/down edges, persisted
// as ONE NVS blob, plus the verbs the app (P7) reads it with:
// `LOG?` / `LOG;GET;<n>` / `LOG;CLEAR`, and the /status summary field.
//
// Bounded means bounded in every dimension the gate can see: the cap is a
// define, the ring drops the OLDEST at the cap, the GET reply is size-bounded
// so one reply can never truncate mid-event, and FACTORY;RESET clears the
// ring with the namespace it lives in.
const evCapH = raw("CrashLog.h");
const evSrc = code(raw("CrashLog.cpp"));
const eventCap = Number(evCapH.match(/#define\s+CAR_EVENT_CAP\s+(\d+)u?/)?.[1] ?? 0);
const evAppend = body(evSrc, "void crashLogEvent") ?? "";
const evBegin = body(evSrc, "void crashLogBegin") ?? "";
const evClear = body(evSrc, "void carLogClear") ?? "";
const evReset = body(evSrc, "void crashLogResetCounters") ?? "";
// The verbs' reply literals live inside C string literals, so this body is
// read from the RAW source (comments kept, literals KEPT): the code() view
// blanks literals and would fail even a correct implementation — the same
// view split the link-admission rule learned above.
const logBody = body(wsc, "bool WebServerComm::handleLogCommand") ?? "";
const inoHook = stripComments(raw("Genum_4WD4M_CAR.ino"));
const statusBody2 = stripComments(body(wsc, "void WebServerComm::buildStatusJson") ?? "");
if (!eventCap) {
  fail("event-log", "CAR_EVENT_CAP is not defined in CrashLog.h - the log grows unbounded and becomes a flash-exhaustion bug (F-89)");
} else if (eventCap < 4 || eventCap > 64) {
  fail("event-log", `CAR_EVENT_CAP (${eventCap}) is outside a sane bounded-window range - a cap this unusual is a bug, not a design`);
} else if (!/memmove\s*\(/.test(evAppend) || !/CAR_EVENT_CAP/.test(evAppend)) {
  fail("event-log", "crashLogEvent() does not drop the OLDEST event at the cap - the ring overflows past CAR_EVENT_CAP");
} else if (!/s_evCount\s*=\s*CAR_EVENT_CAP\s*-\s*1/.test(evAppend)) {
  fail("event-log", "crashLogEvent() drops the oldest but does not re-clamp the count to CAR_EVENT_CAP-1 - the next append indexes past the array");
} else if (!/CAR_EV_BOOT/.test(evBegin) || !/CAR_EV_FAULT/.test(evBegin)) {
  fail("event-log", "crashLogBegin() does not record the BOOT and FAULT events - the ring misses the two facts a reset report is built from");
} else if (!/carLogClear\s*\(/.test(evReset)) {
  fail("event-log", "crashLogResetCounters() (FACTORY;RESET) does not clear the event ring - a fresh start resurrects historical events, a lie about the present");
} else if (!logBody || !logBody.includes("LOG;GET;") || !logBody.includes("LOG;CLEARED")) {
  fail("event-log", "handleLogCommand() does not implement LOG;GET / LOG;CLEAR with their replies - the app cannot read or reset the log");
} else if (!logBody.includes("sizeof(reply) - at - 1") && !logBody.includes("room")) {
  fail("event-log", "LOG;GET does not size-check each event against the reply buffer - one reply can truncate mid-event, which invents a broken record");
} else if (!inoHook.includes("handleLogCommand(cmd)")) {
  fail("event-log", "the sketch's system-command hook does not call handleLogCommand() - the verbs exist but no transport can reach them");
} else if (!inoHook.includes("trackLinkEvents(")) {
  fail("event-log", "the loop does not track link up/down transitions - the link-add/drop events the owner asked for never exist");
} else if (!/.event_log_count./.test(statusBody2)) {
  fail("event-log", "/status JSON does not carry event_log_count - the web transport cannot learn there is a log to fetch");
} else if (!/carLogCount\s*\(/.test(statusBody2)) {
  fail("event-log", "event_log_count is not fed by carLogCount() - a hardcoded count would promise a log that does not exist");
} else {
  pass("event-log", `the car keeps a bounded event ring (cap ${eventCap}, oldest dropped), records boot/fault/low-heap/refusal/link edges, and serves LOG?/LOG;GET;<n>/LOG;CLEAR over the system hook, with /status carrying event_log_count`);
}

// P7 / issue 8 (2026-10-16): EVERY SETTING STORES IN THE CAR AND COMES BACK.
//
// Issue 8's bar (owner): *"every one of these settings must store in the car
// AND in the app promptly and stay in sync."* The car's half of that bar is a
// ROUND TRIP: a toggle command must (1) persist to NVS BEFORE it is acked,
// (2) answer with the state it actually holds - including re-answering when a
// flip is refused - and (3) be readable back on BOTH surfaces a client can be
// attached to: the STATE line (what a Bluetooth-only app polls) and the
// /status JSON (the web transport). The "never both transports off" clamp is
// part of the same honesty - a refused flip is re-answered with the live
// state, so the reply always describes reality.
//
// The firmware held every one of these already; until this rule it was an
// invisible fact, and the app half (adopted values reaching the per-car
// profile, replies honouring the toggle grace, refresh-on-focus) landed the
// same day in genumsolutions-app's `persistenceSync.test.ts`. The device half
// is row U-113-15: toggle -> kill app -> reopen -> car reboot -> still in
// sync. Green here is source shape only (F-61).
const wificfgLits = stripComments(wsc);
const branchFrom = (sig, from) => wificfgLits.indexOf(sig, from);
const wifiBrAt = branchFrom('strncasecmp(rest, ";WIFI", 5)', 0);
const btBrAt = branchFrom('strncasecmp(rest, ";BT", 3)', wifiBrAt);
const ajBrAt = branchFrom('strncasecmp(rest, ";AUTOJOIN", 9)', btBrAt);
const ssidBrAt = branchFrom("rest[0] == ';'", ajBrAt);
const brSlice = (a, b) => (a < 0 || b <= a ? null : wificfgLits.slice(a, b));
const wifiBr = brSlice(wifiBrAt, btBrAt);
const btBr = brSlice(btBrAt, ajBrAt);
const ajBr = brSlice(ajBrAt, ssidBrAt);
const toggleJsonLits = stripComments(body(wsc, "void WebServerComm::buildStatusJson") ?? "");
const toggleStateLits = stripComments(raw("ModeManager.cpp"));
const missingState = [";WIFI=%s", ";BT=%s", ";AUTOJOIN=%s"].filter((k) => !toggleStateLits.includes(k));
const missingJson = ["wifi_enabled", "bt_enabled", "boot_autojoin"].filter((f) => !toggleJsonLits.includes(f));
if (!wifiBr || !btBr || !ajBr) {
  fail("settings-round-trip", "could not locate the WIFICFG transport-toggle branches - check this rule still matches the code");
} else if (!/putBool\s*\(\s*"t_wifi"/.test(wifiBr)) {
  fail("settings-round-trip", "WIFICFG;WIFI;ON|OFF no longer persists to NVS (t_wifi) before acking - the setting dies at the next reboot, so nothing is ever stored in the car (issue 8)");
} else if (!/"WIFICFG;WIFI;%s"/.test(wifiBr)) {
  fail("settings-round-trip", "the Wi-Fi toggle never answers with the state it holds - a set can go unconfirmed and the app has nothing to stay in sync with (issue 8)");
} else if (!/if\s*\(\s*!next\s*&&\s*!btEnabled_\s*\)/.test(wifiBr)) {
  fail("settings-round-trip", "the Wi-Fi toggle no longer refuses to turn the LAST transport off - both transports can be commanded off, which the owner ruled impossible (\"both cant be off at the same time\")");
} else if (!/putBool\s*\(\s*"t_bt"/.test(btBr)) {
  fail("settings-round-trip", "WIFICFG;BT;ON|OFF no longer persists to NVS (t_bt) before acking - the setting dies at the next reboot, so nothing is ever stored in the car (issue 8)");
} else if (!/"WIFICFG;BT;%s"/.test(btBr)) {
  fail("settings-round-trip", "the Bluetooth toggle never answers with the state it holds - a set can go unconfirmed and the app has nothing to stay in sync with (issue 8)");
} else if (!/if\s*\(\s*!next\s*&&\s*!wifiEnabled_\s*\)/.test(btBr)) {
  fail("settings-round-trip", "the Bluetooth toggle no longer refuses to turn the LAST transport off - both transports can be commanded off, which the owner ruled impossible (\"both cant be off at the same time\")");
} else if (!/putBool\s*\(\s*"boot_autojoin"/.test(ajBr)) {
  fail("settings-round-trip", "WIFICFG;AUTOJOIN;ON|OFF no longer persists to NVS (boot_autojoin) before acking - the boot auto-join choice dies at the next reboot");
} else if (!/"WIFICFG;AUTOJOIN;%s"/.test(ajBr)) {
  fail("settings-round-trip", "the boot auto-join toggle never answers with the state it holds");
} else if (missingState.length) {
  fail("settings-round-trip", `the STATE line no longer carries ${missingState.join(" / ")} - a Bluetooth-only client cannot re-sync the toggles after a reconnect or a restart, so panel and car drift apart (issue 8)`);
} else if (missingJson.length) {
  fail("settings-round-trip", `the /status JSON no longer carries ${missingJson.join(" / ")} - the web transport cannot re-sync the toggles`);
} else {
  pass("settings-round-trip", "each toggle persists to NVS before it is acked, answers with the state it holds, is reported on both the STATE line and the /status JSON, and the last-transport-off clamp stands");
}

// Q1 (owner, 2026-10-22 — both halves of the default): THE FIRMWARE BOOT-HOP IS
// STOPPED. A power-up with no explicit opt-in must stay on the own hotspot —
// only the app's smart-link joins a router. Three source facts carry that: the
// NVS READ defaults false, armBootAutoJoin() still honours the flag (so
// WIFICFG;AUTOJOIN;OFF can never be a lying switch, and ON still works), and
// FACTORY;RESET returns a virgin board to that default. check-webpage-js.mjs
// pins the load default independently with its own FAIL text (companion gate,
// same claim, two harnesses — mutants M127-M129 prove this one).
if (!/getBool\s*\(\s*"boot_autojoin"\s*,\s*false\s*\)/.test(wscPlain)) {
  fail("boot-hop-off", "loadStoredWifi() no longer defaults boot_autojoin to OFF - the car hops to a router at power-up again, undoing Q1 (only the app's smart-link may join a router)");
} else if (!bootArmBody) {
  fail("boot-hop-off", "could not locate WebServerComm::armBootAutoJoin() - check this rule still matches the code");
} else if (!/if\s*\(\s*!bootAutojoin_\s*\)/.test(bootArmBody)) {
  fail("boot-hop-off", "armBootAutoJoin() no longer honours bootAutojoin_ - WIFICFG;AUTOJOIN;OFF becomes a switch that reads one way and does nothing, and the stopped default has no effect");
} else if (!/bootAutojoin_ = false;/.test(wscPlain)) {
  fail("boot-hop-off", "FACTORY;RESET no longer restores the virgin-board default (boot auto-join OFF) - a fresh start would hop to a router again");
} else {
  pass("boot-hop-off", "boot auto-join defaults OFF and is honoured (load default + arm guard + factory default): a power-up stays on the own hotspot unless WIFICFG;AUTOJOIN;ON opted in");
}

// The one thing this gate cannot check is the thing that matters most: that a
// real car answers. That is the owner's bench, not a laptop.
console.log("\nfirmware-contract: source checks only - no radio was touched, no car was asked.");
console.log("End-to-end is the device matrix (TRACKS/DEVICE-TESTS.md); green here is not evidence it works (F-61).");

console.log(`\n${failures} gate failures total`);
process.exit(failures > 0 ? 1 : 0);