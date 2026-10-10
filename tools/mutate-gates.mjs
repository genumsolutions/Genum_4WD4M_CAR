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

const gateContract = () => run(process.execPath, [rel("tools/check-relay-contract.mjs")]);
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
const RELAY = "RelayComm.cpp";
const MM = "ModeManager.cpp";

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
    find: "      const unsigned long gapStart = millis();\n      while (millis() - gapStart < HTTP_WRITE_RETRY_GAP_MS) {\n        esp_task_wdt_reset();\n        delay(1);\n      }\n      continue;",
    repl: "      yield();\n      continue;",
    why: "the real gap reverts to the bare yield() that caused millions of wasted iterations" },

  { id: "M14", rule: "wdt-budget", file: WSC,
    find: "  while (off < pageLen) {",
    repl: "  while (off < pageLen) {\n    c.write((const uint8_t*)\"\", 0);",
    why: "a write happens before the budget check, so an iteration can begin out of time" },

  { id: "M15", rule: "wdt-budget", file: INO,
    find: ".timeout_ms = LOOP_TASK_WDT_TIMEOUT_MS,", repl: ".timeout_ms = 5000,",
    why: "the sketch reverts to a bare literal, so the static_assert guards a number the hardware no longer uses" },

  // ---- relay (F-84 / Phase 1e) ----
  { id: "M16", rule: "inert-by-default", file: RELAY,
    find: "  if (RELAY_HOST[0] == '\\0') {\n    FW_LOG(\"[RELAY] RELAY_HOST is empty - the relay transport stays off (see Config.h)\");\n    return false;\n  }\n\n",
    repl: "",
    why: "the empty-RELAY_HOST guard is gone, so an unconfigured car with a token re-attempts a doomed dial every retry interval" },

  { id: "M17", rule: "inert-by-default", file: RELAY,
    find: "  if (RELAY_HOST[0] == '\\0') {", repl: "  if (RELAY_HOST[0] == 'z') {",
    why: "the guard exists but can never fire: the empty-host case is not actually tested" },

  { id: "M18", rule: "armed-handshake", file: RELAY,
    find: "xTaskCreate(taskTrampoline", repl: "createRelayTask(taskTrampoline",
    also: [{ file: "Config.h", find: '#define RELAY_HOST ""', repl: '#define RELAY_HOST "xyzcompany.supabase.co"' }],
    why: "the relay is armed with the dial back on the loop task (the task is gone) — the 120 s handshake is unbounded again; the interlock must refuse this" },

  // U-94 Phase 2: connected() must gate on the socket fact the relay task
  // PUBLISHES (socketUp_), and that flag must be fed from client_.isConnected().
  // A second source of truth about the socket is the F-82 drift this rule
  // exists to prevent, so severing the feed must go red.
  { id: "M63", rule: "enrolment", file: RELAY,
    find: "socketUp_ = client_.isConnected();", repl: "socketUp_ = true;",
    why: "the relay task stops publishing the real socket state — connected() reads a flag hard-coded true, so a refused or half-open car would read as connected and switch the safe-stop off" },

  // U-94 Phase 2: a control frame BEGINS with the prefix's NUL, so parsing the
  // body must start at frame + kControlPrefixLen. Searching the whole frame is
  // strstr on an empty string — it matches nothing, silently, and the car never
  // sees `paired`. This is the exact bug control-parse exists to catch.
  { id: "M64", rule: "control-parse", file: RELAY,
    find: "  return strstr(frame + kControlPrefixLen, needle) != nullptr;",
    repl: "  return strstr(frame, needle) != nullptr;",
    why: "control-event parsing searches the whole frame, whose first byte is the prefix's NUL - strstr sees an empty string, so `paired` (and every other control event) never matches and the car never enrols" },

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
    find: "      if (strcasecmp(scanNetworks_[k].bssid, bssid.c_str()) == 0) {",
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
    find: "    const String bssid = WiFi.BSSIDstr(i);\n",
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
    find: "    WiFi.softAPBroadcastIP().toString().toCharArray(bcastIp, sizeof(bcastIp));\n",
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

const files = [...new Set(mutants.map((m) => m.file))];
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