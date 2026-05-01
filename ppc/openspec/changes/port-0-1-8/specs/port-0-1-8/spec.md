# Spec — Port 0.1.8 to PPC

## ADDED Requirements

### Requirement: PPC server bounds every blocking read on the accept-loop thread
The PPC server SHALL set a finite `SO_RCVTIMEO` and `SO_SNDTIMEO` on every accepted socket whose handler runs on the main accept-loop thread, so that a stalled client cannot wedge the entire server.

#### Scenario: A client sends partial header bytes and stalls on a transfer port
- **WHEN** a client connects to port 5501 (or 5601 with TLS)
- **AND** the client sends fewer than `HL_TRANSFER_HEADER_SIZE` bytes and stops
- **THEN** the per-socket receive timeout fires within 30 seconds
- **AND** the connection is closed
- **AND** the server continues accepting new connections

### Requirement: PPC server promotes repeat rate-limit offenders to the persistent banlist
The PPC server SHALL track per-IP rate-limit violations within a sliding window and add the IP to the persistent banlist once a threshold is crossed, gated by the `AutoBanEnabled` config field.

#### Scenario: An IP exceeds the rate limit ten times within five minutes
- **WHEN** an IP triggers `hl_server_rate_limit_check` ten times within 300 seconds
- **AND** `AutoBanEnabled` is `true`
- **THEN** the server calls `srv->ban_list->vt->add(ip)` exactly once for that escalation
- **AND** an INFO log line of the form `"Auto-banned <ip> after 10 rate-limit violations within 300s"` is written
- **AND** subsequent connection attempts from that IP are rejected at `accept()` before any read

#### Scenario: AutoBanEnabled is false
- **WHEN** an IP exceeds the rate limit ten times within 300 seconds
- **AND** `AutoBanEnabled` is `false`
- **THEN** the server does NOT add the IP to the banlist
- **AND** no `Auto-banned` log line is written

### Requirement: PPC server rejects banned IPs at accept time on every listener
The PPC server SHALL drop banned IPs at `accept()` on all four listeners (5500, 5501, 5600, 5601) before any application read or TLS handshake.

#### Scenario: A banned IP connects to any listener
- **WHEN** an IP that appears in `Banlist.yaml` opens a TCP connection to any of the four listeners
- **THEN** the server closes the connection immediately after `accept()`
- **AND** no read, no TLS handshake, and no log line beyond the original ban event are produced

### Requirement: PPC GUI exposes all nine ChatHistory* configuration keys
The PPC admin GUI SHALL expose every persisted `ChatHistory*` configuration key through native widgets so that GUI saves preserve those keys instead of stripping them from `config.yaml`.

#### Scenario: All chat-history keys round-trip through a GUI save on Tiger
- **WHEN** the operator loads a config that contains all 9 `ChatHistory*` keys
- **AND** the operator changes an unrelated setting
- **AND** the operator saves
- **THEN** the resulting `config.yaml` still contains all 9 `ChatHistory*` keys with unchanged values

### Requirement: PPC GUI master-toggle gates chat-history sub-widgets
The Chat History section's `Persist public chat to disk` checkbox SHALL gate the enabled state of every other widget in the section.

#### Scenario: Master toggle off disables children on Tiger
- **WHEN** the operator unchecks the master toggle
- **THEN** every other widget in the Chat History section becomes disabled
- **AND** the widgets remain visible (not hidden)

### Requirement: PPC GUI generates a 32-byte encryption key on demand
The PPC GUI SHALL provide a `Generate Key…` button that writes a 32-byte random key to disk with `0600` permissions and populates the key path field, using `arc4random()` (looped) on Tiger and `arc4random_buf` on 10.7+ via the TigerCompat shim.

#### Scenario: Generating a key into an empty path on Tiger
- **WHEN** the operator clicks `Generate Key…`
- **AND** no file exists at the default target path
- **THEN** a 32-byte file is written with `0600` permissions
- **AND** the key path field is populated with the path
- **AND** the next config save persists the new path

### Requirement: PPC GUI auto-ban toggle persists AutoBanEnabled
The PPC GUI SHALL provide an `Auto-ban repeat rate-limit offenders` checkbox in the Security/HOPE settings section, default ON, persisted as the `AutoBanEnabled` YAML key.

#### Scenario: Auto-ban toggle round-trips
- **WHEN** the operator unchecks the auto-ban checkbox
- **AND** saves
- **THEN** `config.yaml` contains `AutoBanEnabled: false`
- **AND** the running PPC server stops promoting rate-limit offenders to the banlist after a SIGHUP

### Requirement: PPC GUI Bans tab Refresh re-reads Banlist.yaml
The PPC GUI SHALL provide a `Refresh` button on the Bans subview that re-reads `Banlist.yaml` so auto-bans written by the running server become visible without restarting the GUI.

#### Scenario: Auto-banned IP appears after Refresh on Tiger
- **WHEN** the running server auto-bans an IP and writes it to `Banlist.yaml`
- **AND** the operator clicks `Refresh` in the Bans subview
- **THEN** the new IP appears in the Banned IPs table

### Requirement: PPC version strings identify 0.1.8
The PPC release SHALL identify itself as `0.1.8` in CLI output, the man page, the bundle Info.plist, and the HOPE app-string.

#### Scenario: PPC CLI reports new version
- **WHEN** `lemoniscate --version` is invoked on the PPC binary
- **THEN** the output is `lemoniscate 0.1.8`

#### Scenario: PPC HOPE handshake reports new version
- **WHEN** a HOPE handshake completes with the PPC server
- **THEN** the app string is `Lemoniscate 0.1.8`
