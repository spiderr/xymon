# Xymon Web UI Infrastructure

Detailed reference for the HTML5/Bootstrap 5 web UI on the `html5-bootstrap` branch.
See `docs/agents/xymon.md` for the overall architecture overview.

---

## Template Engine (`lib/headfoot.c`)

All HTML output flows through `headfoot()`:

```c
headfoot(FILE *output, const char *template, const char *pagepath,
         const char *head_or_foot, int bgcolor);
```

CGIs call it twice — once for `"header"`, once for `"footer"`. Between those two calls
the CGI writes its body content via `fprintf(stdout, ...)`. `xymongen/pagegen.c` uses
the same pattern but writes to file handles rather than stdout.

### 4-Tier File Resolution

`headfoot()` resolves the template file by trying these paths in order, stopping at
the first that exists:

| Tier | Path | Purpose |
|---|---|---|
| 1 | `web/<pagepath>/<template>_<type>` | Per-URL-path override (almost never used) |
| 2 | `web/<template>/<type>` | **Canonical** — all current templates live here |
| 3 | `web/<template>_<type>` | Legacy flat path — backward compat only |
| 4 | `web/shared/<type>` | Master fallback — used for all full-page templates |

`web/` resolves to `$XYMONHOME/web/`; source files live in `xymond/webfiles/`.

### Master Template

All full-page templates fall through to:
- `xymond/webfiles/shared/header` — emits `<!DOCTYPE html>`, `<head>`, Bootstrap/FA links,
  `<nav>` (Bootstrap navbar), `<header class="xymon-page-header d-flex">`, and the
  per-page `body_header_inc` injection point.
- `xymond/webfiles/shared/footer` — closes `<main>`, emits `<footer>`, Bootstrap JS,
  and the per-page `footer_inc` injection point.

The `<header>` element has exactly `class="xymon-page-header d-flex"` — visual styles
(padding, background, border, height, etc.) live in `xymon.css`, not in the template.

---

## Per-Page Template Subdirectory Layout

Each page template has its own subdirectory under `xymond/webfiles/<template>/`:

| File | Token in shared/header | Content |
|---|---|---|
| `head_inc` | `&PAGEHEAD` | `<meta http-equiv="refresh">` tags only (7 templates use this). May set `XYMONBODYCLASS=` override. |
| `body_header_inc` | `&XYMONPAGEHEADER` | Full inner `<header>` markup: `<h1>` + optional `#xymon-hdr-right` div with action buttons |
| `footer_inc` | `&PAGEFOOTER` | Page-specific JavaScript (e.g., the enable/disable host-list JS for `maint`) |

**`body_inc` is GONE.** The `&PAGEBODY` token is removed. Do not recreate either.
**`XYMONPAGEACTIONS` env var is REMOVED.** Button HTML lives in `body_header_inc`, not C code.

### `body_header_inc` Pattern

```html
<!-- Minimal (page with no action buttons): -->
<h1>&XYMONPAGETITLE</h1>

<!-- With action buttons: -->
<h1>&XYMONPAGETITLE</h1>
<div class="ms-auto d-flex gap-2" id="xymon-hdr-right">
  <a class="btn btn-outline-light btn-sm" href="...">
    <i class="fa-solid fa-icon-name"></i>
    <span class="d-none d-md-inline">Label</span>
  </a>
</div>
```

The `ms-auto` pushes the button group to the right within the `d-flex` header element.
Use `d-none d-md-inline` for button text labels (hidden on mobile, visible at ≥768px).

### Auto-Populated Variables

`headfoot()` sets these before rendering; templates must not assign them:

| Variable | Value |
|---|---|
| `&XYMONPAGETITLE` | Human-readable page title (from lookup table in headfoot.c ~line 1709) |
| `XYMONBODYCLASS` | Template name (used as `<body class="...">` for CSS scoping); override in `head_inc` if needed |

---

## Token Reference (`lib/headfoot.c:output_parsed`)

Key tokens expanded by the template engine:

| Token | Expands to |
|---|---|
| `&XYMONPAGETITLE` | Page title string |
| `&XYMONPAGEHEADER` | Contents of `body_header_inc` file |
| `&PAGEHEAD` | Contents of `head_inc` file |
| `&PAGEFOOTER` | Contents of `footer_inc` file |
| `&XYMWEBBACKGROUND` | Color name for page background (e.g. `green`) |
| `&XYMONEXTERNALS` | URL path to self-hosted Bootstrap/FA (`/xymon/ext`) |
| `&XYMONSKIN` | URL path to gifs/CSS directory |
| `&CGIBINURL` | URL prefix for CGI links (e.g. `/xymon-cgi`) |
| `&XYMONSTATUSMETA` | Status metadata line (host, service, color) — svcstatus pages |
| `&XYMONHISTORYBUTTON` | History button HTML — svcstatus pages |
| `&XYMONACKNOWLEDGEBUTTON` | Ack button (suppressed when already acked) — svcstatus pages |
| `&XYMONDISABLEBUTTON` | Disable button (suppressed when already disabled) — svcstatus pages |
| `&XYMONCOLORFILTER` | Color filter toggle button group HTML — overview pages |

---

## Bootstrap 5 and FontAwesome 6

Both are **self-hosted** — no CDN. Files live in `xymond/wwwfiles/externals/`.
`shared/header` references them via `&XYMONEXTERNALS`:

```html
<link rel="stylesheet" href="&XYMONEXTERNALS/bootstrap/css/bootstrap.min.css">
<link rel="stylesheet" href="&XYMONEXTERNALS/fontawesome/css/all.min.css">
<script src="&XYMONEXTERNALS/bootstrap/js/bootstrap.bundle.min.js"></script>
```

**Never add CDN links.** The server may have no internet access.

### Bootstrap Usage Conventions

- **No utility spacing/layout classes** (`p-*`, `m-*`, `top-0`, `end-0`, `rounded-*`, etc.)
  in C-emitted or template HTML. Define pixel values in `xymon.css` under a semantic class.
- **Semantic variant classes are fine** inline: `btn-sm`, `text-muted`, `fw-bold`, `btn-outline-secondary`.
  These express *what* something is, not explicit dimensions.
- Dark mode: the `<html>` element has `data-bs-theme="dark"` so Bootstrap's dark-mode
  CSS variables are active globally.

---

## Theme System

`XYMONTHEME` in `xymonserver.cfg` → resolves `&XYMONSTYLESSHEET` → loads
`themes/{name}/xymon.css`.

Shipped themes:
- `default` — base theme; all semantic CSS classes defined here
- `classic` — set as default for new installs; emulates the old Xymon color palette
- `monsters` — Halloween variant

Each theme has a `preview.html` at `/xymon/themes/{name}/preview.html`.

To override a value in a non-default theme, add the class to that theme's `xymon.css`
with a higher-specificity selector or the same selector (cascade wins for same specificity
since the theme CSS is loaded after default).

**Important `clear` state quirk:** The clear-state icon is `fa-regular fa-circle`
(hollow). FontAwesome hollow circles require `font-weight: 400`. The default theme sets
this; if a theme overrides `font-weight` globally, add `.xymon-clear { font-weight: 400; }`.

---

## Status Colors and Icon Rendering

### Color Classes

Every status icon element has two CSS classes:
1. `xymon-icon` — common icon sizing/positioning
2. `xymon-{color}` — color (e.g. `xymon-red`, `xymon-blue`)
3. Optional state: `xymon-acked`, `xymon-recent`

Color values in `xymon.css`:

```css
.xymon-green   { color: #00e676; }
.xymon-yellow  { color: #ffd600; }
.xymon-red     { color: #ff1744; }
.xymon-blue    { color: #00b0ff; }
.xymon-purple  { color: #e040fb; }
.xymon-clear   { color: #9e9e9e; }
.xymon-unknown { color: #9e9e9e; }
```

### `coloricon()` in `lib/color.c`

The primary function for rendering a status dot icon. Returns an HTML string like:

```html
<i class="xymon-icon xymon-red fa-solid fa-circle"></i>
```

Signature:
```c
char *coloricon(int color, int acked, int oldage);
```

- `acked=1` → `fa-solid fa-circle-check` (checkmark variant)
- `oldage=0` (recent change) → `fa-solid fa-circle-dot` (dot variant)
- `oldage=1` (stable) → `fa-solid fa-circle` (solid variant)
- `color=COL_CLEAR` → `fa-regular fa-circle` (hollow, stable); `fa-regular fa-circle-dot` (recent)

The three-variant logic replicates the old GIF naming: `green.gif`, `green-ack.gif`, `green-recent.gif`.

`colorname(int color)` returns the color name string (`"red"`, `"blue"`, etc.).

---

## CSS Architecture (`xymond/wwwfiles/themes/default/xymon.css`)

Section structure (use these comments as landmarks when adding new rules):

```
/* ── Status colors ── */
/* ── Alert banner tints ── */
/* ── Tables ── */
/* ── Status overview / pagegen ── */
/* ── Status overview color filter ── */
/* ── Page header ── */
/* ── Navbar ── */
/* ── RRD graphs / zoom ── */
/* ── nongreen "All OK" message ── */
/* ── Status log (htmllog.c) ── */
/* ── Event log ── */
/* ── Service status info ── */
/* ── Maintenance / enadis ── */
```

### Alert Banner Tints

Ack and Disable banners use `.alert.alert-info` paired with `.xymon-alert-{color}`:

```css
.alert-info.xymon-alert-red { --bs-alert-bg: rgba(255,23,68,0.10); ... }
```

This overrides Bootstrap's color variables while keeping its structural styles. The
current test color (not the banner's own color) determines which tint is applied — e.g.,
a red test that is acknowledged gets `xymon-alert-red`.

Banner CSS classes:
- `.xymon-log-alert` — Disabled banner (margin-bottom)
- `.xymon-log-ack` — Acknowledged / Previously Acknowledged banner (margin-top)

---

## Status Log Banners (`lib/htmllog.c`)

`generate_html_log()` renders the status page body. Banner rendering order:

1. **Disabled banner** — if `disabletime != 0`. Parses `dismsg` for structured fields:
   `Disabled by: USER @ HOST`, `Disabled at: DATE`, `Reason: TEXT`. Uses null-terminate
   + restore pattern (searches all pointers first, then terminates each to extract values,
   then restores before returning).

2. **Previously Acknowledged banner** — rendered immediately after the Disabled banner.
   Checks `dismsg` for `Ack-preserved-reason:` (embedded by `enadis.c` before disabling).
   See § Ack Preservation below.

3. **Acknowledged banner** — if `ackmsg` is non-NULL. Parses `ackmsg` for:
   `Acked by: USER (IP)`, `Acked at: DATE`. The reason is the text before the first
   `\nAcked` tag. Rendered above the "Current status message follows:" divider.

4. **"Current status message follows:"** — separator paragraph.

5. **Status body text** — the raw status log content in a `<pre>` block.

### Ack Preservation (5.0.1 Fix)

**Problem:** `OKCOLORS` includes `blue` by default. When a test is disabled (becomes
blue), `handle_status()` in `xymond.c` calls `decide_alertstate(COL_BLUE)` → returns
`A_OK` → clears `acktime` to 0 → since `acktime ≤ now`, frees and nulls `ackmsg`.

**Fix:** `web/enadis.c` ACT_DISABLE branch now:
1. Queries `xymondboard fields=hostname,testname,acktime,ackmsg` **before** sending the
   disable command.
2. If `acktime > time(NULL)` (active ack), extracts reason/by/at/until from `ackmsg`.
3. Embeds them as structured fields appended to `fullmsg` (which becomes `dismsg`):
   ```
   Ack-preserved-reason: TEXT
   Ack-preserved-by: USER
   Ack-preserved-at: DATE
   Ack-preserved-until: DATE
   ```
4. These fields are stored in `dismsg` which xymond retains even after clearing `ackmsg`.

`lib/htmllog.c` reads `dismsg`, searches for `\nAck-preserved-reason:`, and renders the
"Previously Acknowledged" banner if found.

**Caveat:** Only applies to disables performed after this code is deployed. Previously
disabled tests have no preserved fields.

---

## svcstatus Button Suppression (`lib/headfoot.c`)

`fetch_ackcookie()` queries `xymondboard fields=hostname,testname,cookie,disabletime,ackmsg`.
It populates three statics reset by `sethostenv()` each request:

| Static | Meaning |
|---|---|
| `ackcookie_val` | Cookie integer (0 = test not in alert state) |
| `have_ackmsg` | Non-zero when `ackmsg` field is non-empty (already acked) |
| `is_disabled` | Non-zero when `disabletime` field is non-zero (already disabled) |

Token behavior:
- `&XYMONACKNOWLEDGEBUTTON` — renders only when `ackcookie_val && !have_ackmsg`
- `&XYMONDISABLEBUTTON` — renders only when `!is_disabled`

Both buttons use `btn-outline-secondary` (neutral) rather than colored variants.
Button text labels use `d-none d-md-inline` breakpoint (hidden below 768px).

---

## Color Filter (`xymond/webfiles/shared/header` + `xymon.css`)

Overview pages (xymon.html, nongreen.html, critical.html) have a pure-CSS color filter.
No JavaScript required.

### Mechanism

1. Hidden `<input type="checkbox" class="btn-check xymon-filter-btn" id="filter-red">` +
   `<label>` pairs render as Bootstrap toggle buttons (one per color).

2. `&XYMONCOLORFILTER` token in `body_header_inc` inserts the button group into
   `#xymon-hdr-right`. nongreen omits the green button (no green rows on that page).
   critical includes all colors plus the Edit button.

3. CSS in `xymon.css` (§ Status overview color filter):

```css
/* Hide all host rows when any filter is active */
body:has(.xymon-filter-btn:checked) .xymon-host-row { display: none; }
/* Show back rows matching each checked color */
body:has(#filter-red:checked)    .xymon-host-row:has(.xymon-red)    { display: table-row; }
body:has(#filter-yellow:checked) .xymon-host-row:has(.xymon-yellow) { display: table-row; }
/* … etc for all colors … */
```

4. Host rows in `pagegen.c` have `class="xymon-host-row"`. Header rows, pretitle rows,
   and divider rows do not — they remain visible at all times.

**Browser support:** `:has()` requires Chrome 105+, Firefox 121+, Safari 15.4+.
Degrades gracefully in older browsers (buttons appear but all rows stay visible).

---

## `pagegen.c` Status Grid

`xymongen/pagegen.c` emits the host × service status table:

- Each host row: `<tr class="xymon-host-row">`
- Hostname cell: `<td class="text-nowrap xymon-host-cell">` — contains host link,
  optional page info icon, and inline metacol icons (info/trends/client).
- Service cells: one `<td>` per service column, each containing a `coloricon()` call
  wrapped in a link to `svcstatus.cgi`.
- Column headers: `<thead>` row, no `xymon-host-row` class.

Inline metacol icons (info, trends, client columns) render as small `<i>` tags using
`coloricon()` inside the hostname cell rather than separate service columns.

---

## `enadis.c` Disable/Enable Message Format

The `fullmsg` / `dismsg` field format (as stored in xymond and rendered by htmllog.c):

```
\nDisabled by: USERNAME @ HOSTNAME\nDisabled at: DAY MON DD HH:MM:SS YYYY\nReason: USER_TEXT\n
```

If an active ack was present at disable time, the following lines are appended:
```
\nAck-preserved-reason: TEXT (newlines flattened to spaces)
\nAck-preserved-by: USERNAME (IP)
\nAck-preserved-at: DAY MON DD HH:MM:SS YYYY
\nAck-preserved-until: DAY MON DD HH:MM:SS YYYY
```

The `ackmsg` field format (from `web/acknowledge.c`):
```
REASON_TEXT\nAcked by: USERNAME (IP)\nAcked at: DAY MON DD HH:MM:SS YYYY
```

---

## Event Log Pagination (`web/eventlog.c`)

`render_eventlog_paginated()` handles normal (non-topcount, non-summary) event log output:

1. Calls `do_eventlog(NULL, load_count, ...)` with output=NULL to collect events into a
   linked list (`event_t *events`).
2. Skips `page_offset` events from newest.
3. Renders `perpage` (=`maxcount`, default 100) events.
4. Emits Bootstrap `.pagination` nav above and below the table with Newer/Older links.
5. All current filter params are preserved in pagination link URLs.

`OFFSET` query param drives `page_offset`. `MAXCOUNT` drives `perpage`.

Default time window: `maxminutes = 0` → `do_eventlog` uses its 24-hour fallback
(elapsed time = 0 triggers the `else` branch that defaults to 1440 minutes internally).

---

## Fragment Templates

These templates do NOT fall through to `shared/header+footer`. They start with raw
`<body>` content and are injected inside another page's response by `lib/htmllog.c`:

| Template | Used by | Content |
|---|---|---|
| `xymonhostsvc/` | svcstatus.cgi body | Status log body + Ack/Disable modals |
| `xymonhistlog/` | history.cgi body | History log body |
| `xymontrends/` | trends col body | RRD graph list |
| `xymoninfo/` | info col body | Host info display |
| `xymonnormal/` | pagegen.c row fragments | Host row fragment (rarely used) |

`xymonhostsvc/footer_inc` contains the Ack and Disable Bootstrap modal HTML plus the
JavaScript that wires the disable host-list checkbox toggle.

---

## Hardcoded Paths (Cannot Move)

| File | Hardcoded in | Notes |
|---|---|---|
| `web/critack_form` | `lib/htmllog.c` | Ack form fragment; path built directly |
| `web/zoom.js` | `lib/xymonrrd.c` | RRD zoom script; path built directly |

---

## Common C Patterns

### Safe string buffers
```c
SBUF_DEFINE(buf);          /* declares: char *buf = NULL; size_t buf_buflen = 0; */
SBUF_MALLOC(buf, size);    /* malloc with buflen tracking */
SBUF_REALLOC(buf, size);   /* realloc if new size > current */
```

### HTML-safe output
```c
htmlquoted(str)   /* returns HTML-entity-encoded copy (static buffer — use immediately) */
```

### Environment variables
```c
xgetenv("CGIBINURL")   /* like getenv() but aborts with error message if missing */
```

### nldecode / nlencode
Backslash-escape encoding used in xymondboard/xymondlog protocol responses.
`\n` in a field value is encoded as the two-character sequence `\n`.
Call `nldecode((unsigned char *)p)` to decode in place before parsing field content.
