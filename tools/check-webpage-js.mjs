// =====================================================================
// U-81 gate: the control PAGE's JavaScript.
//
// `WebPage.h` carries the setup page's whole UI as one big C++ raw string.
// Nothing in CI looks inside it: arduino-ci compiles the sketch, and the HTML
// and JS are just string bytes to the compiler. So a syntax error, a call to a
// function that does not exist, a write to an element that was never declared,
// or a message that contradicts the car all sail through green and reach the
// owner as a blank page.
//
// That is not hypothetical — it is the class of defect behind "the webpage is
// not functioning properly". So this script does what the compiler cannot:
//
//   1. extracts the inline <script> from the raw string and makes node PARSE it
//   2. checks the identifiers the U-81 handler calls actually exist
//   3. checks #note exists in the markup (note() writes to it)
//   4. RUNS handleTextLine against the firmware's real reply strings and
//      asserts what the user is shown — including that a REFUSAL is never
//      rendered in the success colour, and that unrelated chatter says nothing
//   5. cross-checks the page's mode <option> data-canon against the tokens
//      ModeManager.cpp actually accepts, in BOTH directions
//   6. RUNS setMode() for every option and asserts the frame it puts on the
//      wire resolves to a mode token the car can parse (and that the empty
//      option sends nothing, F-57)
//
// Step 4 is the one that matters. A page that parses can still lie.
//
// Step 5 exists so the page cannot offer a mode the car does not have, or omit
// one it does. History: U-82 first "fixed" setMode() to send data-canon on the
// belief that value='MODE_*' was unparseable. That was WRONG - handleCommand
// strips the 5-char MODE_ prefix (ModeManager.cpp:302), confirmed on the car,
// where MODE_MANUAL/MODE_PATH/MODE_AUTO all switch the mode. The change was
// reverted; stages 5 and 6 keep only the invariant that is real.
//
// Run: node tools/check-webpage-js.mjs        (exit 1 on any failure)
// The reply strings below are copied from the firmware that emits them:
//   WebServerComm.cpp  230 Reserved / 235 Password too long / 243 FULL
//   258 ADDED / 296 USED / 347 Could not add / 387 Not saved / 398 DELETED
//   427 CLEARED / 449 SCAN;STARTED / 451 Syntax
//   ModeManager.cpp    841 NACK;E=UNKNOWN_MODE;ARG=
// Keep this list in step with those lines.
// =====================================================================
import fs from "node:fs";
import path from "node:path";
import vm from "node:vm";
import { fileURLToPath } from "node:url";

const here = path.dirname(fileURLToPath(import.meta.url));
const header = path.join(here, "..", "WebPage.h");
const src = fs.readFileSync(header, "utf8");
const modeSrc = fs.readFileSync(path.join(here, "..", "ModeManager.cpp"), "utf8");

// ---- extract the raw string, delimiter and all -------------------------
const m = /R"([A-Za-z_]\w*)\(/.exec(src);
if (!m) {
  console.error("FAIL: no C++ raw string found in WebPage.h");
  process.exit(1);
}
const open = `R"${m[1]}(`;
const close = `)${m[1]}"`;
const i = src.indexOf(open);
const end = src.indexOf(close, i);
if (i < 0 || end < 0) {
  console.error("FAIL: raw string is not terminated");
  process.exit(1);
}
const html = src.slice(i + open.length, end);
console.log(`page bytes: ${html.length}`);

const s = /<script[^>]*>([\s\S]*?)<\/script>/i.exec(html);
if (!s) {
  console.error("FAIL: no <script> block in the page");
  process.exit(1);
}
const js = s[1];
console.log(`script bytes: ${js.length}`);

// ---- 1. it must parse ---------------------------------------------------
try {
  new vm.Script(js, { filename: "WebPage-inline.js" });
} catch (e) {
  console.error(`FAIL: inline script does not parse\n${e.message}`);
  process.exit(1);
}
console.log("PASS: inline script parses");

// ---- 2. the handler's dependencies must exist --------------------------
const defined = new Set(
  [...js.matchAll(/(?:function|const|let|var)\s+([A-Za-z_$][\w$]*)/g)].map(
    (x) => x[1],
  ),
);
const missing = ["note", "handleTextLine", "sendCmd"].filter(
  (n) => !defined.has(n),
);
if (missing.length) {
  console.error(`FAIL: called but never defined: ${missing.join(", ")}`);
  process.exit(1);
}
console.log("PASS: note/handleTextLine/sendCmd defined in-page");

// ---- 3. #note must exist ----------------------------------------------
if (!/id=['"]note['"]/.test(html)) {
  console.error("FAIL: #note missing from markup — every message is dropped");
  process.exit(1);
}
console.log("PASS: #note present in markup");

// ---- 4. it must tell the truth ----------------------------------------
const els = new Map();
function el(id) {
  if (!els.has(id)) {
    els.set(id, {
      id,
      innerText: "",
      innerHTML: "",
      textContent: "",
      value: "",
      style: {},
      classList: { add() {}, remove() {} },
      addEventListener() {},
      appendChild() {},
      children: [],
      options: [],
    });
  }
  return els.get(id);
}
const sandbox = {
  console,
  setInterval: () => 0,
  clearInterval: () => {},
  setTimeout: () => 0,
  WebSocket: function () {
    this.close = () => {};
    this.send = () => {};
  },
  document: {
    getElementById: el,
    querySelector: () => null,
    querySelectorAll: () => [],
    createElement: (t) => el(`new-${t}`),
    addEventListener() {},
  },
};
sandbox.window = sandbox;
sandbox.globalThis = sandbox;
sandbox.location = { hostname: "192.168.245.1", host: "192.168.245.1" };
vm.createContext(sandbox);
new vm.Script(js, { filename: "WebPage-inline.js" }).runInContext(sandbox);

if (typeof sandbox.handleTextLine !== "function") {
  console.error("FAIL: handleTextLine is not callable");
  process.exit(1);
}

const OK = "#0a7a4a"; // note()'s success colour — a refusal must never use it
const S = (reply) => `STATE;MODE=4WD4M;REPLY=${reply}\n`;
const cases = [
  [S("ROUTERS;ADDED;HomeNet"), true, "HomeNet"],
  [S("ROUTERS;USED;HomeNet"), true, "HomeNet"],
  [S("ROUTERS;DELETED;HomeNet"), true, "HomeNet"],
  [S("ROUTERS;CLEARED"), true, "cleared"],
  [S("ROUTERS;SCAN;STARTED"), true, "Scanning"],
  [S("ROUTERS;FULL"), false, "delete one first"],
  [S("ROUTERS;ERROR;Reserved"), false, "Reserved"],
  [S("ROUTERS;ERROR;Password too long"), false, "Password too long"],
  [S("ROUTERS;ERROR;SSID length"), false, "SSID length"],
  [S("ROUTERS;ERROR;Not saved:HomeNet"), false, "has not saved"],
  [S("ROUTERS;ERROR;Could not add router"), false, "Could not add router"],
  [S("ROUTERS;ERROR;Syntax"), false, "Syntax"],
  ["NACK;E=UNKNOWN_MODE;ARG=ESP_SER\n", false, "ESP_SER"],
  // ADDED confirms a SAVE only. If this ever claims a switch again, the page is
  // promising the user's phone will change networks on something unconfirmed.
  [S("ROUTERS;ADDED;HomeNet"), true, null],
];

const noteEl = el("note");
let bad = 0;
for (const [line, ok, must] of cases) {
  noteEl.innerText = "";
  noteEl.style = {};
  sandbox.handleTextLine(line);
  const got = noteEl.innerText;
  const green = noteEl.style.color === OK;
  if (!got) {
    console.error(`FAIL: user told nothing for ${JSON.stringify(line)}`);
    bad++;
  } else if (must && !got.toLowerCase().includes(must.toLowerCase())) {
    console.error(`FAIL: "${got}" does not mention "${must}"`);
    bad++;
  } else if (ok && !green) {
    console.error(`FAIL: success not shown as success: "${got}"`);
    bad++;
  } else if (!ok && green) {
    console.error(`FAIL: REFUSAL shown in the success colour: "${got}"`);
    bad++;
  } else {
    console.log(`  ${ok ? "ok " : "no "} ${got}`);
  }
}

// A save must not be reported as a switch.
noteEl.innerText = "";
noteEl.style = {};
sandbox.handleTextLine(S("ROUTERS;ADDED;HomeNet"));
if (/switch/i.test(noteEl.innerText)) {
  console.error(`FAIL: ADDED claims a switch: "${noteEl.innerText}"`);
  bad++;
}

// Unrelated chatter must stay silent rather than invent a message.
for (const quiet of [
  "STATE;MODE=4WD4M;SPD=170\n",
  "CAPS;4WD4M,BT,LIVE\n",
  "STATE;MODE=4WD4M;REPLY=\n",
]) {
  noteEl.innerText = "";
  noteEl.style = {};
  sandbox.handleTextLine(quiet);
  if (noteEl.innerText !== "") {
    console.error(
      `FAIL: ${JSON.stringify(quiet)} spoke up: "${noteEl.innerText}"`,
    );
    bad++;
  }
}

// ---- 5. the mode list must match what the CAR can actually parse ----------
//
// U-82: every <option value='MODE_*'> was a legacy internal id that
// ModeManager::setModeFromString has never heard of. It matches
// MODE_REGISTRY[].token plus two aliases, so all seven website modes NACKed.
// This stage reads the firmware's accepted tokens straight out of
// ModeManager.cpp and cross-checks the page's data-canon both ways, so the
// list cannot drift from the firmware again in either direction.
const registryTokens = new Set(
  [...modeSrc.matchAll(/\{\s*Mode::\w+\s*,\s*"([^"]+)"/g)].map((x) => x[1]),
);
const accepted = new Set(registryTokens);
const fnStart = modeSrc.indexOf("ModeManager::setModeFromString");
for (const x of modeSrc
  .slice(fnStart)
  .matchAll(/strcasecmp\(\s*modeStr\s*,\s*"([^"]+)"\s*\)/g)) {
  // Receive-side legacy aliases (BT, MANUAL). The firmware deliberately keeps
  // accepting these but its own comment says "senders emit <canonical>", so they
  // are valid to RECEIVE and must never be required in the page's list.
  accepted.add(x[1]);
}
if (accepted.size < 7) {
  console.error(
    `FAIL: only ${accepted.size} mode tokens parsed from ModeManager.cpp - ` +
      `the registry shape changed and this gate is now blind. Fix the regex.`,
  );
  process.exit(1);
}

const pageOptions = [
  // Attribute order/amount is not guaranteed and the list is column-aligned
  // with runs of spaces, so match the three data-* attributes in any order
  // after `value` rather than assuming one tidy layout.
  ...html
    .matchAll(
      /<option\s+value='([^']*)'((?:\s+data-[a-z]+='[^']*')+)\s*>/g,
    ),
].map((x) => {
  const attrs = {};
  for (const a of x[2].matchAll(/data-([a-z]+)='([^']*)'/g)) attrs[a[1]] = a[2];
  return { value: x[1], canon: attrs.canon ?? "", label: attrs.label ?? "" };
});
if (!pageOptions.length) {
  console.error("FAIL: no mode <option> tags parsed - markup shape changed");
  process.exit(1);
}
for (const o of pageOptions) {
  if (!o.canon) {
    console.error(`FAIL: page option '${o.value}' has no data-canon token`);
    process.exit(1);
  }
}

for (const o of pageOptions) {
  if (!accepted.has(o.canon)) {
    console.error(
      `FAIL: page option '${o.label}' has data-canon='${o.canon}', which is ` +
        `not a MODE_REGISTRY token (accepted: ${[...accepted].join(", ")}). ` +
        `data-canon is what syncMode() matches the car's reported mode ` +
        `against and what the CAPS-disable path looks up, so the dropdown ` +
        `would stop tracking the car and would not grey out absent modes.`,
    );
    bad++;
  }
}
// Reverse direction: a mode the car offers must be selectable from the page.
for (const tok of registryTokens) {
  if (!pageOptions.some((o) => o.canon === tok)) {
    console.error(
      `FAIL: the car accepts mode '${tok}' but the page has no option for it`,
    );
    bad++;
  }
}
if (!bad) {
  console.log(
    `PASS: all ${pageOptions.length} page modes are tokens the car parses ` +
      `(${[...accepted].join(", ")})`,
  );
}

// ---- 6. setMode() must put a token the car accepts on the wire ----------
// The page sends value='MODE_<token>'. That form is correct: handleCommand
// strips the 5-char "MODE_" prefix before parsing (ModeManager.cpp:302), and
// this was verified on the car - MODE_MANUAL/MODE_PATH/MODE_AUTO all switch
// the mode. A bare '<token>' is equally valid, so both forms are accepted here.
if (typeof sandbox.setMode !== "function") {
  console.error("FAIL: setMode is not callable");
  process.exit(1);
}
const realSendCmd = sandbox.sendCmd;
const mSel = el("m");
const frames = [];
sandbox.sendCmd = (c) => frames.push(c);

// Build the whole option list once, exactly as a real <select> would have it,
// then move the selection. (An earlier version of this check handed setMode a
// one-element `options` array with selectedIndex>0, which made every option
// after the first take a fallback path and failed for the wrong reason.)
const optNodes = pageOptions.map((o) => ({
  value: o.value,
  getAttribute: (n) => (n === "data-canon" ? o.canon : null),
}));
mSel.options = optNodes;

for (let i = 0; i < pageOptions.length; i++) {
  const o = pageOptions[i];
  mSel.value = o.value; // the page stores the wire token here
  mSel.selectedIndex = i;
  frames.length = 0;
  sandbox.setMode();
  const sent = frames[0];
  if (sent === undefined) {
    console.error(`FAIL: selecting '${o.label}' sent nothing`);
    bad++;
    continue;
  }
  // Accept either "MODE_<token>" or a bare "<token>".
  const bare = sent.startsWith("MODE_") ? sent.slice(5) : sent;
  const bareOk = accepted.has(bare);
  const tokenOk = accepted.has(sent);
  if (!bareOk && !tokenOk) {
    console.error(
      `FAIL: selecting '${o.label}' sent '${sent}', which resolves to ` +
        `'${bare}' - not a token ModeManager can parse`,
    );
    bad++;
  }
}

// F-57 must survive: the empty "- current mode -" option sends nothing.
mSel.value = "";
mSel.selectedIndex = 0;
frames.length = 0;
sandbox.setMode();
if (frames.length) {
  console.error(`FAIL: the empty option sent '${frames[0]}' (F-57 regression)`);
  bad++;
}
sandbox.sendCmd = realSendCmd;
if (!bad) {
  console.log(
    `PASS: setMode() puts an accepted token on the wire for all ` +
      `${pageOptions.length} modes, and nothing for the empty option`,
  );
}

// ---- 7. the mode list must NOT be rewritten on every 1 Hz frame ----------
//
// U-83: the JSON handler used to reassign o.text and o.disabled for every
// option on every frame. Writing to an <option> while its popup is open makes
// the browser dismiss it, so the page's mode chooser flickered and taps missed.
// Count real writes: two identical frames must produce zero, and the guard must
// hold while the dropdown is open.
if (typeof sandbox.initWebSocket !== "function") {
  console.error("FAIL: initWebSocket is not callable");
  process.exit(1);
}
let sock = null;
const RealWS = sandbox.WebSocket;
sandbox.WebSocket = function (url) {
  sock = { url, close() {}, send() {}, readyState: 1 };
  return sock;
};
sandbox.WebSocket.OPEN = 1;

const writes = { n: 0 };
const counting = (o, key) => {
  let v = o[key];
  Object.defineProperty(o, key, {
    get: () => v,
    set: (x) => {
      writes.n++;
      v = x;
    },
    configurable: true,
  });
};

// Real option nodes, but writes are counted.
const mOpts = pageOptions.map((o) => {
  const node = {
    value: o.value,
    disabled: false,
    text: o.label,
    getAttribute: (n) => (n === "data-canon" ? o.canon : n === "data-label" ? o.label : null),
  };
  counting(node, "text");
  counting(node, "disabled");
  return node;
});
const mForWs = el("m");
mForWs.value = "";
mForWs.options = mOpts;
el("s").value = "200";
const rSel = el("r");
rSel.options = [{ value: "", text: "" }];

sandbox.initWebSocket();
const onmsg = sock && sock.onmessage;
if (typeof onmsg !== "function") {
  console.error("FAIL: initWebSocket did not install an onmessage handler");
  process.exit(1);
}

const frame = JSON.stringify({
  status: "Stopped",
  mode: "4WD4M",
  mode_name: "4WD4M",
  speed: 200,
  caps: "4WD4M:LIVE;PATH:CS;OBS_US:CS;OBS_IR:CS;MAN:CS;AUTO:CS;2WD1M:CS",
  connected: true,
  ip: "192.168.1.100",
  signal: 60,
  ssid: "HomeNet",
  uptime_ms: 61000,
  free_heap: 120000,
});

// First frame applies the registry; later identical frames must be inert.
onmsg({ data: frame });
const afterFirst = writes.n;
onmsg({ data: frame });
onmsg({ data: frame });
const afterRepeats = writes.n;
if (afterRepeats !== afterFirst) {
  console.error(
    `FAIL: identical frames rewrote the mode list ` +
      `${afterRepeats - afterFirst} more time(s) - this is the flicker`,
  );
  bad++;
}

// A real registry change must still be applied.
writes.n = 0;
onmsg({
  data: JSON.stringify({ status: "Stopped", mode: "4WD4M", speed: 200,
    caps: "4WD4M:LIVE;PATH:LIVE;OBS_US:CS;OBS_IR:CS;MAN:CS;AUTO:CS;2WD1M:CS",
    connected: true, uptime_ms: 62000 }),
});
if (writes.n === 0) {
  console.error("FAIL: a genuine CAPS change was ignored - the list is now stale");
  bad++;
}

// And the dropdown must not be touched while the user has it open.
sandbox.document.activeElement = mForWs;
writes.n = 0;
onmsg({
  data: JSON.stringify({ status: "Stopped", mode: "4WD4M", speed: 200,
    caps: "4WD4M:LIVE;PATH:CS;OBS_US:CS;OBS_IR:CS;MAN:CS;AUTO:CS;2WD1M:CS",
    connected: true, uptime_ms: 63000 }),
});
if (writes.n !== 0) {
  console.error(
    `FAIL: the mode list was rewritten ${writes.n} time(s) while the ` +
      `dropdown was open - that closes the popup and eats the tap`,
  );
  bad++;
}
sandbox.document.activeElement = null;
sandbox.WebSocket = RealWS;
if (bad === 0) {
  console.log(
    "PASS: the mode list is written once per real change, never per frame, " +
      "and never while open",
  );
}

if (bad) {
  console.error(`\n${bad} check(s) FAILED`);
  process.exit(1);
}
console.log("\nPASS: the page reports the car's answers honestly");

// ---- 8. the AP power policy must never be able to strand the car ---------
//
// U-86: the car parks its setup beacon while it is healthy on a router, to save
// power. The whole risk of that is a car that cannot be set up again, so the
// order of operations is the contract. These checks read the firmware source
// rather than the page, because that is where the radio work lives.
const fw = fs.readFileSync(
  path.join(here, "..", "WebServerComm.cpp"),
  "utf8",
);
const hdr = fs.readFileSync(path.join(here, "..", "WebServerComm.h"), "utf8");

const policy = /void WebServerComm::updateApPowerPolicy\(\)\s*\{[\s\S]*?\n\}/.exec(
  fw,
);
if (!policy) {
  console.error("FAIL: updateApPowerPolicy() is missing from WebServerComm.cpp");
  process.exit(1);
}
const body = policy[0];

// Park only once the STA is genuinely up. Matched on the assignment, not the
// comparison, so this cannot be satisfied by a decoy elsewhere in the function.
const staGuard = /const bool staUp\s*=\s*\(\s*WiFi\.status\(\)\s*==\s*WL_CONNECTED\s*\)/;
if (!staGuard.test(body)) {
  console.error(
    "FAIL: the beacon could be parked without checking the STA is connected",
  );
  bad++;
}

// U-87 CORRECTION. The bench caught this: parking whenever the STA was up made
// the car unreachable, because it had joined a router the user was not on. A
// connected client is the only proof that the user can reach the car WITHOUT the
// beacon, so it is now a required condition.
const inUse = /const bool inUse\s*=\s*ws\.connectedClients\(\)\s*>\s*0/;
if (!inUse.test(body)) {
  console.error(
    "FAIL: the policy does not require a connected WebSocket client — the " +
      "car can park its only way in while nobody can reach it",
  );
  bad++;
}
// ...and parking must be gated on it, not merely computed.
const parkBlock = /if\s*\(\s*apParked_\s*\)\s*\{([\s\S]*?)\n  \}/.exec(body);
if (!parkBlock) {
  console.error("FAIL: the parked branch is missing from the power policy");
  bad++;
} else if (!/inUse/.test(parkBlock[1])) {
  console.error(
    "FAIL: a parked beacon is not released when the last client leaves — the " +
      "car would stay unreachable after use",
  );
  bad++;
}
// Grace period, so a router that drops again straight away cannot flap it.
if (!/AP_PARK_GRACE_MS/.test(body)) {
  console.error(
    "FAIL: the beacon is parked with no grace period — a flapping router " +
      "would tear the beacon down and up repeatedly",
  );
  bad++;
}
// Park with softAPdisconnect ONLY. `WiFi.mode()` restarts the driver, which is
// what caused the U-75 watchdog reboot; it must never appear on this path.
if (/WiFi\.mode\s*\(/.test(body)) {
  console.error(
    "FAIL: the power policy calls WiFi.mode() — that is a full driver restart " +
      "and is the documented cause of the task-watchdog reboot",
  );
  bad++;
}
// ...and the disconnect must not take the STA down with it. `wifioff` defaults
// to false, so an omitted or falsy argument is the correct, safe form; only an
// explicit `true` is dangerous.
const dis = /WiFi\.softAPdisconnect\(([^;]*?)\);/.exec(body);
if (!dis) {
  console.error("FAIL: the policy never calls softAPdisconnect — nothing is parked");
  bad++;
} else if (/true/.test(dis[1])) {
  console.error(
    "FAIL: softAPdisconnect(true) would drop the router association too — " +
      "parking the beacon must leave the STA up",
  );
  bad++;
}
// If the association goes, the beacon must come back. This is the single most
// important property: it is what makes parking safe at all.
// Scope the search to the !staUp branch itself, so a startOwnAP() further down
// the function cannot stand in for the restore-on-disconnect.
const staDown = /if\s*\(!staUp\)\s*\{([\s\S]*?)\n  \}/.exec(body);
if (!staDown || !/startOwnAP\(\)/.test(staDown[1])) {
  console.error(
    "FAIL: losing the router does not restore the beacon — a parked car could " +
      "never be set up again",
  );
  bad++;
}
if (!/AP_PARK_GRACE_MS\s+\d+ul/.test(hdr)) {
  console.error("FAIL: AP_PARK_GRACE_MS is not defined as a compile-time constant");
  bad++;
}
// The give-up fallback must clear the parked flag, or the beacon would be
// running while the code believed it was down.
// Scope to the GIVEUP_AP case body so an unrelated apParked_/startOwnAP() pair
// elsewhere in the file cannot satisfy this.
const giveUp = /case GIVEUP_AP:([\s\S]*?)break;/.exec(fw);
if (!giveUp) {
  console.error("FAIL: the GIVEUP_AP fallback case is missing");
  bad++;
} else if (
  !/apParked_\s*=\s*false/.test(giveUp[1]) ||
  !/startOwnAP\(\)/.test(giveUp[1])
) {
  console.error(
    "FAIL: the give-up fallback does not clear apParked_ and restart the " +
      "beacon — the power policy would believe the beacon is down while it " +
      "is up, and the car would have no setup path",
  );
  bad++;
}
if (!bad) {
  console.log(
    "PASS: the beacon can only be parked while the car is reachable on a " +
      "router, and is restored the moment that stops being true",
  );
}

if (bad) {
  console.error(`\n${bad} check(s) FAILED`);
  process.exit(1);
}
// ---- 9. one WS command must produce exactly ONE answer --------------------
//
// U-88: the WebSocket handler dispatched each line immediately AND queued it, so
// every Wi-Fi command ran twice. Invisible for ADD, destructive for the rest - a
// delete answered DELETED and then "ERROR;Not saved" for the SAME user action,
// which is what made add / edit / delete / switch look broken in the app.
const wsc = fs.readFileSync(path.join(here, "..", "WebServerComm.cpp"), "utf8");
const wsFn = /void WebServerComm::handleWebSocketEvent\([\s\S]*?\n\}/.exec(wsc);
if (!wsFn) {
  console.error("FAIL: handleWebSocketEvent is missing from WebServerComm.cpp");
  process.exit(1);
}
// Only lines that are not comments count. The U-88 explanation quotes the old
// handleCommand() call, and the file is CRLF, so a regex strip proved fragile;
// "trimmed line starts with //" says exactly what it means.
const codeLines = wsFn[0]
  .split("\n")
  .map((l) => l.trim())
  .filter((l) => l.length > 0 && !l.startsWith("//"));

const dispatches = codeLines.filter((l) => l.includes("handleCommand(")).length;
const queues = codeLines.filter((l) => l.includes("queuePush(")).length;

if (dispatches !== 0) {
  console.error(
    `FAIL: the WebSocket path calls handleCommand() ${dispatches} time(s) AND ` +
      `queues the line, so the command runs twice - a delete answers DELETED ` +
      `and then ERROR;Not saved for one user action`,
  );
  bad++;
}
if (queues !== 1) {
  console.error(
    `FAIL: the WebSocket path queues the line ${queues} time(s); T-1's single ` +
      `command path needs exactly one, or nothing would run at all`,
  );
  bad++;
}
if (!bad) {
  console.log(
    "PASS: a WebSocket command is queued once and dispatched once, so it " +
      "produces exactly one answer",
  );
}

if (bad) {
  console.error(`\n${bad} check(s) FAILED`);
  process.exit(1);
}