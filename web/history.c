/*----------------------------------------------------------------------------*/
/* Xymon history viewer.                                                      */
/*                                                                            */
/* This is a CGI tool used to view the history of a status log.               */
/*                                                                            */
/* Copyright (C) 2003-2011 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id$";

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <libgen.h>

#include "libxymon.h"

SBUF_DEFINE(selfurl);
static time_t req_endtime = 0;
static char *displayname = NULL;
static int wantserviceid = 1;

static int len1d = 24;
static char *bartitle1d = "1 day summary";
static int len1w = 7;
static char *bartitle1w = "1 week summary";
static int len4w = 28;
static char *bartitle4w = "4 week summary";
static int len1y = 12;
static char *bartitle1y = "1 year summary";

#define DEFPIXELS 960

/* The pixel setup */
static int usepct = 0;
static int pixels = DEFPIXELS;

/* What colorbars and summaries to show */
#define BARSUM_1D 0x0001	/* 1-day bar */
#define BARSUM_1W 0x0002	/* 1-week bar */
#define BARSUM_4W 0x0004	/* 4-week bar */
#define BARSUM_1Y 0x0008	/* 1-year bar */
static unsigned int barsums = (BARSUM_1D|BARSUM_1W|BARSUM_4W|BARSUM_1Y);

static char *tagcolors[COL_COUNT] = {
	"#3AF03A",	/* A bright green */
	"white",
	"blue",
	"purple",
	"yellow",
	"red"
};

#define ALIGN_HOUR  0
#define ALIGN_DAY   1
#define ALIGN_MONTH 2

#define DAY_BAR 0
#define WEEK_BAR 1
#define MONTH_BAR 2
#define YEAR_BAR 3

#define END_START 0
#define END_END 1
#define END_UNCHANGED 2

static unsigned int calc_time(time_t endtime, int change, int alignment, int endofperiod)
{
	int daysinmonth[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
	struct tm *tmbuf;
	time_t result, now;
	int dstsetting = -1;

again:
	tmbuf = localtime(&endtime);
	switch (alignment) {
		case ALIGN_HOUR: 
			tmbuf->tm_hour += change;
			if (endofperiod == END_END) {
				tmbuf->tm_min = tmbuf->tm_sec = 59;
			}
			else if (endofperiod == END_START) {
				tmbuf->tm_min = tmbuf->tm_sec = 0;
			}
			break;

		case ALIGN_DAY:
			tmbuf->tm_mday += change;
			if (endofperiod == END_END) {
				tmbuf->tm_hour = 23;
				tmbuf->tm_min = 59;
				tmbuf->tm_sec = 59;
			}
			else if (endofperiod == END_START) {
				tmbuf->tm_hour = tmbuf->tm_min = tmbuf->tm_sec = 0;
			}
			break;

		case ALIGN_MONTH:
			tmbuf->tm_mon += change;

			if (endofperiod == END_END) {
				/* Need to find the last day of the month */
				tmbuf->tm_mday = daysinmonth[tmbuf->tm_mon];
				if (tmbuf->tm_mon == 1) {
					if (((tmbuf->tm_year + 1900) % 4) == 0) {
						tmbuf->tm_mday = 29;
						if (((tmbuf->tm_year + 1900) % 100) == 0) tmbuf->tm_mday = 28;
						if (((tmbuf->tm_year + 1900) % 400) == 0) tmbuf->tm_mday = 29;
					}
				}

				tmbuf->tm_hour = 23;
				tmbuf->tm_min = 59;
				tmbuf->tm_sec = 59;
			}
			else if (endofperiod == END_START) {
				tmbuf->tm_mday = 1;
				tmbuf->tm_hour = tmbuf->tm_min = tmbuf->tm_sec = 0;
			}
			break;
	}
	tmbuf->tm_isdst = dstsetting;
	result = mktime(tmbuf);
	if ((dstsetting == -1) && (endofperiod == END_END) && (result < endtime)) {
		/* DST->normaltime switchover - redo with forced DST setting */
		dbgprintf("DST rollover with endtime/change/alignment/endodperiod = %u/%d/%d/%d\n",
			(unsigned int)endtime, change, alignment, endofperiod);
		dstsetting = 0;
		goto again;
	}

	/* Don't try to foresee the future */
	now = getcurrenttime(NULL);
	if (result > now) result = now;

	return (unsigned int)result;
}

static int maxcolor(replog_t *periodlog, time_t begintime, time_t endtime)
{
	int result = COL_GREEN;
	replog_t *walk = periodlog;

	while (walk) {
		if (walk->color > result) {
			/*
			 * We want this event, IF:
			 * - it starts sometime during begintime -> endtime, or
			 * - it starts before begintime, but lasts into after begintime.
			 */
			if ( ((walk->starttime >= begintime) && (walk->starttime < endtime))  ||
			     ((walk->starttime <  begintime) && ((walk->starttime + walk->duration) >= begintime)) ) {
				result = walk->color;
			}
		}

		walk = walk->next;
	}

	return result;
}


static void generate_colorbar(
			FILE *htmlrep,		/* Output file */
			time_t begintime,
			time_t endtime,
			int alignment,		/* Align by hour/day/month */
			int bartype,            /* Day/Week/Month/Year bar */
			char *hostname,
			char *service,
			char *caption,		/* Title */
			replog_t *periodlog,	/* Log entries for period */
			reportinfo_t *repinfo) 	/* Info for the percent summary */
{
	int secsperpixel;
	replog_t *colorlog, *walk;
	int changeval = 0;
	int changealign = 0;

	/* How many seconds required for 1 pixel (pixels stays as internal counter) */
	secsperpixel = ((endtime - begintime) / pixels);

	/* Need to re-sort the period-log to chronological order */
	colorlog = NULL;
	{
		replog_t *tmp;
		for (walk = periodlog; (walk); walk = tmp) {
			tmp = walk->next;
			walk->next = colorlog;
			colorlog = walk;
			walk = tmp;
		}
	}

	/* Determine the back/forward link times */
	switch (bartype) {
		case DAY_BAR   : changeval = len1d; changealign = ALIGN_HOUR; break;
		case WEEK_BAR  : changeval = len1w; changealign = ALIGN_DAY; break;
		case MONTH_BAR : changeval = len4w; changealign = ALIGN_DAY; break;
		case YEAR_BAR  : changeval = len1y; changealign = ALIGN_MONTH; break;
	}

	{
		char bdate[32], edate[32];
		struct tm *tm;
		tm = localtime(&begintime); strftime(bdate, sizeof(bdate), "%a %b %d %H:%M %Y", tm);
		tm = localtime(&endtime);   strftime(edate, sizeof(edate), "%a %b %d %H:%M %Y", tm);

		fprintf(htmlrep, "<div class=\"xymon-hist-entry\">\n");

		/* nav — full-width flex row */
		fprintf(htmlrep, "<div class=\"d-flex align-items-center justify-content-between xymon-hist-header\">\n");
		fprintf(htmlrep, "<div class=\"d-flex align-items-center xymon-hist-nav\">\n");
		fprintf(htmlrep, "<a href=\"%s&amp;ENDTIME=%u\" class=\"text-decoration-none\" title=\"Previous period\">",
			selfurl, calc_time(endtime, -changeval, changealign, END_UNCHANGED));
		fprintf(htmlrep, "<i class=\"fa-solid fa-chevron-left\"></i></a>\n");
		fprintf(htmlrep, "<small class=\"text-muted\">%s</small>\n", bdate);
		fprintf(htmlrep, "</div>\n");
		fprintf(htmlrep, "<strong>%s</strong>\n", caption);
		fprintf(htmlrep, "<div class=\"d-flex align-items-center xymon-hist-nav\">\n");
		fprintf(htmlrep, "<small class=\"text-muted\">%s</small>\n", edate);
		fprintf(htmlrep, "<a href=\"%s&amp;ENDTIME=%u\" class=\"text-decoration-none\" title=\"Next period\">",
			selfurl, calc_time(endtime, +changeval, changealign, END_UNCHANGED));
		fprintf(htmlrep, "<i class=\"fa-solid fa-chevron-right\"></i></a>\n");
		fprintf(htmlrep, "</div>\n");
		fprintf(htmlrep, "</div>\n"); /* end nav */

		/* Bootstrap responsive row: summary left, bars right */
		fprintf(htmlrep, "<div class=\"row g-0\">\n");

		/* Summary column: icons row + percentages row stacked */
		fprintf(htmlrep, "<div class=\"col-12 col-sm-4 col-md-3\">\n");

		/* icons — flex-fill cells share equal width so pcts row aligns */
		fprintf(htmlrep, "<div class=\"d-flex\">\n");
		fprintf(htmlrep, "<div class=\"flex-fill text-center\">%s</div>\n", coloricon(COL_GREEN,  0, 1));
		fprintf(htmlrep, "<div class=\"flex-fill text-center\">%s</div>\n", coloricon(COL_YELLOW, 0, 1));
		fprintf(htmlrep, "<div class=\"flex-fill text-center\">%s</div>\n", coloricon(COL_RED,    0, 1));
		fprintf(htmlrep, "<div class=\"flex-fill text-center\">%s</div>\n", coloricon(COL_PURPLE, 0, 1));
		fprintf(htmlrep, "<div class=\"flex-fill text-center\">%s</div>\n", coloricon(COL_CLEAR,  0, 1));
		fprintf(htmlrep, "<div class=\"flex-fill text-center\">%s</div>\n", coloricon(COL_BLUE,   0, 1));
		fprintf(htmlrep, "</div>\n");

		/* percentages — identical structure, aligns under icons */
		fprintf(htmlrep, "<div class=\"d-flex\" style=\"font-size:0.75rem;\">\n");
		fprintf(htmlrep, "<div class=\"flex-fill text-center\">%.1f%%</div>\n", repinfo->fullpct[COL_GREEN]);
		fprintf(htmlrep, "<div class=\"flex-fill text-center\">%.1f%%</div>\n", repinfo->fullpct[COL_YELLOW]);
		fprintf(htmlrep, "<div class=\"flex-fill text-center\">%.1f%%</div>\n", repinfo->fullpct[COL_RED]);
		fprintf(htmlrep, "<div class=\"flex-fill text-center\">%.1f%%</div>\n", repinfo->fullpct[COL_PURPLE]);
		fprintf(htmlrep, "<div class=\"flex-fill text-center\">%.1f%%</div>\n", repinfo->fullpct[COL_CLEAR]);
		fprintf(htmlrep, "<div class=\"flex-fill text-center\">%.1f%%</div>\n", repinfo->fullpct[COL_BLUE]);
		fprintf(htmlrep, "</div>\n");

		fprintf(htmlrep, "</div>\n"); /* end summary col */

		/* Bar column: period labels + color bar */
		fprintf(htmlrep, "<div class=\"col-12 col-sm-8 col-md-9\">\n");

		/* period interval labels as proportional flex children */
		fprintf(htmlrep, "<div style=\"display:flex; align-items:stretch;\">\n");
		{
			time_t begininterval = begintime;
			time_t endofinterval;
			char tag[20];
			int curbg = 0;
			int intervalpixels, tagcolor;
			time_t minduration = 1800;
			struct tm *tmbuf;

			do {
				endofinterval = calc_time(begininterval, 0, alignment, END_END);
				dbgprintf("Period starts %u ends %u - %s",
					(unsigned int)begininterval, (unsigned int)endofinterval,
					ctime(&endofinterval));

				tmbuf = localtime(&begininterval);
				switch (bartype) {
					case DAY_BAR   : minduration = 1800;     strftime(tag, sizeof(tag), "%H",  tmbuf); break;
					case WEEK_BAR  : minduration = 14400;    strftime(tag, sizeof(tag), "%a",  tmbuf); break;
					case MONTH_BAR : minduration = 43200;    strftime(tag, sizeof(tag), "%d",  tmbuf); break;
					case YEAR_BAR  : minduration = 10*86400; strftime(tag, sizeof(tag), "%b",  tmbuf); break;
				}

				intervalpixels = ((endofinterval - begininterval) / secsperpixel);
				tagcolor = maxcolor(colorlog, begininterval, endofinterval);

				{
					const char *bgcss = curbg ? "#555555" : "#000000";
					double ipct = (intervalpixels * 100.0) / pixels;
					fprintf(htmlrep, "<div style=\"flex:0 0 %.4f%%; overflow:hidden; text-align:center; background:%s;\">",
						ipct, bgcss);
				}
				if ((endofinterval - begininterval) > minduration) {
					int dolink = (colorlog && endofinterval >= colorlog->starttime);
					if (dolink) fprintf(htmlrep, "<a href=\"%s&amp;ENDTIME=%u\">",
							    selfurl, (unsigned int)endofinterval);
					fprintf(htmlrep, "<strong style=\"color:%s\">%s</strong>",
						tagcolors[tagcolor], tag);
					if (dolink) fprintf(htmlrep, "</a>");
				}
				fprintf(htmlrep, "</div>\n");

				curbg = (1 - curbg);

				if ((endofinterval + 1) <= begininterval) {
					fprintf(htmlrep, "<!-- time moves backwards: begintime=%u alignment=%d begininterval=%u -->\n",
						  (unsigned int)begintime, alignment, (unsigned int)begininterval);
					begininterval = endtime;
				}
				begininterval = endofinterval + 1;
			} while (begininterval < endtime);
		}
		fprintf(htmlrep, "<div style=\"flex:1;\"></div>\n"); /* absorb rounding gap */
		fprintf(htmlrep, "</div>\n"); /* end labels */

		/* color bar */
		fprintf(htmlrep, "<div style=\"display:flex; height:1.25rem;\">\n");

		if (colorlog == NULL) {
			fprintf(htmlrep, "<div style=\"flex:1; background:white;\"></div>\n");
		}
		else {
			if (colorlog->starttime > begintime) {
				int leadpx = ((colorlog->starttime - begintime) / secsperpixel);
				if (((colorlog->starttime - begintime) >= (secsperpixel/2)) && (leadpx == 0)) leadpx = 1;
				if (leadpx > 0) {
					fprintf(htmlrep, "<div style=\"flex:0 0 %.4f%%; background:white;\"></div>\n",
						(leadpx * 100.0) / pixels);
				}
			}
			for (walk = colorlog; (walk); walk = walk->next) {
				int segpx = (walk->duration / secsperpixel);
				if ((walk->duration >= (secsperpixel/2)) && (segpx == 0)) segpx = 1;
				if (segpx > 0) {
					const char *segcolor = (walk->color == COL_CLEAR) ? "white" : colorname(walk->color);
					fprintf(htmlrep, "<div style=\"flex:0 0 %.4f%%; background:%s;\"></div>\n",
						(segpx * 100.0) / pixels, segcolor);
				}
			}
		}

		fprintf(htmlrep, "<div style=\"flex:1;\"></div>\n"); /* absorb rounding gap */
		fprintf(htmlrep, "</div>\n"); /* end color bar */

		fprintf(htmlrep, "</div>\n"); /* end bar col */
		fprintf(htmlrep, "</div>\n"); /* end row */
		fprintf(htmlrep, "</div>\n"); /* end mb-3 */
	}

}


static void generate_histlog_table(FILE *htmlrep,
		char *hostname, char *service, int entrycount, replog_t *loghead)
{
	replog_t *walk;

	fprintf(htmlrep, "<table class=\"table table-sm table-striped\">\n");
	fprintf(htmlrep, "<caption class=\"caption-top\">\n");
	if (entrycount) {
		fprintf(htmlrep, "<strong>Last %d log entries</strong> ", entrycount);
		fprintf(htmlrep, "<a href=\"%s&amp;ENDTIME=%u&amp;ENTRIES=all\">(Full HTML log)</a>",
			selfurl, (unsigned int)req_endtime);
	}
	else {
		fprintf(htmlrep, "<strong>All log entries</strong>");
	}
	fprintf(htmlrep, "\n</caption>\n");
	fprintf(htmlrep, "<thead><tr><th>Date</th><th class=\"text-center\">Status</th><th class=\"text-center\">Duration</th></tr></thead>\n");
	fprintf(htmlrep, "<tbody>\n");

	for (walk = loghead; (walk); walk = walk->next) {
		char start[30];

		strftime(start, sizeof(start), "%a %b %d %H:%M:%S %Y", localtime(&walk->starttime));

		fprintf(htmlrep, "<tr>\n");
		fprintf(htmlrep, "<td class=\"text-nowrap\">%s</td>\n", start);
		fprintf(htmlrep, "<td class=\"text-center\">");
		fprintf(htmlrep, "<a href=\"%s\">", histlogurl(hostname, service, 0, walk->timespec));
		fprintf(htmlrep, "%s", coloricon(walk->color, 0, 1));
		fprintf(htmlrep, "</a></td>\n");
		fprintf(htmlrep, "<td class=\"text-center\">%s</td>\n", durationstr(walk->duration));
		fprintf(htmlrep, "</tr>\n");
	}

	fprintf(htmlrep, "</tbody></table>\n");
}


void generate_history(FILE *htmlrep, 			/* output file */
		      char *hostname, char *service, 	/* Host and service we report on */
		      char *ip, 			/* IP - for the header only */
		      time_t endtime,			/* End time of color-bar graphs */

                      time_t start1d,			/* Start time of 1-day period */
		      reportinfo_t *repinfo1d, 		/* Percent summaries for 1-day period */
		      replog_t *log1d, 			/* Events during past 1 day */

                      time_t start1w,			/* Start time of 1-week period */
		      reportinfo_t *repinfo1w, 		/* Percent summaries for 1-week period */
		      replog_t *log1w, 			/* Events during past 1 week */

                      time_t start4w,			/* Start time of 4-week period */
		      reportinfo_t *repinfo4w, 		/* Percent summaries for 4-week period */
		      replog_t *log4w, 			/* Events during past 4 weeks */

                      time_t start1y,			/* Start time of 1-year period */
		      reportinfo_t *repinfo1y, 		/* Percent summaries for 1-year period */
		      replog_t *log1y, 			/* Events during past 1 yeary */

		      int entrycount,			/* Log entry maxcount */
		      replog_t *loghead)		/* Eventlog for entrycount events back */
{
	sethostenv(displayname, ip, service, colorname(COL_GREEN), hostname);
	headfoot(htmlrep, "hist", "", "header", COL_GREEN);

	fprintf(htmlrep, "\n");
	if (wantserviceid) {
		fprintf(htmlrep, "<p class=\"fw-semibold xymon-hist-title\">%s &mdash; %s</p>\n",
			htmlquoted(displayname), htmlquoted(service));
	}

	/* Create the color-bars */
	if (log1d) {
		/* 1-day bar */
		generate_colorbar(htmlrep, start1d, endtime, ALIGN_HOUR, DAY_BAR,
				  hostname, service, bartitle1d, log1d, repinfo1d);
	}

	if (log1w) {
		/* 1-week bar */
		generate_colorbar(htmlrep, start1w, endtime, ALIGN_DAY, WEEK_BAR,
				  hostname, service, bartitle1w, log1w, repinfo1w);
	}

	if (log4w) {
		/* 4-week bar */
		generate_colorbar(htmlrep, start4w, endtime, ALIGN_DAY, MONTH_BAR,
				  hostname, service, bartitle4w, log4w, repinfo4w);
	}

	if (log1y) {
		/* 1-year bar */
		generate_colorbar(htmlrep, start1y, endtime, ALIGN_MONTH, YEAR_BAR,
				  hostname, service, bartitle1y, log1y, repinfo1y);
	}

	/* Last N histlog entries */
	generate_histlog_table(htmlrep, hostname, service, entrycount, loghead);

	/* XYMONHISTEXT extensions */
	do_extensions(htmlrep, "XYMONHISTEXT", "hist");

	headfoot(htmlrep, "hist", "", "footer", COL_GREEN);
}


double reportgreenlevel = 99.995;
double reportwarnlevel = 98.0;
int    reportwarnstops = -1;

char *hostname = "";
char *service = "";
char *ip = "";
int entrycount = 50;
cgidata_t *cgidata = NULL;

char *reqenv[] = {
"XYMONHISTDIR",
"XYMONHISTLOGS",
"XYMONREPDIR",
"XYMONREPURL",
"XYMONSKIN",
"CGIBINURL",
"DOTWIDTH",
"DOTHEIGHT",
"XYMONPAGECOLFONT",
"XYMONPAGEROWFONT",
NULL };

static void errormsg(char *msg)
{
	printf("Content-type: %s\n\n", xgetenv("HTMLCONTENTTYPE"));
	printf("<html><head><title>Invalid request</title></head>\n");
	printf("<body>%s</body></html>\n", msg);
	exit(1);
}

static void parse_query(void)
{
	cgidata_t *cwalk;

	cwalk = cgidata;
	while (cwalk) {
		/*
		 * cwalk->name points to the name of the setting.
		 * cwalk->value points to the value (may be an empty string).
		 */

		if (strcasecmp(cwalk->name, "HISTFILE") == 0) {
			char *p = cwalk->value + strspn(cwalk->value, "abcdefghijklmnopqrstuvwxyz0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ:,.\\/_-");
			*p = '\0';

			p = strrchr(cwalk->value, '.');
			if (p) { *p = '\0'; service = strdup(p+1); }
			hostname = strdup(basename(cwalk->value));
			while ((p = strchr(hostname, ','))) *p = '.';
		}
		else if (strcasecmp(cwalk->name, "IP") == 0) {
			ip = strdup(cwalk->value);
		}
		else if (strcasecmp(cwalk->name, "ENTRIES") == 0) {
			if (strcmp(cwalk->value, "all") == 0) entrycount = 0;
			else entrycount = atoi(cwalk->value);
			if (entrycount < 0) errormsg("Invalid parameter");
		}
		else if (strcasecmp(cwalk->name, "PIXELS") == 0) {
			pixels = atoi(cwalk->value);
			if (pixels > 0) usepct = 0; else usepct = 1;
		}
		else if (strcasecmp(cwalk->name, "ENDTIME") == 0) {
			req_endtime = atol(cwalk->value);
			if (req_endtime < 0) errormsg("Invalid parameter");
		}
		else if (strcasecmp(cwalk->name, "BARSUMS") == 0) {
			barsums = atoi(cwalk->value);
		}
		else if (strcasecmp(cwalk->name, "DISPLAYNAME") == 0) {
			displayname = strdup(cwalk->value);
		}

		cwalk = cwalk->next;
	}

	if (!displayname) displayname = strdup(hostname);
}


int main(int argc, char *argv[])
{
	char histlogfn[PATH_MAX];
	FILE *fd;
	time_t start1d, start1w, start4w, start1y;
	reportinfo_t repinfo1d, repinfo1w, repinfo4w, repinfo1y, dummyrep;
	replog_t *log1d, *log1w, *log4w, *log1y;
	char *p;
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
		else if (strcmp(argv[argi], "--no-svcid") == 0) {
			wantserviceid = 0;
		}
	}

	redirect_cgilog("history");

	envcheck(reqenv);
	cgidata = cgi_request();
	parse_query();

	SBUF_MALLOC(selfurl, 4096);

	/* Build our own URL */
	snprintf(selfurl, selfurl_buflen, "%s", histcgiurl(hostname, service));

	p = selfurl + strlen(selfurl);
	snprintf(p, selfurl_buflen - (p - selfurl), "&amp;BARSUMS=%d", barsums);

	if (strlen(ip)) {
		SBUF_REALLOC(selfurl, selfurl_buflen + 6*strlen(ip));
		p = selfurl + strlen(selfurl);
		snprintf(p, selfurl_buflen - (p - selfurl), "&amp;IP=%s", htmlquoted(ip));
	}

	if (entrycount) {
		p = selfurl + strlen(selfurl);
		snprintf(p, selfurl_buflen - (p - selfurl), "&amp;ENTRIES=%d", entrycount);
	}
	else strncat(selfurl, "&amp;ENTRIES=ALL", selfurl_buflen - strlen(selfurl));

	if (usepct) {
		/* Must modify 4-week charts to be 5-weeks, or the last day is 19% of the bar */
		/*
		 * Percent-based charts look awful with 24 hours / 7 days / 28 days / 12 months as basis
		 * because these numbers don't divide into 100 neatly. So the last item becomes
		 * too large (worst with the 28-day char: 100/28 = 3, last becomes (100-27*3) = 19% wide).
		 * So adjust the periods to something that matches percent-based calculations better.
		 */
		len1d = 25; bartitle1d = "25 hour summary";
		len1w = 10; bartitle1w = "10 day summary";
		len4w = 33; bartitle4w = "33 day summary";
		len1y = 10; bartitle1y = "10 month summary";
	}

	snprintf(histlogfn, sizeof(histlogfn), "%s/%s.%s", xgetenv("XYMONHISTDIR"), commafy(hostname), service);
	fd = fopen(histlogfn, "r");
	if (fd == NULL) {
		errormsg("Cannot open history file");
	}

	log1d = log1w = log4w = log1y = NULL;
	if (req_endtime == 0) req_endtime = getcurrenttime(NULL);
	/*
	 * Calculate the beginning time of each colorbar. We go back the specified length
	 * of time, except 1 second - so days are from midnight -> 23:59:59 etc.
	 */
	start1d = calc_time(req_endtime, -len1d, ALIGN_HOUR,  END_UNCHANGED) + 1;
	start1w = calc_time(req_endtime, -len1w, ALIGN_DAY,   END_UNCHANGED) + 1;
	start4w = calc_time(req_endtime, -len4w, ALIGN_DAY,   END_UNCHANGED) + 1;
	start1y = calc_time(req_endtime, -len1y, ALIGN_MONTH, END_UNCHANGED) + 1;

	/*
	 * Collect data for the color-bars and summaries. Multiple scans over the history file,
	 * but doing it all in one go would be hideously complex.
	 */
	if (barsums & BARSUM_1D) {
		parse_historyfile(fd, &repinfo1d, NULL, NULL, start1d, req_endtime, 1, reportwarnlevel, reportgreenlevel, reportwarnstops, NULL);
		log1d = save_replogs();
	}

	if (barsums & BARSUM_1W) {
		parse_historyfile(fd, &repinfo1w, NULL, NULL, start1w, req_endtime, 1, reportwarnlevel, reportgreenlevel, reportwarnstops, NULL);
		log1w = save_replogs();
	}

	if (barsums & BARSUM_4W) {
		parse_historyfile(fd, &repinfo4w, NULL, NULL, start4w, req_endtime, 1, reportwarnlevel, reportgreenlevel, reportwarnstops, NULL);
		log4w = save_replogs();
	}

	if (barsums & BARSUM_1Y) {
		parse_historyfile(fd, &repinfo1y, NULL, NULL, start1y, req_endtime, 1, reportwarnlevel, reportgreenlevel, reportwarnstops, NULL);
		log1y = save_replogs();
	}

	if (entrycount == 0) {
		/* All entries - just rewind the history file and do all of them */
		rewind(fd);
		parse_historyfile(fd, &dummyrep, NULL, NULL, 0, getcurrenttime(NULL), 1, reportwarnlevel, reportgreenlevel, reportwarnstops, NULL);
		fclose(fd);
	}
	else {
		SBUF_DEFINE(tailcmd);

		/* Last 50 entries - we cheat and use "tail" in a pipe to pick the entries */
		fclose(fd);
		SBUF_MALLOC(tailcmd, 1024 + strlen(histlogfn));

		snprintf(tailcmd, tailcmd_buflen, "tail -%d %s", entrycount, histlogfn);
		fd = popen(tailcmd, "r");
		if (fd == NULL) errormsg("Cannot run tail on the histfile");
		parse_historyfile(fd, &dummyrep, NULL, NULL, 0, getcurrenttime(NULL), 1, reportwarnlevel, reportgreenlevel, reportwarnstops, NULL);
		pclose(fd);
	}


	/* Now generate the webpage */
	printf("Content-Type: %s\n\n", xgetenv("HTMLCONTENTTYPE"));

	generate_history(stdout, 
			 hostname, service, ip, req_endtime, 
			 start1d, &repinfo1d, log1d, 
			 start1w, &repinfo1w, log1w, 
			 start4w, &repinfo4w, log4w, 
			 start1y, &repinfo1y, log1y, 
			 entrycount, reploghead);

	return 0;
}

