# Xymon CGI Layer

CGI programs in `web/` generate HTML on-demand. Each binary is invoked by Apache, emits
`Content-type: text/html\n\n`, calls `headfoot()` for the page header, writes body content
via `fprintf(stdout, ...)`, then calls `headfoot()` for the footer. The template system is
described in `ui.md`.

---

## CGI Inventory

### Public CGIs (`cgi-bin/` — no authentication required)

| Binary | Source | URL / Purpose |
|---|---|---|
| `svcstatus.cgi` | `web/svcstatus.c` | Status log for one host/service: `?HOST=h&SERVICE=s` |
| `history.cgi` | `web/history.c` | Color-change timeline: `?HISTFILE=host.service` |
| `criticalview.cgi` | `web/criticalview.c` | Critical systems dashboard |
| `eventlog.cgi` | `web/eventlog.c` | State-change event log: `?STARTTIME=&ENDTIME=&OFFSET=` |
| `acknowledge.cgi` | `web/acknowledge.c` | Submit an alert acknowledgement (POST) |
| `acknowledgements.cgi` | `web/acknowledgements.c` | List of active acknowledgements |
| `notifications.cgi` | `web/notifications.c` | Pending alert notification list |
| `findhost.cgi` | `web/findhost.c` | Search for a host: `?QUERY=name` |
| `ghostlist.cgi` | `web/ghostlist.c` | Hosts sending data but not in hosts.cfg |
| `hostgraphs.cgi` | `web/hostgraphs.c` | All RRD graphs for one host |
| `showgraph.cgi` | `web/showgraph.c` | Single RRD graph (`image/png`) or graph menu page |
| `report.cgi` | `web/report.c` | Availability report form + output |
| `reportlog.cgi` | `web/reportlog.c` | Report log for one host/service |
| `snapshot.cgi` | `web/snapshot.c` | View the status grid at a past point in time |
| `confreport.cgi` | `web/confreport.c` | Configuration report (hosts, tests, alerts) |
| `csvinfo.cgi` | `web/csvinfo.c` | Display a CSV file as an HTML table |
| `datepage.cgi` | `web/datepage.c` | HTTP redirect to a date-specific static page |
| `hostlist.cgi` | `web/hostlist.c` | List all monitored hosts (HTML or CSV) |
| `perfdata.cgi` | `web/perfdata.c` | Performance data export (HTML, CSV, XML) |
| `statusreport.cgi` | `web/statusreport.c` | One test across all hosts as a table |
| `boilerplate.cgi` | `web/boilerplate.c` | Render any named template as a page |
| `xymonpage.cgi` | `web/xymonpage.c` | Serve a named static Xymon page |
| `appfeed.cgi` | `web/appfeed.c` | **XML only** — mobile/API status feed, not an HTML target |

### Secure CGIs (`cgi-secure/` — require HTTP auth)

| Binary | Source | Purpose |
|---|---|---|
| `enadis.cgi` | `web/enadis.c` | Enable/disable a host or service |
| `criticaleditor.cgi` | `web/criticaleditor.c` | Edit `critical.cfg` priority rules |
| `ackinfo.cgi` | `web/ackinfo.c` | Submit a priority ack (ackinfo message) |
| `chpasswd.cgi` | `web/chpasswd.c` | Change web UI password |
| `useradm.cgi` | `web/useradm.c` | User administration |

Secure CGI URLs use `XYMONSERVERSECURECGIURL` env var (vs `XYMONSERVERCGIURL` for public).

---

## `cgi_request()` — Input Parsing (`lib/cgi.c`)

```c
typedef struct cgidata_t {
    char *name;
    char *value;
    struct cgidata_t *next;
} cgidata_t;

cgidata_t *cgi_request(void);   /* parses GET QUERY_STRING or POST body */
int        cgi_ispost(void);    /* 1 if REQUEST_METHOD == POST */
char      *cgi_error(void);     /* returns error text if cgi_request() returned NULL */
```

`cgi_request()` handles both `GET` (reads `QUERY_STRING`) and `POST` (reads `CONTENT_LENGTH`
bytes from stdin). It URL-decodes and splits on `&` / `=`, returning a linked list of
`cgidata_t`. Maximum request size is 1 MB.

Typical usage:

```c
cgidata_t *params = cgi_request();
if (!params) { /* error, output error page */ }

cgidata_t *pwalk;
for (pwalk = params; pwalk; pwalk = pwalk->next) {
    if (strcmp(pwalk->name, "HOST") == 0)    hostname = pwalk->value;
    if (strcmp(pwalk->name, "SERVICE") == 0) service  = pwalk->value;
}
```

---

## URL Helpers (`lib/cgiurls.c`)

These return static or heap-allocated URL strings. Use `CGIBINURL` env var for public CGIs.

```c
char *hostsvcurl(char *hostname, char *service, int htmlformat);
    /* → CGIBINURL/svcstatus.sh?HOST=h&SERVICE=s */

char *histcgiurl(char *hostname, char *service);
    /* → CGIBINURL/history.sh?HISTFILE=host.service */

char *histlogurl(char *hostname, char *service, time_t t, char *timetxt);
    /* → CGIBINURL/historylog.sh?HOST=h&SERVICE=s&TIMEBUF=... */
```

`commafy(hostname)` converts dots in a hostname to commas (the format used in `HISTFILE=`).

---

## Security — `csp_header()` and Referer Checking

### Content Security Policy

```c
char *csp_header(const char *pagename);
```

Returns a multi-line HTTP header string with `Content-Security-Policy`,
`X-Content-Security-Policy`, and `X-Webkit-CSP` headers, looked up from `cgioptions.cfg`.

Usage in secure CGIs:

```c
fprintf(stdout, "Content-type: text/html\n");
fprintf(stdout, "%s", csp_header("enadis"));
fprintf(stdout, "\n");
```

### Referer check

Before processing a form POST, secure CGIs call `cgi_refererok(expected_path)` (inside
`cgi.c`) which compares `HTTP_REFERER` against `XYMONWEBHOST + expected_path`. This prevents
cross-site form submissions.

### `oksender()` — daemon-side IP whitelist

Inside xymond, board query commands (`xymondboard`, `xymondlog`) are only honored from IPs in
`wwwsenders`. Admin commands use a separate `adminsenders` list. Both lists are populated from
`--www-senders` and `--admin-senders` command-line options on `xymond`.

---

## Typical CGI Skeleton

```c
#include "libxymon.h"

int main(int argc, char *argv[])
{
    char *hostname = NULL, *service = NULL;
    cgidata_t *params, *pwalk;

    /* Must be first — reads env, sets up xgetenv() */
    libxymon_init(argv[0]);

    /* Emit HTTP headers */
    fprintf(stdout, "Content-type: text/html\n\n");

    /* Parse query params */
    params = cgi_request();
    for (pwalk = params; pwalk; pwalk = pwalk->next) {
        if (strcmp(pwalk->name, "HOST") == 0)    hostname = pwalk->value;
        if (strcmp(pwalk->name, "SERVICE") == 0) service  = pwalk->value;
    }

    /* Emit page header via template engine */
    headfoot(stdout, "mytemplate", "", "header", COL_GREEN);

    /* ... body content ... */
    fprintf(stdout, "<p>Hello from %s.%s</p>\n",
            hostname ? hostname : "(none)",
            service  ? service  : "(none)");

    /* Emit page footer */
    headfoot(stdout, "mytemplate", "", "footer", COL_GREEN);

    return 0;
}
```

---

## Key Environment Variables Used by CGIs

| Variable | Purpose |
|---|---|
| `CGIBINURL` | URL prefix for public CGI scripts |
| `XYMONSERVERSECURECGIURL` | URL prefix for secure CGI scripts |
| `XYMONWEBHOSTURL` | Full URL to the Xymon web root |
| `XYMONWEB` | URL path component for the Xymon static files |
| `XYMSRV` | IP/hostname of xymond (for `sendmessage()`) |
| `XYMONHOME` | Filesystem path to the Xymon installation root |
| `HOSTSCFG` | Path to `hosts.cfg` |
| `XYMONWEBHOST` | Host portion of the web URL (for referer checking) |

---

## Notes on Specific CGIs

### `svcstatus.cgi`

Calls `lib/htmllog.c:showsvcstatus()` for the body. This function queries `xymondlog` for the
current status and renders the formatted log text, ack/disable banners, and the history button.
Template: `hostsvc` (full page) or `xymonhostsvc` (fragment for embedding).

### `history.cgi`

Reads per-test history log files from `XYMONHISTDIR`. Template: `hist`.

### `enadis.cgi`

Handles both the disable/enable form (`GET` → show form, `POST` → send command) and the
scheduled maintenance list. Sends `disable HOST.TEST DURATION MESSAGE` or `enable HOST.TEST`
to xymond via `sendmessage()`. Template: `maintact` (form), `maint` (list).

### `eventlog.cgi`

Queries `xymondboard` with time filters. Supports server-side pagination via `OFFSET` param.
Template: `event`.

### `criticalview.cgi`

Reads `critical.cfg` and queries `xymondboard` for matching hosts. Template: `critical`
(single section), `critmulti` (multiple sections), `divider`.

### `showgraph.cgi`

When called with graph parameters returns `image/png` directly (no HTML template). When called
without parameters renders a graph menu page. Template: `graphs` for HTML mode.
