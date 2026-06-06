# Xymon — Architecture Overview for AI Agents

This document orients an AI coding agent to the Xymon codebase. Read this first, then
follow the topic links for deeper detail.

---

## What Xymon Is

Xymon is a C-based network and systems monitoring server. It collects host metrics from
remote clients, holds all monitoring state in memory, and serves a web UI showing the
health of every monitored host and service. The canonical upstream repository is
`https://github.com/xymon-monitoring/xymon`.

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
  └──► (other workers: xymond_client, xymond_capture, xymond_distribute …)

Web layer — two independent sub-systems:
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
| `xymond/webfiles/` | Web template files installed to `$XYMONHOME/web/` |
| `xymond/wwwfiles/` | Static web assets: GIF icons, CSS, menu images |
| `xymond/wwwfiles/gifs/` | Status dot GIFs and background images |
| `xymond/wwwfiles/menu/` | Menu bar CSS and gradient images |
| `xymond/etcfiles/` | Default config file templates (.DIST) |
| `xymongen/` | Static page generator (`pagegen.c` is the main target) |
| `web/` | CGI source files (one `.c` per CGI binary) |
| `lib/` | Shared libraries used by both CGIs and xymongen |
| `client/` | Remote host monitoring client |
| `xymonnet/` | Network test prober |
| `docs/agents/` | Architecture documentation for AI coding agents |

---

## HTML Generation — Two Paths

### Path 1: xymongen (static batch)

`xymongen/pagegen.c` generates the main overview pages: `xymon.html` (main view),
`nongreen.html` (all non-green), `critical.html`, and subpage variants. It runs as a
batch job via `xymonlaunch` and writes `.html` files to disk under `$XYMONSERVERWWWDIR`.

The page chrome (header/footer) comes from template files loaded by `headfoot()`. The
status grid (host rows × service columns) is emitted inline via `fprintf`. Each status
cell contains an `<IMG>` tag pointing to a GIF icon built by `dotgiffilename()`.

Template names: `stdnormal`, `stdnongreen`, `stdcritical`, `snapnormal`, `snapnongreen`,
`snapcritical`.

### Path 2: CGI programs (on-demand)

Each CGI in `web/` emits `Content-type: text/html\n\n`, calls `headfoot()` for the
header, writes its body content via `fprintf(stdout, ...)`, then calls `headfoot()` for
the footer.

See `docs/agents/ui.md` for the full template system details.

---

## Wire Protocol

The Xymon protocol is a text protocol over TCP port 1984. Key commands used by the UI:

| Command | Purpose |
|---|---|
| `xymondboard fields=f1,f2,... [host=^RE$ test=^RE$]` | Query current status of hosts/tests |
| `xymondlog HOST.TEST` | Fetch full status log for one host/test |
| `disable HOST.TEST DURATION MESSAGE` | Disable a test for N minutes |
| `enable HOST.TEST` | Re-enable a disabled test |
| `acknowledge HOST.TEST COOKIE DURATION MESSAGE` | Acknowledge an alert |
| `notify HOST.TEST MESSAGE` | Send a notification |

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
| `disabletime` | Disable expiry timestamp (0 = not disabled; -1 = disabled until OK) |
| `dismsg` | Disable message / reason |

---

## Key Library Files

| File | Role |
|---|---|
| `lib/headfoot.c` | Template engine — file resolution, `&VARIABLE` token substitution |
| `lib/htmllog.c` | Status log HTML body (disable/ack rendering, status text, history button) |
| `lib/color.c` | Color constants, `dotgiffilename()`, `colorname()` |
| `lib/eventlog.c` | Event log table rows (shared by eventlog.cgi and nongreen pages) |
| `lib/acknowledgementslog.c` | Acknowledgements table (shared by acknowledgements.cgi) |
| `lib/environ.c` | Default environment variable values (XYMONSKIN, XYMONBODYHEADER, etc.) |
| `lib/cgi.c` / `cgiurls.c` | CGI input parsing and URL construction |
| `lib/sendmsg.c` | Xymon protocol client |
| `lib/rrd_api_compat.h` | RRDtool argv API abstraction (autodetects const/non-const) |

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

**Important:** `OKCOLORS` defaults to `"green,blue,clear"` (defined in `lib/environ.c`).
`decide_alertstate(COL_BLUE)` returns `A_OK`. This means `handle_status()` clears
`ackmsg`/`acktime` the moment a test is disabled (turns blue). An acknowledged test
that is then disabled will have its ack record silently erased by the daemon.

---

## Build System

```bash
# Configure (run once; remove Makefile first if reconfiguring)
./configure --server --prefix=/usr --mandir=/usr/share/man \
  --with-logdir=/var/log/xymon --with-xymonhome=/var/lib/xymon

# Compile
make -j$(nproc)
```

If `lib/rrd_api_compat.h` is added or changed, a fresh `./configure` (delete Makefile
first) is required to redefine `RRD_CONST_ARGS`.

---

## Further Reading

- `docs/agents/ui.md` — Template engine detail, token reference, status icon system,
  and HTML generation conventions
