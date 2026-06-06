# Xymon Web UI Infrastructure

Detailed reference for HTML generation, the template engine, and the status icon system.
See `docs/agents/xymon.md` for the overall architecture.

---

## Template Engine (`lib/headfoot.c`)

All HTML output flows through `headfoot()`:

```c
headfoot(FILE *output, const char *template, const char *pagepath,
         const char *head_or_foot, int bgcolor);
```

CGIs call it twice — once with `"header"`, once with `"footer"`. Between those calls the
CGI writes its body content via `fprintf(stdout, ...)`. `xymongen/pagegen.c` uses the
same pattern but writes to file handles on disk.

### Template File Resolution

`headfoot()` resolves the template file via a path-walking fallback:

1. **Pagepath-specific:** Takes the `pagepath` argument (e.g. `"servers/web"`) and
   builds a filename by joining path components with underscores:
   `$XYMONHOME/web/template_servers_web_header`. Tries progressively shorter paths
   (dropping the rightmost component each time) until a file opens.
2. **Template default:** `$XYMONHOME/web/template_header` — the canonical flat file.

All template files live as flat files in `$XYMONHOME/web/` (source:
`xymond/webfiles/`). The naming convention is `<template>_<type>`, for example:

| Template name | Header file | Footer file |
|---|---|---|
| `stdnormal` | `stdnormal_header` | `stdnormal_footer` |
| `hostsvc` | `hostsvc_header` | `hostsvc_footer` |
| `event` | `event_header` | `event_footer` |
| `maint` | `maint_header` | `maint_footer` |

Form fragments (e.g. `maint_form`, `event_form`) follow the same naming but are loaded
by `showform()` in `lib/headfoot.c` rather than by `headfoot()` itself.

There is **no shared master template** — every template file is a complete,
self-contained HTML document from `<!DOCTYPE HTML>` to `</HTML>`.

---

## Template Structure — What Each File Contains

Every `*_header` file is a complete HTML document opening:

```html
<!DOCTYPE HTML PUBLIC "-//W3C//DTD HTML 4.0//EN">
<HTML>
<HEAD>
  <META HTTP-EQUIV="Content-Type" CONTENT="&HTMLCONTENTTYPE">
  <META HTTP-EQUIV="REFRESH" CONTENT="&XYMWEBREFRESH">
  <META HTTP-EQUIV="EXPIRES" ...>
  <META HTTP-EQUIV="Set-Cookie" CONTENT="pagepath=&XYMWEBPAGEPATH; path=/">
  <TITLE>&XYMWEBBACKGROUND : Xymon - ...</TITLE>
  <link rel="stylesheet" href="&XYMONBODYCSS">
  <link rel="stylesheet" href="&XYMONBODYMENUCSS">
  <link rel="shortcut icon" href="&XYMONSKIN/favicon-&XYMWEBBACKGROUND.ico">
</HEAD>
<BODY class="&XYMWEBBACKGROUND">
&XYMONBODYHEADER
<!-- … page-specific layout … -->
```

Every `*_footer` file closes the document:

```html
<TABLE SUMMARY="Bottomline" ...>
  <TR><TD ALIGN=RIGHT><FONT ...>Xymon &XYMONDREL</FONT></TD></TR>
</TABLE>
<!-- cookie JS workaround for browsers that ignore <meta http-equiv="Set-Cookie"> -->
<script>...</script>
</BODY>
</HTML>
```

The **cookie JavaScript** in every footer is a known workaround: some browsers ignore
`<meta http-equiv="Set-Cookie">`, so the footer JS parses those meta tags and calls
`document.cookie` directly. Do not remove it without understanding the context.

---

## Token Reference (`lib/headfoot.c:output_parsed`)

Tokens are `&UPPERCASE_NAME` sequences in template files. `output_parsed()` scans for
`&` followed by uppercase letters, digits, and underscores, and substitutes:

### Page context tokens

| Token | Also known as | Expands to |
|---|---|---|
| `&XYMWEBDATE` | `&BBDATE` | Current date/time (formatted by `XYMONDATEFORMAT`) |
| `&XYMWEBBACKGROUND` | `&BBBACKGROUND` | Page overall color name (e.g. `red`) |
| `&XYMWEBCOLOR` | `&BBCOLOR` | Current host/service color name |
| `&XYMWEBSVC` | `&BBSVC` | Current service name |
| `&XYMWEBHOST` | `&BBHOST` | Current host name |
| `&XYMWEBHIKEY` | `&BBHIKEY` | Host highlight key (falls back to hostname) |
| `&XYMWEBIP` | `&BBIP` | Current host IP address |
| `&XYMWEBREFRESH` | `&BBREFRESH` | Page auto-refresh interval in seconds |
| `&XYMWEBPAGEPATH` | `&BBPAGEPATH` | URL page path component |
| `&LOGTIME` | | Timestamp of the status log entry |
| `&XYMONDREL` | | Xymon version string |

### Asset and URL tokens

| Token | Default value | Purpose |
|---|---|---|
| `&XYMONBODYCSS` | `$XYMONSKIN/xymonbody.css` | Main body stylesheet URL |
| `&XYMONBODYMENUCSS` | `$XYMONMENUSKIN/xymonmenu.css` | Menu bar stylesheet URL |
| `&XYMONSKIN` | `$XYMONSERVERWWWURL/gifs` | URL to GIF icon directory |
| `&CGIBINURL` | | URL prefix for CGI scripts (e.g. `/xymon-cgi`) |
| `&XYMONWEB` | | URL to the Xymon web directory |

### Navigation

| Token | Expands to |
|---|---|
| `&XYMONBODYHEADER` | Menu bar HTML loaded from `$XYMONHOME/etc/xymonmenu.cfg` (or `XYMONBODYHEADER` env var) |
| `&XYMONBODYFOOTER` | Optional page footer HTML (default empty) |

### Fallback

Any `&TOKEN` whose name is not handled explicitly is looked up as an environment
variable via `xgetenv(TOKEN)`. If the variable exists its value is substituted; if not,
the `&TOKEN` literal is left in the output unchanged.

---

## Navigation Menu (`xymonmenu.cfg`)

`&XYMONBODYHEADER` loads `$XYMONHOME/etc/xymonmenu.cfg` (the `file:` prefix in the env
default triggers a file-read rather than literal substitution). This file contains raw
HTML for the navigation menu bar — a custom CSS dropdown using `xymonmenu-blue.css` /
`xymonmenu-grey.css` and small gradient GIFs (`b2t-blue.gif`, `t2b-blue.gif`, etc.) in
`xymond/wwwfiles/menu/`. The menu uses `$XYMONSERVERWWWURL` and `$XYMONSERVERCGIURL`
variable substitution within the file for link hrefs.

---

## Status Icons

### `dotgiffilename()` (`lib/color.c:103`)

The primary function for building a status icon filename for use in `<IMG SRC="...">`:

```c
char *dotgiffilename(int color, int acked, int oldage);
```

Returns a static buffer with a filename like `green.gif`, `red-ack.gif`, or
`yellow-recent.gif`. Logic:

- Base = `colorname(color)` (e.g. `"green"`)
- If `acked`: append `"-ack"` → `green-ack.gif`
- Else if `use_recentgifs && !oldage`: append `"-recent"` → `green-recent.gif`
- Append `"." + $IMAGEFILETYPE` (default `"gif"`)

`use_recentgifs` is a global in `lib/color.c` controlled by the `XYMON_USE_RECENTGIFS`
env var. When false, the `-recent` variant is never generated.

### Icon dimensions

`$DOTHEIGHT` and `$DOTWIDTH` (default `16`) control the `HEIGHT` and `WIDTH` attributes
on every `<IMG>` tag. Set in `lib/environ.c` and overridable in `xymonserver.cfg`.

### Full icon set (`xymond/wwwfiles/gifs/`)

| GIF filename | Meaning |
|---|---|
| `green.gif` | OK, stable |
| `green-recent.gif` | OK, recently changed |
| `green-ack.gif` | OK, acknowledged |
| `yellow.gif` | Warning, stable |
| `yellow-recent.gif` | Warning, recently changed |
| `yellow-ack.gif` | Warning, acknowledged |
| `red.gif` | Critical/error, stable |
| `red-recent.gif` | Critical/error, recently changed |
| `red-ack.gif` | Critical/error, acknowledged |
| `blue.gif` | Disabled / maintenance |
| `purple.gif` | No data / unreachable |
| `clear.gif` | Not tested / no status |
| `arrow.gif` | Event log from→to separator |
| `zoom.gif` | RRD graph zoom link |
| `bkg-{color}.gif` | Page body background tint |

---

## `pagegen.c` Status Grid

`xymongen/pagegen.c` emits the host × service overview table. Each cell:

```c
fprintf(output,
  "<A HREF=\"%s/svcstatus.sh?HOST=%s&SERVICE=%s\">"
  "<IMG SRC=\"%s/%s\" ALT=\"%s\" TITLE=\"%s\" HEIGHT=\"%s\" WIDTH=\"%s\" BORDER=0></A>",
  cgibinurl, hostname, service,
  skin, dotgiffilename(color, acked, oldage),
  colorname(color), title_text,
  xgetenv("DOTHEIGHT"), xgetenv("DOTWIDTH"));
```

The info, trends, and client columns render as separate `<IMG>` cells using
`infocolumngif`, `trendscolumngif`, and `clientcolumngif` — each a `dotgiffilename()`
result, optionally overridden by `INFOCOLUMNGIF`, `TRENDSCOLUMNGIF`, `CLIENTCOLUMNGIF`
env vars.

---

## Status Log Page (`lib/htmllog.c`)

`generate_html_log()` renders the status page body. Structure:

1. Service identification header (`<TABLE>` + `<FONT>`)
2. History button (top position if `histlocation == HIST_TOP`)
3. **Disabled banner** (if `disabletime != 0`): `<H3>Disabled until DATE</H3>` + `<PRE>` disable message
4. **Planned downtime** (if `dismsg` set but `disabletime == 0`): `<H3>Planned downtime:</H3>`
5. **Status modifiers** (`modifiers` field from xymond, nldecoded)
6. First-line status heading (`textwithcolorimg()` in an `<H3>`)
7. Status body text in `<PRE>` block
8. Metadata footer: time-since-change, sender, client-data link
9. **Acknowledged banner** (if `ackmsg` set): inline `<font>` with reason, "Acked by:", duration
10. RRD graph links (if applicable)
11. History button (bottom position if `histlocation == HIST_BOTTOM`)

The ack banner is at the **bottom** of the page, after the status body text.

---

## Acknowledgement and Disable Workflow

**Acknowledging:** `web/acknowledge.c` reads the `cookie` field from the URL/form,
constructs an `xymondack COOKIE DURATION MESSAGE` command, and sends it. The `COOKIE`
value is fetched by querying `xymondboard fields=cookie` or passed directly in the URL.

**Disabling:** `web/enadis.c` sends `disable HOST.TEST DURATION MESSAGE`. The disable
message (`fullmsg`) format is:

```
\nDisabled by: USERNAME @ HOSTNAME\nReason: USER_TEXT\n
```

The rendered disable banner shows the expiry time via `ctime(&disabletime)` and the
raw disable message in a `<PRE>` block. Clicking Disable navigates to a separate form
page (`enadis.cgi`) — there is no inline modal on the status page.

---

## Event Log (`web/eventlog.c` + `lib/eventlog.c`)

Default time window: `maxminutes = 240` (4 hours). No server-side pagination — the full
result set is emitted as a single HTML table. Service name in each row is plain text
(no link to svcstatus). Timestamps use `ctime()` format.

---

## Known Behavioral Nuance: COL_BLUE and OKCOLORS

`OKCOLORS` defaults to `"green,blue,clear"`. When a test is disabled it becomes blue.
`decide_alertstate(COL_BLUE)` returns `A_OK`, so `handle_status()` zeroes `acktime`.
Since `acktime ≤ now`, it then frees and nulls `ackmsg`. The net result: **any active
acknowledgement is silently discarded the moment a test is disabled**.

---

## Common C Patterns

### Safe string buffers
```c
SBUF_DEFINE(buf);          /* char *buf = NULL; size_t buf_buflen = 0; */
SBUF_MALLOC(buf, size);    /* malloc with buflen tracking */
SBUF_REALLOC(buf, size);   /* realloc only if new size > current */
```

### Stack-allocated buffers
```c
MEMDEFINE(buf);    /* zero-initialize a stack array (compiler appeasement) */
MEMUNDEFINE(buf);  /* companion no-op — used symmetrically for clarity */
```

### HTML-safe output
```c
htmlquoted(str)   /* HTML-entity-encode; returns a static buffer — use immediately */
```

### Environment
```c
xgetenv("VARNAME")  /* like getenv() but calls errprintf+exit if variable is missing */
```

### nldecode / nlencode
Backslash-escape encoding used in `xymondboard` / `xymondlog` protocol responses.
`\n` in a field value is transmitted as the two-character sequence `\n`.
Call `nldecode((unsigned char *)p)` to decode a field value in-place before parsing.

