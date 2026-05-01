# Port 0.1.8 to PPC (bot-protection + chat-history GUI)

## Why

The `modern` branch shipped 0.1.8 on 2026-04-30 with three tightly-related
deliveries that have not yet been ported to the PPC (Mac OS X 10.4 Tiger /
10.5 Leopard) line:

1. **Slow-loris fix on file-transfer accept** — a probe sent 2 of 16 header
   bytes to port 5501 on 2026-04-19 and pinned the entire `modern` server
   for 8.5 days because `handle_file_transfer_connection` ran on the main
   accept-loop thread with no socket timeout. Same code path exists on PPC.
2. **Auto-ban escalation** — repeat per-IP rate-limit hits now promote the
   IP to the persistent banlist; a `drop_if_banned` helper rejects banned
   IPs at `accept()` before any read or TLS handshake. Gated on a new
   `AutoBanEnabled` config field (default on).
3. **Chat History GUI surface** — operators editing settings via the Cocoa
   GUI no longer silently strip `ChatHistory*` keys from `config.yaml` on
   save. Adds an encryption-at-rest UI block with one-click `Generate Key…`,
   a master-toggle grey-out, and a folded "Advanced" disclosure for the
   rate-limit knobs.

The PPC line is a supported deployment target (see the parent
`flip-main-to-modern` archived change) and historically backports each
modern release on its own cadence. `chat-history` was backported via
`docs/PPC_BACKPORT_CHAT_HISTORY.md`; this change follows that precedent.

**References**:
- Modern commits: `9755822` (bot-protection), `837ecd6` (chat-history-gui),
  `2cbfeb2` (Info.plist bump)
- Prior backport doc: `docs/PPC_BACKPORT_CHAT_HISTORY.md`
- Tiger compatibility shims: `ppc/src/gui/TigerCompat.h`

## What Changes

### Server (C, 10.4-compatible)

- Add 30 s `SO_RCVTIMEO`/`SO_SNDTIMEO` to `handle_file_transfer_connection`
  in `ppc/src/hotline/server.c` immediately after the connection wrapper
  is created. Identical to the modern patch — no Tiger-specific concerns;
  `setsockopt`+`SO_RCVTIMEO` has been on BSD/Darwin since forever.
- Extend `hl_rate_limiter_t` in `ppc/include/hotline/server.h` with
  `violations` / `first_violation` / `auto_banned` fields. Add
  `HL_AUTOBAN_VIOLATION_THRESHOLD` (10) and `HL_AUTOBAN_WINDOW_SECONDS`
  (300) constants. Pure C, no Tiger concerns.
- Wire auto-ban escalation into `hl_server_rate_limit_check`: on rate-limit
  hit, increment violations within a sliding window; on threshold, call
  `srv->ban_list->vt->add(ip)` and log `"Auto-banned %s after N
  rate-limit violations within Ns"`. Gated on `srv->config.auto_ban_enabled`.
- Add `drop_if_banned` static helper above `hl_server_listen_and_serve`
  and call it after `accept()` on all four ports (5500, 5501, 5600, 5601).
- Add `auto_ban_enabled` field to `hl_config_t`, default `1` in
  `hl_config_init`. Wire YAML key `AutoBanEnabled` in
  `ppc/src/mobius/config_loader.c`.

### GUI (Cocoa, Tiger-compatible)

The PPC GUI already has the 6 basic chat-history widgets from the prior
backport. This change adds:

- **Auto-ban checkbox** in the Security/HOPE disclosure section,
  persisted as `AutoBanEnabled`, default ON.
- **Refresh button** on the Bans subview that calls `loadBanListData`
  so auto-bans written by the running server become visible without
  restarting the GUI.
- **Encryption-at-rest UI block** in the existing Chat History disclosure
  section: key path field + `Choose…` button + `Generate Key…` button
  (writes 32 random bytes via `arc4random()`-loop polyfill, `0600`
  permissions, refuses to overwrite existing keys without confirmation).
- **Master-toggle grey-out**: `updateChatHistoryWidgetEnablement` helper,
  called from `loadSettings` and from the master checkbox's action.
- **Advanced fold** for `ChatHistoryRateCapacity` and
  `ChatHistoryRateRefillPerSec`, hidden by default behind a small
  in-section disclosure. Falls back to plain visible fields if
  `NSBezelStyleInline` is unavailable on Tiger (see design.md).
- New file `ppc/src/gui/AppController+ChatHistoryActions.inc` for the
  four new action methods, mirroring the modern layout.
- Extend `ppc/src/gui/TigerCompat.h` with three new shims:
  `NSDataWritingAtomic`, `NSAlertSecondButtonReturn` (if missing),
  and an `arc4random_buf`-shaped polyfill that loops over
  `arc4random()`.

### Version strings

Bump `0.1.7` → `0.1.8` in:

- `ppc/man/lemoniscate.1`
- `ppc/resources/Info.plist` (`CFBundleVersion` + `CFBundleShortVersionString`)
- `ppc/src/main.c` (`--version` printf)
- `ppc/src/hotline/hope.c` (`HOPE_APP_STRING`)

### Operator docs

Backport the "Rate limits: per-IP connection vs. per-connection chat"
subsection into `ppc/docs/SERVER.md` (or wherever PPC operator docs
live), tweaked for PPC's actual config-key plumbing if it diverges.

## Impact

- **Affected specs**: new `port-0-1-8` capability spec captures the PPC
  server-behaviour and GUI-behaviour contracts that must hold on Tiger.
- **Affected code**: `ppc/include/hotline/{server.h,config.h}`,
  `ppc/src/hotline/{server.c,config.c,hope.c}`,
  `ppc/src/mobius/config_loader.c`, `ppc/src/main.c`, all of `ppc/src/gui/`,
  `ppc/man/lemoniscate.1`, `ppc/resources/Info.plist`,
  `ppc/src/gui/TigerCompat.h`.
- **Backwards compatibility**: a 0.1.7 PPC config.yaml continues to load
  unchanged; new `AutoBanEnabled` key defaults to ON when absent. No
  protocol-level changes; the wire format is identical to 0.1.7 on PPC.
- **Build-environment requirement**: PPC build happens on the PowerMac G4
  (10.4 Tiger / 10.5 Leopard, gcc-4.0 + 10.4u SDK). See the
  `powerpc-development` skill for SSH-based remote-build workflow. The
  prep work in this change can land in the repo today; verification needs
  the G4.
