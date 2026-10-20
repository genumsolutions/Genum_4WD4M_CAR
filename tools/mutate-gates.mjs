// tools/mutate-gates.mjs — proof that the gates in this repo are not decorative.
//
// WHY THIS FILE EXISTS. The U-95 round produced three gates that could not have
// worked, and two of them printed `ok` while doing so (F-83):
//
//   1. wdt-feed counted `esp_task_wdt_reset()` occurrences in the RAW file and
//      reported four watchdog feeds for a loop containing three. The fourth was
//      the function's name inside the comment explaining the fix.
//   2. The crash-record idle rule matched `next[0] != '\0'` against
//      COMMENT-STRIPPED source, where code() blanks character literals, so it
//      could never match — and it failed on the fixed tree.
//   3. The Phase 1e ordering rule used indexOf("loadToken()") on raw source and
//      matched the word inside the comment explaining the ordering.
//
// A gate that cannot fail is worse than no gate: it prints `ok`, and `ok` is
// believed. This harness is the answer to "is that gate real?" — for each rule,
// break the thing it claims to police and confirm the INTENDED rule goes red.
//
// THE ONE RULE THAT MATTERS MOST: every mutant is proven in BOTH directions.
// Running only on green code proves nothing at all; that is exactly how all three
// bugs above survived. Revert the fix, confirm red, restore, confirm green.
//
// USAGE
//   node tools/mutate-gates.mjs                 # every mutant
//   node tools/mutate-gates.mjs -k wdt          # only mutants matching /re/
//   node tools/mutate-gates.mjs --list          # list mutants, run nothing
//
// A full run is ~35 s: two node gates per mutant, no compiler. The static_assert
// invariants (HTTP_BODY_BUDGET_MS < LOOP_TASK_WDT_TIMEOUT_MS) are covered here by
// M14/M15 through the source rule, and are additionally proven against the real
// compiler by hand: restore the 6000 ms budget and read the failure, which names
// F-77 in the diagnostic. That is done once per invariant rather than per run,
// because all four static_assert mutants assert the same mechanism and four 40 s
// compiles prove nothing the first one did not. For a future compiler-enforced
// invariant, add `compile: true` to the mutant and a `gateSyntax([...])` call in
// the runner — the classify() branch for it already exists.
//
// WHY THIS IS A REPO TOOL AND NOT A SCRATCH SCRIPT. The previous harness lived
// in %TEMP% and was deleted mid-session by something outside the workspace, taking
// ~260 lines of accumulated knowledge with it. Verification that lives in a temp
// directory is verification nobody inherits.
//
// SAFETY. This file EDITS THE WORKING TREE. It refuses to run against a dirty
// tree, snapshots every file it will touch, restores in a finally block, and
// re-checks the tree against the snapshot afterwards. A restore that mangles a
// byte (encoding, line endings) is reported as loudly as an undetected mutant,
// because a mangled source file that still passes its gate is the worst outcome
// available here.

import { execFileSync } from "node:child_process";
import { existsSync, readFileSync, writeFileSync, mkdtempSync } from "node:fs";
import { tmpdir } from "node:os";
import { fileURLToPath } from "node:url";
import { dirname, join } from "node:path";

const root = join(dirname(fileURLToPath(import.meta.url)), "..");
const rel = (p) => join(root, p);

const argv = process.argv.slice(2);
const has = (f) => argv.includes(f);
const val = (f, d = null) => { const i = argv.indexOf(f); return i >= 0 ? argv[i + 1] : d; };
const filter = val("-k");
const listOnly = has("--list");

let failures = 0, wrongReason = 0, caught = 0;

// ---- process helpers ----------------------------------------------------

function run(cmd, args, opts = {}) {
  try {
    const out = execFileSync(cmd, args, { cwd: root, encoding: "utf8", stdio: ["ignore", "pipe", "pipe"], ...opts });
    return { code: 0, out };
  } catch (e) {
    // execFileSync throws on non-zero. stdout and stderr are captured separately
    // on purpose: PowerShell interleaves them into one stream, which truncated
    // compiler diagnostics mid-message and made a correctly-failing compile look
    // like an unrelated error. Both streams are searched below.
    return { code: e.status ?? 1, out: `${e.stdout ?? ""}${e.stderr ?? ""}` };
  }
}

const gateContract = () => run(process.execPath, [rel("tools/check-firmware-contract.mjs")]);
const gatePage = () => run(process.execPath, [rel("tools/check-webpage-js.mjs")]);


function gitDirty() {
  const { out } = run("git", ["status", "--porcelain"]);
  return out.trim().length > 0 ? out.trim() : null;
}

// ---- mutation primitives ------------------------------------------------

const readBytes = (p) => readFileSync(rel(p));
const snapshot = new Map();
let scratch;

function takeSnapshot(files) {
  scratch = mkdtempSync(join(tmpdir(), "genum-mutate-"));
  for (const f of files) {
    const bytes = readBytes(f);
    snapshot.set(f, bytes);
    writeFileSync(join(scratch, f.replace(/[\\/]/g, "_")), bytes);
  }
}

function restoreAll() {
  for (const [f, bytes] of snapshot) writeFileSync(rel(f), bytes);
}

/** Replace `find` with `repl`, requiring the anchor to be present EXACTLY once.
 *  A zero-match or multi-match is an error, not a shrug: it means the mutant
 *  does not describe the tree it is about to mutate, and a silently-inert
 *  mutant is how a bad mutant reports itself as a good gate. */
// EOL-NORMALIZING ANCHORS. Multi-line anchors are written in the table below
// with "\n", because a literal CRLF in a JS string is unreadable and cannot be
// eyeballed against the source. This repo's .cpp files are CRLF on disk, so the
// first version of this harness failed three mutants with "anchor matched 0
// times" on correct code — the exact failure mode this file exists to catch,
// committed by me in the file meant to catch it. Rather than paste invisible
// \r characters into the table, anchors are normalized to the file's own line
// endings before matching.
//
// This is also why the byte-exact restore check below matters: writing a mutated
// file with the wrong endings would otherwise leave a diff behind that survives
// the run.
function detectEol(text) {
  const crlf = (text.match(/\r\n/g) ?? []).length;
  const lf = (text.match(/(?<!\r)\n/g) ?? []).length;
  return crlf >= lf ? "\r\n" : "\n";
}

const fit = (text, needle) => text.includes(needle)
  ? needle
  : needle.replace(/\n/g, detectEol(text));

function mutate(file, find, repl) {
  const before = readFileSync(rel(file), "utf8");
  const parts = before.split(fit(before, find));
  if (parts.length - 1 !== 1) {
    throw new Error(`anchor matched ${parts.length - 1} times in ${file} (need exactly 1):\n${JSON.stringify(find)}`);
  }
  writeFileSync(rel(file), before.replace(fit(before, find), fit(before, repl)), "utf8");
}

// ---- the mutants --------------------------------------------------------
//
// `rule` is the gate rule the mutant is EXPECTED to catch it. Anything else going
// red is reported separately as caught-wrong-reason: a mutant caught by an
// unrelated rule has not tested the rule it was written for, and usually means
// the rule is redundant or the mutant is blunt.
//
// `compile: true` means this mutant is enforced by the COMPILER (a static_assert)
// rather than by a source rule. Those are checked differently and slowly: the
// evidence is not "a rule went red" but "the build failed AND the diagnostic
// names the invariant", because "it failed to build" alone is not actionable six
// weeks from now.

const CRASH = "CrashLog.cpp";
const WSC = "WebServerComm.cpp";
const WSH = "WebServerComm.h";
const INO = "Genum_4WD4M_CAR.ino";
const MM = "ModeManager.cpp";
const CL = "CrashLog.cpp";
const CLH = "CrashLog.h";

const mutants = [
  // ---- crash record (F-79) ----
  { id: "M1", rule: "reset-truth", file: INO,
    find: ".trigger_panic = false,", repl: ".trigger_panic = true,",
    why: "the unreachable-label bug re-introduced: a hung loop() reports PANIC again, so TASK_WDT can never be seen" },

  { id: "M2", rule: "crash-record", file: INO,
    find: "  crashLogBegin();", repl: "  // crashLogBegin(); moved below",
    why: "crashLogBegin() no longer runs before bring-up, so the recorded heap is post-allocation" },

  { id: "M3", rule: "crash-record", file: CRASH,
    find: "    default:\n      return true;\n  }\n}", repl: "    default:\n      return false;\n  }\n}",
    why: "isFaultReset() defaults to 'fine', so an unknown reset is silently dismissed" },

  { id: "M4", rule: "crash-record", file: CRASH,
    find: "    case ESP_RST_POWERON:\n    case ESP_RST_EXT:", repl: "    case ESP_RST_EXT:",
    why: "a clean power-on is counted as a fault, so boot count becomes crash count" },

  { id: "M5", rule: "crash-record", file: CRASH,
    find: "if (next[0] != '\\0' && s_phaseWriteSeen &&",
    repl: "if (s_phaseWriteSeen &&",
    why: "the idle transition is rate-limited again: a parked-car crash is blamed on whatever the car last did (83ecf15)" },

  { id: "M6", rule: "crash-record", file: CRASH,
    find: "if (next[0] != '\\0' && s_phaseWriteSeen && (now - s_lastPhaseWriteMs) < CRASHLOG_PHASE_MIN_MS) return;",
    repl: "if (false) return;",
    why: "the opposite regression: no write floor at all, so flash writes become unbounded" },

  // ---- status JSON (silently malformed output) ----
  // The anchor runs through the Phase 1c feed line and the first comment line:
  // the HTTP copy of this JSON begins with the same two lines, so a three-line
  // anchor would match twice after Phase 1c split build and broadcast apart.
  { id: "M7", rule: "status-buffer", file: WSC,
    find: "  static char buf[3584];\n  buildStatusJson(buf, sizeof(buf));\n  // U-95 Phase 1c: the write gets its OWN fresh window.",
    repl: "  static char buf[1200];\n  buildStatusJson(buf, sizeof(buf));\n  // U-95 Phase 1c: the write gets its OWN fresh window.",
    why: "the two status-JSON buffers drift apart, so the two copies disagree about which got truncated" },

  // Anchored on `server.send(200, "application/json", buf)`, which is the HTTP
  // copy of the status JSON. An earlier version of this anchor guessed
  // `server.sendContent(buf)` and matched nothing -- the harness reported it as
  // an undetected mutant, which is the correct outcome for an anchor that does
  // not describe the tree, and the reason anchors are never allowed to fail
  // quietly.
  { id: "M8", rule: "status-buffer", file: WSC,
    find: "  static char buf[3584];\n  buildStatusJson(buf, sizeof(buf));\n  server.send(200, \"application/json\", buf);",
    repl: "  static char buf[900];\n  buildStatusJson(buf, sizeof(buf));\n  server.send(200, \"application/json\", buf);",
    why: "a buffer shrunk below what the fragments need: snprintf stops silently and the client gets malformed JSON" },

  { id: "M9", rule: "crash-visible", file: WSC,
    find: '"\\"crash_count\\":%lu,"', repl: '"\\"crash_cnt\\":%lu,"',
    why: "a crash key is renamed out of the status JSON, so /status stops reporting it" },

  // U-96i, both halves: the fw SLOT must exist, and it must be fed from
  // FW_VERSION in that slot's position. M66 deletes the key (the app can never
  // learn the firmware version); M67 severs the feed (the slot reports some
  // other argument's bytes - identity, a stale literal, anything but the
  // version). Each must fail status-fw by name.
  { id: "M66", rule: "status-fw", file: WSC,
    find: '"\\"fw\\":\\"%s\\","', repl: '"\\"fw_ver\\":\\"%s\\","',
    why: "the fw key is renamed out of the status JSON, so the app never learns the firmware version and falls back to a static catalog guess presented as truth" },

  { id: "M67", rule: "status-fw", file: WSC,
    find: "      boardId,\n      FW_VERSION,\n      networks,", repl: "      boardId,\n      networks,",
    why: "FW_VERSION is severed from its snprintf slot - the fw key now reports whatever argument follows, so the app is told a version that is not one (the board-id-as-version bug, restated on the car side)" },

  // ---- page (own harness) ----
  { id: "M10", rule: "crash-visible", file: "WebPage.h",
    find: "cc>0", repl: "cc>-1",
    why: "the page fabricates a restart row on a clean car instead of showing one only when crash_count > 0" },

  // The row is a `<span id='rst-wrap'>` inside the status aside, not a div named
  // restartRow -- the first version of this anchor guessed a name and matched
  // nothing. Renaming the id breaks both the script that fills it and the page
  // harness that checks it, which is the point: evidence exists, nothing is shown.
  { id: "M11", rule: "page-harness", file: "WebPage.h",
    find: "<span id='rst-wrap' style='display:none'>", repl: "<span id='rst-wrapX' style='display:none'>",
    why: "the restart row can never be shown, so evidence exists and nothing is displayed" },

  // ---- page-send watchdog (F-77) ----
  // This mutant is caught by `wdt-feed`, not by anything budget-shaped. Writing
  // `wdt-budget` here first was wrong: starving the watchdog has nothing to do
  // with the budget, and the harness correctly flagged the mismatch. The rule
  // field is the claim being tested, so it has to name the rule that should
  // actually fire.
  { id: "M12", rule: "wdt-feed", file: WSC,
    find: "    esp_task_wdt_reset();\n    if (w == 0) {", repl: "    if (w == 0) {",
    also: [{ find: "        esp_task_wdt_reset();\n        delay(1);", repl: "        delay(1);" }],
    why: "the stall path stops feeding the watchdog entirely — the spin returns" },

  { id: "M13", rule: "wdt-feed", file: WSC,
    find: "      const unsigned long gapStart = millis();\n      while (millis() - gapStart < HTTP_WRITE_RETRY_GAP_MS) {\n        esp_task_wdt_reset();\n        delay(1);\n      }",
    repl: "      yield();",
    why: "the real gap reverts to the bare yield() that caused millions of wasted iterations" },

  { id: "M14", rule: "wdt-budget", file: WSC,
    find: "  while (off < pageLen) {",
    repl: "  while (off < pageLen) {\n    c.write((const uint8_t*)\"\", 0);",
    why: "a write happens before the budget check, so an iteration can begin out of time" },

  { id: "M15", rule: "wdt-budget", file: INO,
    find: ".timeout_ms = LOOP_TASK_WDT_TIMEOUT_MS,", repl: ".timeout_ms = 5000,",
    why: "the sketch reverts to a bare literal, so the static_assert guards a number the hardware no longer uses" },

  // ---- WebSocket pump vs the watchdog (U-95 1b/1c) ----
  { id: "M19", rule: "ws-loop-feed", file: WSC,
    find: "  esp_task_wdt_reset();\n  ws.loop();",
    repl: "  ws.loop();",
    why: "the partial-frame read spends whatever the rest of the pass left in the window instead of a fresh one" },

  { id: "M20", rule: "ws-guard", file: WSC,
    find: "  if (!hasWsClients()) return;\n  // 1536: status JSON",
    repl: "  // 1536: status JSON",
    why: "the client guard is gone: the 1 s ticker builds the JSON and walks sockets with nobody listening" },

  { id: "M21", rule: "ws-broadcast-feed", file: WSC,
    find: "  esp_task_wdt_reset();\n  ws.broadcastTXT(buf);",
    repl: "  ws.broadcastTXT(buf);",
    why: "the stalled-client write (5-8 s) runs on a window a ws.loop() may have already spent" },

  { id: "M22", rule: "ws-budget", file: WSH,
    find: "#define LOOP_TASK_WDT_TIMEOUT_MS 10000ul",
    repl: "#define LOOP_TASK_WDT_TIMEOUT_MS 5000ul",
    why: "the watchdog ties the library's worst operation again — the stall races the reset instead of fitting under it" },

  // ---- router switching (U-95 Phase 2) ----
  // U-90 shipped the claim these rules enforce as a COMMENT: mode(WIFI_AP_STA)
  // "restarts the driver". It early-returns when the mode already equals
  // WIFI_AP_STA (WiFiGeneric.cpp:602), i.e. on every switch after the first —
  // the teardown did not exist and every gate stayed green. Restore the no-op
  // drop and the rule must go red.
  { id: "M23", rule: "switch-teardown", file: WSC,
    find: '      noteRadioStep("rejoin/mode-AP");\n      WiFi.mode(WIFI_AP);',
    repl: '      noteRadioStep("rejoin/mode-AP_STA");\n      WiFi.mode(WIFI_AP_STA);',
    why: "the drop reverts to mode(WIFI_AP_STA) — the WiFiGeneric.cpp:602 early-return no-op: no teardown on any switch after the first (the U-90 defect)" },

  { id: "M24", rule: "switch-teardown", file: WSC,
    find: "      if (WiFi.getMode() == WIFI_AP) {",
    repl: "      if (true) {",
    why: "the drop is never confirmed — the getMode() poll is gone, so the machine proceeds on a timer alone against a driver that may not have settled" },

  { id: "M25", rule: "switch-teardown", file: WSC,
    find: '      noteRadioStep("rejoin/mode-AP_STA");\n      WiFi.mode(WIFI_AP_STA);\n      rejoinStep_ = REJOIN_STEP_BEGIN;',
    repl: '      rejoinStep_ = REJOIN_STEP_BEGIN;',
    why: "the STA side is never re-raised — the car would stay AP-only after a switch, never joining the router it was asked for" },

  { id: "M26", rule: "wifi-persistent", file: WSC,
    find: "  WiFi.persistent(false);\n",
    repl: "",
    why: "the guard is gone: every WiFi.begin()/softAP() writes flash NVS again (Arduino default persistent=true) — router retries multiply the erase wear" },

  { id: "M27", rule: "wifi-persistent", file: WSC,
    find: "  WiFi.persistent(false);\n",
    repl: "",
    also: [{ find: "void WebServerComm::advanceRejoinStep() {",
            repl: "void WebServerComm::advanceRejoinStep() {\n  WiFi.persistent(false);" }],
    why: "the guard exists but not in the boot bring-up — it runs after the first config write it exists to prevent" },

  // ---- retry split (U-95 Phase 2) ----
  // The give-up path rebooted the car with exactly this shape (U-75): two
  // radio calls back to back inside one loop pass. The retry branch still
  // had it. Revert the split and the rule must go red.
  { id: "M28", rule: "retry-split", file: WSC,
    find: '        noteRadioStep("retry/disconnect");\n        WiFi.disconnect();\n        retryBeginPending_ = true;',
    repl: '        noteRadioStep("retry/disconnect");\n        WiFi.disconnect();\n        noteRadioStep("retry/begin");\n        WiFi.begin(storedSsid_, storedPass_);',
    why: "disconnect and begin share one pass again — the U-75 shape: two radio calls in one loop iteration, the begin racing the teardown" },

  { id: "M29", rule: "retry-split", file: WSC,
    find: "  if (retryBeginPending_) {\n    retryBeginPending_ = false;",
    repl: "  if (false && retryBeginPending_) {\n    retryBeginPending_ = false;",
    why: "the deferred-begin guard exists but can never fire — the flag is armed and the begin is never issued, leaving the STA down" },

  { id: "M30", rule: "retry-split", file: WSC,
    find: "  retryBeginPending_ = false;\n  lastWifiRetryMs_ = millis();",
    repl: "  lastWifiRetryMs_ = millis();",
    why: "rejoinNetwork() no longer cancels an in-flight retry begin — a stale deferred begin lands after the user asked for a different network (U-75 pattern)" },

  { id: "M31", rule: "retry-split", file: WSC,
    find: "  retryBeginPending_ = false;\n  lastWifiRetryMs_ = millis();",
    repl: "  retryBeginPending_ = false;",
    why: "rejoinNetwork() no longer re-stamps the backoff — the retry fires on pass 1 of the step machine and disconnects the teardown mid-walk" },

  // ---- honest + ranked scan (U-95 Phase 2c) ----
  // startScan() used to refuse SILENTLY while the handler claimed STARTED
  // unconditionally, and the collector capped at INSERT time in radio-order
  // while deduping on names. Each mutant restores one half of that shape;
  // scan-honest / scan-quality must go red for each.
  { id: "M32", rule: "scan-honest", file: WSC,
    find: '    if (startScan()) {\n      snprintf(reply, sizeof(reply), "ROUTERS;SCAN;STARTED");\n    } else {\n      snprintf(reply, sizeof(reply), "ROUTERS;SCAN;BUSY");\n    }',
    repl: '    startScan();\n    snprintf(reply, sizeof(reply), "ROUTERS;SCAN;STARTED");',
    why: "the handler claims STARTED unconditionally again — a dropped scan reports success and the app waits forever for a result that was never coming" },

  { id: "M33", rule: "scan-honest", file: WSC,
    find: "  if (connectingPending_) return false;         // associating - do not race the join\n",
    repl: "",
    why: "the associating gate is gone — a scan is kicked between WiFi.begin() and association, racing a driver that is mid-join and returning garbage" },

  { id: "M38", rule: "scan-honest", file: WSC,
    find: "  if (scanPending_ || scanBusy_) return false;  // armed or in flight already\n",
    repl: "",
    why: "the in-flight refusal is gone — a double-tap arms a second scan over one already running, and the reply lies about both" },

  { id: "M34", rule: "scan-quality", file: WSC,
    find: "      if (strcasecmp(scanNetworks_[k].bssid, bssid) == 0) {",
    repl: "      if (strcasecmp(scanNetworks_[k].ssid, name) == 0) {",
    why: "dedupe falls back to names — two different routers broadcasting one SSID silently merge into one row, and a multi-AP router's second radio disappears" },

  { id: "M35", rule: "scan-quality", file: WSC,
    find: "    if (!merged) {\n      ScannedNetwork entry;",
    repl: "    if (!merged && scanNetworks_.size() < SCAN_MAX_RESULTS) {\n      ScannedNetwork entry;",
    why: "the insert-time cap is back — the first N results in radio-order survive, dropping strong networks on report-order luck" },

  { id: "M36", rule: "scan-quality", file: WSC,
    find: "    while (b > 0 && scanNetworks_[b - 1].rssi < key.rssi) {",
    repl: "    while (false) {",
    why: "the strongest-first sort is neutered — which rows survive the cap is again whichever the radio reported first" },

  { id: "M37", rule: "scan-quality", file: WSC,
    find: "  scanNetworks_.clear();\n  for (int i = 0; i < scanCount_; ++i) {",
    repl: "  scanNetworks_.clear();\n  int n = scanCount_;\n  if (n > SCAN_MAX_RESULTS) n = SCAN_MAX_RESULTS;\n  for (int i = 0; i < n; ++i) {",
    why: "the pre-truncation is back — the loop only ever sees the first SCAN_MAX_RESULTS results, before any ranking can run" },

  { id: "M39", rule: "scan-quality", file: WSC,
    find: "    char bssid[18];\n    WiFi.BSSIDstr(i).toCharArray(bssid, sizeof(bssid));\n",
    repl: "",
    why: "the BSSID is never captured — the dedupe key does not exist at runtime, so the per-RADIO rule cannot hold" },

  { id: "M41", rule: "scan-quality", file: WSC,
    find: "  if (scanNetworks_.size() > SCAN_MAX_RESULTS) {\n    scanNetworks_.resize(SCAN_MAX_RESULTS);\n  }\n",
    repl: "",
    why: "the cap is gone entirely — an unbounded list bloats the status broadcast and the app's table with noise rows" },

  { id: "M42", rule: "scan-quality", file: WSC,
    find: "  for (size_t a = 1; a < scanNetworks_.size(); ++a) {\n    ScannedNetwork key = scanNetworks_[a];\n    size_t b = a;\n    while (b > 0 && scanNetworks_[b - 1].rssi < key.rssi) {\n      scanNetworks_[b] = scanNetworks_[b - 1];\n      --b;\n    }\n    scanNetworks_[b] = key;\n  }\n\n  // The ONE cap, last: the wire forms are buffer-bounded downstream, but an\n  // unbounded vector would still bloat the status broadcast (and the app's\n  // table) with noise rows nobody asked for.\n  if (scanNetworks_.size() > SCAN_MAX_RESULTS) {\n    scanNetworks_.resize(SCAN_MAX_RESULTS);\n  }",
    repl: "  if (scanNetworks_.size() > SCAN_MAX_RESULTS) {\n    scanNetworks_.resize(SCAN_MAX_RESULTS);\n  }\n\n  for (size_t a = 1; a < scanNetworks_.size(); ++a) {\n    ScannedNetwork key = scanNetworks_[a];\n    size_t b = a;\n    while (b > 0 && scanNetworks_[b - 1].rssi < key.rssi) {\n      scanNetworks_[b] = scanNetworks_[b - 1];\n      --b;\n    }\n    scanNetworks_[b] = key;\n  }",
    why: "the cap runs before the sort — rows are truncated while still in radio-order, so the ranking only ranks what survived the lottery" },

  { id: "M40", rule: "scan-quality", file: WSH,
    find: "    char bssid[18];  // \"AA:BB:CC:DD:EE:FF\"\n",
    repl: "",
    why: "the BSSID field is removed from ScannedNetwork — there is no per-RADIO dedupe key at all, forcing name-dedupe" },

  // ---- mode-walk tokens (U-95 Phase 3) ----
  // cycleMode() lived only on the GPIO switch: three tokens, one walk, both
  // wrap directions. Each mutant removes one fact the rule states.
  { id: "M43", rule: "mode-cycle", file: MM,
    find: '  if (strcmp(cmdBuf, "MODECYCLE") == 0 || strcmp(cmdBuf, "MODENEXT") == 0) {',
    repl: '  if (strcmp(cmdBuf, "MODENEXT") == 0) {',
    why: "MODECYCLE is gone — the walk is reachable only under the MODENEXT alias, and every consumer keyed on MODECYCLE gets an NACK" },

  { id: "M44", rule: "mode-cycle", file: MM,
    find: "  if (next < 0) next = MODE_COUNT - 1;\n",
    repl: "",
    why: "the negative wrap is gone — MODEPREV from the first registry row passes an out-of-range enum to setMode()" },

  { id: "M45", rule: "mode-cycle", file: MM,
    find: '  if (strcmp(cmdBuf, "MODEPREV") == 0) {\n    cycleMode(-1);',
    repl: '  if (strcmp(cmdBuf, "MODEPREV") == 0) {\n    cycleMode(1);',
    why: "MODEPREV walks FORWARDS — a 'previous' button that advances is the kind of silent inversion this project has learned to fear" },

  // ---- network status data (U-95 Phase 3) ----
  // The status buffers must be static and big enough together; the scan rows
  // must carry channel/own/saved/current captured from car truth; the four IP
  // fields must all be present with the broadcast getter actually called.
  { id: "M51", rule: "status-buffer", file: WSC,
    find: "  static char buf[3584];\n  buildStatusJson(buf, sizeof(buf));\n  // U-95 Phase 1c",
    repl: "  static char buf[2000];\n  buildStatusJson(buf, sizeof(buf));\n  // U-95 Phase 1c",
    also: [{ find: "  static char buf[3584];\n  buildStatusJson(buf, sizeof(buf));\n  server.send(200, \"application/json\", buf);",
             repl: "  static char buf[2000];\n  buildStatusJson(buf, sizeof(buf));\n  server.send(200, \"application/json\", buf);" }],
    why: "both copies shrink TOGETHER — equal sizes pass the drift check, but below what the fragments need, so snprintf silently truncates into malformed JSON" },

  { id: "M46", rule: "status-buffer", file: WSC,
    find: "  static char buf[3584];\n  buildStatusJson(buf, sizeof(buf));\n  server.send",
    repl: "  char buf[3584];\n  buildStatusJson(buf, sizeof(buf));\n  server.send",
    why: "the HTTP copy goes back on the stack — a 3.5 KB frame inside the 8 KB loopTask, nested under the send path, is the F-60 shape again" },

  { id: "M47", rule: "status-net-fields", file: WSC,
    find: "      entry.channel = static_cast<uint8_t>(WiFi.channel(i));",
    repl: "      entry.channel = 0;",
    why: "the channel is never read from the radio — every row claims channel 0, a column only the hardware can answer" },

  { id: "M48", rule: "status-net-fields", file: WSC,
    find: "      if (strcasecmp(rowSsid, savedRouterSsid_[s]) == 0) saved = true;",
    repl: "      if (false) saved = true;",
    why: "the saved-flag loop never consults the saved-router list — `saved` is guessed, not car truth, and three app modules already disagree about that rule" },

  { id: "M49", rule: "status-net-fields", file: WSC,
    find: "    formatIp(bcastIp, sizeof(bcastIp), WiFi.softAPBroadcastIP());\n",
    repl: "",
    why: "the broadcast getter is never called — the directed-broadcast address (the whole point of Phase 3's IP work) stays empty and the 'find the car on this network' path has nothing to use" },

  { id: "M50", rule: "status-net-fields", file: WSC,
    find: '       "\\"station_ip\\":\\"%s\\",',
    repl: '       ""',
    why: "station_ip is dropped from the format — the single legacy `ip` slot cannot say which interface the lease belongs to" },

  { id: "M55", rule: "status-net-fields", file: WSC,
    find: '                 "\\"channel\\":%u,\\"own\\":%s,\\"saved\\":%s,\\"current\\":%s}"',
    repl: '                 "\\"own\\":%s,\\"saved\\":%s,\\"current\\":%s}"',
    why: "the channel key is dropped from the row format — the column exists in the struct but never reaches a client" },

  { id: "M52", rule: "status-net-fields", file: WSC,
    find: '                 "\\"channel\\":%u,\\"own\\":%s,\\"saved\\":%s,\\"current\\":%s}"',
    repl: '                 "\\"channel\\":%u,\\"saved\\":%s,\\"current\\":%s}"',
    why: "the own key is dropped — the page cannot label the car's own network from car truth (the owner's first-class-row ruling)" },

  { id: "M54", rule: "status-net-fields", file: WSC,
    find: '                 "\\"channel\\":%u,\\"own\\":%s,\\"saved\\":%s,\\"current\\":%s}"',
    repl: '                 "\\"channel\\":%u,\\"own\\":%s,\\"current\\":%s}"',
    why: "the saved key is dropped — the table's saved column would be guessed client-side from three disagreeing modules" },

  { id: "M53", rule: "status-net-fields", file: WSC,
    find: '                 "\\"channel\\":%u,\\"own\\":%s,\\"saved\\":%s,\\"current\\":%s}"',
    repl: '                 "\\"channel\\":%u,\\"own\\":%s,\\"saved\\":%s}"',
    why: "the current key is dropped — 'the car is on THIS row' becomes a client guess against a dropdown that historically preselects unconfirmed networks" },

  // ---- the car's own page (U-95 Phase 3; re-aimed at the router dropdown) ----
  // Rule names must contain "page" so runGate routes WebPage.h mutants to the
  // PAGE harness, and must match the `FAIL <rule>` lines stage 10 prints.
  { id: "M56", rule: "page-router-dropdown", file: "WebPage.h",
    find: "sendCmd('ROUTERS;USE;' + name);", repl: "sendCmd('REQ_NETW');",
    why: "selecting a saved router no longer sends ROUTERS;USE - the dropdown lists networks but cannot switch to one" },

  { id: "M57", rule: "page-router-dropdown", file: "WebPage.h",
    find: "sendCmd('WIFICFG;'+ssid+';'+pass);", repl: "sendCmd('ROUTERS;ADD;'+ssid);",
    why: "the add option stops saving the pair with WIFICFG - the router is never stored, so the car refuses every later USE with Not saved" },

  { id: "M58", rule: "page-router-dropdown", file: "WebPage.h",
    find: "sendCmd('ROUTERS;DEL;'+ssid)", repl: "sendCmd('REQ_NETW');",
    why: "the delete option no longer sends ROUTERS;DEL - a saved router can never be removed from the car" },

  { id: "M59", rule: "page-router-dropdown", file: "WebPage.h",
    find: "if(sel.value === '__add__'){",
    repl: "if(false){",
    why: "the Add option is never taken - a new router is never saved (WIFICFG unreachable from the dropdown)" },

  { id: "M62", rule: "page-router-dropdown", file: "WebPage.h",
    find: "if(sel.value === '__delete__'){", repl: "if(false){",
    why: "the Delete option is never taken - a saved router cannot be removed from the dropdown" },

  { id: "M60", rule: "page-reconnect", file: "WebPage.h",
    find: "wsDelay = (wsDelay === 0) ? 1000 : Math.min(wsDelay * 2, 15000);",
    repl: "wsDelay = 3000;",
    why: "the reconnect reverts to a fixed delay — no backoff, no cap: the old unbounded 3 s hammer, exactly what Phase 3 replaced" },

  { id: "M61", rule: "page-reconnect", file: "WebPage.h",
    find: "        wsDelay = 0;\n        initWebSocket();\n      });",
    repl: "      });",
    why: "the visibilitychange handler is registered but does nothing — returning to a hidden tab leaves the page 'Disconnected' until the next timer fires" },

  { id: "M68", rule: "extra-status-decoupled", file: WSC,
    find: "  if (now - lastStatusMs >= STATUS_INTERVAL) {",
    repl: "  if (now - lastExtraMs >= EXTRA_INTERVAL) {",
    why: "the status broadcast is wired back onto the extra row's tick — the two cadences are one again, so tuning the OLED also retunes every client's JSON (U-98 Phase 2 re-coupled)" },

  { id: "M69", rule: "extra-status-decoupled", file: WSC,
    find: "const unsigned long EXTRA_INTERVAL = 500;",
    repl: "const unsigned long EXTRA_INTERVAL = 2000;",
    why: "the extra row refreshes SLOWER than the 1 s broadcast — the ticks are split but the OLED is now the staler of the two, the same defect the other way around" },

  // ---- U-112: the heap floor + the page stall-abort (the owner's post-1.3.9
  // report: reset while connecting BT + own hotspot; freeze while using the
  // car's page with the app connected; field heap 13-18 kB, boots 1026-1029).
  { id: "M70", rule: "heap-guard", file: WSC,
    find: "  if (ESP.getFreeHeap() < SCAN_HEAP_FLOOR_BYTES) {",
    repl: "  if (false) {",
    why: "startScan() no longer refuses below the scan floor — a scan is kicked against a heap that cannot allocate it and lands 8 s later on a silently EMPTY router list, which the owner reads as the list being broken" },

  { id: "M71", rule: "heap-guard", file: WSC,
    find: "    if (millis() - statusDeferSinceMs_ < STATUS_HEAP_DEFER_MAX_MS) {",
    repl: "    if (true) {",
    why: "the below-floor defer loses its time box — at a chronically low heap every broadcast is deferred forever and telemetry black-holes, a worse failure than the one the floor prevents" },

  { id: "M72", rule: "heap-guard", file: "WebServerComm.h",
    find: "#define STATUS_HEAP_DEFER_MAX_MS 4000ul",
    repl: "#define STATUS_HEAP_DEFER_MAX_MS 4000000ul",
    why: "the defer window becomes ~66 minutes — the time box still 'exists' but no longer bounds anything (a window must be <= 10 s to be a window)" },

  { id: "M73", rule: "heap-guard", file: WSC,
    find: "      if (stallRetries >= HTTP_STALL_ABORT_AFTER) {",
    repl: "      if (stallRetries >= 100000) {",
    why: "the page-send loop's stall abort is disconnected from its own constant — an undrained reader costs the full 3500 ms budget every pass again, so no ws.loop()/BT poll runs while a page loads (the owner's freeze)" },

  { id: "M74", rule: "heap-guard", file: "WebServerComm.h",
    find: "#define SCAN_HEAP_FLOOR_BYTES 65536ul",
    repl: "#define SCAN_HEAP_FLOOR_BYTES 32768ul",
    why: "the scan floor drops BELOW the status floor — the biggest transient in the system gets the smaller cushion, inverting the ordering the guard exists for" },

  { id: "M75", rule: "heap-guard", file: WSC,
    find: "    stallRetries = 0;  // progress: a moving reader keeps the full budget",
    repl: "    // stall counter no longer reset on progress",
    why: "the stall counter never resets when bytes flow — one slow moment would end EVERY later page pass, so slow-but-alive readers can no longer finish a page" },

  // U-114: the telemetry-parameter surface. Five mutants, one per direction the
  // `tele-params` rule polices. Before this rule the tree carried all of the
  // first two at once and the gate was green.
  { id: "M76", rule: "tele-params", file: "Config.h",
    find: '#define CONFIG_TELEMETRY_PARAMETERS   "tele.parameters,frequency,1600,vysexcvr,0"',
    repl: "#define CONFIG_TELEMETRY_PARAMETERS   tele.parameters,frequency,1600,vysexcvr,0",
    why: "the surface goes back to a bare token list — `const char* x = tele.parameters, …` is an undeclared identifier, i.e. the sketch does not build (this is the exact HEAD-broken state 79e6d26 shipped)" },

  { id: "M77", rule: "tele-params", file: WSC,
    find: '"\\"tele_params\\":\\"freq=%s, vyse=%s\\",',
    repl: '"\\"tele_prm\\":\\"freq=%s, vyse=%s\\",',
    why: "the status key is renamed — the surface is still read, still parsed, and the client never sees it (an unreferenced value is the same as no value)" },

  { id: "M78", rule: "tele-params", file: WSC,
    find: "      freqBuf,\n      vyseBuf,\n      wifiEnabled_ ? \"true\" : \"false\",",
    repl: "      wifiEnabled_ ? \"true\" : \"false\",\n      freqBuf,\n      vyseBuf,",
    why: "the two surface buffers move off the tele_params slot — snprintf feeds each %s from the NEXT argument, so wifi_enabled/bt_enabled/boot_autojoin report their neighbours' values and the app ADOPTS them (U-111)" },

  { id: "M79", rule: "tele-params", file: WSC,
    find: "  teleSurfaceField(CONFIG_TELEMETRY_PARAMETERS, TELE_FREQ_FIELD, freqBuf,\n                   sizeof(freqBuf));\n  teleSurfaceField(CONFIG_TELEMETRY_PARAMETERS, TELE_VYSE_FIELD, vyseBuf,\n                   sizeof(vyseBuf));",
    repl: "  const char* teleP = CONFIG_TELEMETRY_PARAMETERS;\n  strcpy(freqBuf, strchr(teleP, ',') + 1);\n  strcpy(vyseBuf, strchr(teleP, ',') + 1);",
    why: "the surface is parsed by hand again with `strchr(...) + 1`, which adds 1 to a NULL result the moment a field is missing — undefined behaviour, and it drops the last-field-as-remainder rule too" },

  { id: "M80", rule: "tele-params", file: WSC,
    find: "    p = strchr(p, ',');\n    if (!p) return;          // fewer fields than asked for -> leave \"\"\n    ++p;",
    repl: "    p = strchr(p, ',');\n    ++p;",
    why: "the helper's missing-field bail-out is deleted — a surface with fewer fields than asked for walks `p` past its own NUL terminator" },

  // P4 (issue 6, 2026-10-15) · the E1 heap waterfall. One mutant per stage the
  // `heap-waterfall` rule pins, plus the two halves of the helper's wire
  // format. A hole in the waterfall is exactly how a "we have heap data" claim
  // goes stale: the capture still looks complete and is missing the stage that
  // matters.
  { id: "M81", rule: "heap-waterfall", file: INO,
    find: "heapWaterfall(HEAPW_STAGE_BOOT);",
    repl: "// boot breadcrumb removed",
    why: "the boot sample disappears - the waterfall starts mid-story, so what the BT stack then eats cannot be totalled against a baseline" },

  { id: "M82", rule: "heap-waterfall", file: "BluetoothComm.cpp",
    find: "if (started_) heapWaterfall(HEAPW_STAGE_BT);",
    repl: "// bt-stack breadcrumb removed",
    why: "the single biggest drop in the waterfall is no longer emitted - the BT controller's cost is invisible and P4 has nothing to fix against" },

  { id: "M83", rule: "heap-waterfall", file: INO,
    find: "if (!btEverConnected) heapWaterfall(HEAPW_STAGE_SPP);",
    repl: "// spp breadcrumb removed",
    why: "the SPP-connect stage goes dark, so a pairing-time drop cannot be told apart from the controller's own cost" },

  { id: "M84", rule: "heap-waterfall", file: WSC,
    find: "heapWaterfall(HEAPW_STAGE_AP);",
    repl: "// wifi-ap breadcrumb removed",
    why: "the softAP cost is no longer sampled - exactly the stage that stacks on BT to cross the owner's <14 KB reset line" },

  { id: "M85", rule: "heap-waterfall", file: WSC,
    find: "heapWaterfall(HEAPW_STAGE_PAGE);",
    repl: "// page-served breadcrumb removed",
    why: "the page-serve stage goes dark, so the freeze-while-page-open path loses its heap figure" },

  { id: "M86", rule: "heap-waterfall", file: INO,
    find: "heapWaterfall(HEAPW_STAGE_BOTH);",
    repl: "// both-links breadcrumb removed",
    why: "the both-transports-up sample is gone - the one state issue 6 is actually about is the one with no number" },

  { id: "M87", rule: "heap-waterfall", file: "Config.h",
    find: "\"[HEAPW] %s free=%u max=%u\\n\"",
    repl: "\"[HEAPW] %s free=%u\\n\"",
    why: "the wire format drops max= - free heap alone cannot show BT-restart fragmentation (the largest contiguous block), which is the whole point of suspect (c)" },

  { id: "M88", rule: "heap-waterfall", file: "Config.h",
    find: "static_cast<unsigned>(ESP.getMaxAllocHeap())",
    repl: "static_cast<unsigned>(ESP.getFreeHeap())",
    why: "max= is printed from the FREE value - the format still claims max, the number is meaningless, and getMaxAllocHeap() is gone from the tree entirely" },

  // P4 (issue 6, 2026-10-15) · the diet. Two ways the radio can silently go
  // back to BTDM: drop the third argument (the library default), or pass the
  // wrong boolean. Either re-allocates the BLE controller+host memory this
  // SPP-only car never uses, i.e. the ~100 KB collapse returns while the
  // waterfall still looks complete.
  { id: "M89", rule: "bt-heap", file: "BluetoothComm.cpp",
    find: "SerialBT.begin(BT_SPP_NAME, false, true)",
    repl: "SerialBT.begin(BT_SPP_NAME)",
    why: "the classic-only argument is gone - the radio is back to the BTDM default (disableBLE=false), so the BLE controller+host memory is allocated and never used (the issue-6 collapse)" },

  { id: "M90", rule: "bt-heap", file: "BluetoothComm.cpp",
    find: "SerialBT.begin(BT_SPP_NAME, false, true)",
    repl: "SerialBT.begin(BT_SPP_NAME, false, false)",
    why: "disableBLE is explicitly FALSE - the argument COUNT is right and the value is wrong, which is the shape a partial revert or a copy-paste takes" },

  // P5 (U-113 issues 7 + 9, 2026-10-15) · the Wi-Fi transport switch. Issue 7's
  // cause was that NO bring-up path consulted the switch, so Wi-Fi OFF left the
  // own beacon broadcasting. Three independent ways back to that state, one per
  // assert in `wifi-off-hides-ap` - plus the two `page-always-on` regressions
  // (a client gate on the page, and a parked beacon while Wi-Fi is on).
  { id: "M91", rule: "wifi-off-hides-ap", file: WSC,
    find: "WiFi.softAPdisconnect(true);",
    repl: "if (apRunning_) WiFi.softAPdisconnect(true);",
    why: "the teardown is gated on the believed flag again - stop() skips softAPdisconnect() whenever apRunning_ is stale, which is exactly the AP_STA<->AP bounce the flag is force-written around (issue 7's 'the hotspot is still there')" },

  { id: "M92", rule: "wifi-off-hides-ap", file: WSC,
    find: "if (wifiEnabled_ && !apRunning_ && apNow - lastApRetryMs_ >= AP_RETRY_INTERVAL_MS) {",
    repl: "if (!apRunning_ && apNow - lastApRetryMs_ >= AP_RETRY_INTERVAL_MS) {",
    why: "handle()'s AP retry no longer checks the transport switch - stop() clears apRunning_, so the very next pass re-creates the beacon ~2 s after Wi-Fi OFF and nothing ever takes it down again" },

  { id: "M93", rule: "wifi-off-hides-ap", file: WSC,
    find: "if (!wifiEnabled_) return;  // U-113 (#7): no beacon while the transport is off",
    repl: "// U-113 (#7) guard removed from startOwnAP()",
    why: "startOwnAP() loses its switch guard - the boot bring-up, the rejoin and the give-up machines can each raise a beacon the owner switched off (the invariant stated at the wrong end only)" },

  { id: "M94", rule: "page-always-on", file: WSC,
    find: "  serveRootBounded();",
    repl: "  if (hasWsClients()) serveRootBounded();",
    why: "the root page is gated on a connected client again - a phone that browses the car's own AP before the app attaches gets no page (issue 9: 'never wait for the app/remote to connect first')" },

  { id: "M95", rule: "page-always-on", file: WSC,
    find: "if (!wifiEnabled_) return;  // Wi-Fi transport deliberately off (U-105)",
    repl: "if (!wifiEnabled_) { WiFi.softAPdisconnect(true); return; }  // Wi-Fi transport deliberately off (U-105)",
    why: "updateApPowerPolicy() parks the beacon - the own hotspot is torn down on a policy pass, which is the U-87 'car dropped the link it was being driven over' shape and the issue-9 regression" },

  // P7/U-115: boot auto-join scans first. Five mutants, one per direction the
  // `boot-scan-first` rule polices.
  { id: "M96", rule: "boot-scan-first", file: WSC,
    find: "  if (startBootReachabilityScan()) {",
    repl: "  if (false) {",
    why: "the boot path no longer probes the air — armBootAutoJoin() falls straight through to the blind rejoin, so a stored-but-absent router holds the own hotspot on a moving channel for the whole budget again (the owner's 'detected but won't connect')" },

  { id: "M97", rule: "boot-scan-first", file: WSC,
    find: "  if (inRange || !conclusive) {",
    repl: "  if (inRange) {",
    why: "the fallback is dropped — an INCONCLUSIVE scan (radio never answered) is treated as ABSENT, so a probe that could not run strands the car without its router" },

  { id: "M98", rule: "boot-scan-first", file: WSC,
    find: "  serviceBootReachabilityScan();",
    repl: "  // (the boot probe is no longer dragged from handle())",
    why: "handle() stops dragging the probe — it arms and never runs, so the boot auto-join silently never happens and the car never returns to its router" },

  { id: "M99", rule: "boot-scan-first", file: WSC,
    find: "  if (ESP.getFreeHeap() < BOOT_SCAN_HEAP_FLOOR_BYTES) return false;",
    repl: "  // boot heap floor check removed",
    why: "startBootReachabilityScan() loses its heap floor — the scan can be issued into the starved boot heap and land silently empty, which reads as 'router absent'" },

  { id: "M100", rule: "boot-scan-first", file: WSC,
    find: "  bootScanArmed_ = false;\n  bootScanKicked_ = false;\n  // U-95 Phase 2: a fresh request gets a fresh drop attempt budget, or the",
    repl: "  // U-95 Phase 2: a fresh request gets a fresh drop attempt budget, or the",
    why: "rejoinNetwork() no longer clears an armed boot probe — a stale probe fires a SECOND rejoin after the owner picked a network by hand" },

  // P6 / issue 1 (2026-10-16): both transports stay up. Three mutants, one per
  // direction the `dual-link-kept` rule polices — a stop inside the
  // both-enabled branch, and each of the two raises it exists to make.
  { id: "M101", rule: "dual-link-kept", file: INO,
    find: '      webServerComm.beginAlwaysOn();\n      webServerComm.armBootAutoJoin();  // own hotspot stays up\n      Serial.println("[POWER] Wi-Fi up (both transports on)");',
    repl: '      webServerComm.beginAlwaysOn();\n      webServerComm.armBootAutoJoin();  // own hotspot stays up\n      btComm.stop();\n      Serial.println("[POWER] Wi-Fi up (both transports on)");',
    why: "the both-enabled branch now stops the OTHER transport - bringing Wi-Fi up tears Bluetooth down, so the second link can never exist (the retired one-link teardown, back in the branch that owns it)" },

  { id: "M102", rule: "dual-link-kept", file: INO,
    find: '      btUp = ensureBtAdvertising();\n      if (btUp) Serial.println("[POWER] Bluetooth advertising (both transports on)");',
    repl: '      // (the Bluetooth raise is removed from the both-enabled branch)',
    why: "the both-enabled branch never raises Bluetooth - after a reboot where BT came up down, Wi-Fi is up and the second link can never appear, so a one-link car is permanent" },

  { id: "M103", rule: "dual-link-kept", file: INO,
    find: '      webServerComm.beginAlwaysOn();\n      webServerComm.armBootAutoJoin();  // own hotspot stays up\n      Serial.println("[POWER] Wi-Fi up (both transports on)");',
    repl: '      // (the web-layer raise is removed from the both-enabled branch)',
    why: "the both-enabled branch never raises the web layer - Bluetooth up with the hotspot and page down means the second link is unreachable even though the toggle says ON" },

  // P7 / issue 8 (2026-10-16): every setting stores in the car and comes back.
  // Six mutants, one per claim the `settings-round-trip` rule polices: the
  // three NVS writes (a toggle that is not stored dies at the reboot), the
  // STATE keys (a Bluetooth-only client can never re-sync), the /status JSON
  // fields (same, web transport), and the last-transport-off clamp (the owner
  // ruled both-off impossible; without it a reply can promise a dead car).
  { id: "M104", rule: "settings-round-trip", file: WSC,
    find: '        wifiEnabled_ = next;\n        Preferences prefs;\n        prefs.begin("botcfg", false);\n        prefs.putBool("t_wifi", wifiEnabled_);\n        stampConfigWrite(prefs);\n        prefs.end();',
    repl: '        wifiEnabled_ = next;\n        Preferences prefs;\n        prefs.begin("botcfg", false);\n        stampConfigWrite(prefs);\n        prefs.end();',
    why: "WIFICFG;WIFI;ON|OFF flips the flag in RAM but never writes NVS t_wifi - the Wi-Fi transport setting dies at the next reboot and the app re-adopts the OLD value on reconnect (issue 8)" },

  { id: "M105", rule: "settings-round-trip", file: WSC,
    find: '        btEnabled_ = next;\n        Preferences prefs;\n        prefs.begin("botcfg", false);\n        prefs.putBool("t_bt", btEnabled_);\n        stampConfigWrite(prefs);\n        prefs.end();',
    repl: '        btEnabled_ = next;\n        Preferences prefs;\n        prefs.begin("botcfg", false);\n        stampConfigWrite(prefs);\n        prefs.end();',
    why: "WIFICFG;BT;ON|OFF flips the flag in RAM but never writes NVS t_bt - the Bluetooth transport setting dies at the next reboot (issue 8)" },

  { id: "M106", rule: "settings-round-trip", file: WSC,
    find: '      bootAutojoin_ = (strcasecmp(arg, ";ON") == 0);\n      Preferences prefs;\n      prefs.begin("botcfg", false);\n      prefs.putBool("boot_autojoin", bootAutojoin_);\n      stampConfigWrite(prefs);\n      prefs.end();',
    repl: '      bootAutojoin_ = (strcasecmp(arg, ";ON") == 0);\n      Preferences prefs;\n      prefs.begin("botcfg", false);\n      stampConfigWrite(prefs);\n      prefs.end();',
    why: "WIFICFG;AUTOJOIN;ON|OFF flips the flag in RAM but never writes NVS boot_autojoin - the owner's boot auto-join choice dies at the next reboot (issue 8)" },

  { id: "M107", rule: "settings-round-trip", file: MM,
    find: '        snprintf(tKeys + strlen(tKeys), sizeof(tKeys) - strlen(tKeys), ";WIFI=%s",\n                 ws->wifiEnabled() ? "ON" : "OFF");\n        snprintf(tKeys + strlen(tKeys), sizeof(tKeys) - strlen(tKeys), ";BT=%s",\n                 ws->btEnabled() ? "ON" : "OFF");\n        snprintf(tKeys + strlen(tKeys), sizeof(tKeys) - strlen(tKeys), ";AUTOJOIN=%s",\n                 ws->bootAutojoin() ? "ON" : "OFF");',
    repl: '        // (the transport-toggle keys are removed from the STATE line)',
    why: "the STATE line no longer carries ;WIFI= ;BT= ;AUTOJOIN= - a Bluetooth-only app can never re-sync the toggles, so a panel change made from another phone survives forever in one app's memory (issue 8)" },

  { id: "M108", rule: "settings-round-trip", file: WSC,
    find: '       "\\"wifi_enabled\\":%s,"\n       "\\"bt_enabled\\":%s,"\n       "\\"boot_autojoin\\":%s,',
    repl: '       "\\"scan_disabled_note\\":\\"toggles moved to STATE only\\",',
    why: "the /status JSON no longer carries wifi_enabled/bt_enabled/boot_autojoin - the web transport cannot re-sync the toggles, so an app on Wi-Fi holds a stale panel (issue 8)" },

  { id: "M109", rule: "settings-round-trip", file: WSC,
    find: '      if (!next && !btEnabled_) {',
    repl: '      if (false) {',
    why: "the Wi-Fi toggle no longer refuses to turn the LAST transport off - both transports can be commanded off in two taps, which the owner ruled impossible ('both cant be off at the same time')" },

  // P4 / item F (2026-10-17): the car refuses a SECOND link, honestly. Six
  // mutants, one per claim the `link-admission` rule polices: the heap floor
  // exists and is above the status floor, admitSecondLink() actually compares
  // and LATCHES, BOTH raises in the both-enabled branch are gated, a refused
  // Wi-Fi raise reverts the optimistic mark (or the loop never retries), and the
  // verdict is on BOTH surfaces (STATE line + /status JSON).
  { id: "M110", rule: "link-admission", file: WSC,
    find: '  if (freeHeap < LINK_ADMISSION_FLOOR_BYTES) {',
    repl: '  if (false) {',
    why: "admitSecondLink() no longer compares the free heap to the floor - it always admits, so the second transport comes up into a starved heap again and the run-time starves with it (item 6)" },

  { id: "M111", rule: "link-admission", file: WSC,
    find: '      linkCapRefused_ = true;',
    repl: '      // (the refusal is not latched)',
    why: "the refusal is not LATCHED - the app can no longer tell a refusal from a link still coming up, so it cannot honestly say multi-connect is unavailable" },

  { id: "M112", rule: "link-admission", file: INO,
    find: '    if (shouldStartWeb && btUp) {',
    repl: '    if (shouldStartWeb) {',
    why: "the Wi-Fi raise is no longer gated on admission - with Bluetooth up, Wi-Fi comes up unconditionally and the second-link freeze is back" },

  { id: "M113", rule: "link-admission", file: MM,
    find: '        snprintf(tKeys + strlen(tKeys), sizeof(tKeys) - strlen(tKeys), ";LINKCAP=%d",\n                 ws->linkCapCode());',
    repl: '        // (the link-admission verdict is removed from the STATE line)',
    why: "the STATE line no longer carries ;LINKCAP= - a Bluetooth-only app can never learn the car refused the second link" },

  { id: "M114", rule: "link-admission", file: WSC,
    find: '       "\\"link_cap\\":%d,"\n       "\\"page_on\\":%d,"\n       "\\"event_log_count\\":%lu"',
    repl: '       "\\"page_on\\":%d,"\n       "\\"event_log_count\\":%lu"',
    why: "the /status JSON no longer carries link_cap - the web transport can never learn the car refused the second link" },

  { id: "M115", rule: "link-admission", file: WSH,
    find: '#define LINK_ADMISSION_FLOOR_BYTES 32000ul',
    repl: '#define LINK_ADMISSION_FLOOR_BYTES 2000ul',
    why: "the link-admission floor drops BELOW the status floor - a second link (a standing cost) is admitted on less headroom than a single status broadcast needs, so the refusal protects nothing" },

  { id: "M116", rule: "link-admission", file: WSC,
    find: '  if (!(wifiEnabled_ && btEnabled_)) return 1;',
    repl: '  if (false) return 1;',
    why: "linkCapCode() no longer folds the transport toggles - with one transport off it reports a refusal for a second link that was never going to exist, a verdict about nothing" },

  // P6 / item D (2026-10-17): the bounded event log the app can hand over.
  { id: "M117", rule: "event-log", file: CL,
    find: '    s_evCount = CAR_EVENT_CAP - 1;',
    repl: '    // (the cap break removed - the ring overflows)',
    why: "crashLogEvent() no longer keeps the newest CAR_EVENT_CAP facts - the ring overflows past the hard cap and the log becomes a flash-exhaustion bug (F-89)" },

  { id: "M118", rule: "event-log", file: CL,
    find: '  crashLogEvent(CAR_EV_BOOT, 0, nullptr);',
    repl: '  // (the boot event is not recorded)',
    why: "crashLogBegin() stops recording the BOOT event - a clean boot is indistinguishable from a faulted one, and the reset report loses its timeline anchor" },

  { id: "M119", rule: "event-log", file: CL,
    find: '  if (isFaultReset(reason)) {\n    crashLogEvent(CAR_EV_FAULT, (uint8_t)reason,\n                  s_info.phase[0] ? s_info.phase : nullptr);',
    repl: '  if (false) {',
    why: "fault boot extremes: the FAULT event is never recorded - the ring misses the one fact the owner is acting on (why did it reset)" },

  { id: "M120", rule: "event-log", file: WSC,
    find: '    lastWifiReply_ = "LOG;CLEARED";',
    repl: '    lastWifiReply_ = "LOG;ERROR;Syntax";',
    why: "LOG;CLEAR no longer confirms the clear - the app cannot tell a cleared log from a refused one, so restart the car and the old events come back" },

  { id: "M121", rule: "event-log", file: WSC,
    find: '      static_cast<unsigned long>(carLogCount())\n  );',
    repl: '      0ul\n  );',
    why: "/status stops reporting event_log_count - the web transport cannot learn there is a log to fetch, so the per-car viewer reads a lie (empty) forever" },

  { id: "M122", rule: "event-log", file: INO,
    find: '    const bool isLogCmd = webServerComm.handleLogCommand(cmd);',
    repl: '    const bool isLogCmd = false; (void)0;',
    why: "the verbs stop riding the system hook - LOG?/LOG;GET/LOG;CLEAR fall through to the drive path and are answered by nothing, while the contract still claims the app can read them" },

  // U-117/U-118 (2026-10-20): the page-server state must be PUBLISHED on the
  // Bluetooth surface, READ BACK across a power cycle, and HONOURED by the
  // serve path — the three claims behind "the webpage toggle off button is
  // not working".
  { id: "M123", rule: "page-state", file: MM,
    find: '        snprintf(tKeys + strlen(tKeys), sizeof(tKeys) - strlen(tKeys), ";PAGE=%s",\n                 ws->pageServerIsOn() ? "ON" : "OFF");',
    repl: '        // (the page-server state is removed from the STATE line)',
    why: "the STATE line no longer carries ;PAGE= - a Bluetooth-only app can never read the page-server state, so its switch shows an assumed off while the car keeps serving" },

  { id: "M124", rule: "page-state", file: WSC,
    find: '  pageOff_ = prefs.getBool("page_off", false);',
    repl: '  pageOff_ = false;',
    why: "the boot load stops reading NVS page_off - a turned-OFF page silently turns itself back ON at every power cycle, the exact 'toggle not working' report" },

  { id: "M125", rule: "page-state", file: WSC,
    find: '  if (pageOff_ || pageServerMustBeHeld()) {',
    repl: '  if (pageServerMustBeHeld()) {',
    why: "the serve path no longer gates on pageOff_ - WIFICFG;WIFI;PAGE;OFF keeps answering while every HTTP GET still gets the full control page" },

  { id: "M126", rule: "page-state", file: WSC,
    find: '       "\\"page_on\\":%d,"\n       "\\"event_log_count\\":%lu"',
    repl: '       "\\"event_log_count\\":%lu"',
    why: "the /status JSON no longer carries page_on - the web transport cannot read the page-server state, so an app on Wi-Fi holds a stale panel" },
];

// ---- runner -------------------------------------------------------------

if (listOnly) {
  for (const m of mutants) console.log(`${m.id.padEnd(4)} ${m.rule.padEnd(18)} ${m.file.padEnd(22)} ${m.why}`);
  console.log(`\n${mutants.length} mutants.`);
  process.exit(0);
}

const dirty = gitDirty();
if (dirty) {
  console.error("REFUSING TO RUN: the working tree is not clean.\n");
  console.error(dirty);
  console.error("\nThis harness edits source files. Commit or stash first — it restores from its");
  console.error("own snapshot, not from git, so it will not discard your work by accident.");
  process.exit(2);
}

// Snapshot EVERY file a mutant can touch, including the `also` edits - which may
// target a DIFFERENT file than the mutant's primary `file` (M18 arms `Config.h`,
// not RelayComm.cpp). Snapshotting only `m.file` left Config.h mutated after the
// run: the byte-exact check below only walked the snapshot, so it reported
// success while an armed RELAY_HOST sat in the working tree - a donor car that
// now dials. The harness must restore exactly what it can dirty.
const files = [...new Set(mutants.flatMap((m) => [m.file, ...(m.also ?? []).map((e) => e.file ?? m.file)]))];
takeSnapshot(files);

const runGate = (m) => {
  if (m.file === "WebPage.h" && /page/.test(m.rule)) return gatePage();
  if (m.file.endsWith(".cpp") || m.file.endsWith(".h")) return gateContract();
  return gateContract();
};

const classify = (m, g) => {
  if (m.rule === "page-harness") {
    return g.code !== 0 ? "caught" : "undetected";
  }
  if (m.compile) return "compile";
  const pat = new RegExp(`FAIL\\s+${m.rule.replace(/[.*+?^${}()|[\]\\]/g, "\\$&")}\\b`);
  if (pat.test(g.out)) return "caught";
  if (g.code !== 0) return "wrong";
  return "undetected";
};

const selected = filter ? mutants.filter((m) => new RegExp(filter, "i").test(`${m.id} ${m.rule} ${m.why}`)) : mutants;
if (filter && selected.length === 0) {
  console.error(`no mutant matches -k ${filter}`);
  process.exit(2);
}

console.log(`baseline: ${mutants.length} mutants defined, ${selected.length} selected\n`);
const baseline = gateContract();
if (baseline.code !== 0) {
  console.error("BASELINE IS RED — fix the tree before mutating it. Gate said:\n");
  console.error(baseline.out);
  process.exit(2);
}
const baselinePage = gatePage();
if (baselinePage.code !== 0) {
  console.error("BASELINE PAGE HARNESS IS RED — fix before mutating:\n");
  console.error(baselinePage.out);
  process.exit(2);
}
console.log("baseline green (contract + page)\n");

for (const m of selected) {
  let verdict = "undetected";
  let detail = "";
  try {
    mutate(m.file, m.find, m.repl);
    for (const extra of m.also ?? []) mutate(extra.file ?? m.file, extra.find, extra.repl);

    const g = runGate(m);
    verdict = classify(m, g);
    if (verdict === "wrong") {
      const which = g.out.split(/\r?\n/).filter((l) => /FAIL\s+/.test(l)).join(" | ").trim();
      detail = `expected ${m.rule}, got: ${which}`;
    } else if (verdict === "caught") {
      detail = `by ${m.rule}`;
    }
  } catch (e) {
    detail = `HARNESS ERROR (anchor did not match): ${e.message}`;
    verdict = "harness-error";
  } finally {
    restoreAll();
  }

  if (verdict === "caught") { caught++; console.log(`caught      ${m.id}  ${detail.padEnd(28)} ${m.why}`); }
  else if (verdict === "wrong") { wrongReason++; console.log(`caught-WRONG ${m.id}  ${detail}\n             ${m.why}`); }
  else { failures++; console.log(`UNDETECTED  ${m.id}  ${detail}\n             ${m.why}`); }
}

// ---- restore verification ----------------------------------------------

console.log("");
const drifted = [];
for (const [f, bytes] of snapshot) {
  const now = readBytes(f);
  if (Buffer.compare(bytes, now) !== 0) drifted.push(f);
}
if (drifted.length) {
  console.error(`RESTORE WAS NOT BYTE-EXACT: ${drifted.join(", ")}`);
  console.error("A mangled source file can still pass its gate. Treat this as a failure.");
  process.exit(1);
}
console.log("restore verified byte-exact against the pre-mutation snapshot");

if (failures || wrongReason) {
  console.error(`\nMUTATION HARNESS FAILED: ${failures} undetected, ${wrongReason} caught by the wrong rule`);
  process.exit(1);
}
console.log(`MUTATION HARNESS PASS: all ${caught} selected mutants caught by their intended rule`);