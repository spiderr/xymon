# xymond — Central Daemon Internals

`xymond/xymond.c` is the core of Xymon. It listens on TCP 1984, holds all monitoring state in
memory, and fans messages out to worker modules via System V IPC shared-memory channels.

---

## State Model

All live monitoring data lives in two interlinked in-memory trees:

### `xymond_hostlist_t` (per host)

```c
typedef struct xymond_hostlist_t {
    char        *hostname;
    char         ip[IP_ADDR_STRLEN];
    xymond_log_t *logs;        /* linked list of test logs for this host */
    xymond_log_t *pinglog;     /* pointer into logs — the conn/ping test */
    ...
} xymond_hostlist_t;
```

Hosts are stored in a red-black tree keyed by hostname (`rbhosts`).

### `xymond_log_t` (per host/test pair)

```c
typedef struct xymond_log_t {
    xymond_hostlist_t *host;
    testinfo_t        *test;         /* test name */
    int    color, oldcolor;          /* current and previous color */
    int    activealert;              /* non-zero if we have sent/are sending alerts */
    time_t *lastchange;              /* array[flapcount] of timestamps of recent changes */
    time_t  logtime;                 /* last update received */
    time_t  validtime;               /* expires at this time (goes purple) */
    time_t  enabletime;              /* 0=not disabled; DISABLED_UNTIL_OK=-1; otherwise expiry */
    int     disableignoreok;         /* 1 = stay disabled even when test reports OK */
    time_t  acktime;                 /* 0=not acked; otherwise ack expiry time */
    unsigned char *dismsg, *ackmsg;  /* reason strings (nlencode'd when on the wire) */
    char   *cookie;                  /* alert cookie string (integer) */
    time_t  cookieexpires;
    int     flapping;                /* non-zero when flap suppression is active */
    ...
} xymond_log_t;
```

- `enabletime == 0` — test is running normally
- `enabletime > now` — test is disabled until that timestamp
- `enabletime == DISABLED_UNTIL_OK` (`-1`) — test is disabled indefinitely ("Forever")
- `acktime > now` — test has an active acknowledgement

`disableignoreok` is an independent boolean that modifies re-enable-on-green behavior:

| `enabletime` | `disableignoreok` | Behavior |
|---|---|---|
| `timestamp` | `0` (default) | Timed — clears early if test reports OK |
| `timestamp` | `1` | Timed — stays blue until clock expires |
| `DISABLED_UNTIL_OK` | `0` (default) | Forever — re-enables on OK ← legacy behavior |
| `DISABLED_UNTIL_OK` | `1` | Forever — stays blue no matter what |

**Wire protocol:** `disableignoreok` is embedded as a parseable prefix line in the disable
message body: `"disableignoreok: yes\n"`. `handle_enadis()` checks `txtstart` for this prefix,
sets `log->disableignoreok = 1`, and strips the line before storing as `log->dismsg`.
Old callers that don't send the prefix default to `disableignoreok=0`.

**Behavior change (5.0.2):** Timed disables now default to re-enable on green (`disableignoreok=0`).
Previously, timed disables always ran to expiry regardless of status. The new default is
explicitly "re-enable early if the test recovers before the timer expires."

- `cookie` is a non-zero integer string while the test is in alert state; `"0"` or empty otherwise

---

## `decide_alertstate()` and `OKCOLORS`

```c
enum alertstate_t { A_OK, A_ALERT, A_UNDECIDED };

enum alertstate_t decide_alertstate(int color)
{
    if ((okcolors   & (1 << color)) != 0) return A_OK;
    if ((alertcolors & (1 << color)) != 0) return A_ALERT;
    return A_UNDECIDED;
}
```

`okcolors` is parsed from the `OKCOLORS` env var (default: `green,blue,clear`).  
`alertcolors` is parsed from `ALERTCOLORS` (default: `red,yellow,purple`).

**Critical consequence:** `COL_BLUE` (disabled/maintenance) is in `OKCOLORS`. This means
`decide_alertstate(COL_BLUE) == A_OK`. Inside `handle_status()`, whenever a test transitions to
an OK state, the ack fields are cleared:

```c
if (decide_alertstate(newcolor) == A_OK) {
    log->acktime = 0;
    log->maxackedcolor = 0;
}
```

So the moment `handle_status()` processes a disable (which forces `newcolor = COL_BLUE`), it
clears `ackmsg`/`acktime`. Any code that needs to preserve the ack through a disable must save
the ack fields *before* the disable command reaches `handle_status()` — see `web/enadis.c` and
the ack-preservation pattern in `lib/htmllog.c`.

---

## `handle_status()` — Key Steps

`handle_status()` (`xymond.c:1415`) is called for every inbound `status` / `status+N` message
and for internally generated color transitions:

1. **Modifier check** — if `log->modifiers` has an active entry, it can override `newcolor`
   upward (never downward).
2. **Flap detection** — if more than `flapcount` changes occurred within `flapthreshold` seconds,
   the color is held at the most severe recent color and `log->flapping` is set.
3. **Disable check** — if `enabletime > now` or `enabletime == DISABLED_UNTIL_OK`, force
   `newcolor = COL_BLUE`. If `!disableignoreok` and `decide_alertstate(newcolor) == A_OK`,
   clear the disable early (applies to both timed and forever disables). If `disableignoreok`
   is set, the test stays blue until the timer expires (or manual re-enable).
4. **Ack check** — if `acktime > now`, suppress alerting. If `decide_alertstate(newcolor) == A_OK`
   or ack expired, clear ack fields.
5. **Delay check** — `DELAYRED` / `DELAYYELLOW` in hosts.cfg can hold a status at its current
   level until the delay timer has passed.
6. **Color commit** — `log->oldcolor = log->color; log->color = newcolor;`
7. **Channel fanout** — if color changed:
   - Post to `C_STACHG` (status-change history)
   - Post to `C_CLICHG` if client data triggered the change
   - Post to `C_PAGE` (alert module) if alert state changed
   - Always post to `C_STATUS` (full status message, consumed by `xymond_rrd`)

---

## IPC Channel Architecture

Xymond uses System V shared-memory message queues. Each channel is a fixed-size shared buffer
plus a semaphore set.

### Channel IDs (`lib/xymond_buffer.h`)

```c
enum msgchannels_t {
    C_STATUS = 1,   /* Full status messages */
    C_STACHG,       /* Brief status-change events */
    C_PAGE,         /* Alert trigger events */
    C_DATA,         /* Raw "data" messages */
    C_NOTES,        /* "notes" messages */
    C_ENADIS,       /* enable/disable events */
    C_CLIENT,       /* Raw client data */
    C_CLICHG,       /* Client-triggered status changes */
    C_USER,         /* "usermsg" messages */
    C_FEEDBACK_QUEUE, /* Client backfeed (workers → xymond) */
    C_LAST
};
```

### How channels work

- `setup_channel(chnid, CHAN_MASTER)` — xymond creates the shared segment.
- `setup_channel(chnid, CHAN_CLIENT)` — a worker attaches to an existing segment.
- xymond writes a message to the segment and raises a semaphore (`GOCLIENT`).
- Each worker decrements `GOCLIENT` atomically to claim one message.
- After processing, the worker raises `CLIENTREADY` to tell xymond it is done.
- Buffer sizes are controlled by `MAXMSG_*` env vars (KB); default 256 KB for `C_STATUS`.

### Worker infrastructure (`xymond/xymond_channel.c` + `xymond_worker.c`)

Every worker follows this pattern:

```c
/* In main() */
unsigned char *msg;
int seq;
while ((msg = get_xymond_message(C_STACHG, "myworker", &seq, NULL)) != NULL) {
    /* parse msg and do work */
}
```

`get_xymond_message()` blocks on the semaphore until xymond posts a message, then copies the
shared buffer to a private allocation before releasing the semaphore.

---

## Worker Modules

| Binary | Channel(s) | Role |
|---|---|---|
| `xymond_history` | `C_STACHG` | Appends status-change records to per-test flat-file history logs |
| `xymond_alert` | `C_PAGE` | Evaluates `alerts.cfg` rules and sends email/SMS/script alerts |
| `xymond_rrd` | `C_STATUS`, `C_DATA` | Parses status/data messages and writes RRD time-series databases |
| `xymond_client` | `C_CLIENT` | Parses client-collected system data sections and generates per-service status messages back to xymond via the backfeed queue |
| `xymond_hostdata` | `C_CLICHG` | Saves raw client data snapshot to disk when a client-driven status changes |
| `xymond_filestore` | `C_STATUS` (or other) | Optional: writes raw status messages to flat files on disk |
| `xymond_distribute` | `C_ENADIS` | Forwards enable/disable/drop/rename commands to peer Xymon servers |
| `xymond_channel` | any | Thin wrapper that attaches to one channel and execs a user-supplied worker binary via stdin/stdout |

### `xymond_channel` wrapper

Most workers in `tasks.cfg` are invoked as:

```
xymond_channel --channel=stachg xymond_history
```

`xymond_channel` attaches to the named IPC channel, reads messages, and writes them to the child
process stdin. This means any program that reads from stdin can be a Xymon worker — no IPC
knowledge required.

---

## Checkpoint and Restart

xymond periodically writes its full in-memory state to a checkpoint file
(`XYMONTMP/xymond.chk`). On restart it reads this file to restore state, so monitored hosts do
not all go purple after a brief daemon restart.

```
xymond --checkpoint-file=$XYMONTMP/xymond.chk --checkpoint-interval=600
```

The checkpoint format is a custom text format; it is read by `load_checkpoint()` and written by
`save_checkpoint()` inside `xymond.c`.

---

## Admin Senders

Commands that modify state (disable, enable, drop, rename, acknowledge, schedule) are only
accepted from IP addresses listed in `--admin-senders`:

```
xymond --admin-senders=127.0.0.1,$XYMONSERVERIP
```

`oksender(adminsenders, NULL, sender_addr, msg)` returns 0 and xymond silently ignores the
message if the sender IP is not in the whitelist.

---

## Ghost Hosts

A "ghost" is a host that sends status messages to xymond but is not listed in `hosts.cfg`.
xymond records these in `rbghosts` (a red-black tree) and serves the list to
`ghostlist.cgi` via the `ghostlist` protocol command. Ghosts do not get entries in `rbhosts`
or the status grid.

---

## Key Global Variables

| Variable | Purpose |
|---|---|
| `rbhosts` | Red-black tree of `xymond_hostlist_t *` keyed by hostname |
| `rbtests` | Red-black tree of `testinfo_t *` keyed by test name |
| `rbcookies` | Red-black tree of alert cookies (integer → `xymond_log_t *`) |
| `okcolors` | Bitmask from `OKCOLORS` env var |
| `alertcolors` | Bitmask from `ALERTCOLORS` env var |
| `flapcount` | Number of changes to track for flap detection (default 5) |
| `flapthreshold` | Time window in seconds for flap detection (default 1500s) |
| `defaultcookietime` | Seconds an alert cookie is valid (default 86400 = 1 day) |
| `defaultvalidity` | Default `STATUSLIFETIME` in minutes (default 30) |
