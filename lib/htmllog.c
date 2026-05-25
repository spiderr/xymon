/*----------------------------------------------------------------------------*/
/* Xymon monitor library.                                                     */
/*                                                                            */
/* This is a library module, part of libxymon.                                */
/* It contains routines for generating HTML version of a status log.          */
/*                                                                            */
/* Copyright (C) 2002-2011 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id$";

#include <ctype.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "libxymon.h"
#include "version.h"

#include "htmllog.h"

static char *cgibinurl = NULL;
static char *documentationurl = NULL;
static char *doctarget = NULL;

#define HOSTPOPUP_COMMENT 1
#define HOSTPOPUP_DESCR   2
#define HOSTPOPUP_IP      4

static int hostpopup = (HOSTPOPUP_COMMENT | HOSTPOPUP_DESCR | HOSTPOPUP_IP);

enum histbutton_t histlocation = HIST_BOTTOM;

static void hostpopup_setup(void)
{
	static int setup_done = 0;
	char *val, *p;

	if (setup_done) return;

	val = xgetenv("HOSTPOPUP");
	if (val) {
		hostpopup = 0;

		for (p = val; (*p); p++) {
			switch (*p) {
			  case 'C': case 'c': hostpopup = (hostpopup | HOSTPOPUP_COMMENT); break;
			  case 'D': case 'd': hostpopup = (hostpopup | HOSTPOPUP_DESCR); break;
			  case 'I': case 'i': hostpopup = (hostpopup | HOSTPOPUP_IP); break;
			  default: break;
			}
		}
	}

	setup_done = 1;
}

static void hostsvc_setup(void)
{
	static int setup_done = 0;

	if (setup_done) return;

	hostpopup_setup();
	getenv_default("NONHISTS", "info,trends,graphs", NULL);
	getenv_default("CGIBINURL", "/cgi-bin", &cgibinurl);
	getenv_default("XYMONWEB", "/xymon", NULL);
	{
		SBUF_DEFINE(dbuf);
		SBUF_MALLOC(dbuf, strlen(xgetenv("XYMONWEB")) + 6);
		snprintf(dbuf, dbuf_buflen, "%s/gifs", xgetenv("XYMONWEB"));
		getenv_default("XYMONSKIN", dbuf, NULL);
		xfree(dbuf);
	}

	setup_done = 1;
}



static void textwithcolorimg(char *msg, FILE *output)
{
	char *p, *restofmsg;

	restofmsg = msg;
	do {
		int color, acked, recent;

		color = -1; acked = recent = 0;
		p = strchr(restofmsg, '&');
		if (p) {
			*p = '\0';
			fprintf(output, "%s", restofmsg);
			*p = '&';

			if (strncmp(p, "&red", 4) == 0) color = COL_RED;
			else if (strncmp(p, "&yellow", 7) == 0) color = COL_YELLOW;
			else if (strncmp(p, "&green", 6) == 0) color = COL_GREEN;
			else if (strncmp(p, "&clear", 6) == 0) color = COL_CLEAR;
			else if (strncmp(p, "&blue", 5) == 0) color = COL_BLUE;
			else if (strncmp(p, "&purple", 7) == 0) color = COL_PURPLE;

			if (color == -1) {
				fprintf(output, "&");
				restofmsg = p+1;
			}
			else {
				acked = (strncmp(p + 1 + strlen(colorname(color)), "-acked", 6) == 0);
				recent = (strncmp(p + 1 + strlen(colorname(color)), "-recent", 7) == 0);

				fprintf(output, "%s", coloricon(color, acked, !recent));

				restofmsg = p+1+strlen(colorname(color));
				if (acked) restofmsg += 6;
				if (recent) restofmsg += 7;
			}
		}
		else {
			fprintf(output, "%s", restofmsg);
			restofmsg = NULL;
		}
	} while (restofmsg);
}


void generate_html_log(char *hostname, char *displayname, char *service, char *ip,
		       int color, int flapping, char *sender, char *flags,
		       time_t logtime, char *timesincechange,
		       char *firstline, char *restofmsg, char *modifiers,
		       time_t acktime, char *ackmsg, char *acklist,
		       time_t disabletime, char *dismsg,
		       int is_history, int wantserviceid, int htmlfmt, int locatorbased,
		       char *multigraphs,
		       char *linktoclient,
		       char *prio, char *ttgroup, char *ttextra,
		       int graphtime,
		       FILE *output)
{
	int linecount = 0;
	xymonrrd_t *rrd = NULL;
	xymongraph_t *graph = NULL;
	char *tplfile = "hostsvc";
	char xymontpl[32];
	char pagetitle[512];
	SBUF_DEFINE(graphs);
	char *graphsenv;
	char *graphsptr;
	time_t now = getcurrenttime(NULL);

	if (graphtime == 0) {
		if (getenv("TRENDSECONDS")) graphtime = atoi(getenv("TRENDSECONDS"));
		else graphtime = 48*60*60;
	}

	hostsvc_setup();
	if (!displayname) displayname = hostname;
	sethostenv(displayname, ip, service, colorname(color), hostname);
	if (logtime) sethostenv_snapshot(logtime);

	if (is_history) tplfile = "histlog";
	if (strcmp(service, xgetenv("INFOCOLUMN")) == 0) tplfile = "info";
	else if (strcmp(service, xgetenv("TRENDSCOLUMN")) == 0) tplfile = "trends";

	snprintf(xymontpl, sizeof(xymontpl), "xymon%s", tplfile);
	if (is_history)
		snprintf(pagetitle, sizeof(pagetitle), "Historical Status: %s - %s", displayname, service);
	else if (strcmp(tplfile, "info") == 0)
		snprintf(pagetitle, sizeof(pagetitle), "%s - Host Information", displayname);
	else if (strcmp(tplfile, "trends") == 0)
		snprintf(pagetitle, sizeof(pagetitle), "%s - Host Trends", displayname);
	else
		snprintf(pagetitle, sizeof(pagetitle), "%s - %s", displayname, service);

	/* Signal whether the HISTORY button should appear in the sub-header template */
	{
		char _nh[256], _sv[256];
		snprintf(_nh, sizeof(_nh), ",%s,", xgetenv("NONHISTS"));
		snprintf(_sv, sizeof(_sv), ",%s,", service);
		setenv("XYMONSHOWHISTORY",
		       (histlocation != HIST_NONE && strstr(_nh, _sv) == NULL) ? "1" : "",
		       1);
	}

	{
		char metabuf[1024];
		int n = 0;

		if (timesincechange && *timesincechange) {
			n += snprintf(metabuf + n, sizeof(metabuf) - n,
				"<small class=\"text-muted xymon-status-duration\">"
				"<i class=\"fa-regular fa-clock me-1\"></i>%s</small>",
				timesincechange);
		}
		setenv("XYMONSTATUSMETA", (n > 0 ? metabuf : ""), 1);
	}
	/* Pre-set XYMWEBDATE to logtime so navbar shows data freshness, not request time */
	if (logtime) {
		char _lwdate[64];
		strftime(_lwdate, sizeof(_lwdate)-1, "%a %b %d %H:%M:%S", localtime(&logtime));
		setenv("XYMWEBDATE", _lwdate, 1);
	}
	headfoot(output, xymontpl, "", "header", color);
	unsetenv("XYMWEBDATE");
	setenv("XYMONSTATUSMETA", "", 1);

	if (strcmp(service, xgetenv("TRENDSCOLUMN")) == 0) {
		int formfile;
		char formfn[PATH_MAX];

		snprintf(formfn, sizeof(formfn), "%s/web/trends_form", xgetenv("XYMONHOME"));
		formfile = open(formfn, O_RDONLY);

		if (formfile >= 0) {
			char *inbuf;
			struct stat st;
			int n;

			fstat(formfile, &st);
			inbuf = (char *) malloc(st.st_size + 1); *inbuf = '\0';
			n = read(formfile, inbuf, st.st_size);
			if (n > 0) inbuf[n] = '\0';
			close(formfile);

			sethostenv_backsecs(graphtime);
			output_parsed(output, inbuf, color, 0);
			xfree(inbuf);
		}
	}

	if (prio) {
		int formfile;
		char formfn[PATH_MAX];

		snprintf(formfn, sizeof(formfn), "%s/web/critack_form", xgetenv("XYMONHOME"));
		formfile = open(formfn, O_RDONLY);

		if (formfile >= 0) {
			char *inbuf;
			struct stat st;
			int n;

			fstat(formfile, &st);
			inbuf = (char *) malloc(st.st_size + 1); *inbuf = '\0';
			n = read(formfile, inbuf, st.st_size);
			if (n > 0) inbuf[st.st_size] = '\0';
			close(formfile);

			sethostenv_critack(atoi(prio), ttgroup, ttextra,
				 hostsvcurl(hostname, xgetenv("INFOCOLUMN"), 1), hostlink(hostname));

			output_parsed(output, inbuf, color, 0);
			xfree(inbuf);
		}
	}

	if (acklist && *acklist) {
		/* received:validuntil:level:ackedby:msg */
		time_t received, validuntil;
		int level;
		char *ackedby, *msg;
		char *bol, *eol, *tok;
		char receivedstr[200];
		char untilstr[200];

		fprintf(output, "<div class=\"table-responsive mb-3\"><table class=\"table table-sm table-bordered\">\n");
		fprintf(output, "<thead><tr><th colspan=\"4\" class=\"text-center\">Acknowledgments</th></tr>\n");
		fprintf(output, "<tr><th>Level</th><th>From</th><th>Validity</th><th>Message</th></tr></thead>\n");
		fprintf(output, "<tbody>\n");

		nldecode(acklist);

		bol = acklist;
		do {
			eol = strchr(bol, '\n'); if (eol) *eol = '\0';

			tok = strtok(bol, ":");
			if (tok) { received = atoi(tok); tok = strtok(NULL, ":"); } else received = 0;
			if (tok) { validuntil = atoi(tok); tok = strtok(NULL, ":"); } else validuntil = 0;
			if (tok) { level = atoi(tok); tok = strtok(NULL, ":"); } else level = -1;
			if (tok) { ackedby = tok; tok = strtok(NULL, "\n"); } else ackedby = NULL;
			if (tok) msg = tok; else msg = NULL;

			if (received && validuntil && (level >= 0) && ackedby && msg) {
				strftime(receivedstr, sizeof(receivedstr)-1, "%Y-%m-%d %H:%M", localtime(&received));
				strftime(untilstr, sizeof(untilstr)-1, "%Y-%m-%d %H:%M", localtime(&validuntil));
				fprintf(output, "<tr>");
				fprintf(output, "<td class=\"text-center\">%d</td>", level);
				fprintf(output, "<td>%s</td>", htmlquoted(ackedby));
				fprintf(output, "<td>%s &ndash; %s</td>", receivedstr, untilstr);
				fprintf(output, "<td>%s</td>", htmlquoted(msg));
				fprintf(output, "</tr>\n");
			}

			if (eol) { *eol = '\n'; bol = eol+1; } else bol = NULL;
		} while (bol);

		fprintf(output, "</tbody></table></div>\n");
	}

	fprintf(output, "<a name=\"begindata\"></a>\n");

	if (flapping) fprintf(output, "<div class=\"alert alert-warning text-center fw-bold\">WARNING: Flapping status</div>\n");

	/* Look up RRD graph definition before opening the row so we can size columns */
	if (!is_history) {
		rrd = find_xymon_rrd(service, flags);
		if (rrd) {
			graph = find_xymon_graph(rrd->xymonrrdname);
			if (graph == NULL) {
				errprintf("Setup error: Service %s has a graph %s, but no graph-definition\n",
					  service, rrd->xymonrrdname);
			}
		}
	}

	fprintf(output, "<div class=\"row g-3 mb-3\">\n");
	fprintf(output, "<div class=\"%s\">\n", (rrd && graph) ? "col-sm-12 col-md-6" : "col-12");

	if (wantserviceid) {
		fprintf(output, "<h5 class=\"fw-semibold\">%s &mdash; %s</h5><hr class=\"my-2\">\n",
			htmlquoted(displayname), htmlquoted(service));
	}

	if (disabletime != 0) {
		fprintf(output, "<div class=\"alert alert-info mb-2\"><strong>Disabled until %s</strong>",
			(disabletime == -1 ? "OK" : ctime(&disabletime)));
		fprintf(output, "<pre class=\"mt-1 mb-0 bg-transparent border-0\">%s</pre></div>\n", htmlquoted(dismsg));
		fprintf(output, "<p class=\"text-muted\">Current status message follows:</p>\n");

		if (strlen(firstline)) {
			fprintf(output, "<h2 class=\"fs-6\">");
			textwithcolorimg(firstline, output);
			fprintf(output, "</h2>\n");
		}
	}
	else {
		char *txt = skipword(firstline);

		if (dismsg) {
			fprintf(output, "<div class=\"alert alert-warning mb-2\">Planned downtime: %s</div>\n", htmlquoted(dismsg));
			fprintf(output, "<p class=\"text-muted\">Current status message follows:</p>\n");
		}

		if (modifiers) {
			char *modtxt;

			nldecode(modifiers);
			modtxt = strtok(modifiers, "\n");
			while (modtxt) {
				fprintf(output, "<h2 class=\"fs-6\">");
				textwithcolorimg(modtxt, output);
				fprintf(output, "</h2>\n");
				modtxt = strtok(NULL, "\n");
			}
		}

		/* Strip ctime date prefix — locale-independent: weekday check uses
		 * hardcoded English names (Xymon always formats timestamps in C locale),
		 * year-scan uses only ASCII digits so LC_TIME doesn't matter. */
		{
			char *msg = skipwhitespace(txt);
			static const char * const wdays[] =
				{ "Mon","Tue","Wed","Thu","Fri","Sat","Sun", NULL };
			int i, has_date = 0;
			for (i = 0; wdays[i]; i++) {
				if (strncmp(msg, wdays[i], 3) == 0 &&
				    (msg[3] == ' ' || msg[3] == '\t')) { has_date = 1; break; }
			}
			if (has_date) {
				/* Scan forward to 4-digit year, skip past it */
				char *p = msg;
				while (*p) {
					if (isdigit((unsigned char)p[0]) && isdigit((unsigned char)p[1]) &&
					    isdigit((unsigned char)p[2]) && isdigit((unsigned char)p[3]) &&
					    (p[4]=='\0' || p[4]==' ' || p[4]=='\t')) {
						p += 4;
						while (*p == ' ' || *p == '\t') p++;
						msg = p;
						break;
					}
					p++;
				}
			}
			if (*msg) {
				fprintf(output, "<h2 class=\"fs-6\">");
				textwithcolorimg(msg, output);
				fprintf(output, "</h2>\n");
			}
		}
	}

	/* Sender below the summary heading, above the message body */
	if (sender) {
		fprintf(output, "<p class=\"text-muted small mb-2\">");
		if (linktoclient)
			fprintf(output, "<a class=\"text-muted\" href=\"%s\">Status</a> from %s",
				linktoclient, htmlquoted(sender));
		else
			fprintf(output, "%s", htmlquoted(sender));
		fprintf(output, "</p>\n");
	}

	if (!htmlfmt) fprintf(output, "<pre class=\"bg-dark p-3 rounded border border-secondary overflow-auto\">\n");
	textwithcolorimg(restofmsg, output);
	if (!htmlfmt) fprintf(output, "\n</pre>\n");

	if (ackmsg) {
		char *ackedby;
		char ackuntil[200];

		MEMDEFINE(ackuntil);

		strftime(ackuntil, sizeof(ackuntil)-1, xgetenv("ACKUNTILMSG"), localtime(&acktime));
		ackuntil[sizeof(ackuntil)-1] = '\0';

		ackedby = strstr(ackmsg, "\nAcked by:");
		if (ackedby) {
			*ackedby = '\0';
			fprintf(output, "<div class=\"alert alert-info mt-2 mb-0\"><strong>Acknowledged:</strong> %s<br>%s<br>%s</div>\n",
				htmlquoted(ackmsg), (ackedby+1), ackuntil);
			*ackedby = '\n';
		}
		else {
			fprintf(output, "<div class=\"alert alert-info mt-2 mb-0\"><strong>Acknowledged:</strong> %s<br>%s</div>\n",
				htmlquoted(ackmsg), ackuntil);
		}

		MEMUNDEFINE(ackuntil);
	}

	fprintf(output, "</div>\n"); /* end message column */

	if (rrd && graph) {
		fprintf(output, "<div class=\"col-sm-12 col-md-6\">\n");
		int may_have_rrd = 1;

		char *lcstr = strstr(restofmsg, "<!-- linecount=");
		if (lcstr) {
			linecount=atoi(lcstr+15);
		}
		else {
			SBUF_DEFINE(multikey);
			char *p;

			if (multigraphs == NULL) multigraphs = ",disk,inode,qtree,quotas,snapshot,TblSpace,if_load,";

			if (strncmp(rrd->xymonrrdname,"devmon",6) == 0) may_have_rrd=0;

			SBUF_MALLOC(multikey, strlen(service) + 3);
			snprintf(multikey, multikey_buflen, ",%s,", service);
			if (strstr(multigraphs, multikey)) {
				int netwarediskreport = (strstr(firstline, "NetWare Volumes") != NULL);
				int tblspacereport = (strstr(restofmsg, "dbcheck.pl") != NULL);
				int header = (strchr(firstline, '/') == NULL);

				linecount = 0; p = restofmsg;
				do {
					while ((*p) && (isspace((int)*p) || iscntrl((int)*p))) p++;
					if (*p) {
						if ((*p == '&') && (parse_color(p+1) != -1)) {
							if (netwarediskreport) linecount++;
						}
						else {
							if (!netwarediskreport) linecount++;
						}

						if (strlen(p) > 10 &&  *p == '<' ) {
							if(!strncmp(p, "<!--DEVMON",10)) {
								linecount = -2;
								may_have_rrd=1;
							}
						}

						p = strchr(p, '\n');
					}
				} while (p && (*p));

				if (!netwarediskreport && header && (linecount > 1)) linecount--;
				if (tblspacereport && (linecount > 2)) linecount-=2;

			}
			xfree(multikey);
		}

		if (may_have_rrd) {
			fprintf(output, "<!-- linecount=%d -->\n", linecount);
			fprintf(output, "<a name=\"begingraph\">&nbsp;</a>\n");

			SBUF_MALLOC(graphs, 7 + strlen(service) + 1);
			snprintf(graphs, graphs_buflen, "GRAPHS_%s", service);
			graphsenv=getenv(graphs);
			if (graphsenv) {
				fprintf(output, "<!-- GRAPHS_%s: %s -->\n", service, graphsenv);
				graphsptr = strtok(graphsenv,",");
				while (graphsptr != NULL) {
					graph->xymonrrdname = strdup(graphsptr);
					fprintf(output, "%s\n", xymon_graph_data(hostname, displayname, graphsptr, color, graph, linecount, HG_WITHOUT_STALE_RRDS, HG_PLAIN_LINK, locatorbased, now-graphtime, now));
					graphsptr = strtok(NULL,",");
				}

			}
			else {
				fprintf(output, "%s\n", xymon_graph_data(hostname, displayname, service, color, graph, linecount, HG_WITHOUT_STALE_RRDS, HG_PLAIN_LINK, locatorbased, now-graphtime, now));
			}
			xfree(graphs);
		}
		fprintf(output, "</div>\n"); /* end graph column */
	}

	fprintf(output, "</div>\n"); /* end row */

	headfoot(output, xymontpl, "", "footer", color);
}

char *alttag(char *columnname, int color, int acked, int propagate, char *age)
{
	static char tag[1024];
	size_t remain;

	remain = sizeof(tag) - 1;
	remain -= snprintf(tag, remain, "%s:%s:", columnname, colorname(color));
	if (remain > 20) {
		if (acked) { strncat(tag, "acked:", remain); remain -= 6; }
		if (!propagate) { strncat(tag, "nopropagate:", remain); remain -= 12; }
		strncat(tag, age, remain);
	}
	tag[sizeof(tag)-1] = '\0';

	return tag;
}


static char *nameandcomment(void *host, char *hostname, int usetooltip)
{
	STATIC_SBUF_DEFINE(result);
	char *cmt, *disp, *hname;

	if (result) xfree(result);

	hostpopup_setup();

	if (!host) return hostname;

	hname = xmh_item(host, XMH_HOSTNAME);
	disp = xmh_item(host, XMH_DISPLAYNAME);

	cmt = NULL;
	if (!cmt && (hostpopup & HOSTPOPUP_COMMENT))             cmt = xmh_item(host, XMH_COMMENT);
	if (!cmt && usetooltip && (hostpopup & HOSTPOPUP_DESCR)) cmt = xmh_item(host, XMH_DESCRIPTION);
	if (!cmt && usetooltip && (hostpopup & HOSTPOPUP_IP))    cmt = xmh_item(host, XMH_IP);

	if (disp == NULL) disp = hname;

	if (cmt) {
		if (usetooltip) {
			SBUF_MALLOC(result, strlen(disp) + strlen(cmt) + 30);
			snprintf(result, result_buflen, "<span title=\"%s\">%s</span>", cmt, disp);
		}
		else {
			SBUF_MALLOC(result, strlen(disp) + strlen(cmt) + 4);
			snprintf(result, result_buflen, "%s (%s)", disp, cmt);
		}
		return result;
	}
	else
		return disp;
}

static char *urldoclink(const char *docurl, const char *hostname)
{
	static char linkurl[PATH_MAX];

	if (docurl) {
		snprintf(linkurl, sizeof(linkurl), docurl, hostname);
	}
	else {
		linkurl[0] = '\0';
	}

	return linkurl;
}


void setdocurl(char *url)
{
	if (documentationurl) xfree(documentationurl);
	documentationurl = strdup(url);
}

void setdoctarget(char *target)
{
	if (doctarget) xfree(doctarget);
	doctarget = strdup(target);
}

char *hostnamehtml(char *hostname, char *defaultlink, int usetooltip)
{
	static char result[4096];
	void *hinfo = hostinfo(hostname);
	char *hostlinkurl;

	if (!doctarget) doctarget = strdup("");

#if __GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 6)
	#pragma GCC diagnostic push
	#pragma GCC diagnostic ignored "-Wformat-truncation"
#endif  // __GNUC__
	if (documentationurl) {
		snprintf(result, sizeof(result), "<a href=\"%s\" %s>%s</a>",
			urldoclink(documentationurl, hostname),
			doctarget, nameandcomment(hinfo, hostname, usetooltip));
	}
	else if ((hostlinkurl = hostlink(hostname)) != NULL) {
		snprintf(result, sizeof(result), "<a href=\"%s\" %s>%s</a>",
			hostlinkurl, doctarget, nameandcomment(hinfo, hostname, usetooltip));
	}
	else if (defaultlink) {
		snprintf(result, sizeof(result), "<a href=\"%s/%s\" %s>%s</a>",
			xgetenv("XYMONWEB"), defaultlink, doctarget,
			nameandcomment(hinfo, hostname, usetooltip));
	}
	else {
		snprintf(result, sizeof(result), "%s",
			nameandcomment(hinfo, hostname, usetooltip));
	}
#if __GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 6)
	#pragma GCC diagnostic pop

#endif  // __GNUC__
	return result;
}
