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
//      wire is a token the car can parse (and that the empty option sends
//      nothing, F-57)
//
// Step 4 is the one that matters. A page that parses can still lie.
//
// Step 5 exists because of U-82: setMode() sent each option's legacy
// `value='MODE_*'` id, tokens the car has never heard of, so every mode in the
// website dropdown was dead. Steps 5+6 make that class of drift a build failure.
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
      `FAIL: page option '${o.label}' has data-canon='${o.canon}', which ` +
        `ModeManager::setModeFromString cannot parse (accepted: ` +
        `${[...accepted].join(", ")}). The car would answer ` +
        `NACK;E=UNKNOWN_MODE;ARG=${o.canon}.`,
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

// ---- 6. setMode() must put an ACCEPTED token on the wire ------------------
// Driven by the real markup above: select each option in turn and check the
// exact frame setMode() would send.
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
// after the first take setMode's `|| sel.value` fallback and failed for the
// wrong reason. A real select always has all of them.)
const optNodes = pageOptions.map((o) => ({
  value: o.value,
  getAttribute: (n) => (n === "data-canon" ? o.canon : null),
}));
mSel.options = optNodes;

for (let i = 0; i < pageOptions.length; i++) {
  const o = pageOptions[i];
  mSel.value = o.value; // the page stores the internal id here...
  mSel.selectedIndex = i;
  frames.length = 0;
  sandbox.setMode();
  const sent = frames[0];
  if (sent === undefined) {
    console.error(`FAIL: selecting '${o.label}' sent nothing`);
    bad++;
  } else if (sent !== o.canon) {
    console.error(
      `FAIL: selecting '${o.label}' sent '${sent}', not the canonical ` +
        `'${o.canon}'`,
    );
    bad++;
  } else if (!accepted.has(sent)) {
    console.error(
      `FAIL: selecting '${o.label}' sent '${sent}', which the car cannot parse`,
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

if (bad) {
  console.error(`\n${bad} check(s) FAILED`);
  process.exit(1);
}
console.log("\nPASS: the page reports the car's answers honestly");