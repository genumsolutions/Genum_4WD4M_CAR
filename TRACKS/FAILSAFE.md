# FAILSAFE — Genum_4WD4M_CAR (do-not-break invariants)

Inherited from the donor's hard-won rules. Any change that weakens one needs an explicit owner
ruling + a ledger note BEFORE the change.

1. **Non-blocking boot** — nothing at boot waits for a BT or Wi-Fi connection (donor F-16).
   AP up + STA join attempts never stall the drive loop.
2. **Failsafe = command silence → stop** — `FAILSAFE_TIMEOUT_MS` 2000 ms; `S` (stop) is ALWAYS
   accepted in every mode over every transport. Link loss (BT or WS) triggers the safe-stop path.
3. **One command grammar, both transports** — everything funnels through the same
   `handleCommand()`; no transport-specific command sets.
4. **No secrets in git** — `secrets.h` gitignored; `secrets.h.example` is the only template.
   CI compiles secrets-free.
5. **huge_app partition** — the default 1.3 MB app partition FAILS to fit; never change the
   FQBN away from `esp32:esp32:esp32:PartitionScheme=huge_app`.
6. **Unique on-air identity** — BT name and AP SSID must never collide with another fleet car
   (the physical remote pairs by name; identical names caused real past wrong-car pairings).
7. **192.168.4.x is forbidden for the AP** — phone-side subnet collision is a proven past bug
   (FIN-48); this car uses 192.168.245.1/24.
8. **CAPS truth** — the car never claims a capability it doesn't have; availability changes
   ship with CAPS + docs in the same commit.
9. **CAPS carrier is a `;`-delimited STRING, not a JSON object** (confirmed 2026-09-28 against
   `buildStatusJson`, which emits `"caps":"4WD4M:LIVE;ESP_SER:CS;ESP_CLI:CS;…"`). The app had been
   parsing only an object, so a green test suite coexisted with a completely missing mode table
   over Wi-Fi. **Any consumer of `caps` must parse the string form, and must be tested with a
   literal captured payload, never a hand-written shape.** Changing this field's shape is a
   breaking protocol change: it needs an app change in the same commit (invariant 8's spirit).
10. **`setMode()` accepts and PERSISTS any registry token, including `CS`** (deliberate, see the
    comment at `ModeManager.cpp:239-244`). So a client that offers a Coming-Soon mode can strand the
    car in a no-drive stub that **survives a power cycle**.
    - **Recovery is a live command, not a reboot:** `MODE_4WD4M`, or
      `http://192.168.245.1/mode?val=4WD4M`. The web layer is always-on, so this works even from a
      stub.
    - **A client must not offer a mode it has not confirmed LIVE.** When availability is unknown,
      treat it as not-live.
11. **There is no ESTOP in this firmware** — zero `ESTOP` handling in any source file, while the app
    still carries the `ESTOP_LINE` protocol constant. `T7` in `DEVICE-TESTS.md` still asks for an
    ESTOP test and is therefore **stale**; safe stop is `S` / `SPD0` / disconnect, and
    `FAILSAFE_TIMEOUT_MS` (2000 ms, invariant 2) is the real backstop. Do not re-add an e-stop UI
    without an owner ruling on the firmware side first.
12. **The brownout threshold is NOT settable from a sketch (U-96, 2026-10-06).** Core 3.3.x moved
    brownout level to a Kconfig option compiled into the prebuilt `esp32-libs`. There is no
    `DET_LVL` register in any public header, and `esp_brownout_init()` takes no level argument.
    Two wrong symbols were committed in a row — `RTC_CNTL_BROWN_OUT_DET_LVL_SEL_REG`, then
    `CONFIG_BROWNOUT_DET_LVL_SEL_REG` (a Kconfig name misread as a register) — and both broke the
    build. **Do not reintroduce a register write for this.**
    The old comment was also inverted: IDF runs SEL_0 = *lowest* (~2.43V) to SEL_7 = *highest*
    (~2.80V), so "write 0 to raise it" would have *lowered* it. The mitigation is already in
    effect — the core ships level 0, the most tolerant setting. An `#error` guard in `setup()`
    pins that; if a core bump trips it, re-read the brownout table before touching anything.
13. **The syntax gate MUST compile the `.ino`.** It used to filter `-Filter *.cpp` and reported
    `PASS: 9/9` while the sketch itself did not build — which is exactly how two broken brownout
    symbols shipped through a green gate. `tools/check-firmware-syntax.ps1` now sweeps `*.cpp`
    **and** `*.ino` (with `-x c++` and the sketch dir on the include path) and refuses to report
    a pass if it finds no `.ino`. A gate that compiles fewer files than it used to is worse than
    no gate: it turns an uncompiled file into a trusted one. **If you ever narrow that sweep,
    treat it as a build break.**
    Also note the core-version split: the gate runs **3.3.12** locally while
    `.github/workflows/arduino-ci.yml` pins **3.3.11**. They are not proving the same thing.
14. **A green gate is not a build.** `tools/check-firmware-syntax.ps1` proves the sources PARSE.
    The Arduino CI job is the real build. Neither proves behaviour, and neither substitutes for
    flashing. F-61 stands.
