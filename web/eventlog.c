/*----------------------------------------------------------------------------*/
/* Xymon eventlog generator tool.                                             */
/*                                                                            */
/* This displays the "eventlog" found on the "All non-green status" page.     */
/* It also implements a CGI tool to show an eventlog for a given period of    */
/* time, as a reporting function.                                             */
/*                                                                            */
/* Copyright (C) 2002-2011 Henrik Storner <henrik@storner.dk>                 */
/* Host/test/color/start/end filtering code by Eric Schwimmer 2005            */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id$";

#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>

#include "libxymon.h"

int	maxcount = 100;		/* Default: Include last 100 events */
int	maxminutes = 0;		/* Default: 0 → 24-hour window (see do_eventlog else branch) */
int	page_offset = 0;	/* Events to skip from newest — used for pagination */
char	*totime = NULL;
char	*fromtime = NULL;
char	*hostregex = NULL;
char	*exhostregex = NULL;
char	*testregex = NULL;
char	*extestregex = NULL;
char	*pageregex = NULL;
char	*expageregex = NULL;
char	*colorregex = NULL;
int	ignoredialups = 0;
int	topcount = 0;
eventsummary_t summarybar = XYMON_S_NONE;
countsummary_t counttype = XYMON_COUNT_NONE;
char	*webfile_hf = "event";
char	*webfile_form = "event_form";
cgidata_t *cgidata = NULL;
char 	*periodstring = NULL;


static void parse_query(void)
{
	cgidata_t *cwalk;

	cwalk = cgidata;
	while (cwalk) {
		/*
		 * cwalk->name points to the name of the setting.
		 * cwalk->value points to the value (may be an empty string).
		 */

		if (strcasecmp(cwalk->name, "MAXCOUNT") == 0) {
			maxcount = atoi(cwalk->value);
		}
		else if (strcasecmp(cwalk->name, "MAXTIME") == 0) {
			maxminutes = atoi(cwalk->value);
		}
		else if (strcasecmp(cwalk->name, "FROMTIME") == 0) {
			if (*(cwalk->value)) fromtime = strdup(cwalk->value);
		}
		else if (strcasecmp(cwalk->name, "TOTIME") == 0) {
			if (*(cwalk->value)) totime = strdup(cwalk->value);
		}
		else if (strcasecmp(cwalk->name, "HOSTMATCH") == 0) {
			if (*(cwalk->value)) hostregex = strdup(cwalk->value);
		}
		else if (strcasecmp(cwalk->name, "EXHOSTMATCH") == 0) {
			if (*(cwalk->value)) exhostregex = strdup(cwalk->value);
		}
		else if (strcasecmp(cwalk->name, "TESTMATCH") == 0) {
			if (*(cwalk->value)) testregex = strdup(cwalk->value);
		}
		else if (strcasecmp(cwalk->name, "EXTESTMATCH") == 0) {
			if (*(cwalk->value)) extestregex = strdup(cwalk->value);
		}
		else if (strcasecmp(cwalk->name, "PAGEMATCH") == 0) {
			if (*(cwalk->value)) pageregex = strdup(cwalk->value);
		}
		else if (strcasecmp(cwalk->name, "EXPAGEMATCH") == 0) {
			if (*(cwalk->value)) expageregex = strdup(cwalk->value);
		}
		else if (strcasecmp(cwalk->name, "COLORMATCH") == 0) {
			if (*(cwalk->value)) colorregex = strdup(cwalk->value);
		}
		else if (strcasecmp(cwalk->name, "OFFSET") == 0) {
			if (*(cwalk->value)) page_offset = atoi(cwalk->value);
			if (page_offset < 0) page_offset = 0;
		}
		else if (strcasecmp(cwalk->name, "NODIALUPS") == 0) {
			ignoredialups = 1;
		}
		else if (strcasecmp(cwalk->name, "TOP") == 0) {
			if (*(cwalk->value)) topcount = atoi(cwalk->value);
		}
		else if (strcasecmp(cwalk->name, "SUMMARY") == 0) {
			if (strcasecmp(cwalk->value, "hosts") == 0) summarybar = XYMON_S_HOST_BREAKDOWN;
			else if (strcasecmp(cwalk->value, "services") == 0) summarybar = XYMON_S_SERVICE_BREAKDOWN;
			else summarybar = XYMON_S_NONE;
		}
		else if (strcasecmp(cwalk->name, "COUNTTYPE") == 0) {
			if (strcasecmp(cwalk->value, "events") == 0) counttype = XYMON_COUNT_EVENTS;
			else if (strcasecmp(cwalk->value, "duration") == 0) counttype = XYMON_COUNT_DURATION;
			else counttype = XYMON_COUNT_NONE;
		}
		else if (strcasecmp(cwalk->name, "TIMETXT") == 0) {
			if (*(cwalk->value)) periodstring = strdup(cwalk->value);
		}

		cwalk = cwalk->next;
	}
}

void show_topchanges(FILE *output, 
		     countlist_t *hostcounthead, countlist_t *svccounthead, event_t *eventhead, 
		     int topcount, time_t firstevent, time_t lastevent)
{
	fprintf(output, "<p class=\"event-period\">%s</p>\n", (periodstring ? periodstring : ""));

	fprintf(output, "<div class=\"topchanges row\">\n");
	if (hostcounthead && (output != NULL)) {
		countlist_t *cwalk;
		int i;
		unsigned long others = 0, totalcount = 0;
		strbuffer_t *s = newstrbuffer(0);
		strbuffer_t *othercriteria = newstrbuffer(0);

		if (hostregex) {
			addtobuffer(othercriteria, "&amp;HOSTMATCH=");
			addtobuffer(othercriteria, hostregex);
		}
		if (exhostregex) addtobuffer(s, exhostregex);
		if (testregex) {
			addtobuffer(othercriteria, "&amp;TESTMATCH=");
			addtobuffer(othercriteria, testregex);
		}
		if (extestregex) {
			addtobuffer(othercriteria, "&amp;EXTESTMATCH=");
			addtobuffer(othercriteria, extestregex);
		}
		if (pageregex) {
			addtobuffer(othercriteria, "&amp;PAGEMATCH=");
			addtobuffer(othercriteria, pageregex);
		}
		if (expageregex) {
			addtobuffer(othercriteria, "&amp;EXPAGEMATCH=");
			addtobuffer(othercriteria, expageregex);
		}
		if (colorregex) {
			addtobuffer(othercriteria, "&amp;COLORMATCH=");
			addtobuffer(othercriteria, colorregex);
		}
		if (ignoredialups) {
			addtobuffer(othercriteria, "&amp;NODIALUPS=on");
		}
		addtobuffer(othercriteria, "&amp;SUMMARY=services");
		addtobuffer(othercriteria, "&amp;TIMETXT=");
		addtobuffer(othercriteria, periodstring);
		if (counttype == XYMON_COUNT_EVENTS) addtobuffer(othercriteria, "&amp;COUNTTYPE=events");
		else if (counttype == XYMON_COUNT_DURATION) addtobuffer(othercriteria, "&amp;COUNTTYPE=duration");

		fprintf(output, "<div class=\"col-12 col-md-6\">\n");
		fprintf(output, "<div class=\"table-responsive\"><table class=\"table table-sm table-bordered topchanges-hosts\">\n");
		fprintf(output, "<tr><th colspan=\"3\">Top %d hosts</th></tr>\n", topcount);
		fprintf(output, "<tr><th>Host</th><th colspan=\"2\">%s</th></tr>\n",
			(counttype == XYMON_COUNT_EVENTS) ? "State changes" : "Seconds red/yellow");

		/* Compute the total count */
		for (i=0, cwalk=hostcounthead; (cwalk); i++, cwalk=cwalk->next) totalcount += cwalk->total;

		for (i=0, cwalk=hostcounthead; (cwalk && (cwalk->total > 0)); i++, cwalk=cwalk->next) {
			if (i < topcount) {
				fprintf(output, "<tr><td><a href=\"eventlog.sh?HOSTMATCH=^%s$&amp;MAXCOUNT=-1&amp;MAXTIME=-1&amp;FROMTIME=%lu&amp;TOTIME=%lu%s\">%s</a></td><td class=\"text-end\">%lu</td><td class=\"text-end\">(%6.2f %%)</td></tr>\n",
					xmh_item(cwalk->src, XMH_HOSTNAME), 
					(unsigned long)firstevent, (unsigned long)lastevent,
					STRBUF(othercriteria),
					xmh_item(cwalk->src, XMH_HOSTNAME), 
					cwalk->total, ((100.0 * cwalk->total) / totalcount));
				if (STRBUFLEN(s) > 0) addtobuffer(s, "|"); 
				addtobuffer(s, "^");
				addtobuffer(s, xmh_item(cwalk->src, XMH_HOSTNAME));
				addtobuffer(s, "$");
			}
			else {
				others += cwalk->total;
			}
		}
		fprintf(output, "<tr><td><a href=\"eventlog.sh?EXHOSTMATCH=%s&amp;MAXCOUNT=-1&amp;MAXTIME=-1&amp;FROMTIME=%lu&amp;TOTIME=%lu%s\">%s</a></td><td class=\"text-end\">%lu</td><td class=\"text-end\">(%6.2f %%)</td></tr>\n",
			STRBUF(s),
			(unsigned long)firstevent, (unsigned long)lastevent,
			STRBUF(othercriteria),
			"Other hosts", 
			others, ((100.0 * others) / totalcount));
		fprintf(output, "<tr><td colspan=\"3\"><hr></td></tr>\n");
		fprintf(output, "<tr><th>Total</th><th>%lu</th><th>&nbsp;</th></tr>\n", totalcount);
		fprintf(output, "</table></div>\n");
		fprintf(output, "</div>\n");

		freestrbuffer(s);
		freestrbuffer(othercriteria);
	}
	if (svccounthead && (output != NULL)) {
		countlist_t *cwalk;
		int i;
		unsigned long others = 0, totalcount = 0;
		strbuffer_t *s = newstrbuffer(0);
		strbuffer_t *othercriteria = newstrbuffer(0);

		if (hostregex) {
			addtobuffer(othercriteria, "&amp;HOSTMATCH=");
			addtobuffer(othercriteria, hostregex);
		}
		if (exhostregex) {
			addtobuffer(othercriteria, "&amp;EXHOSTMATCH=");
			addtobuffer(othercriteria, exhostregex);
		}
		if (testregex) {
			addtobuffer(othercriteria, "&amp;TESTMATCH=");
			addtobuffer(othercriteria, testregex);
		}
		if (extestregex) addtobuffer(s, extestregex);
		if (pageregex) {
			addtobuffer(othercriteria, "&amp;PAGEMATCH=");
			addtobuffer(othercriteria, pageregex);
		}
		if (expageregex) {
			addtobuffer(othercriteria, "&amp;EXPAGEMATCH=");
			addtobuffer(othercriteria, expageregex);
		}
		if (colorregex) {
			addtobuffer(othercriteria, "&amp;COLORMATCH=");
			addtobuffer(othercriteria, colorregex);
		}
		if (ignoredialups) {
			addtobuffer(othercriteria, "&amp;NODIALUPS=on");
		}
		addtobuffer(othercriteria, "&amp;SUMMARY=hosts");
		addtobuffer(othercriteria, "&amp;TIMETXT=");
		addtobuffer(othercriteria, periodstring);
		if (counttype == XYMON_COUNT_EVENTS) addtobuffer(othercriteria, "&amp;COUNTTYPE=events");
		else if (counttype == XYMON_COUNT_DURATION) addtobuffer(othercriteria, "&amp;COUNTTYPE=duration");


		fprintf(output, "<div class=\"col-12 col-md-6\">\n");
		fprintf(output, "<div class=\"table-responsive\"><table class=\"table table-sm table-bordered topchanges-services\">\n");
		fprintf(output, "<tr><th colspan=\"3\">Top %d services</th></tr>\n", topcount);
		fprintf(output, "<tr><th>Service</th><th colspan=\"2\">%s</th></tr>\n",
			(counttype == XYMON_COUNT_EVENTS) ? "State changes" : "Seconds red/yellow");

		/* Compute the total count */
		for (i=0, cwalk=svccounthead; (cwalk); i++, cwalk=cwalk->next) totalcount += cwalk->total;

		for (i=0, cwalk=svccounthead; (cwalk && (cwalk->total > 0)); i++, cwalk=cwalk->next) {
			if (i < topcount) {
				fprintf(output, "<tr><td><a href=\"eventlog.sh?TESTMATCH=^%s$&amp;MAXCOUNT=-1&amp;MAXTIME=-1&amp;FROMTIME=%lu&amp;TOTIME=%lu%s\">%s</a></td><td class=\"text-end\">%lu</td><td class=\"text-end\">(%6.2f %%)</td></tr>\n",
					((htnames_t *)cwalk->src)->name, 
					(unsigned long)firstevent, (unsigned long)lastevent,
					STRBUF(othercriteria),
					((htnames_t *)cwalk->src)->name, 
					cwalk->total, ((100.0 * cwalk->total) / totalcount));
				if (STRBUFLEN(s) > 0) addtobuffer(s, "|"); 
				addtobuffer(s, "^");
				addtobuffer(s, ((htnames_t *)cwalk->src)->name);
				addtobuffer(s, "$");
			}
			else {
				others += cwalk->total;
			}
		}
		fprintf(output, "<tr><td><a href=\"eventlog.sh?EXTESTMATCH=%s&amp;MAXCOUNT=-1&amp;MAXTIME=-1&amp;FROMTIME=%lu&amp;TOTIME=%lu%s\">%s</a></td><td class=\"text-end\">%lu</td><td class=\"text-end\">(%6.2f %%)</td></tr>\n",
			STRBUF(s),
			(unsigned long)firstevent, (unsigned long)lastevent,
			STRBUF(othercriteria),
			"Other services", 
			others, ((100.0 * others) / totalcount));
		fprintf(output, "<tr><td colspan=\"3\"><hr></td></tr>\n");
		fprintf(output, "<tr><th>Total</th><th>%lu</th><th>&nbsp;</th></tr>\n", totalcount);
		fprintf(output, "</table></div>\n");
		fprintf(output, "</div>\n");

		freestrbuffer(s);
		freestrbuffer(othercriteria);
	}
	fprintf(output, "</div>\n");
}

static void emit_event_pagination_nav(FILE *output, const char *pbase,
		int has_prev, int prev_off,
		int has_next, int next_off,
		const char *title)
{
	fprintf(output,
		"<nav class=\"xymon-event-pagination\" aria-label=\"Event log pagination\">"
		"<ul class=\"pagination pagination-sm justify-content-center\">\n");

	if (has_prev)
		fprintf(output,
			"<li class=\"page-item\">"
			"<a class=\"page-link\" href=\"%s&amp;OFFSET=%d\">"
			"<i class=\"fa-solid fa-chevron-left\"></i> Newer</a></li>\n",
			pbase, prev_off);
	else
		fprintf(output,
			"<li class=\"page-item disabled\">"
			"<span class=\"page-link\">"
			"<i class=\"fa-solid fa-chevron-left\"></i> Newer</span></li>\n");

	fprintf(output,
		"<li class=\"page-item disabled\">"
		"<span class=\"page-link\">%s</span></li>\n", title);

	if (has_next)
		fprintf(output,
			"<li class=\"page-item\">"
			"<a class=\"page-link\" href=\"%s&amp;OFFSET=%d\">"
			"Older <i class=\"fa-solid fa-chevron-right\"></i></a></li>\n",
			pbase, next_off);
	else
		fprintf(output,
			"<li class=\"page-item disabled\">"
			"<span class=\"page-link\">"
			"Older <i class=\"fa-solid fa-chevron-right\"></i></span></li>\n");

	fprintf(output, "</ul></nav>\n");
}

static void render_eventlog_paginated(FILE *output, event_t *events,
		int offset, int perpage,
		int maxminutes_arg, char *fromtime, char *totime,
		char *hostregex_arg, char *exhostregex_arg,
		char *testregex_arg, char *extestregex_arg,
		char *pageregex_arg, char *expageregex_arg,
		char *colorregex_arg, int ignoredialups_arg,
		char *periodstring_arg)
{
	int total = 0;
	event_t *ewalk;
	char tmp[64];

	for (ewalk = events; ewalk; ewalk = ewalk->next) total++;

	int has_prev = (offset > 0);
	int has_next = (total > offset + perpage);
	int prev_off = (offset > perpage) ? offset - perpage : 0;
	int next_off = offset + perpage;

	/* Skip to page start */
	ewalk = events;
	for (int i = 0; i < offset && ewalk; i++) ewalk = ewalk->next;
	event_t *pagestart = ewalk;

	/* Count events on this page and terminate the list there */
	int count = 0;
	event_t *last_on_page = NULL;
	while (ewalk && count < perpage) { last_on_page = ewalk; ewalk = ewalk->next; count++; }
	if (last_on_page) last_on_page->next = NULL;

	/* Build base URL (all current filter params except OFFSET) for pagination links */
	strbuffer_t *pbase = newstrbuffer(512);
	addtobuffer(pbase, xgetenv("CGIBINURL"));
	addtobuffer(pbase, "/eventlog.sh?MAXCOUNT=");
	snprintf(tmp, sizeof(tmp), "%d", perpage);
	addtobuffer(pbase, tmp);

	if (maxminutes_arg != 0) {
		snprintf(tmp, sizeof(tmp), "%d", maxminutes_arg);
		addtobuffer(pbase, "&amp;MAXTIME=");
		addtobuffer(pbase, tmp);
	}
	if (fromtime)        { addtobuffer(pbase, "&amp;FROMTIME=");    addtobuffer(pbase, fromtime);        }
	if (totime)          { addtobuffer(pbase, "&amp;TOTIME=");      addtobuffer(pbase, totime);          }
	if (hostregex_arg)   { addtobuffer(pbase, "&amp;HOSTMATCH=");   addtobuffer(pbase, hostregex_arg);   }
	if (exhostregex_arg) { addtobuffer(pbase, "&amp;EXHOSTMATCH="); addtobuffer(pbase, exhostregex_arg); }
	if (pageregex_arg)   { addtobuffer(pbase, "&amp;PAGEMATCH=");   addtobuffer(pbase, pageregex_arg);   }
	if (expageregex_arg) { addtobuffer(pbase, "&amp;EXPAGEMATCH="); addtobuffer(pbase, expageregex_arg); }
	if (testregex_arg)   { addtobuffer(pbase, "&amp;TESTMATCH=");   addtobuffer(pbase, testregex_arg);   }
	if (extestregex_arg) { addtobuffer(pbase, "&amp;EXTESTMATCH="); addtobuffer(pbase, extestregex_arg); }
	if (colorregex_arg)  { addtobuffer(pbase, "&amp;COLORMATCH=");  addtobuffer(pbase, colorregex_arg);  }
	if (ignoredialups_arg) addtobuffer(pbase, "&amp;NODIALUPS=on");

	/* Period heading */
	if (periodstring_arg)
		fprintf(output, "<p class=\"event-period\"><strong>%s</strong></p>\n",
			htmlquoted(periodstring_arg));

	if (!pagestart) {
		fprintf(output, "<p class=\"event-title\">No events found.</p>\n");
		freestrbuffer(pbase);
		return;
	}

	/* Title shown in table header and pagination controls */
	char title[128];
	snprintf(title, sizeof(title), "Events %d&ndash;%d%s",
		offset + 1, offset + count, has_next ? "+" : "");

	emit_event_pagination_nav(output, STRBUF(pbase),
		has_prev, prev_off, has_next, next_off, title);

	/* Event table */
	fprintf(output,
		"<div class=\"table-responsive\">"
		"<table class=\"table table-sm table-dark table-striped event-log\">\n");
	fprintf(output, "<thead><tr>");
	fprintf(output, "<th colspan=\"3\">%s</th>", title);
	fprintf(output,
		"<th class=\"text-end\">"
		"<a class=\"btn btn-outline-light btn-sm\" href=\"%s/eventlog.sh\">"
		"<i class=\"fa-solid fa-filter\"></i>"
		"<span class=\"d-none d-sm-inline\"> Filter</span></a></th>",
		xgetenv("CGIBINURL"));
	fprintf(output, "</tr></thead>\n");

	for (ewalk = pagestart; ewalk; ewalk = ewalk->next) {
		char *hostname = xmh_item(ewalk->host, XMH_HOSTNAME);
		char evttime[32];
		strftime(evttime, sizeof(evttime), "%b %d %H:%M", localtime(&ewalk->eventtime));
		fprintf(output, "<tr>\n");
		fprintf(output, "<td class=\"text-nowrap\">%s</td>\n", evttime);
		fprintf(output, "<td>%s</td>\n", hostname);
		fprintf(output,
			"<td><a href=\"%s/svcstatus.sh?HOST=%s&amp;SERVICE=%s\">%s</a></td>\n",
			xgetenv("CGIBINURL"), hostname, ewalk->service->name, ewalk->service->name);
		fprintf(output, "<td><a href=\"%s\">",
			histlogurl(hostname, ewalk->service->name, ewalk->changetime, NULL));
		fprintf(output, "%s</a>\n", coloricon(ewalk->oldcolor, 0, 0));
		fprintf(output, "<i class=\"fa-solid fa-arrow-right xymon-event-arrow\"></i>\n");
		fprintf(output, "<a href=\"%s\">",
			histlogurl(hostname, ewalk->service->name, ewalk->eventtime, NULL));
		fprintf(output, "%s</a></td>\n", coloricon(ewalk->newcolor, 0, 0));
		fprintf(output, "</tr>\n");
	}

	fprintf(output, "</table></div>\n");

	emit_event_pagination_nav(output, STRBUF(pbase),
		has_prev, prev_off, has_next, next_off, title);

	freestrbuffer(pbase);
}

int main(int argc, char *argv[])
{
	int argi;
	char *envarea = NULL;

	for (argi=1; (argi < argc); argi++) {
		if (argnmatch(argv[argi], "--env=")) {
			char *p = strchr(argv[argi], '=');
			loadenv(p+1, envarea);
		}
		else if (argnmatch(argv[argi], "--area=")) {
			char *p = strchr(argv[argi], '=');
			envarea = strdup(p+1);
		}
		else if (argnmatch(argv[argi], "--top")) {
			topcount = 10;
			webfile_hf = "topchanges";
			webfile_form = "topchanges_form";
			maxminutes = -1;
			maxcount = -1;
		}
		else if (strcmp(argv[argi], "--debug=")) {
			debug = 1;
		}
	}

	redirect_cgilog("eventlog");
	load_hostnames(xgetenv("HOSTSCFG"), NULL, get_fqdn());

	fprintf(stdout, "Content-type: %s\n\n", xgetenv("HTMLCONTENTTYPE"));

	cgidata = cgi_request();
	if (cgidata == NULL) {
		/* Present the query form */
		sethostenv("", "", "", colorname(COL_BLUE), NULL);
		showform(stdout, webfile_hf, webfile_form, COL_BLUE, getcurrenttime(NULL), NULL, NULL);
		return 0;
	}

	parse_query();

	if (periodstring && (fromtime || totime)) {
		strbuffer_t *pstr = newstrbuffer(1024);

		if (fromtime && totime) {
			addtobuffer(pstr, "Events between ");
			addtobuffer(pstr, htmlquoted(fromtime));
			addtobuffer(pstr, "- ");
			addtobuffer(pstr, htmlquoted(totime));
		}
		else if (fromtime) {
			addtobuffer(pstr, "Events since ");
			addtobuffer(pstr, htmlquoted(fromtime));
		}
		else if (totime) {
			addtobuffer(pstr, "Events until ");
			addtobuffer(pstr, htmlquoted(totime));
		}

		xfree(periodstring);
		periodstring = grabstrbuffer(pstr);
	}

	/* Now generate the webpage */
	headfoot(stdout, webfile_hf, "", "header", COL_GREEN);
	if (topcount == 0) {
		if (summarybar != XYMON_S_NONE) {
			/* Summary breakdown: delegate to do_eventlog for rendering */
			do_eventlog(stdout, maxcount, maxminutes, fromtime, totime,
				    pageregex, expageregex, hostregex, exhostregex, testregex, extestregex,
				    colorregex, ignoredialups, NULL,
				    NULL, NULL, NULL, counttype, summarybar, periodstring);
		}
		else {
			/* Paginated event table.
			 * Load one extra event beyond what the current page needs so we can
			 * detect whether an "Older" next page exists without a full log scan. */
			int perpage = (maxcount > 0) ? maxcount : 100;
			int load_count = page_offset + perpage + 1;
			event_t *events = NULL;
			do_eventlog(NULL, load_count, maxminutes, fromtime, totime,
				    pageregex, expageregex, hostregex, exhostregex, testregex, extestregex,
				    colorregex, ignoredialups, NULL,
				    &events, NULL, NULL, counttype, XYMON_S_NONE, NULL);
			render_eventlog_paginated(stdout, events,
				page_offset, perpage,
				maxminutes, fromtime, totime,
				hostregex, exhostregex, testregex, extestregex,
				pageregex, expageregex, colorregex, ignoredialups,
				periodstring);
		}
	}
	else {
		countlist_t *hcounts, *scounts;
		event_t *events;
		time_t firstevent, lastevent;

		do_eventlog(NULL, -1, -1, fromtime, totime, 
			    pageregex, expageregex, hostregex, exhostregex, testregex, extestregex,
			    colorregex, ignoredialups, NULL,
			    &events, &hcounts, &scounts, counttype, XYMON_S_NONE, NULL);

		lastevent = (totime ? eventreport_time(totime) : getcurrenttime(NULL));

		if (fromtime) {
			firstevent = eventreport_time(fromtime);
		}
		else if (events) {
			event_t *ewalk;
			ewalk = events; while (ewalk->next) ewalk = ewalk->next;
			firstevent = ewalk->eventtime;
		}
		else
			firstevent = 0;

		show_topchanges(stdout, hcounts, scounts, events, topcount, firstevent, lastevent);
	}

	headfoot(stdout, webfile_hf, "", "footer", COL_GREEN);

	return 0;
}

