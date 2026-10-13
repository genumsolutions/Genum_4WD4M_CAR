# RECORDS INVENTORY — car NVS · app storage · Supabase

Owner ask (2026-10-13): *"clean up the old records that is not needed from the car and the
user profile, we will start these too from the fresh and keep proper timestamp of those all
too so cleaning the database and app and car will be easy."*

This is the single reference for **what is stored, where, whether it is timestamped, and what
the Phase 7 fresh-start wipes vs keeps**. Scope of the wipe (owner decision): **this car
(board `1FB608`) + the owner's account**, NOT the shared `device_models` registry and NOT
other fleet cars.

Timestamp rule (owner decision): **the app sends a real Unix epoch on connect**; the car stamps
each record with it. DB tables already carry `timestamptz` columns.

---

## 1. Car NVS — namespace `botcfg` (Preferences)

No timestamps today. Phase 7 adds an app-epoch stamp per record + a wipe marker, and a
factory-reset that clears the whole namespace.

| Key | Holds | Action on fresh start |
|-----|-------|-----------------------|
| `wifi_ssid`, `wifi_pass` | active STA pair | **wipe** |
| `last_ok_ssid`, `last_ok_pass` | last-good STA pair | **wipe** |
| `rnet0s`..`rnet5s`, `rnet0p`..`rnet5p` | 6 saved routers (SSID/PASS) | **wipe** |
| `t_wifi` | Wi-Fi transport toggle | **wipe → default ON** |
| `t_bt` | Bluetooth transport toggle | **wipe → default ON** |
| `boot_autojoin` | join last router at power-up | **wipe → default ON** |
| `mode` | last mode id | **wipe → default `4WD4M`** |
| `spd` | last speed | **wipe → default** |
| `relay_token` | cloud-relay token (if ever provisioned) | **wipe** |
| `fw_mark` | firmware version that last migrated NVS | **wipe → rewritten by the reset** |
| `bcount`, `ccount` | total boots / unexpected boots | **reset to 0** |
| `lreason`, `lheap`, `lphase` | last reset reason / heap / phase | **wipe** |
| `last_transport` | legacy one-active-link residue | **wipe (already purged on reflash)** |
| `ap_psave` | legacy power-save residue | **wipe (already purged on reflash)** |
| `allow_parallel` | legacy parallel residue | **wipe (already purged on reflash)** |

Existing reset machinery to extend (not invent): `CrashLog.cpp` keys + `WebServerComm.cpp`
`wipeRoutersOnFreshFirmware()` (`fw_mark` migration) + the `prefs.remove(...)` calls at
`WebServerComm.cpp:102-109 / 276-277 / 316-333 / 536-539 / 680-681 / 704-712`.

## 2. App local storage — AsyncStorage

**Car-related (Phase 7 wipes + timestamps):**

| Key | Service | Purpose |
|-----|---------|---------|
| `genum-car-profiles-v1` | `carProfileService.ts` | cached car profiles |
| `genum.device.<address>` | `deviceProfileRegistryService.ts` / `types.ts` | per-car DevicePrefs (prefix) |
| `genum.device.index` | `deviceProfileRegistryService.ts` | local index of stored DevicePrefs keys |
| `genum.lastDevice` | `deviceProfileRegistryService.ts` + `useControlHub.ts` | last-used car |
| `genum.crashReports.queue` | `crashReportService.ts` | offline crash-report queue |
| `genum.crashReports.last` | `crashReportService.ts` | last crash-sync marker |
| `genum.activeTransport.v1` | `linkManager.ts` | last active transport |
| `genum.app.logs.ring` | `appLogService.ts` | local warn/error ring |
| `genum.app.logs.pending` | `appLogService.ts` | pending log flush queue |

**Not car-related (KEEP):** `genum-theme-mode`, `genum-user-settings`, `genum_products_v2`,
`genum-recently-viewed`, `genum-robot-settings`, `genum_native_cart_v1`, `genum-native-session`,
`genum-biometrics-admin`, `genum-haptics`, `genum.remote.fabSlot`, `genum.remote.lastCategory`,
`genum.remote.fabDismissed`, `genum.apk_alt_media`.

None carry an `updatedAt` today — Phase 7 wraps the car-related ones with a timestamp so a
stale record is provable.

## 3. Supabase — project `bkylfnlybtsujwzropru`

All rows already carry timestamps **except** where noted; the gap is **retention** (crash reports
and app logs grow forever).

| Table | Timestamp columns | Phase 7 action |
|-------|-------------------|----------------|
| `car_profiles` | `created_at`, `updated_at` | **wipe rows for this car + owner account** |
| `device_crash_reports` | `reported_at`, `synced_at` | **wipe this board + owner** (then retention) |
| `app_logs` | `created_at` | **wipe** (then retention) |
| `devices` | `created_at`, `updated_at` | **wipe row for board `1FB608`** |
| `user_devices` | `created_at`, `updated_at` | **wipe this car's rows for the owner** |
| `device_relay_tokens` | `created_at`, `updated_at` | wipe this car's row if present |
| `device_models` | `created_at`, `updated_at` | **KEEP — shared canonical registry** |
| `robo_car_modes_flags`, `project_components`, `robot_user_settings` | (verify) | review; not car-instance data |

Migrations live in `genumsolutions-website\supabase\migrations\`. The fresh-start DELETE + any
retention policy ship as a new additive migration (no table dropped/renamed) in the website repo.

## 4. Phase 7 deliverables (fresh start + timestamps)

1. **Car:** `FACTORY;RESET` (or equivalent) that wipes the whole `botcfg` namespace + a
   `written_epoch` stamp per record set from the app-provided epoch; reset counters.
2. **App:** a **Reset car data** action that clears the car-related AsyncStorage keys above, and
   writes `updatedAt` on the remaining car records.
3. **DB:** an additive migration that (a) deletes this car + owner's car-related rows, and
   (b) adds retention for `device_crash_reports` + `app_logs` (e.g. older than N days).
