# Design — Port 0.1.8 to PPC

The server-side patches map 1:1 from `modern` (no Tiger concerns — they're
pure POSIX/C). The GUI port is where the work concentrates because the
chat-history-gui change uses several modern Cocoa idioms that need polyfills.

## 1. arc4random_buf is not on Tiger

**Decision**: Add a shim in `ppc/src/gui/TigerCompat.h`:

```c
#if MAC_OS_X_VERSION_MAX_ALLOWED < 1070
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
```

`arc4random()` (no `_buf`) is present on Tiger via `<stdlib.h>` and uses
the same underlying RC4 keystream. Output quality is identical for the
purpose of generating a chat-history encryption key.

**Why**: avoids touching the action implementation — it can call
`arc4random_buf(key, 32)` exactly as on `modern`.

**Pitfall**: `arc4random()` was on every BSD by the time Tiger shipped,
but if the build SDK is the 10.4u SDK targeted at i386 the prototype may
need `extern uint32_t arc4random(void);` if `<stdlib.h>` doesn't surface
it. Verified at port time on the G4.

## 2. NSDataWritingAtomic / NSAtomicWrite

**Decision**: Add to TigerCompat.h:

```c
#ifndef NSDataWritingAtomic
#define NSDataWritingAtomic NSAtomicWrite
#endif
```

`NSAtomicWrite` is the Tiger-era constant; `NSDataWritingAtomic` is the
10.6+ rename. Same underlying value (`1UL << 0`).

**Why**: keeps `[data writeToFile:options:NSDataWritingAtomic error:]`
identical between branches.

## 3. NSAlertSecondButtonReturn

**Decision**: Verify presence at port time. If absent, add to
TigerCompat.h:

```c
#ifndef NSAlertSecondButtonReturn
#define NSAlertSecondButtonReturn 1001
#endif
```

`addButtonWithTitle:` returns 1000 for the first button added, 1001 for
the second, etc. The named constants were formalised over time but the
integer values are stable.

**Why**: lets the generate-key action's overwrite-confirmation flow use
the same constant on both branches.

## 4. NSSavePanel.setDirectoryURL: vs setDirectory:

**Decision**: Use the runtime-respond check rather than #if version:

```objc
NSSavePanel *sp = [NSSavePanel savePanel];
if ([sp respondsToSelector:@selector(setDirectoryURL:)]) {
    [sp setDirectoryURL:[NSURL fileURLWithPath:_configDir isDirectory:YES]];
} else {
    [sp setDirectory:_configDir];
}
```

**Why**: `setDirectory:` was deprecated in 10.6 but works through Tiger.
`setDirectoryURL:` is 10.6+. Runtime check keeps one source file working
across both SDKs without conditional compilation.

**Pitfall**: don't replace `[sp setNameFieldStringValue:]` similarly —
that's 10.6+, no Tiger equivalent. On Tiger we just skip pre-filling the
name; user types it manually. Acceptable degradation.

## 5. NSColor secondaryLabelColor (warning label tint)

**Decision**: Replace with `[NSColor grayColor]`. The chat-history
encryption-warning label uses `secondaryLabelColor` on `modern` for that
slightly-faded grey. On Tiger that selector doesn't exist; `grayColor`
is close enough visually and doesn't need a runtime check.

**Why**: the warning label is decorative, not functional. Cosmetic
difference is acceptable.

## 6. NSBezelStyleInline disclosure button

**Decision**: The `Advanced ▶` toggle on `modern` uses
`NSBezelStyleInline` (Sierra 10.12) with `NSButtonTypeMomentaryPushIn`.
On Tiger neither bezel style nor the inline tint exists. Use plain
`NSBezelStyleSmallSquare` (already shimmed in TigerCompat.h) which gives
a reasonable button-styled look on Tiger.

**Why**: simpler than building a custom NSView for a disclosure triangle.
The visual difference is "subtle inline" vs "small square button" — same
affordance, slightly chunkier on Tiger. Acceptable.

**Pitfall**: don't use `NSBezelStyleDisclosure` — that's a different,
triangular widget that doesn't take a title.

## 7. Master-toggle grey-out works without changes

The `setEnabled:` method has been on every NSControl since Mac OS X 10.0.
The helper `updateChatHistoryWidgetEnablement` ports verbatim.

## 8. drop_if_banned helper is a straight C port

`ppc/src/hotline/server.c` has the same `srv->ban_list` pointer layout
and the `inet_ntop` call site is byte-identical. Insert the helper above
`hl_server_listen_and_serve` and add the four call sites at each accept
location, matching modern line-for-line.

## 9. Auto-ban escalation under PPC's threading model

PPC server is single-threaded just like modern (kqueue + select pre-kqueue
on Tiger 10.4 if applicable; the existing event loop is the same).
`pthread_mutex_lock`/`unlock` on `srv->rate_limiters_mu` are already used
on PPC — no change. The "unlock before calling ban_list->vt->add" pattern
on modern (to avoid holding the rate-limit mutex across disk I/O) ports
verbatim.

## 10. Build/test on the G4

The actual compile-and-run verification cannot happen on the dev Mac —
PPC is on a separate PowerMac G4 connected via SSH (see the
`powerpc-development` skill). Workflow:

1. Land all C and ObjC edits in this branch.
2. SSH to the G4: `ssh g4` (per skill config).
3. `cd` into the SMB-mounted repo and `make app`.
4. Smoke-test: launch GUI, open settings, toggle chat-history master,
   click Generate Key, verify file at `<configdir>/chat_history.key` with
   `0600` perms, save settings, verify YAML round-trip.
5. Server smoke: connect from a Hotline client, send a chat message,
   confirm `ChatHistory/channel-0.jsonl` is created.
6. Auto-ban smoke: open and immediately close 11+ TCP connections from
   one host within 5 min, verify the IP appears in `Banlist.yaml`.

## Out of scope

- Modernising other Tiger Cocoa idioms beyond what 0.1.8 forces.
- Audit of Mac OS X 10.5 Leopard parity (Tiger is the harder target;
  Leopard usually subsumes Tiger).
- Any chat-history protocol or storage changes — those are stable since
  the 0.1.7 backport.
