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

// The one thing this gate cannot check is the thing that matters most: that the
// relay actually answers. That is the owner's bench (T-REL), not a laptop.
console.log("\nrelay-contract: source checks only - no socket was opened, no hello was sent.");
console.log("End-to-end is T-REL on the owner's bench; green here is not evidence it works.");

if (failures) {
  console.error(`\nrelay-contract: ${failures} FAILURE(S)`);
  process.exit(1);
}
console.log("relay-contract: all checks passed");
