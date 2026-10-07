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
    find: "  char buf[1536];\n  buildStatusJson(buf, sizeof(buf));\n  // U-95 Phase 1c: the write gets its OWN fresh window.",
    repl: "  char buf[1200];\n  buildStatusJson(buf, sizeof(buf));\n  // U-95 Phase 1c: the write gets its OWN fresh window.",
    why: "the two status-JSON buffers drift apart, so the two copies disagree about which got truncated" },

  // Anchored on `server.send(200, "application/json", buf)`, which is the HTTP
  // copy of the status JSON. An earlier version of this anchor guessed
  // `server.sendContent(buf)` and matched nothing -- the harness reported it as
  // an undetected mutant, which is the correct outcome for an anchor that does
  // not describe the tree, and the reason anchors are never allowed to fail
  // quietly.
  { id: "M8", rule: "status-buffer", file: WSC,
    find: "  char buf[1536];\n  buildStatusJson(buf, sizeof(buf));\n  server.send(200, \"application/json\", buf);",
    repl: "  char buf[900];\n  buildStatusJson(buf, sizeof(buf));\n  server.send(200, \"application/json\", buf);",
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

  { id: "M18", rule: "armed-handshake", file: "Config.h",
    find: '#define RELAY_HOST ""', repl: '#define RELAY_HOST "xyzcompany.supabase.co"',
    why: "the relay is armed while the 120 s handshake is still unbounded — the interlock must refuse this" },

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