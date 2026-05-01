# Tasks — Port 0.1.8 to PPC

## 1. Server: slow-loris fix on file-transfer accept

- [ ] 1.1 In `ppc/src/hotline/server.c` `handle_file_transfer_connection`, immediately after the `hl_conn_wrap_plain` block, add 30 s `SO_RCVTIMEO` and `SO_SNDTIMEO` setsockopt calls on `client_fd`. Mirror modern lines [src/hotline/server.c:1631-1645](../../../src/hotline/server.c#L1631-L1645).
- [ ] 1.2 Add a comment explaining the slow-loris incident (single-thread accept loop + blocking `read` = total DoS).

## 2. Server: rate-limiter struct + auto-ban tunables

- [ ] 2.1 In `ppc/include/hotline/server.h`, extend `hl_rate_limiter_t` with `int violations`, `time_t first_violation`, `int auto_banned`. Match modern.
- [ ] 2.2 In the same file, define `HL_AUTOBAN_VIOLATION_THRESHOLD` (10) and `HL_AUTOBAN_WINDOW_SECONDS` (300) with comments explaining the rationale (legitimate retrying clients can't accumulate 10 violations in 5 min unless they ignore backoff entirely).

## 3. Server: auto-ban escalation in rate-limit check

- [ ] 3.1 In `ppc/src/hotline/server.c` `hl_server_rate_limit_check`, when `allowed = 0`, track sliding-window violations on the rate-limiter struct.
- [ ] 3.2 When violations cross threshold and `srv->config.auto_ban_enabled` is true, set a `trigger_autoban` flag and capture the violation count for the log line.
- [ ] 3.3 After unlocking the rate-limit mutex, if `trigger_autoban` and `srv->ban_list` is non-NULL, call `srv->ban_list->vt->add(ip)` and log `"Auto-banned %s after N rate-limit violations within Ns"` at INFO.
- [ ] 3.4 Reason for the unlock-before-call: `ban_add` writes the YAML banlist to disk; holding the rate-limit mutex across disk I/O blocks every other accept on the box.

## 4. Server: drop_if_banned helper at accept sites

- [ ] 4.1 Add `static int drop_if_banned(hl_server_t *srv, const struct sockaddr_in *addr, int client_fd)` immediately above `hl_server_listen_and_serve` in `ppc/src/hotline/server.c`. Body uses `inet_ntop` + `srv->ban_list->vt->is_banned`; closes `client_fd` and returns 1 on hit, 0 otherwise.
- [ ] 4.2 Insert `if (drop_if_banned(srv, &client_addr, client_fd)) continue;` after `accept` returns ≥ 0 at all four call sites: plain protocol port (5500), plain transfer port (5501), TLS protocol port (5600), TLS transfer port (5601). Match modern line-for-line.

## 5. Server: AutoBanEnabled config wiring

- [ ] 5.1 Add `int auto_ban_enabled;` to `hl_config_t` in `ppc/include/hotline/config.h`. Place it next to other security-cluster fields with a short comment block.
- [ ] 5.2 In `ppc/src/hotline/config.c` `hl_config_init`, set `cfg->auto_ban_enabled = 1;` (default ON, matches modern).
- [ ] 5.3 In `ppc/src/mobius/config_loader.c`, add YAML key `AutoBanEnabled` parsed via the existing `yaml_parse_bool` pattern. Match modern's wiring next to `EnableHOPE`.

## 6. GUI: TigerCompat shims

- [ ] 6.1 In `ppc/src/gui/TigerCompat.h`, add `#define NSDataWritingAtomic NSAtomicWrite` (guarded with `#ifndef`).
- [ ] 6.2 Add `arc4random_buf` polyfill (looped `arc4random()` calls per `design.md` §1), guarded `#if MAC_OS_X_VERSION_MAX_ALLOWED < 1070`.
- [ ] 6.3 Verify `NSAlertSecondButtonReturn` is present in the Tiger SDK headers; if not, add `#define NSAlertSecondButtonReturn 1001` guarded `#ifndef`.

## 7. GUI: Auto-ban checkbox in Security/HOPE section

- [ ] 7.1 Add `NSButton *_autoBanCheckbox` ivar to `ppc/src/gui/AppController.h` next to the HOPE-section ivars.
- [ ] 7.2 In `ppc/src/gui/AppController+LayoutAndTabs.inc`, add the checkbox at the bottom of the existing Security/HOPE disclosure section. Title: "Auto-ban repeat rate-limit offenders". Default state: ON. Help text describes that 10 violations within 5 min promotes the IP to the persistent banlist.
- [ ] 7.3 In `ppc/src/gui/AppController+LifecycleConfig.inc` `loadSettings`, read `AutoBanEnabled` (default ON when key absent) and set checkbox state.
- [ ] 7.4 In the same file's save block, write `AutoBanEnabled` from the checkbox state.

## 8. GUI: Refresh button on Bans subview

- [ ] 8.1 In `ppc/src/gui/AppController+LayoutAndTabs.inc`, find the existing IP-bans add-button block and add a `Refresh` button next to it. Resize the IP field and `Add` button frames to make room (mirror modern's frame numbers: field 88×22, Add 46×22, Refresh 60×22).
- [ ] 8.2 Add `- (void)refreshBanList:(id)sender;` declaration in `AppController.h` near `removeIPBan:`.
- [ ] 8.3 Implement in `ppc/src/gui/AppController+AccountsActions.inc` next to `removeIPBan:` — body just calls `[self loadBanListData]`.

## 9. GUI: chat-history extension widgets (ivars + actions file)

- [ ] 9.1 Append 5 new ivars to the existing `Chat History section` block in `AppController.h`: `_chatHistoryKeyPathField`, `_chooseChatHistoryKeyButton`, `_generateChatHistoryKeyButton`, `_chatHistoryKeyWarningLabel`, `_chatHistoryAdvancedDisclosure`, `_chatHistoryAdvancedView`, `_chatHistoryRateCapacityField`, `_chatHistoryRateRefillField`.
- [ ] 9.2 Declare 5 new action methods in `AppController.h`: `chatHistoryToggled:`, `chooseChatHistoryKey:`, `generateChatHistoryKey:`, `toggleChatHistoryAdvanced:`, `updateChatHistoryWidgetEnablement`.
- [ ] 9.3 Create `ppc/src/gui/AppController+ChatHistoryActions.inc` mirroring the modern file. Use the runtime-respond check for `setDirectoryURL:` per `design.md` §4. Use `[NSColor grayColor]` instead of `secondaryLabelColor`.
- [ ] 9.4 `#import` the new file in `ppc/src/gui/AppController.m` next to the other `+Actions.inc` imports.

## 10. GUI: chat-history layout extensions

- [ ] 10.1 In `ppc/src/gui/AppController+LayoutAndTabs.inc`, find the existing Chat History disclosure section and append the encryption-at-rest UI block (key path field + Choose + Generate Key) after the `_chatHistoryLogJoinsCheckbox` block.
- [ ] 10.2 Append the warning label using `[NSColor grayColor]` (not `secondaryLabelColor`).
- [ ] 10.3 Append the Advanced disclosure button. Use `NSBezelStyleSmallSquare` instead of modern's `NSBezelStyleInline`. Wire to `toggleChatHistoryAdvanced:`.
- [ ] 10.4 Append the hidden `FlippedView` containing capacity + refill fields. Default state hidden.
- [ ] 10.5 Wire the existing master `_chatHistoryEnableCheckbox` with `[setTarget:self]` + `[setAction:@selector(chatHistoryToggled:)]`.

## 11. GUI: chat-history load/save round-trip

- [ ] 11.1 In `ppc/src/gui/AppController+LifecycleConfig.inc` `loadSettings`, after the existing chat-history load block, add reads for `ChatHistoryEncryptionKey`, `ChatHistoryRateCapacity`, `ChatHistoryRateRefillPerSec`.
- [ ] 11.2 At the end of the chat-history load block, call `[self updateChatHistoryWidgetEnablement]`.
- [ ] 11.3 In the save block, append writes for the 3 new keys with proper int-clamp (≥ 0) on the rate fields.

## 12. Version bump

- [ ] 12.1 `ppc/man/lemoniscate.1` line 3 — replace `0.1.7` with `0.1.8` and update the date string ("April 2026" → current month).
- [ ] 12.2 `ppc/resources/Info.plist` — both `CFBundleVersion` and `CFBundleShortVersionString` to `0.1.8`.
- [ ] 12.3 `ppc/src/main.c` — `--version` printf to `0.1.8`.
- [ ] 12.4 `ppc/src/hotline/hope.c` — `HOPE_APP_STRING` to `Lemoniscate 0.1.8`.

## 13. Operator docs

- [ ] 13.1 Add the "Rate limits: per-IP connection vs. per-connection chat" subsection to `ppc/docs/SERVER.md` (verify the doc exists and find the matching section header).
- [ ] 13.2 Mention `AutoBanEnabled` config key in the Rate Limiting section.

## 14. Build verification on the G4

- [ ] 14.1 `ssh` to the G4 (per `powerpc-development` skill).
- [ ] 14.2 `make clean && make app` from the SMB-mounted repo.
- [ ] 14.3 Address any Tiger-specific compile errors (likely candidates: a missing TigerCompat shim, an `arc4random` prototype quirk on the 10.4u SDK, or an `NSDataWritingAtomic` symbol issue).
- [ ] 14.4 Run `./lemoniscate --version` and verify `lemoniscate 0.1.8`.
- [ ] 14.5 Launch `Lemoniscate.app`, open settings, verify the new Chat History encryption widgets render and the master toggle greys/un-greys children.
- [ ] 14.6 Click `Generate Key…`, verify a 32-byte file at `<configdir>/chat_history.key` with `0600` perms.
- [ ] 14.7 Save settings; reload the dict and verify all 9 `ChatHistory*` keys are preserved.
- [ ] 14.8 Server smoke: connect a Hotline client, send a public chat message, verify `ChatHistory/channel-0.jsonl` is created.
- [ ] 14.9 Auto-ban smoke: from a second host, open and close 11+ connections within 5 min and verify the IP appears in `Banlist.yaml`.

## 15. Backport doc

- [ ] 15.1 Write `docs/PPC_BACKPORT_0_1_8.md` mirroring the existing `docs/PPC_BACKPORT_CHAT_HISTORY.md` structure: required commits, files touched, Tiger-specific build adjustments, smoke tests.
