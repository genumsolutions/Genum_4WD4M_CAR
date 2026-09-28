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
