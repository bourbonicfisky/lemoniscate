# Backporting 0.1.8 to the PPC (Tiger / Leopard) Branch

This guide covers porting the 0.1.8 modern release — bot protection
(slow-loris fix + auto-ban) and the chat-history GUI surface — to the
PPC branch targeting Mac OS X 10.4 PPC.

The server-side patches map 1:1 from `modern` (no Tiger concerns —
they're pure POSIX/C). The GUI work is where Tiger compatibility matters,
because the chat-history-gui change uses several modern Cocoa idioms
that need polyfills.

## What to cherry-pick (informationally)

The 0.1.8 release on `modern` lives in three commits:

1. **`9755822`** — *bot-protection: file-transfer read timeout +
   auto-ban toggle + GUI*
   - `SO_RCVTIMEO` on file-transfer accept (fixes the 8.5-day hang
     on hotline.semihosted.xyz from 2026-04-19)
   - `hl_rate_limiter_t` extended with violation-tracking fields
   - `drop_if_banned` helper at all four accept sites
   - `AutoBanEnabled` config field, default ON
   - GUI auto-ban checkbox + Bans-tab Refresh button
2. **`837ecd6`** — *chat-history-gui: surface 9 ChatHistory* keys +
   generate-key + 0.1.8*
   - GUI encryption-at-rest UI (key path + Choose + one-click Generate)
   - GUI master-toggle grey-out
   - GUI folded "Advanced" disclosure for rate-limit knobs
   - Version bump 0.1.7 → 0.1.8 in `src/main.c` and `src/hotline/hope.c`
   - Operator docs subsection on rate-limit semantics
3. **`2cbfeb2`** — *chat-history-gui: bump Info.plist to 0.1.8*

Direct cherry-pick is impractical because PPC keeps its codebase under
`ppc/` and has its own GUI fork with Tiger compatibility shims. Use
these commits as the diff reference rather than `git cherry-pick` source.

## Tiger-specific Cocoa shims to add

The chat-history GUI introduces three Cocoa idioms that don't exist on
Tiger. Add to `ppc/src/gui/TigerCompat.h`:

```c
/* NSData write options renamed in 10.6 */
#ifndef NSDataWritingAtomic
#define NSDataWritingAtomic NSAtomicWrite
#endif

/* arc4random_buf was added in 10.7. arc4random() (no _buf) has been on
 * BSD/Darwin since the dawn of time. Loop it for the buffer variant. */
#if MAC_OS_X_VERSION_MAX_ALLOWED < 1070
#include <stdlib.h>
#include <string.h>
static inline void arc4random_buf(void *buf, size_t n)
{
    uint8_t *p = (uint8_t *)buf;
    while (n >= 4) {
        uint32_t r = arc4random();
        memcpy(p, &r, 4);
        p += 4;
        n -= 4;
    }
    if (n > 0) {
        uint32_t r = arc4random();
        memcpy(p, &r, n);
    }
}
#endif

/* Verify presence in your Tiger SDK before adding — exists on 10.5+. */
#ifndef NSAlertSecondButtonReturn
#define NSAlertSecondButtonReturn 1001
#endif
```

## Cocoa idioms requiring runtime checks

`NSSavePanel`'s `setDirectoryURL:` is 10.6+. `setDirectory:` works
through Tiger but is deprecated in 10.6. Use a runtime check rather
than `#if`:

```objc
NSSavePanel *sp = [NSSavePanel savePanel];
if ([sp respondsToSelector:@selector(setDirectoryURL:)]) {
    [sp setDirectoryURL:[NSURL fileURLWithPath:_configDir isDirectory:YES]];
} else {
    [sp setDirectory:_configDir];
}
```

`[NSColor secondaryLabelColor]` is 10.10+. Replace with `[NSColor
grayColor]` — cosmetic difference only.

`NSBezelStyleInline` is 10.7+. The `Advanced ▶` toggle uses it on
modern; on Tiger fall back to `NSBezelStyleSmallSquare` (already
shimmed in TigerCompat.h).

## Server changes (no Tiger concerns)

These map line-for-line from modern:

- `ppc/src/hotline/server.c` — `handle_file_transfer_connection` gains
  the `SO_RCVTIMEO`/`SO_SNDTIMEO` block at the top.
- `ppc/include/hotline/server.h` — `hl_rate_limiter_t` extension +
  `HL_AUTOBAN_VIOLATION_THRESHOLD` (10) + `HL_AUTOBAN_WINDOW_SECONDS` (300).
- `ppc/src/hotline/server.c` — `hl_server_rate_limit_check` extension
  for sliding-window violation tracking + `drop_if_banned` helper +
  the four accept-site call sites.
- `ppc/include/hotline/config.h` — `int auto_ban_enabled;` field.
- `ppc/src/hotline/config.c` — default `cfg->auto_ban_enabled = 1;`.
- `ppc/src/mobius/config_loader.c` — YAML key `AutoBanEnabled`.

## Version strings

Bump `0.1.7` → `0.1.8` in:
- `ppc/man/lemoniscate.1` (line 3)
- `ppc/resources/Info.plist` (`CFBundleVersion` + `CFBundleShortVersionString`)
- `ppc/src/main.c` (`--version` printf)
- `ppc/src/hotline/hope.c` (`HOPE_APP_STRING`)

## Build and smoke test on the G4

1. SSH to the PowerMac G4 (per `powerpc-development` skill).
2. From the SMB-mounted repo root:
   ```bash
   make clean
   make app
   ```
3. `./lemoniscate --version` should print `lemoniscate 0.1.8`.
4. Launch `Lemoniscate.app`. Open Settings.
5. **Chat History section**: verify the encryption key path field +
   Choose… + Generate Key… render. Click Generate Key…, verify a
   32-byte file at `<configdir>/chat_history.key` with `0600` perms.
6. Verify master toggle greys/un-greys all chat-history widgets.
7. Verify Advanced ▶ disclosure folds/unfolds the rate-limit fields.
8. Save settings. Reopen. Verify all 9 `ChatHistory*` keys persist.
9. **Server smoke**: from a Hotline client, send a public chat message;
   verify `<configdir>/ChatHistory/channel-0.jsonl` is created.
10. **Auto-ban smoke**: from a second host, open and close 11+ TCP
    connections to port 5500 within 5 minutes; verify the IP appears
    in `Banlist.yaml` and a log line `Auto-banned <ip> after N
    rate-limit violations within 300s` was written.

## Files added on PPC

- `ppc/openspec/changes/port-0-1-8/{proposal,design,tasks}.md`
- `ppc/openspec/changes/port-0-1-8/specs/port-0-1-8/spec.md`
- `ppc/src/gui/AppController+ChatHistoryActions.inc` (new file)

## Files NOT touched (PPC scope only)

- Anything under `src/` (modern branch)
- `Lemoniscate.app` resources (modern bundle)
- `docs/PPC_BACKPORT_CHAT_HISTORY.md` — superseded once 0.1.8 lands
  but kept for historical reference.
