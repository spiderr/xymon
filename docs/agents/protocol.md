# Xymon Wire Protocol

All communication with xymond uses a plain-text TCP protocol on port 1984 (configurable via
`XYMONDPORT`). The client opens a socket, sends one message (no framing — the whole message is
written then the write-end is shut down), reads the optional response, and closes the socket.
Requests that return data block until the daemon closes the connection.

---

## C API — `lib/sendmsg.c`

```c
/* Send a single message; response written to resp->respstr or resp->respfd */
sendresult_t sendmessage(char *msg, char *recipient, int timeout, sendreturn_t *response);

/* Create/free/consume a response buffer */
sendreturn_t *newsendreturnbuf(int fullresponse, FILE *respfd);
void          freesendreturnbuf(sendreturn_t *s);
char         *getsendreturnstr(sendreturn_t *s, int takeover);
```

- `recipient` is an IP string or `NULL` to use `XYMSRV` env var.
- `timeout` — use `XYMON_TIMEOUT` (the standard 10-second constant).
- `fullresponse=1` reads until EOF; `0` reads until the first newline.
- Commands in `multircptcmds[]` (`status`, `combo`, `extcombo`, `meta`, `data`, `notify`,
  `enable`, `disable`, `drop`, `rename`, `client`) are fanned out to all servers listed in
  `XYMSERVERS`.  Query commands (`xymondboard`, `xymondlog`, `schedule`, etc.) go to a single
  server only.

### Combo messages

Clients that report many status checks at once should batch them:

```c
combo_start();
  init_status(color);
  addtostatus("status host.test color ...\n...");
  finish_status();
  /* repeat per check */
combo_end();   /* flushes as extcombo messages */
```

---

## Core message types

### `status`

```
status HOST.TEST COLOR DATE SUMMARY
<multi-line body>
```

- `HOST` — hostname (dots replace hyphens per `FQDN` setting)
- `TEST` — service/check name
- `COLOR` — `green`, `yellow`, `red`, `blue`, `purple`, `clear`
- Body text is stored verbatim; first line after the header line becomes `line1`
- `STATUS_LIFETIME` env var controls how long a status is valid before going purple

### `status+N` (extended lifetime)

```
status+N HOST.TEST COLOR DATE SUMMARY
```

Extends validity to N minutes from now instead of the default `STATUSLIFETIME`.

### `combo` / `extcombo`

Multiple status messages packed into one TCP frame:

```
extcombo N O1 O2 ... ON
status ...
status ...
```

Where `O1..ON` are byte offsets of each sub-message within the buffer.

### `data`

```
data HOST.SERVICE
<data payload>
```

Raw data delivered to the `data` IPC channel; consumed by `xymond_rrd` and others.

### `client`

```
client HOSTNAME OS
<sections delimited by [SECTION] markers>
```

Full client-collected system data. Delivered to the `client` channel; processed by
`xymond_client` to generate per-service status messages.

### `enable` / `disable`

```
disable HOST.TEST DURATION MESSAGE
disable HOST.TEST -1 MESSAGE     (until test recovers)
enable  HOST.TEST
```

`DURATION` is in minutes; `-1` means "until the test turns green/clear/blue".  
Delivered to the `enadis` IPC channel; also handled inline by xymond.

### `acknowledge`

```
xymondack COOKIE DURATION MESSAGE
```

`COOKIE` is the integer alert cookie from `xymondboard`'s `cookie` field.  
`DURATION` is in minutes.

### `drop` / `rename`

```
drop HOST [TEST]
rename HOST NEWHOST
rename HOST.TEST HOST.NEWTEST
```

Admin commands — remove a host/test from state, or rename it.

### `schedule`

```
schedule TIMESTAMP CMD ARGS...
schedule
```

First form schedules a command to run at `TIMESTAMP` (Unix time).  
Second form (blank) retrieves the list of scheduled commands.

### `query`

```
query HOST.TEST
```

Returns `COLOR SUMMARY_LINE` for the given test, or empty if unknown.

---

## Board queries — `xymondboard` and `xymondlog`

### `xymondboard`

```
xymondboard [fields=F1,F2,...] [host=^REGEX$] [test=^REGEX$] [color=red,yellow] [page=PAGEPATH] ...
```

Returns one tab-free `|`-separated line per matching test, then a blank line to signal EOF.  
Default fields (when `fields=` is omitted): `hostname|testname|color|flags|lastchange|logtime|validtime|acktime|disabletime|sender|cookie|line1`

### `xymondlog`

```
xymondlog HOST.TEST [fields=F1,F2,...]
```

Returns fields for a single test followed by the full status message text.  
Default fields: `hostname|testname|color|flags|lastchange|logtime|validtime|acktime|disabletime|sender|cookie|ackmsg|dismsg|client|modifiers`

### Board field reference

| Field name | Type | Description |
|---|---|---|
| `hostname` | string | Host name |
| `testname` | string | Service/check name |
| `color` | string | Current color: `green`, `yellow`, `red`, `blue`, `purple`, `clear` |
| `flags` | string | Test flags string (content depends on test type) |
| `lastchange` | Unix ts | When the current color started |
| `logtime` | Unix ts | When the last status update was received |
| `validtime` | Unix ts | When the status expires (turns purple if no update) |
| `acktime` | Unix ts | When the acknowledgement expires (0 = not acked) |
| `disabletime` | Unix ts | When the disable expires (0 = not disabled, negative = until-OK) |
| `sender` | string | IP/hostname of the last sender |
| `cookie` | int | Alert cookie (0 = not in alert state) |
| `line1` | string | First line of the status message body |
| `ackmsg` | nlencode | Full acknowledgement message (empty = not acked) |
| `dismsg` | nlencode | Full disable/reason message (empty = not disabled) |
| `msg` | nlencode | Full status message text |
| `client` | `Y`/`N` | Whether client data exists for this host |
| `clntstamp` | Unix ts | Timestamp of last client data |
| `acklist` | nlencode | Active ack list (for multi-level acks) |
| `modifiers` | nlencode | Modifier causes applied to this test |
| `flapinfo` | string | Flap state: `flapping/t0/tN/oldcolor/curcolor` |
| `stats` | string | `statuschanges=N` |
| `XMH_FIELD` | string | Any hosts.cfg tag via `xmh_item()`, e.g. `XMH_PAGEPATH` |
| `ip` | string | Host IP address |

### Board filter options

In addition to `fields=`, `xymondboard` accepts filter parameters:

| Parameter | Example | Matches |
|---|---|---|
| `host=` | `host=^web` | Hostname regex |
| `test=` | `test=^http$` | Test name regex |
| `color=` | `color=red,yellow` | Comma-separated color list |
| `page=` | `page=infrastructure` | Page path string |
| `lastchange<N` | `lastchange<3600` | Status changed within N seconds ago |
| `lastchange>N` | `lastchange>86400` | Status has not changed in N seconds |
| `acktime>0` | — | Has an active acknowledgement |
| `disabletime>0` | — | Currently disabled |

---

## Field encoding — `nlencode` / `nldecode`

Multi-line field values are encoded so they fit on a single `|`-delimited line:

- `nlencode()` (`lib/encoding.c:173`) replaces `\n` → `\f` and `|` → `\001`.
- `nldecode()` (`lib/encoding.c:222`) reverses the transformation in-place.

Always call `nldecode()` on `ackmsg`, `dismsg`, `msg`, and `acklist` fields before
parsing their content.

---

## Error codes (`sendresult_t`)

| Constant | Meaning |
|---|---|
| `XYMONSEND_OK` | Success |
| `XYMONSEND_EBADIP` | Bad or unresolvable IP |
| `XYMONSEND_ECONNFAILED` | TCP connect failed |
| `XYMONSEND_ETIMEOUT` | Timeout waiting for daemon |
| `XYMONSEND_EWRITEERROR` | Write error on socket |
| `XYMONSEND_EREADERROR` | Read error on socket |

---

## Typical usage pattern (CGI querying board)

```c
sendreturn_t *sret = newsendreturnbuf(1, NULL);  /* fullresponse=1 */
sendmessage("xymondboard fields=hostname,testname,color,cookie,ackmsg,dismsg",
            NULL, XYMON_TIMEOUT, sret);
char *board = getsendreturnstr(sret, 1);   /* takeover=1 */
freesendreturnbuf(sret);

/* Parse lines */
char *line = board;
while (line && *line) {
    char *eol = strchr(line, '\n');
    if (eol) *eol = '\0';
    /* split on | ... */
    if (eol) { *eol = '\n'; line = eol+1; } else break;
}
xfree(board);
```
