# Xymon Configuration System

Xymon's configuration is split across several files. The master environment file is
`xymonserver.cfg`; all other config files are resolved through paths defined there.

---

## `hosts.cfg` — Host and Page Hierarchy

`hosts.cfg` (path in `HOSTSCFG` env var, default `$XYMONHOME/etc/hosts.cfg`) defines:
- Which hosts Xymon monitors
- The page/subpage tree that organizes the web UI
- Per-host options and network test specifications

### Page structure keywords

```
page      PAGENAME  "Display Title"
subpage   PAGENAME  "Display Title"
subparent PAGENAME  PARENTPAGENAME  "Display Title"
```

- `page` creates a top-level page (generates `PAGENAME.html`)
- `subpage` creates a subpage under the immediately preceding `page`
- `subparent` creates a subpage under any named parent

All host lines after a `page`/`subpage` keyword belong to that page until the next
structural keyword.

### Group keywords

```
group        "Group Heading"
group-only   SERVICE[,SERVICE...]   "Group Heading"
group-except SERVICE[,SERVICE...]   "Group Heading"
```

Groups visually cluster hosts on a page without creating subpages.
`group-only` shows only the listed services; `group-except` hides them.

### Host line format

```
IP.AD.DR.ESS   hostname   # tags...
```

- Fields are separated by whitespace; the `#` is not a comment but the tag separator
- Tags after `#` are space-separated key:value or bare-word flags
- Any tag can appear multiple times; last value wins unless tag is a list

### Common host tags (source: `lib/loadhosts.c` `xmh_item_key[]`)

| Tag | Example | Meaning |
|---|---|---|
| `NET:` | `NET:london` | Network zone — only xymonnet with `XYMONNETWORK=london` tests this host |
| `NAME:` | `NAME:webserver01` | Display name (overrides hostname in the UI) |
| `COMMENT:` | `COMMENT:Primary DB` | One-line description shown in info page |
| `DESCR:` | `DESCR:Production Oracle` | Extended description |
| `DOC:` | `DOC:http://...` | URL to host documentation |
| `DOWNTIME=` | `DOWNTIME=*:0800:1800` | Planned downtime window (days:start:end; `*` = all days) |
| `NOCOLUMNS:` | `NOCOLUMNS:disk,cpu` | Hide these service columns from the status grid |
| `NK:` | `NK:cpu,disk` | Services that appear on the critical page |
| `NKTIME=` | `NKTIME=W:0800:1700` | Time restriction for NK (critical) alerting |
| `NOPROP:` | `NOPROP:yellow` | Do not propagate this color up to the page summary |
| `NOPROPRED:` | `NOPROPRED:disk` | Do not propagate red from these services |
| `NOPROPYELLOW:` | `NOPROPYELLOW:cpu` | Do not propagate yellow from these services |
| `NOPROPPURPLE:` | `NOPROPPURPLE:conn` | Do not propagate purple from these services |
| `REPORTTIME=` | `REPORTTIME=W:0800:1700` | SLA window for availability reports |
| `WARNPCT:` | `WARNPCT:98` | Yellow threshold for availability report (%) |
| `CLASS:` | `CLASS:web` | Host class (used in `analysis.cfg` pattern matching) |
| `OS:` | `OS:linux` | Operating system (affects client data parsing) |
| `TRENDS:` | `TRENDS:cpu,disk` | Override which services get RRD trend graphs |
| `delayred=` | `delayred=cpu:15` | Delay going red for N minutes after trigger |
| `delayyellow=` | `delayyellow=disk:30` | Delay going yellow for N minutes |
| `depends=` | `depends=host1.conn` | Suppress alerts if another host/test is failing |
| `holidays=` | `holidays=us` | Holiday calendar for SLA calculations |
| `ssldays=` | `ssldays=30` | Warn N days before SSL cert expiry |
| `sslbits=` | `sslbits=128` | Minimum acceptable SSL cipher strength |

### Boolean flag tags (presence = enabled)

| Tag | Meaning |
|---|---|
| `nodisp` | Hide host from the web UI (still monitored) |
| `nonongreen` | Exclude host from the "non-green" overview page |
| `noinfo` | Do not generate an info column for this host |
| `notrends` | Do not generate a trends column for this host |
| `noclient` | Do not process client data for this host |
| `noconn` / `noping` | Do not ping-test this host |
| `dialup` | Host is a dialup/intermittent host; purple is tolerated |
| `testip` | Test the IP directly rather than resolving the hostname |
| `prefer` | When hostname has multiple IPs, prefer this entry |
| `trace` / `notrace` | Enable/disable traceroute on failure |
| `NOFLAP` | Disable flap detection for this host |
| `MULTIHOMED` | Host sends from multiple IPs; suppress sender-change warnings |
| `PULLDATA` | Data is pulled rather than pushed; disable sender IP check |
| `NOCLEAR` | Do not show "clear" status for this host |

### Network test tags

Network tests are specified as bare words or `proto://host/path` URLs after `#`:

```
1.2.3.4   myhost   # bbd http://myhost/ https://myhost/ ftp smtp ssh
```

| Tag | Test performed |
|---|---|
| `bbd` | TCP connection to xymond port (checks the xymon server itself) |
| `http://URL` | HTTP fetch and response code check |
| `https://URL` | HTTPS fetch with cert validation |
| `ftp` | FTP connection |
| `smtp` | SMTP connection |
| `ssh` | SSH connection |
| `ldap://URL` | LDAP query |
| `pop3` / `imap` | Mail protocol checks |
| `dns` | DNS resolution check |
| `telnet` | Telnet connection |

### Reading hosts.cfg in C

```c
/* Load and parse hosts.cfg */
load_hostnames(xgetenv("HOSTSCFG"), NULL, get_fqdn());

/* Look up a host */
void *hinfo = hostinfo(hostname);
if (hinfo) {
    char *pagepath = xmh_item(hinfo, XMH_PAGEPATH);
    char *net      = xmh_item(hinfo, XMH_NET);
    /* boolean flags: non-NULL return = flag is set */
    int nodisp = (xmh_item(hinfo, XMH_FLAG_NODISP) != NULL);
}
```

`xmh_item()` is the single accessor for all host attributes. Flags return a non-NULL pointer
(the flag's key string) when set, NULL when absent.

---

## `xymonserver.cfg` — Main Environment

This file is sourced as a shell-like environment file (key=value, `$VAR` expansion, no
quoting required). It is NOT a shell script — no `export`, no conditionals.

### Required settings

```sh
XYMONSERVERROOT="/usr/lib/xymon/server"  # Installation root
XYMONSERVERLOGS="/var/log/xymon"         # Log directory
XYMONSERVERHOSTNAME="monitor.example.com"
XYMONSERVERIP="192.168.1.10"
XYMONSERVERWWWNAME="monitor.example.com"
XYMONSERVERWWWURL="/xymon"               # Web root path
XYMONSERVERCGIURL="/xymon-cgi"
XYMONSERVERSECURECGIURL="/xymon-seccgi"
```

### Key tunable settings

| Variable | Default | Purpose |
|---|---|---|
| `XYMONDPORT` | `1984` | xymond listen port |
| `XYMSRV` | `$XYMONSERVERIP` | IP for `sendmessage()` single-server mode |
| `XYMSERVERS` | (empty) | Space-separated IPs for multi-server mode |
| `STATUSLIFETIME` | `30` | Minutes before a status goes purple |
| `FQDN` | `TRUE` | Use fully-qualified hostnames |
| `ALERTCOLORS` | `red,yellow,purple` | Colors that trigger alerts |
| `OKCOLORS` | `green,blue,clear` | Colors treated as non-alerting |
| `PINGCOLUMN` | `conn` | Column name for ping results |
| `INFOCOLUMN` | `info` | Column name for the info page |
| `TRENDSCOLUMN` | `trends` | Column name for RRD graphs |
| `CLIENTCOLUMN` | `clientlog` | Column name for raw client logs |
| `MAXMSGSPERCOMBO` | `100` | Max status messages per combo |
| `DELAYRED` | (empty) | Global delay format: `test:minutes,...` |
| `DELAYYELLOW` | (empty) | Same for yellow |
| `FLAPCOUNT` | `5` | Changes in window to detect flapping |
| `FLAPTHRESHOLD` | `1500` | Flap detection window in seconds |
| `ACK_COOKIE_EXPIRATION` | `86400` | Alert cookie lifetime in seconds |
| `XYMONNETWORK` | (empty) | Network zone for this xymonnet instance |

---

## `tasks.cfg` — Daemon Task List

`xymonlaunch` reads `tasks.cfg` to know which programs to start, monitor, and restart.

### Task block format

```ini
[TASKNAME]
    ENVFILE /path/to/env/file
    NEEDS   other-task-name
    CMD     program --args...
    INTERVAL N                  # seconds between runs (periodic tasks)
    DISABLED                    # comment out a task without removing it
    ONHOST  hostname-pattern    # only run on matching hosts
    GROUP   groupname           # logical grouping
```

- `NEEDS` — the named task must be running before this one starts
- `INTERVAL` — for non-daemon tasks (scripts), run every N seconds
- Tasks without `INTERVAL` are daemons — xymonlaunch restarts them if they exit

### Core daemon tasks

| Task name | Program | Channel |
|---|---|---|
| `xymond` | `xymond` | (master daemon) |
| `history` | `xymond_channel ... xymond_history` | `stachg` |
| `alert` | `xymond_channel ... xymond_alert` | `page` |
| `clientdata` | `xymond_channel ... xymond_client` | `client` |
| `rrdstatus` | `xymond_channel ... xymond_rrd` | `status` |
| `rrddata` | `xymond_channel ... xymond_rrd` | `data` |
| `hostdata` | `xymond_channel ... xymond_hostdata` | `clichg` |

### xymongen task

```ini
[xymongen]
    ENVFILE .../xymonserver.cfg
    NEEDS   xymond
    INTERVAL 60
    CMD xymongen ...
```

`xymongen` runs periodically (not a daemon) to regenerate static HTML overview pages.

---

## `alerts.cfg` — Alert Routing Rules

Rules are evaluated top-to-bottom. The first matching RULE block controls which RECIPIENTs
are notified. A rule with no filter criteria matches everything.

### Rule syntax

```
PAGE=REGEX   HOST=REGEX   SERVICE=REGEX   COLOR=red,yellow   DURATION>10   TIME=W:0800:1700
    MAIL user@example.com  REPEAT=30  RECOVERED  NOTICE
    SCRIPT /path/to/script  RECIPIENT  FORMAT=SMS  DURATION>60
```

### Rule filter keywords

| Keyword | Example | Matches |
|---|---|---|
| `HOST=` | `HOST=%^web.*` | Hostname (prefix `%` for regex) |
| `EXHOST=` | `EXHOST=test.example.com` | Exclude hostname |
| `SERVICE=` | `SERVICE=http,https` | Service/test name |
| `EXSERVICE=` | `EXSERVICE=conn` | Exclude service |
| `PAGE=` | `PAGE=%infrastructure` | Page path (prefix `%` for regex) |
| `EXPAGE=` | `EXPAGE=dev` | Exclude page path |
| `COLOR=` | `COLOR=red` | Alert color(s) |
| `DURATION>N` | `DURATION>20` | Alert has lasted more than N minutes |
| `DURATION<N` | `DURATION<30` | Alert has lasted less than N minutes |
| `TIME=` | `TIME=W:0800:1700` | Time-of-day window |
| `GROUP=` | `GROUP=database` | Group ID from `analysis.cfg` |

### Recipient keywords

| Keyword | Example | Meaning |
|---|---|---|
| `MAIL` | `MAIL ops@example.com` | Send email |
| `SCRIPT` | `SCRIPT /usr/local/bin/alert.sh RECIPIENT` | Run script with env vars |
| `REPEAT=N` | `REPEAT=20` | Repeat alert every N minutes while active |
| `RECOVERED` | | Also send when alert recovers |
| `NOTICE` | | Send to `XYMONNOTICE` recipients as well |
| `UNMATCHED` | | Alert only when no other recipient matched |
| `FORMAT=SMS` | | Short message format for the script |

---

## `analysis.cfg` — Client Data Analysis Rules

`analysis.cfg` tells `xymond_client` how to interpret the sections in client data messages
and generate per-service status checks. This is how disk, CPU, memory, process, and log checks
are defined.

### Key directives

```
UP      3 minutes    # Host must have been up at least 3 minutes before reporting
DISK /   98 99       # Disk / yellow at 98%, red at 99%
DISK /var 85 90
MEMPHYS  80 90       # Physical memory yellow/red %
MEMSWAP  50 80       # Swap usage yellow/red %
PROC httpd 1         # Process 'httpd' must have at least 1 instance
PROC mysqld 1 1      # Exactly 1 instance
LOG /var/log/messages "ERROR|CRITICAL"  # Alert on pattern in log file
PORT 80 tcp          # Port 80/tcp must be listening
```

`analysis.cfg` is loaded by `xymond_client` and matched against the `[proc]`, `[disk]`,
`[memory]`, `[msgs]`, `[ports]` sections in client data.

---

## `xymonmenu.cfg` — Navigation Menu

Referenced via `XYMONBODYHEADER=file:$XYMONHOME/etc/xymonmenu.cfg`, this file defines the
navigation links rendered at the top of every page. Format is a Xymon-specific markup
(not HTML) parsed by `lib/headfoot.c`.

---

## How C Code Reads Configuration

All environment variables (from `xymonserver.cfg`) are accessed via:

```c
char *val = xgetenv("VARNAME");   /* Returns "" not NULL if unset */
```

`xgetenv()` is defined in `lib/environ.c`. It expands `$VAR` references and applies defaults
from the `env_defaults[]` table in `lib/environ.c`.

Host configuration is accessed via:

```c
load_hostnames(xgetenv("HOSTSCFG"), NULL, get_fqdn());
void *hinfo = hostinfo("myhost.example.com");
char *val   = xmh_item(hinfo, XMH_PAGEPATH);
```

`xmh_item()` returns NULL if the tag is absent. For flag-type tags, non-NULL means "set".
