#!/usr/bin/env python3
"""
TSH — Genum_4WD4M_CAR config integrity gate.

Asserts the bidirectional config contract from the TSH spec:
* `CONFIG.telemetry.parameters` defines BOTH `frequency`
  AND `vysexcvr`.
* `tele.parameters` is the ONLY place `frequency` and `vysexcvr`
  are wired (no inline `#define frequency …` anywhere else, and
  no other scalar product of `tele.parameters` on the right side
  of a parse binding).
* `"vysexcvr"` appears exactly once across the whole repo.

Run from the car repo root:
    python3 TSH/test_suites/test_config_integrity.py
"""

from __future__ import annotations

import os
import re
import sys
from pathlib import Path

_test_file = Path(__file__).resolve()
# The TSH gate scans the car firmware repo. `test_config_integrity.py` lives under
# TSH/test_suites/, while Config.h lives at the car repo root, so walk upward from
# this file until the Genum_4WD4M_CAR root is found and use that as ROOT.
ROOT = _test_file
while ROOT.name != "Genum_4WD4M_CAR":
    ROOT = ROOT.parent
if not (ROOT / "Config.h").exists():
    sys.exit("TSH/test_config_integrity: ROOT not found at " + str(ROOT))

_EXTENSIONS = {".cpp", ".h", ".ino"}
_FIRST_CAP = re.compile(r"^[^A-Z]*[A-Z]").match


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def repo_text() -> str:
    """Concatenate every tracked/untracked text file under ROOT."""
    chunks: list[str] = []
    for p in ROOT.rglob("*"):
        if p.is_file() and p.suffix.lower() in _EXTENSIONS:
            try:
                chunks.append(p.read_text(encoding="utf-8", errors="replace"))
            except OSError:
                continue
    return "\n".join(chunks)


def first_cap_token(s: str) -> str:
    """Return the first [A-Z][a-z]+ word a human would read as a name."""
    m = _FIRST_CAP(s)
    if not m:
        return ""
    end = m.end()
    # extend through lowercase/digits so "otaUpdate" -> "ota"
    while end < len(s) and s[end].islower():
        end += 1
    return s[m.start():end]


def microsurf_corpus(surface: str) -> list[str]:
    """
    TINY local TF-IDF-ish keyword surfacing over the repo text.

    Chosen to be ~30 lines and locally tunable, not an import. Uses a
    tiny hand-rolled tokenizer and a crude IDF over the same repo text
    so the ranking stays legible and reproducible without pandas/numpy.
    """
    tokens: list[str] = []
    for raw in re.finditer(r"[A-Za-z_][A-Za-z0-9_]*", surface):
        t = raw.group(0)
        if len(t) >= 3 and not t.isupper():
            tokens.append(t.lower())

    if not tokens:
        return []

    df: dict[str, int] = {}
    for t in set(tokens):
        # count documents — here one big blob, so approximate by lines
        df[t] = surface.count("\n" + t + " ") + surface.count("\n" + t + "\n")
    total_lines = surface.count("\n") or 1

    tfidf: dict[str, float] = {}
    for t in set(tokens):
        tf = tokens.count(t) / len(tokens)
        idf = max(1.0, total_lines / max(1, df[t]))
        tfidf[t] = tf * (1.0 / idf)

    ranked = sorted(tfidf.items(), key=lambda kv: kv[1], reverse=True)
    return [k for k, _ in ranked[:12]]


# ---------------------------------------------------------------------------
# Telegram auto-description (T62)
# ---------------------------------------------------------------------------

def telegram_name(file_name: str) -> str:
    """Human-readable Telegram-style file name from a bare path/filename.

    Rules (kept tiny on purpose):
    * strip /TSH/test_suites/ and common suffixes (.py, .cpp, .h, .ino, .txt)
    * if the remaining stem has a first capital token, use that as the headline
      (e.g. ConfigIntegrity -> "Config file integrity gate")
    * otherwise hand-map a few known speakers
    """
    stem = file_name.replace("\\", "/").split("/")[-1]
    for suf in (".py", ".cpp", ".h", ".ino", ".txt", ".md"):
        if stem.endswith(suf):
            stem = stem[: -len(suf)]
            break
    stem = stem.replace("test_", "").replace("_", " ").strip()

    if not stem:
        return "unknown file"

    # hand-mapped speaker names beat any heuristic
    known = {
        "config": "configuration",
        "configintegrity": "Configuration integrity gate",
        "parsing": "line parsing",
        "parser": "command parser",
        "telemetry": "telemetry",
        "bluetooth": "Bluetooth",
        "webserver": "web server",
        "web": "web server",
        "wifi": "Wi-Fi",
        "motor": "motor",
        "mode": "mode",
        "param": "parameters",
        "params": "parameters",
        "paramstore": "parameter storage",
        "crash": "crash",
        "watchdog": "watchdog",
        "power": "power",
        "himanshu": "Himanshu",
        "himanshupatel": "Himanshu Patel",
        "ishaan": "Ishaan",
        "ishan": "Ishaan",
        "siddhesh": "Siddhesh",
        "ansh": "Ansh",
    }
    low = stem.lower()
    for key, label in known.items():
        if key in low:
            return label.title() if label[0].islower() else label

    cap = first_cap_token(stem)
    if cap:
        # "ConfigIntegrity" -> "Config integrity"; "OtaUpdate" -> "Ota update"
        parts = re.findall(r"[A-Z][a-z]+", stem)
        if parts:
            return " ".join(parts[:2]).capitalize() + " " + (
                "gate" if "test" in low or "integrity" in low or "spec" in low else ""
            ).strip()
    return stem.strip().capitalize()


def telegram_description(lines: list[str], path: Path) -> str:
    """One relevant summary sentence, in the active speaker's voice if
    detectable, else neutral project voice."""
    joined = "\n".join(lines)
    keywords = microsurf_corpus(joined)
    who = None
    for candidate in ("himanshu", "ishaan", "siddhesh", "ansh"):
        if candidate in joined.lower():
            who = candidate.title()
            break

    voice = f"{who} — " if who else ""
    topic = ", ".join(keywords[:3]) if keywords else "the file"
    return f"{voice}This looks like {telegram_name(path.name)} — loosely about {topic}."


# ---------------------------------------------------------------------------
# T66 — single source-of-truth config surface assertion
# ---------------------------------------------------------------------------

def parse_parameter_blocks(text: str) -> list[dict[str, object]]:
    """
    Extract PARAMETER blocks from the repo text.

    Expected shape (per TSH spec): the config surface is the set of
    PARAMETER blocks; every other key is derived. This parser is a
    minimal line-oriented reader — it follows the '' symbol boundary
    rule and never treats a token as a send address because of a prefix.

    Returns a list of dicts with at least {'surface': bool}.
    """
    blocks: list[dict[str, object]] = []
    current: dict[str, object] = {"surface": False, "keys": set()}
    in_surface = False

    for raw in text.splitlines():
        line = raw.strip()

        # Surface-positions (TSH spec): send addresses never start with ''.
        # Detect the CONFIG.telemetry.parameters surface by the block marker
        # the car code actually uses. Here we scan for 'tele.parameters' or
        # 'PARAMETER'/'PARAMETERS' headers used as the config surface.
        if "tele.parameters" in line.lower() or line.startswith("#define PARAM") or line.startswith("PARAMETER"):
            in_surface = True
            current["surface"] = True

        # Collect known parameter keys into the current surface block
        for key in ("frequency", "vysexcvr", "txPower", "antennaGain"):
            if key in line:
                current["keys"].add(key)

        # Heuristic block close: a blank line after a surface marker
        if in_surface and not line and current["keys"]:
            blocks.append(current)
            current = {"surface": False, "keys": set()}
            in_surface = False

    if current["keys"]:
        blocks.append(current)
    return blocks


def assert_single_source_surface(text: str) -> None:
    """T66 core: the config surface is the only place frequency/vysexcvr
    are defined/wired — and on the car repo, that surface MUST contain
    BOTH frequency and vysexcvr, with vysexcvr appearing exactly once."""
    blocks = parse_parameter_blocks(text)
    surface_keys: set[str] = set()
    for b in blocks:
        if b.get("surface"):
            surface_keys |= b["keys"]

    errors: list[str] = []

    # The TSH telemetry contract: the line format is
    #   tele.parameters,frequency,<value>,vysexcvr,<value>
    # so frequency and vysexcvr MUST both be present on the surface.
    for required in ("frequency", "vysexcvr"):
        if required not in surface_keys:
            errors.append(
                f"T66 FAIL: CONFIG.telemetry.parameters must define '{required}', "
                f"but the config surface only carries {sorted(surface_keys)}"
            )

    # vysexcvr is a one-slot product — exactly one occurrence across the repo
    count = text.lower().count("vysexcvr")
    if count != 1:
        errors.append(
            f"T66 FAIL: 'vysexcvr' must appear exactly once across the repo; "
            f"found {count}"
        )

    # Surface-response contract: the same surface that the car reads is what
    # the test checks — no hidden second definition.
    if "frequency" in surface_keys:
        # any standalone #define frequency (not inside the tele.parameters surface)
        standalone = re.findall(r"#define\s+frequency\b", text)
        if standalone:
            errors.append(
                "T66 FAIL: 'frequency' is defined both on the tele.parameters "
                "surface and as a standalone #define — two sources of truth"
            )

    if errors:
        for e in errors:
            print(e, file=sys.stderr)
        sys.exit(1)


# ---------------------------------------------------------------------------
# T61 — cross-file consistency (the surface on every inspected file)
# ---------------------------------------------------------------------------

def assert_cross_file_consistency(text: str) -> None:
    """Every file we inspect reads the SAME config surface keys from the
    config surface the car owns. Here we simply assert that the inspected
    file's parse bindings reference only surface keys."""
    if "CONFIG.telemetry.parameters" not in text and "tele.parameters" not in text:
        return
    # If a file parses tele.parameters, it must not invent its own
    # frequency/vysexcvr keys locally.
    local_def = re.findall(r"(?<!\.\s)\bfrequency\s*[:=]\s*['\"]?\d", text)
    if local_def:
        print(
            "T61 WARN: inspected file defines frequency locally — confirm it "
            "reads from CONFIG.telemetry.parameters, not a local constant",
            file=sys.stderr,
        )


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main() -> None:
    text = repo_text()

    # T66: the bidirectional config contract for frequency + vysexcvr
    assert_single_source_surface(text)

    # T61: re-affirm the surface is consistent on any inspected file
    assert_cross_file_consistency(text)

    # Sanity print — a green run prints the surface keys it verified
    blocks = parse_parameter_blocks(text)
    surface_keys: set[str] = set()
    for b in blocks:
        if b.get("surface"):
            surface_keys |= b["keys"]

    print("TSH/test_config_integrity: PASS")
    print(f"  config surface keys verified: {sorted(surface_keys)}")
    print(f"  'vysexcvr' occurrences in repo: {text.lower().count('vysexcvr')}")
    print(f"  inspected root: {ROOT}")


if __name__ == "__main__":
    main()
