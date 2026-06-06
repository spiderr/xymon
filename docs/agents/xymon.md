# Xymon — Architecture Overview for AI Agents

This document gives a concise orientation to the Xymon codebase for an AI coding agent
starting a new session. Read this first, then follow the topic links for deeper detail.

---

## What Xymon Is

Xymon is a C-based network and systems monitoring server. It collects host metrics from
remote clients, holds all monitoring state in memory, and serves a web UI showing the
health of every monitored host and service. The active development branch is
`html5-bootstrap` — a full HTML5/Bootstrap 5 modernization of the web UI.

---

## Data Flow

```
Clients (client/)
  │  send status messages over the Xymon protocol (TCP port 1984)
  ▼
xymond (xymond/xymond.c)
  │  central daemon — holds all state in memory, implements BB/Xymon wire protocol
  │  fans events out to worker modules via System V IPC message queues
  ├──► xymond_rrd     — writes RRD time-series databases
  ├──► xymond_history — maintains per-test history log files
  ├──► xymond_alert   — sends alert notifications
  └──► (other workers)
  
Web layer (two sub-systems):
  ├── xymongen (xymongen/)
  │     batch tool run periodically by xymonlaunch; walks the host/page tree
  │     and writes static .html overview files to disk
  └── CGI tools (web/*.c)
        on-demand CGI programs invoked by Apache for individual pages
```

---

## Key Directories

| Directory | Contents |
|---|---|
| `xymond/` | Central daemon source + worker modules |
| `xymond/webfiles/` | Web template files (installed to `$XYMONHOME/web/`) |
| `xymond/wwwfiles/` | Static web assets: CSS, JS, icons, themes |
| `xymond/wwwfiles/themes/` | Per-theme CSS overrides |
| `xymond/wwwfiles/externals/` | Self-hosted Bootstrap 5.3 and FontAwesome 6 |
| `xymongen/` | Static page generator (`pagegen.c` is the main target) |
| `web/` | CGI source files (one `.c` per CGI binary) |
| `lib/` | Shared libraries used by both CGIs and xymongen |
| `client/` | Remote host monitoring client (out of scope for UI work) |
| `xymonnet/` | Network test prober (out of scope for UI work) |
| `docs/agents/` | Architecture documentation for AI coding agents |

---

## HTML Generation — Two Paths

### Path 1: xymongen (static batch)

`xymongen/pagegen.c` generates the main overview pages (`xymon.html`, `nongreen.html`,
`critical.html` and subpage variants). It runs as a batch job and writes `.html` files
to disk. The page chrome (header/footer) comes from template files; the status grid
(host rows × service columns) is emitted inline via `fprintf`.

Template names used: `stdnormal`, `stdnongreen`, `stdcritical`, `snapnormal`,
`snapnongreen`, `snapcritical`.

### Path 2: CGI programs (on-demand)

Each CGI in `web/` emits `Content-type: text/html\n\n`, calls `headfoot()` for the
header, writes its body content via `fprintf(stdout, ...)`, then calls `headfoot()` for
the footer.

See `docs/agents/ui.md` for the full template system and HTML generation details.

---

## Wire Protocol (read-only reference)

The Xymon protocol is a simple text protocol over TCP port 1984.  Key commands used
internally by the UI:

| Command | Purpose |
|---|---|
| `xymondboard fields=f1,f2,... [host=^RE$ test=^RE$]` | Query current status of hosts/tests |
| `xymondlog HOST.TEST` | Fetch full status log for one host/test |
| `disable HOST.TEST DURATION MESSAGE` | Disable a test for N minutes |
| `enable HOST.TEST` | Re-enable a disabled test |
| `acknowledge HOST.TEST COOKIE DURATION MESSAGE` | Acknowledge an alert |

`sendmessage()` / `newsendreturnbuf()` / `getsendreturnstr()` in `lib/sendmsg.c` handle
all client-side protocol I/O.

---

## Monitoring State Fields (xymondboard columns)

Common fields queried from `xymondboard`:

| Field | Meaning |
|---|---|
| `hostname` | Host name |
| `testname` | Service/test name |
| `color` | Current status color (green/yellow/red/blue/purple/clear) |
| `flags` | Bit flags (acked, disabled, etc.) |
| `cookie` | Alert cookie integer (0 = not alerting) |
| `ackmsg` | Active acknowledgement message (empty = not acked) |
| `acktime` | Ack expiry Unix timestamp (0 = not acked) |
| `disabletime` | Disable expiry Unix timestamp (0 = not disabled, -1 = until OK) |
| `dismsg` | Disable message / reason |

---

## Key Library Files

| File | Role |
|---|---|
| `lib/headfoot.c` | Template engine — 4-tier file resolution, `&VARIABLE` substitution |
| `lib/htmllog.c` | Status log HTML body (ack/disable banners, status text, history button) |
| `lib/color.c` | Color constants, `coloricon()`, `dotgiffilename()`, `colorname()` |
| `lib/eventlog.c` | Event log table rows (shared by eventlog.cgi and nongreen pages) |
| `lib/acknowledgementslog.c` | Acknowledgements table (shared by acknowledgements.cgi) |
| `lib/environ.c` | Default environment variable values (XYMONALLOKTEXT etc.) |
| `lib/cgi.c` / `cgiurls.c` | CGI input parsing and URL construction |
| `lib/sendmsg.c` | Xymon protocol client (sendmessage, newsendreturnbuf, etc.) |

---

## Status Colors

| Color | Constant | Meaning |
|---|---|---|
| green | `COL_GREEN` (0) | OK |
| yellow | `COL_YELLOW` (1) | Warning |
| blue | `COL_BLUE` (2) | Disabled / in scheduled maintenance |
| purple | `COL_PURPLE` (3) | No data / unreachable |
| clear | `COL_CLEAR` (4) | Not tested / no status yet |
| red | `COL_RED` (5) | Critical / error |

**Important:** `OKCOLORS` defaults to `"green,blue,clear"`. This means `COL_BLUE` is
treated as "OK" by `decide_alertstate()`. As a consequence, `handle_status()` clears
`ackmsg`/`acktime` the moment a test is disabled (turns blue). See `docs/agents/ui.md`
§ Ack Preservation for the 5.0.1 fix.

---

## Build System

```bash
# Configure (run once from source/)
./configure --server --prefix=/usr --mandir=/usr/share/man \
  --with-logdir=/var/log/xymon --with-xymonhome=/var/lib/xymon

# Compile
make -j$(nproc)
```

The build produces binaries in place; no `make install` — the RPM handles installation.
RPM packaging is a separate session (see `rpmbuild/` sibling directory).

---

## Scope of html5-bootstrap Branch

UI modernization work touches only:

- `source/web/` — CGI source files
- `source/xymongen/` — static page generator
- `source/xymond/webfiles/` — HTML template files
- `source/xymond/wwwfiles/` — static assets (CSS, JS, icons, themes)
- `source/lib/headfoot.c` — template engine
- `source/docs/agents/` — this documentation

Do **not** modify: `xymond/xymond*.c`, `xymonnet/`, `xymond/rrd/`, `client/`.
Preserve all CGI query string APIs exactly — only HTML output changes.

---

## Further Reading

- `docs/agents/ui.md` — Template system, Bootstrap conventions, CSS architecture,
  status icon rendering, svcstatus banner system, color filter implementation
- `docs/agents/protocol.md` — Wire protocol: all message types, `sendmessage()` API,
  `xymondboard` field reference, `nlencode`/`nldecode` encoding
- `docs/agents/daemon.md` — `xymond` internals: in-memory state model, `handle_status()`,
  `decide_alertstate()`, IPC channels, and all worker module roles
- `docs/agents/cgi.md` — CGI layer: full program inventory, `cgi_request()` input parsing,
  URL helpers, security (`csp_header()`), and the standard CGI skeleton
- `docs/agents/config.md` — Configuration: `hosts.cfg` syntax and host tags,
  `xymonserver.cfg` key variables, `tasks.cfg` format, `alerts.cfg` rule syntax
