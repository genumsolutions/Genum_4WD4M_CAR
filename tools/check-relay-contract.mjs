// U-94 Phase 2: the relay contract, checked from the car's side of the wire.
//
// WHY THIS IS A SEPARATE GATE AND NOT MORE ASSERTIONS IN THE ARDUINO BUILD:
// the Arduino job answers "does it compile". It cannot answer "is the transport
// actually wired in", because a transport that compiles and is never connected
// looks identical to one that works. The four ways to get that wrong are all
// silent, and all of them have the same symptom - a phone pairs, or believes it
// paired, and nothing happens:
//
//   1. the class is registered but run() never drains it -> commands vanish;
//   2. sendState() never fans out to it -> the phone watches a stopped car;
//   3. isConnectedNow() ignores it -> the 2 s safe-stop fires forever while the
//      owner is driving, and it looks like a flaky relay;
//   4. handle() is not pumped -> the socket never connects and never notices.
//
// (1) and (2) are half-finished features. (3) is a car that stops itself every
// two seconds from a different city. None of them produce a compiler error, so
// they need a gate that reads the source. This is that gate.
//
// A NOTE ON HOW THIS FILE READS SOURCE. Every structural rule below runs
// against `code()` - the file with comments and string literals removed - and
// not against the raw text. That is not fussiness: the first version of this
// gate failed itself, because prose in a comment ("never setInsecure() on a
// car...") matched the rule banning setInsecure(), and a comment explaining why
// RelayComm has no command path matched the rule banning one. A gate that reads
// its own documentation as evidence is worse than no gate, because it produces
// red for the wrong reason and gets switched off.
//
// THE TOKEN RULE. The relay token is a credential, and the one place a
// credential leaks in this codebase is a log line or an acknowledgement that
// interpolates it. So: no printf-family call in RelayComm.cpp may mention the
// token member, and every reply must be a plain string literal with no format
// specifier. `RELAYCFG;SHOW` deliberately reports SET / NOT_SET.

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

const relayRaw = raw("RelayComm.cpp");
const relayH = raw("RelayComm.h");
const relayCode = code(relayRaw);
const mmCode = code(raw("ModeManager.cpp"));
const inoCode = code(raw("Genum_4WD4M_CAR.ino"));
const cfg = raw("Config.h");

let failures = 0;
const fail = (rule, detail) => {
  failures++;
  console.error(`  FAIL  ${rule}\n        ${detail}`);
};
const pass = (rule, detail) => console.log(`  ok    ${rule} - ${detail}`);

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

// ---- 1. the four wiring points ----------------------------------------
// Each CommInterface subclass must appear in all four ModeManager functions
// plus the sketch's pump. The class list is derived from the headers, so a
// FOURTH transport added later is covered by this rule for free.
const headers = ["BluetoothComm.h", "WebServerComm.h", "RelayComm.h", "CommInterface.h"];
const transports = headers
  .flatMap((h) => [...raw(h).matchAll(/class (\w+)\s*:\s*public CommInterface/g)].map((m) => m[1]))
  .filter((v, i, a) => a.indexOf(v) === i);

const memberFor = { BluetoothComm: "auxComm_", WebServerComm: "comm_", RelayComm: "relayComm_" };
// currentLink() is allowed to name the class directly, because that is how the
// web transport has always been interrogated there (WebServerComm::instance()
// carries the WS-client and STA facts the OLED needs). The other three functions
// must go through the pointer - naming the class there would mean the transport
// was bypassed.
const alsoAccepts = { WebServerComm: "WebServerComm", BluetoothComm: "BluetoothComm", RelayComm: "RelayComm" };

if (transports.length < 3) {
  fail("transport-seam", `only found ${transports.length} CommInterface subclasses: ${transports}`);
} else {
  pass("transport-seam", `${transports.length} transports: ${transports.join(", ")}`);
}

const seams = [
  ["ModeManager::run() drains it", "void ModeManager::run()", false],
  ["ModeManager::sendState() fans out to it", "void ModeManager::sendState()", false],
  ["ModeManager::isConnectedNow() counts it", "bool ModeManager::isConnectedNow()", false],
  ["ModeManager::currentLink() reports it", "ModeManager::LinkInfo ModeManager::currentLink()", true],
];

for (const t of transports) {
  const member = memberFor[t];
  if (!member) {
    fail("member-known", `${t} has no member name here - add it, or its seam checks are a no-op`);
    continue;
  }
  for (const [label, sig, allowClassName] of seams) {
    const fnBody = body(mmCode, sig);
    const alt = alsoAccepts[t];
    const hit = fnBody?.includes(member) || (allowClassName && fnBody?.includes(alt));
    if (!fnBody) fail("seam-present", `${label}: could not locate the function`);
    else if (!hit) fail("seam-present", `${label}: neither "${member}" nor "${alt}" appears in it`);
    else pass("seam-present", `${label} (${member})`);
  }
}

// The relay is pumped in the sketch, and exactly one instance exists: the WS
// event callback is a plain function pointer, so a second instance would be a
// second, silently-wrong singleton.
const instances = (raw("Genum_4WD4M_CAR.ino").match(/^RelayComm relayComm;/gm) ?? []).length;
if (instances !== 1) {
  fail("singleton", `expected exactly one "RelayComm relayComm;" in the sketch, found ${instances}`);
} else {
  pass("singleton", "one RelayComm instance");
}
if (!/^\s*relayComm\s*\.\s*handle\s*\(\s*\)\s*;/m.test(inoCode)) {
  fail("pumped", "relayComm.handle() is not called unconditionally in loop()");
} else {
  pass("pumped", "relayComm.handle() runs every loop");
}

// ---- 2. the grammar lock ----------------------------------------------
// RelayComm must queue, never parse. One parse site, in ModeManager (T-1).
if (/handleCommand\s*\(/.test(relayCode)) {
  fail("grammar-lock", "RelayComm.cpp calls handleCommand() - that would be a second command path (T-1)");
} else {
  pass("grammar-lock", "RelayComm.cpp has no command path of its own");
}
const runBody = body(mmCode, "void ModeManager::run()") ?? "";
if (!/relayComm_\s*->\s*available\s*\(/.test(runBody)) {
  fail("grammar-lock", "ModeManager::run() never drains the relay's queue");
} else if (!/handleCommand\s*\(\s*cmdBuf\s*,\s*CmdSource::WIRE\s*\)/.test(runBody)) {
  fail("grammar-lock", "the relay is not drained through handleCommand(..., CmdSource::WIRE)");
} else {
  pass("grammar-lock", "relay commands reach the ONE parser as CmdSource::WIRE");
}
// And the drain must not carry a relay-specific branch: if a relay ever needs
// its own handling, that is the second path this rule exists to prevent.
if (/relayComm_\s*->\s*(?!available|connected)/.test(runBody.replace(/relayComm_\s*->\s*available[^;]*;/g, "").replace(/relayComm_\s*->\s*connected[^;]*;/g, ""))) {
  fail("grammar-lock", "run() calls a relay method other than available()/connected() - inspect this");
} else {
  pass("grammar-lock", "run() uses only available()/connected() on the relay");
}

// ---- 3. control frames cannot be read as car lines --------------------
// '\0' is what makes this structural: handleCommand reads a C string, so a
// NUL-prefixed frame cannot be one. The prefix check must come first.
if (!/kControlPrefix\[\]\s*=\s*"\\x00"/.test(relayH)) {
  fail("control-prefix", 'kControlPrefix must start with "\\x00" so a control frame can never be a car line');
} else {
  pass("control-prefix", 'prefix is NUL + "relay"');
}
const frameBody = body(relayCode, "void RelayComm::handleTextFrame");
if (!frameBody) {
  fail("control-prefix", "could not locate handleTextFrame()");
} else {
  const checkAt = frameBody.search(/isControlFrame\s*\(/);
  const readsAt = frameBody.search(/controlEventIs|controlNumber|strstr|strchr/);
  if (checkAt < 0) fail("control-prefix", "handleTextFrame() never calls isControlFrame()");
  else if (readsAt >= 0 && readsAt < checkAt) {
    fail("control-prefix", "handleTextFrame() inspects the frame's contents before the prefix check");
  } else {
    pass("control-prefix", "prefix is checked before anything reads the frame's contents");
  }
}
// memcmp, not strncmp: the payload is not NUL-terminated and the prefix contains
// a NUL, so every str* function is wrong here by construction.
const isControlBody = body(relayCode, "bool RelayComm::isControlFrame") ?? "";
if (/str(n?cmp|casecmp)/.test(isControlBody)) {
  fail("control-prefix", "isControlFrame() uses a string comparison - the payload is not NUL-terminated");
} else if (!/memcmp/.test(isControlBody)) {
  fail("control-prefix", "isControlFrame() does not use memcmp");
} else {
  pass("control-prefix", "isControlFrame() uses memcmp");
}

// ---- 4. TLS ------------------------------------------------------------
if (/setInsecure\s*\(/.test(relayCode)) {
  fail("tls", "setInsecure() is called - never on a car that takes drive commands");
} else {
  pass("tls", "no setInsecure()");
}
if (!/beginSslWithCA\s*\(/.test(relayCode)) {
  fail("tls", "no beginSslWithCA() - the connection must be verified against a pinned root");
} else {
  pass("tls", "verified TLS via beginSslWithCA()");
}
// THE ANCHOR, ACTUALLY CHECKED. The rule this replaces was
// /ISRG Root X1/.test(relayRaw) - and the only place that string appears in the
// file is the COMMENT naming the anchor. So it reported "the anchor is ISRG Root
// X1" about a certificate nobody had ever checked, and would have reported it
// identically over a wrong, truncated or absent PEM. This file's own header
// explains that gates must not read their own documentation as evidence; that
// rule was doing exactly that, while passing.
//
// It matters more here than elsewhere: a wrong anchor does not fail a build or a
// test, it produces a car that silently never connects, from a distance where the
// symptom looks like a network problem rather than a typo in base64.
//
// So parse the embedded PEM and pin the DER's SHA-256. The pin is a second value
// that must be edited deliberately, so rotating the anchor is a visible act
// rather than a diff nobody reads.
const ANCHOR_FP = "96:BC:EC:06:26:49:76:F3:74:60:77:9A:CF:28:C5:A7:CF:E8:A3:C0:AA:E1:1A:8F:FC:EE:05:C0:BD:DF:08:C6";
const pem = relayRaw.match(/-----BEGIN CERTIFICATE-----[\s\S]*?-----END CERTIFICATE-----/);
if (!pem) {
  fail("tls-anchor", "no certificate embedded in RelayComm.cpp - what is beginSslWithCA() being given?");
} else {
  // Parse inside a try. A hand-edited PEM throws out of X509Certificate as
  // "bad base64 decode", which exits non-zero - so CI is safe - while telling
  // the owner nothing about the thing they need to fix.
  let cert;
  try {
    cert = new X509Certificate(pem[0].trim());
  } catch (err) {
    fail("tls-anchor", `the embedded block does not parse as a certificate (${err.message})`);
  }
  // Flatten the subject: X509Certificate renders it across several lines, which
  // would run straight through the report format.
  const subject = cert ? String(cert.subject).replace(/\s*\n\s*/g, ", ").replace(/,\s*$/, "") : "";
  if (!cert) {
    // Everything below rests on this one fact. Say it once and stop.
  } else if (!subject.includes("ISRG Root X1")) {
    fail("tls-anchor", `the embedded anchor is "${subject}" - expected ISRG Root X1`);
  } else if (cert.issuer !== cert.subject) {
    fail("tls-anchor", "the embedded anchor is not self-signed, so it is not a root");
  } else if (!cert.ca) {
    fail("tls-anchor", "the embedded certificate is not a CA");
  } else {
    const fp = createHash("sha256").update(cert.raw).digest("hex").toUpperCase().match(/../g).join(":");
    if (fp !== ANCHOR_FP) {
      fail("tls-anchor", `fingerprint ${fp}\n        expected ${ANCHOR_FP}\n        a wrong anchor fails as a handshake timeout on a car, not as a build error`);
    } else {
      pass("tls-anchor", `ISRG Root X1, self-signed CA, SHA-256 ${fp.slice(0, 23)}...`);
      // A second opinion from this machine's own TLS stack, when it has one.
      // Never the assertion: a CI runner's trust store is not the firmware's
      // trust decision, but a mismatch would mean the bytes are not what an
      // ordinary client trusts.
      const inStore = rootCertificates.some((der) => {
        try {
          return new X509Certificate(der).raw.equals(cert.raw);
        } catch {
          return false;
        }
      });
      pass("tls-anchor", inStore
        ? "also present, byte-identical, in this platform's trust store"
        : "not in this platform's trust store (fine - the fingerprint is what is pinned)");
    }
  }
}
// Liveness must be measured. Without a heartbeat a half-open socket reads as
// connected, and a link that reads as connected has switched the safe-stop off.
if (!/enableHeartbeat\s*\(/.test(relayCode)) {
  fail("liveness", "no enableHeartbeat() - a half-open socket would read as connected");
} else {
  pass("liveness", "heartbeat on, so a dead link is detected rather than assumed");
}

// ---- 5. the token is a credential --------------------------------------
const logLines = relayRaw.split("\n").filter((l) => /FW_LOG|Serial\s*\.\s*(print|printf)/.test(l));
const leaky = logLines.filter((l) => /\btoken_\b/.test(l));
if (!logLines.length) {
  fail("token-secret", "no log lines found at all - is the logger still there?");
} else if (leaky.length) {
  fail("token-secret", `log line passes the token: ${leaky[0].trim()}`);
} else {
  pass("token-secret", `no log line passes token_ (${logLines.length} log lines checked)`);
}

// Every reply is a fixed literal: `reply("...")` with no % and no argument. If a
// reply ever interpolates the token, it gets printed on every STATE broadcast,
// to every transport, forever.
const replies = [...relayRaw.matchAll(/reply\(\s*"([^"]*)"\s*\)/g)].map((m) => m[1]);
if (!replies.length) {
  fail("token-secret", "no reply() calls found - is the provisioning ack path gone?");
} else {
  const formatted = replies.filter((r) => r.includes("%"));
  if (formatted.length) fail("token-secret", `formatted reply: ${formatted[0]}`);
  else pass("token-secret", `${replies.length} replies, all plain literals`);
}
if (!/"RELAYCFG;SET"/.test(relayRaw) || !/"RELAYCFG;NOT_SET"/.test(relayRaw)) {
  fail("token-secret", "RELAYCFG;SHOW must answer SET / NOT_SET, never the value");
} else {
  pass("token-secret", "SHOW answers SET / NOT_SET");
}
// The token lives in NVS, not in a tracked header.
if (/^\s*#define\s+\w*RELAY_TOKEN\w*/m.test(cfg)) {
  fail("token-secret", "Config.h defines a RELAY_TOKEN - Config.h and secrets.h are both tracked");
} else {
  pass("token-secret", "no token literal in tracked headers");
}
// Search the RAW text here, not the stripped view: `code()` blanks string
// literals, and this rule is about the literal's value.
if (!/prefs\.begin\(\s*"botcfg"/.test(relayRaw) || !/"relay_token"/.test(relayRaw)) {
  fail("token-secret", "the token is not stored at NVS botcfg/relay_token (the namespace the plan names)");
} else {
  pass("token-secret", "stored at NVS botcfg/relay_token");
}
// Nothing outside RelayComm may read it.
const outsiders = ["ModeManager.cpp", "WebServerComm.cpp", "Oled_1_3.cpp", "Genum_4WD4M_CAR.ino", "Config.h"]
  .filter((f) => raw(f).includes("relay_token"));
if (outsiders.length) fail("token-secret", `relay_token is reachable from: ${outsiders.join(", ")}`);
else pass("token-secret", "relay_token is confined to RelayComm");

// ---- 6. the caps and the ids must agree across the wire ---------------
// 128 in, 448 out - the numbers the relay uses. If either side's notion of them
// changes, this gate is where it should be noticed.
const outCap = cfg.match(/RELAY_MAX_OUT_BYTES\s*=\s*(\d+)/);
if (!outCap) fail("caps", "Config.h does not define RELAY_MAX_OUT_BYTES");
else if (outCap[1] !== "448") fail("caps", `RELAY_MAX_OUT_BYTES is ${outCap[1]}, the relay admits 448`);
else pass("caps", "outbound cap 448 = the car's STATE buffer");

const cmdBuf = raw("WebServerComm.h").match(/#define\s+CMD_BUF_LEN\s+(\d+)/);
const kLine = relayH.match(/kLineLen\s*=\s*(\d+)/);
if (!cmdBuf) fail("caps", "cannot read CMD_BUF_LEN from WebServerComm.h");
else if (!kLine) fail("caps", "RelayComm has no kLineLen");
else if (kLine[1] !== cmdBuf[1]) {
  fail("caps", `RelayComm::kLineLen is ${kLine[1]}, CMD_BUF_LEN is ${cmdBuf[1]} - the relay admits 128`);
} else {
  pass("caps", `inbound cap ${cmdBuf[1]} = handleCommand's buffer`);
}
if (!/static_assert\(\s*(RelayComm::)?\s*kLineLen\s*==\s*CMD_BUF_LEN/.test(relayCode)) {
  fail("caps", "the static_assert tying kLineLen to CMD_BUF_LEN is missing");
} else {
  pass("caps", "static_assert ties the inbound cap to CMD_BUF_LEN at compile time");
}

// boardIdHex must be the 6-char string the relay's regex demands. The relay's
// regex is 6 after commit 40dac31; this is the car side of the same fact, and it
// is asserted here because a car printing 12 would be refused by every hello -
// the defect Phase 1 shipped with, invisible until a car actually dialled.
const boardFmt = raw("WebServerComm.cpp").match(/snprintf\(\s*out\s*,\s*outLen\s*,\s*"%0?(\d*)X"/);
if (!boardFmt) fail("board-id", "cannot read WebServerComm::boardIdHex()'s format string");
else {
  const width = parseInt(boardFmt[1] || "0", 10);
  if (width !== 6) {
    fail("board-id", `boardIdHex prints ${width} hex chars; the relay's BOARD_ID_RE demands exactly 6`);
  } else {
    pass("board-id", "boardIdHex prints 6 uppercase hex - the relay's BOARD_ID_RE");
  }
}
// (U-95 Phase 5) THE SAME PRODUCER MUST FEED THE APP-FACING `;ID=`.
// The website's claim loader keys its digest map on the app's `fw:<id>`
// profile key, which is built from `STATE;...;ID=` — so if ID= and the relay
// hello ever stop coming from boardIdHex(), a car can be CLAIMED under one
// identity and REFUSED at the hello under another. F-75 caught that split at
// the relay; this pins the car side of it.
// VIEW (F-83, declared): comments stripped, string LITERALS INTACT. The rule
// matches the format literal `";ID=%s"` — which `code()` (above) would blank —
// while prose in a comment ("plus the stable board id (`;ID=`)",
// ModeManager.cpp:518) must NOT count. Raw would credit the comment;
// literal-blanked cannot see the literal. This is the view where neither trap
// applies, and the rule is proven both directions (break the producer, break
// the consumer, break the pairing — each red; restore — green).
const stripComments = (text) =>
  text
    .replace(/\/\*[\s\S]*?\*\//g, (m) => m.replace(/[^\n]/g, " "))
    .replace(/\/\/[^\n]*/g, (m) => " ".repeat(m.length));
const mmLiterals = stripComments(raw("ModeManager.cpp"));
const idProducerAt = mmLiterals.search(/boardIdHex\(\s*id\s*,/);
const idWriteAt = mmLiterals.search(/";ID=%s",\s*id\s*\)/);
if (idProducerAt < 0) {
  fail("board-id", "ModeManager.cpp no longer fills `id` via boardIdHex() - what feeds STATE ;ID=?");
} else if (idWriteAt < 0) {
  fail(
    "board-id",
    "STATE ;ID= no longer prints boardIdHex()'s `id` buffer - the app's fw:<id> profile key " +
      "now comes from somewhere else; re-verify BOTH consumers (website claim loader, relay hello)"
  );
} else if (idWriteAt < idProducerAt || idWriteAt - idProducerAt > 300) {
  fail("board-id", ";ID= write and boardIdHex(id, ...) are no longer in the same block - re-verify they are one producer");
} else {
  pass("board-id", "STATE ;ID= prints boardIdHex()'s buffer - one producer for the app key and the relay hello");
}
// (U-95 Phase 5) THE CROSS-REPO HALF. The CONSUMER patterns live in
// genumsolutions-website (relay-core: BOARD_ID_RE for the hello,
// CLAIM_UNIQUE_ID_RE for the claim loader — both asserted by that repo's
// tests/car-relay.test.ts). This checkout may or may not contain the sibling
// repo; CI's checkout never does. When it is here, compare the literal
// patterns; when it is not, say `skip`, never `ok`: a check that did not run
// must not read as one that passed (F-83).
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
// The hello must be role "car" at the relay's protocol version, or the relay
// refuses it by name before it ever looks at the token.
if (!/role\\"\s*:\s*\\"\s*car/.test(relayRaw)) {
  fail("hello", "the hello does not declare role car");
} else {
  pass("hello", "hello declares role car");
}
if (!/\\"\s*v\\"\s*:\s*1\b/.test(relayRaw)) {
  fail("hello", "the hello does not send protocol v1 (RELAY_PROTOCOL_VERSION)");
} else {
  pass("hello", "hello sends protocol v1");
}

// ---- 7. inert by default ----------------------------------------------
// A donor car must not dial out. If this ever becomes "on by default" the owner
// should have said so, and this gate should have to be edited deliberately.
if (!/#define\s+RELAY_HOST\s+""/.test(cfg)) {
  fail("inert-by-default", "Config.h has a non-empty RELAY_HOST - the transport is not inert by default");
} else {
  pass("inert-by-default", 'RELAY_HOST is "" - a build with no host never dials');
}
if (!/if\s*\(\s*!hasToken\(\)\s*\)/.test(relayCode)) {
  fail("inert-by-default", "begin() does not refuse to dial without a token");
} else {
  pass("inert-by-default", "no token means no dial");
}

// (U-95 Phase 1e) An EMPTY RELAY_HOST must be refused explicitly, and BEFORE the
// token is even read. This is not the same as the check above: with a token
// stored and no host configured, beginSslWithCA("") simply fails, and the library
// treats a failed connect as a reason to retry — so "no host" was not inert, it
// was a DNS lookup plus a socket connect every kRedialMs, on the loop task,
// inside the watchdog window. Busy, not off.
const beginBodyRaw = body(raw("RelayComm.cpp"), "bool RelayComm::begin") ?? "";
// Matched on RAW source, not relayCode. The comparison is against the NUL
// character literal `'\0'`, and code() blanks character literals — so against
// stripped source this reads `== '  '` and can never match. Same trap as the
// crash-record idle rule: check against the text the rule will be handed.
const hostGuardRe = /RELAY_HOST\s*\[\s*0\s*\]\s*==\s*'\s*\\0\s*'/;
const hostGuard = beginBodyRaw.search(hostGuardRe);
// The loadToken() position is taken from a COMMENT-STRIPPED view, and that is the
// third time this gate has been caught reading prose as code: the comment above
// the guard explains that the guard must precede loadToken(), so a plain
// indexOf("loadToken()") found the sentence and reported the guard as misplaced.
// Comment-stripping is what makes "where is it CALLED" answerable at all. (The
// host-guard test above still needs the raw text, because it matches the '\0'
// character literal that code() blanks.)
const loadTokenCallAt = code(beginBodyRaw).search(/loadToken\s*\(\s*\)/);
if (!beginBodyRaw) {
  fail("inert-by-default", "RelayComm::begin() not found");
} else if (hostGuard < 0) {
  fail("inert-by-default", "begin() has no explicit empty-RELAY_HOST guard - an unconfigured car with a token will re-attempt a doomed dial every retry interval instead of staying off");
} else if (beginBodyRaw.indexOf("loadToken()") >= 0 && hostGuard > loadTokenCallAt) {
  // Order is the substance of the fix, not a style point: a car with no host has
  // no business reading credentials or reaching the TLS path at all.
  fail("inert-by-default", "the empty-RELAY_HOST guard runs AFTER loadToken() - it must come first, so an unconfigured car never touches credentials or the TLS code");
} else {
  pass("inert-by-default", "an empty RELAY_HOST is refused up front, before credentials are read or the TLS path is reached");
}

// ---- 7b. the relay must not be ARMED while the handshake is unbounded ----
// The numbers this section and sections 9/10 all reason about, parsed ONCE so
// the armed-handshake message, the budget rule and the ws rules can never
// disagree about what the watchdog currently is (F-82: one source or two
// copies that drift).
const wsh = raw("WebServerComm.h");
const budget = Number(wsh.match(/#define\s+HTTP_BODY_BUDGET_MS\s+(\d+)ul/)?.[1] ?? 0);
const wdt = Number(wsh.match(/#define\s+LOOP_TASK_WDT_TIMEOUT_MS\s+(\d+)ul/)?.[1] ?? 0);

// This is the interlock for a measured, still-unfixed defect, and it is the
// reason U-94's deployment is blocked rather than merely discouraged.
//
// The facts, from the toolchain rather than from a comment:
//   - NetworkClientSecure sets sslclient->handshake_timeout = 120000.
//   - ssl_starttls_handshake() loops on that value and nothing else.
//   - WebSocketsClient::loop() runs it, on the loop task.
//   - The loop task is subscribed to the task watchdog (LOOP_TASK_WDT_TIMEOUT_MS
//     in WebServerComm.h, 10000 ms since Phase 1b), and that watchdog
//     measures WALL-CLOCK time since the last esp_task_wdt_reset() — so the
//     vTaskDelay(2) inside the handshake loop does not save it.
// Therefore a relay host that completes TCP and then stalls TLS resets the car
// for up to two minutes.
//
// It cannot be tuned from here: WebSocketsClient keeps its WiFiClientSecure in a
// protected member with no setter, deletes and recreates it every retry, and
// WEBSOCKETS_TCP_TIMEOUT is read inside the library's own translation unit — so
// #define'ing it in this sketch is a no-op, and the owner's Arduino IDE build
// passes no build flags.
//
// So the honest position is that ARMING the relay is the dangerous act, and the
// gate refuses it. Shipping RELAY_HOST="" keeps every car on the safe side of
// this bug; a build that sets it is a build that will reboot the car whenever the
// relay is slow to answer. The fix is to move the dial off the loop task, after
// which this rule is deleted deliberately rather than quietly.
const hostIsSet = !/#define\s+RELAY_HOST\s+""/.test(cfg);
const handshakeBounded =
  /RELAY_HANDSHAKE_BOUNDED/.test(cfg) ||
  /xTaskCreate|RelayTask|relayTask/.test(relayCode);
if (hostIsSet && !handshakeBounded) {
  fail("armed-handshake",
      "RELAY_HOST is set, but the relay TLS handshake still runs on the loop task with a 120000 ms handshake_timeout against a " + wdt + " ms watchdog. " +
    "This is how the car reboots. Either keep RELAY_HOST empty, or move the dial to its own task (see RelayComm.cpp) before deploying.");
} else if (hostIsSet) {
  pass("armed-handshake", "RELAY_HOST is set and the handshake is bounded");
} else {
  pass("armed-handshake", "RELAY_HOST is empty, so the unbounded handshake cannot be reached");
}
// connected() must require enrolment, not just a socket: a car refused with
// close 4401 is not connected, and must not switch the safe-stop off.
const connBody = body(relayCode, "bool RelayComm::connected") ?? "";
if (!/enrolled_/.test(connBody)) {
  fail("enrolment", "connected() does not require the relay's paired ack");
} else if (!/client_\s*\.\s*isConnected\s*\(\s*\)/.test(connBody)) {
  fail("enrolment", "connected() does not require the socket");
} else {
  pass("enrolment", "connected() requires paired AND an open socket");
}

// ---- 8. the restart record (U-95/F-79) ---------------------------------
// Everything above was green while the car rebooted in a loop for a week. These
// rules exist because of that, and each one is a FACT that used to be a COMMENT.
// F-82: prose is invisible to every tool and gets certified by plausibility;
// gates fail. So the four claims below are now asserted, not described.

// (a) trigger_panic must be FALSE. With it true, a task-watchdog trip is routed
// through the panic handler and esp_reset_reason() reports ESP_RST_PANIC — so
// the one label that identifies a hung loop() can never be printed, and a
// watchdog starvation is indistinguishable from a crash. The owner was told to
// look for a label the firmware could not produce.
const panicOn = /trigger_panic\s*=\s*true/.test(inoCode);
if (panicOn) {
  fail("reset-truth", "the task watchdog still escalates to panic, so TASK_WDT is unreachable and a hung loop() reads as a crash");
} else if (!/trigger_panic\s*=\s*false/.test(inoCode)) {
  fail("reset-truth", "trigger_panic is neither true nor false - the watchdog escalation path is unstated");
} else {
  pass("reset-truth", "trigger_panic = false, so a hung loop() reports TASK_WDT rather than PANIC");
}

// (b) The record must be taken before anything else allocates, or the heap it
// reports is the heap after setup rather than the heap at boot — which is the
// number that matters when the car is dying of memory pressure.
const setupAt = inoCode.indexOf("void setup(");
const beginAt = inoCode.indexOf("crashLogBegin()");
// The first hardware bring-up allocates (OLED framebuffer, BT stack, motor PWM
// timers). Recording the heap after any of those describes steady state, not the
// boot that failed — and a car dying of memory pressure is precisely the case
// where the boot heap is the number worth keeping.
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

// (c) An UNKNOWN reset must count as a fault. Classifying "we do not know why it
// rebooted" as "probably fine" would hide precisely the case this record exists
// to surface, so the default branch has to be the suspicious one.
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

  // (d) Returning to idle must NOT be rate-limited. This rule exists because the
  // bug it catches was real and completely invisible: CrashLog.h promised that
  // the idle transition is always written, while the floor in crashLogPhase()
  // applied to every change. A parked car that crashed within 30 s of its last
  // real operation therefore kept naming that operation — so the record blamed
  // "http-root" for a crash that had nothing to do with the page, which is
  // precisely the confident-and-wrong answer this module was built to avoid.
  // Every other gate stayed green through it.
  //
  // So the rule is not "the header says so" — it is checked against the
  // CONDITION in the code, and it requires the exemption to be expressed as a
  // test on the phase being empty. That way the guarantee cannot be quietly
  // dropped and left behind in a comment.
  // Deliberately the RAW file, not clCode. The test being checked is
  // `next[0] != '\0'`, and code() blanks character literals — so against stripped
  // source the guard reads `next[0] != '  '` and can never match. That mistake is
  // the same one as counting a comment as evidence: the rule has to be written
  // against the text it will actually be handed. Stripping comments here buys
  // nothing anyway, since the floor line is code.
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

// (d) Both status-JSON buffers must be the SAME size. These two drifted apart
// once with no symptom except that the two copies of one JSON disagreed about
// which got truncated — and the truncation is silent, because snprintf just
// stops, so the client receives malformed JSON and reports "the page is not
// working properly".
const wsc = raw("WebServerComm.cpp");
const bufSizes = [...wsc.matchAll(/char\s+buf\[(\d+)\]\s*;\s*\n\s*buildStatusJson/gm)].map((m) => Number(m[1]));
if (bufSizes.length < 2) {
  fail("status-buffer", `found ${bufSizes.length} status-JSON buffer size(s) - expected the HTTP and WS copies`);
} else if (new Set(bufSizes).size !== 1) {
  fail("status-buffer", `the two status-JSON buffers differ (${bufSizes.join(" vs ")}) - one copy truncates differently`);
} else {
  // And it must actually be big enough: the fragments buildStatusJson writes
  // into it, plus room for the fixed literal fields between them.
  const frag = (re) => Number(wsc.match(re)?.[1] ?? 0);
  const need =
    frag(/char\s+networks\[(\d+)\]/) + frag(/char\s+scanJson\[(\d+)\]/) +
    frag(/char\s+caps\[(\d+)\]/) + frag(/char\s+statusWord\[(\d+)\]/) +
    frag(/char\s+boardId\[(\d+)\]/) + 400;  // ssid/ap/ip + every literal field
  if (bufSizes[0] < need) {
    fail("status-buffer", `buffer is ${bufSizes[0]} but the fragments alone need ~${need} - snprintf will truncate into invalid JSON`);
  } else {
    pass("status-buffer", `both copies are ${bufSizes[0]} bytes, above the ~${need} the fragments need`);
  }
}

// (e) The crash fields must be in the status JSON, and the page must gate them
// on crash_count > 0 rather than on the field merely existing — an absent field
// on an un-updated car has to read as "no data", never as a fabricated zero.
const stJson = body(wsc, "void WebServerComm::buildStatusJson") ?? "";
// Match the bare names, not `"name"`: these keys live inside ONE snprintf format
// string, so in the source they appear as \"name\" — a quote-delimited search
// finds nothing and the rule fails on a correct build. Verified by the mutation
// below, which renames a key and must then fail.
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

// ---- 9. the page-send loop cannot starve the watchdog (U-95/F-77) ------
// The bug this forbids is deterministic, not racy: serveRootBounded() spun on
// zero-byte writes for up to a 6 s budget against a 5 s watchdog, feeding it
// never. A client that stops reading resets the car EVERY time. The numbers were
// asserted by a comment; the loop was asserted by nothing, so the loop is what
// these two rules check. (wsh/budget/wdt are parsed once at section 7b.)

// The sketch's REAL watchdog config must be the same number the header declares.
// Two copies of the timeout, one used by the compiler for the static_assert and
// one handed to esp_task_wdt_reconfigure(), is exactly how the invariant rots:
// assert against the header, configure the hardware with a literal, and the
// static_assert silently guards a number nothing uses.
// The sketch's REAL watchdog config must NAME the constant, not merely agree with
// it. Comparing values is what I did first and it is a decorative check: a
// reverted `.timeout_ms = 5000` still equals the header's 5000, so the rule
// passed while the thing it exists to prevent — the sketch drifting back to its
// own literal — had happened. Coincidence is not a tie. Naming the constant makes
// the two numbers one value, so there is nothing left to drift.
const inoWdt = inoCode.match(/esp_task_wdt_config_t\s+\w+\s*=\s*\{([^}]*)\}/)?.[1] ?? "";
const inoTimeoutRaw = inoWdt.match(/timeout_ms\s*=\s*([^,}]+)/)?.[1]?.trim() ?? "";
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

// The LOOP itself. This analysis runs on COMMENT-STRIPPED source, and that is
// not tidiness. The first version counted `esp_task_wdt_reset()` occurrences in
// the raw file and cheerfully reported four feeds for a loop that has three,
// because the surrounding prose explains the fix and names the function. A rule
// that counts a COMMENT as evidence of code is F-82 pointed the other way: prose
// is what got certified by plausibility in the first place, and letting it
// satisfy a gate makes the gate agree with the story instead of the source.
const wscCode = code(wsc);
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
    // BOTH paths must feed, not just one. A single feed placed before the stall
    // branch satisfies "the stall path feeds" in the loosest reading while the
    // progress path - the one that runs 56 times for a normal page load, each
    // blocking up to HTTP_WRITE_TIMEOUT_MS - goes unmonitored. So the rule counts
    // the feeds in the loop rather than settling for one.
    fail("wdt-feed", `only ${feeds} watchdog feed(s) in the send loop - both the stall path and the progress path need one`);
  } else {
    pass("wdt-feed", `the zero-write branch feeds the watchdog AND waits a real gap; ${feeds} feeds across both paths`);
  }

  // The budget must be checked BEFORE the write, not after. Checking it after
  // means the loop can always begin one more write past the deadline - which is
  // up to a full HTTP_WRITE_TIMEOUT_MS of overrun, and on the stall path far
  // longer, because that iteration also burns the retry gap.
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

// ---- 10. the WebSocket pump cannot race the watchdog (U-95 1b/1c) -------
// ws.loop() contains a read the sketch cannot shorten: WebSockets 2.7.2 waits
// WEBSOCKETS_TCP_TIMEOUT = 5000 ms for a partial frame, inside the library's
// own translation unit (#define in this TU is a no-op; the owner's Arduino IDE
// passes no build flags - the same reasoning the armed-handshake rule states).
// broadcastStatus() contains a write that measured 5-8 s to a client that
// stopped draining TCP (U-77: 5 s of no-progress retries plus the core's 3 s
// socket send timeout). Both run on the loop task, both used to draw on
// whatever was left of the pass's window, and against a 5000 ms watchdog fed
// once per pass both were stopwatches racing the reset. Phase 1b/1c do three
// things, and each gets a rule - a budget that ties the watchdog is the reset,
// not the budget (the F-77 static_assert says the same about HTTP):
//
//   1. feed IMMEDIATELY before each untunable operation;
//   2. client-guard the broadcast (the plan's Phase 1c gate);
//   3. hold LOOP_TASK_WDT_TIMEOUT_MS strictly above the larger operation.
const handleBody = body(wscCode, "void WebServerComm::handle()") ?? "";
const broadcastBody = body(wscCode, "void WebServerComm::broadcastStatus") ?? "";

if (!handleBody) {
  fail("ws-loop-feed", "could not locate WebServerComm::handle() - check this rule still matches the code");
} else if (!/esp_task_wdt_reset\s*\(\s*\)\s*;\s*ws\.loop\s*\(\s*\)\s*;/.test(handleBody)) {
  fail("ws-loop-feed", "ws.loop() is not immediately preceded by a watchdog feed - the untunable partial-frame read (up to 5000 ms) spends whatever the rest of the pass left in the window, not a fresh one");
} else {
  pass("ws-loop-feed", "ws.loop() is fed immediately before it - the untunable read gets a fresh window");
}

// The guard must be INSIDE broadcastStatus and must run before the write -
// a guard anywhere else (or after it) is the no-client case the plan named,
// still walking the sockets. Comment-stripped view: the prose above the guard
// explains it and would otherwise satisfy the rule (F-82).
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

if (!broadcastBody) {
  fail("ws-broadcast-feed", "could not locate broadcastStatus() - check this rule still matches the code");
} else if (!/esp_task_wdt_reset\s*\(\s*\)\s*;\s*ws\.broadcastTXT/.test(broadcastBody)) {
  fail("ws-broadcast-feed", "ws.broadcastTXT() is not immediately preceded by a watchdog feed - a stalled-client write (5-8 s measured) can run after a ws.loop() that already spent 5000 ms of the same window");
} else {
  pass("ws-broadcast-feed", "the broadcast write is fed immediately before it");
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

// ---- 11. a router switch must be a real teardown (U-95 Phase 2) ---------
// U-90 shipped the claim this rule enforces as a COMMENT: the MODE step's
// prose said "WiFi.mode(WIFI_AP_STA) restarts the WiFi driver, the stale STA
// state is torn down". The installed core does not restart anything when the
// requested mode equals the current one (WiFiGeneric.cpp:602 early-returns),
// and the current mode IS WIFI_AP_STA after the first switch — so the
// teardown did not exist on any switch after the first, the stale
// association survived, and every gate in this file stayed green through it.
// Prose certified by plausibility, F-82 in one line.
//
// So the sequence is asserted, IN ORDER, against the comment-stripped body:
//   drop (mode WIFI_AP, always unequal -> always real work)
//   -> poll (getMode, confirms the drop took, 0 radio calls)
//   -> restore (startOwnAP while the STA is down — the one window
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
// auto-join adds more, and none of it is data this firmware reads back — its
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
// racing a driver that is still tearing the old association down — the plan's
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

// U-95 Phase 2C: the SCAN answer must be TRUE or BUSY — never an unconditional
// STARTED for a scan that was silently dropped — and startScan() must refuse
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
} else if (!/connectingPending_/.test(scanStartBody)) {
  fail("scan-honest", "startScan() no longer gates on connectingPending_ - a scan is started between WiFi.begin() and association, racing a driver that is mid-join");
} else if (!routerCmdBody) {
  fail("scan-honest", "could not locate handleRouterCommand() - check this rule still matches the code");
} else if (!/if\s*\(\s*startScan\s*\(\s*\)\s*\)/.test(routerCmdBody)) {
  fail("scan-honest", "the SCAN handler does not key the reply on startScan()'s result - ROUTERS;SCAN;STARTED is claimed unconditionally, so a dropped scan still reports success and the app waits forever");
} else if (!scanLiterals.includes('"ROUTERS;SCAN;BUSY"')) {
  fail("scan-honest", "the source never answers ROUTERS;SCAN;BUSY - a refusal has no honest form");
} else if (!scanLiterals.includes('"ROUTERS;SCAN;STARTED"')) {
  fail("scan-honest", "the source never answers ROUTERS;SCAN;STARTED - a scan that DID start has no acknowledgement");
} else {
  pass("scan-honest", "startScan() answers (false = BUSY, gated on in-flight + associating) and the handler keys the reply on it");
}

// U-95 Phase 2C: the collected list must be ranked by STRENGTH, deduped per
// RADIO (BSSID, not name), with exactly ONE cap applied AFTER the sort. The
// old shape capped before the loop and at insert, never sorted, and deduped
// on names — so it kept the first N results in radio-order, dropped strong
// networks, and silently merged two different routers broadcasting one SSID.
const collectBody = body(wscCode, "void WebServerComm::collectScanIfDone") ?? "";
const hRaw = raw("WebServerComm.h");
const sortAt = collectBody.search(/scanNetworks_\[\s*b\s*-\s*1\s*\]\s*\.\s*rssi\s*<\s*key\s*\.\s*rssi/);
const capAt = collectBody.search(/scanNetworks_\s*\.\s*resize\s*\(\s*SCAN_MAX_RESULTS/);
if (!collectBody) {
  fail("scan-quality", "could not locate collectScanIfDone() - check this rule still matches the code");
} else if (!/char\s+bssid\s*\[/.test(hRaw)) {
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
// tokens the page, the app and the relay would each grow their own — three
// implementations, three wrap rules, three drift stories. The rule checks the
// tokens reach cycleMode with the right SIGN (prev must not walk forward),
// that cycleMode wraps both ways (a missing negative wrap hands setMode() an
// out-of-range enum), and that the GPIO still uses the same walk.
// The token checks run on stripComments(mmHandle), NOT mmHandle: code() BLANKS
// string literals, so a strcmp against `"MODECYCLE"` reads as strcmp against
// nothing there — while raw would let the comment above the tokens (which
// NAMES them) pass for code. stripComments is the only view that is both.
const mmHandle = stripComments(body(raw("ModeManager.cpp"), "void ModeManager::handleCommand") ?? "");
const mmCycleBody = body(mmCode, "void ModeManager::cycleMode") ?? "";
const inoSrc = raw("Genum_4WD4M_CAR.ino");
if (!mmHandle) {
  fail("mode-cycle", "could not locate ModeManager::handleCommand - check this rule still matches the code");
} else if (!/strcmp\s*\(\s*cmdBuf\s*,\s*"MODECYCLE"\s*\)/.test(mmHandle)) {
  fail("mode-cycle", "no MODECYCLE token - the walk is GPIO-only, so page/app/relay must each invent their own and drift apart");
} else if (!/strcmp\s*\(\s*cmdBuf\s*,\s*"MODEPREV"\s*\)/.test(mmHandle)) {
  fail("mode-cycle", "no MODEPREV token - there is no way to walk backwards");
} else if (!/strcmp\s*\(\s*cmdBuf\s*,\s*"MODENEXT"\s*\)/.test(mmHandle)) {
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

// The one thing this gate cannot check is the thing that matters most: that the
// relay actually answers. That is the owner's bench (T-REL), not a laptop.
console.log("\nrelay-contract: source checks only - no socket was opened, no hello was sent.");
console.log("End-to-end is T-REL on the owner's bench; green here is not evidence it works.");

if (failures) {
  console.error(`\nrelay-contract: ${failures} FAILURE(S)`);
  process.exit(1);
}
console.log("relay-contract: all checks passed");
