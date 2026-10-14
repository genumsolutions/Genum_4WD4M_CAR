// U-111: stripped of the U-94 Phase 2 relay rules. The remaining rules
// are the restart-record gate (F-79), the watchdog & budget gates (F-60/F-77)
// and the WebSocket pump / broadcast gates (F-75/F-76). All other sections
// were merged into mutate-gates.mjs or removed entirely.

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

console.log(`\n${failures} gate failures total`);
process.exit(failures > 0 ? 1 : 0);