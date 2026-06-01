/*----------------------------------------------------------------------------*/
/* Xymon monitor library.                                                     */
/*                                                                            */
/* This is a library module, part of libxymon.                                */
/* It contains routines for handling header- and footer-files.                */
/*                                                                            */
/* Copyright (C) 2002-2011 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id$";

#include <sys/types.h>
#include <sys/stat.h>
#include <limits.h>
#include <time.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <ctype.h>
#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

#include "libxymon.h"
#include "version.h"

/* Stuff for headfoot - variables we can set dynamically */
static char *hostenv_hikey = NULL;
static char *hostenv_host = NULL;
static char *hostenv_ip = NULL;
static char *hostenv_svc = NULL;
static char *hostenv_color = NULL;
static char *hostenv_pagepath = NULL;

static time_t hostenv_reportstart = 0;
static time_t hostenv_reportend = 0;

static char *hostenv_repwarn = NULL;
static char *hostenv_reppanic = NULL;

static time_t hostenv_snapshot = 0;
static char *hostenv_logtime = NULL;
static char *hostenv_templatedir = NULL;
static int hostenv_refresh = 60;

static char *statusboard = NULL;
static char *scheduleboard = NULL;

static int  ackcookie_val  = 0;
static int  have_ackcookie = 0;

static char *hostpattern_text = NULL;
static pcre2_code *hostpattern = NULL;
static char *pagepattern_text = NULL;
static pcre2_code *pagepattern = NULL;
static char *ippattern_text = NULL;
static pcre2_code *ippattern = NULL;
static char *classpattern_text = NULL;
static pcre2_code *classpattern = NULL;
static void * hostnames;
static void * testnames;

typedef struct treerec_t {
	char *name;
	int flag;
} treerec_t;

static int backdays = 0, backhours = 0, backmins = 0, backsecs = 0;
static char hostenv_eventtimestart[20];
static char hostenv_eventtimeend[20];

typedef struct listrec_t {
	char *name, *val, *extra;
	int selected;
	struct listrec_t *next;
} listrec_t;
typedef struct listpool_t {
	char *name;
	struct listrec_t *listhead, *listtail;
	struct listpool_t *next;
} listpool_t;
static listpool_t *listpoolhead = NULL;

typedef struct bodystorage_t {
	char *id;
	strbuffer_t *txt;
} bodystorage_t;


static void clearflags(void * tree)
{
	xtreePos_t handle;
	treerec_t *rec;

	if (!tree) return;

	for (handle = xtreeFirst(tree); (handle != xtreeEnd(tree)); handle = xtreeNext(tree, handle)) {
		rec = (treerec_t *)xtreeData(tree, handle);
		rec->flag = 0;
	}
}

void sethostenv(char *host, char *ip, char *svc, char *color, char *hikey)
{
	if (hostenv_hikey) xfree(hostenv_hikey);
	if (hostenv_host)  xfree(hostenv_host);
	if (hostenv_ip)    xfree(hostenv_ip);
	if (hostenv_svc)   xfree(hostenv_svc);
	if (hostenv_color) xfree(hostenv_color);

	hostenv_hikey = (hikey ? strdup(htmlquoted(hikey)) : NULL);
	hostenv_host = strdup(htmlquoted(host));
	hostenv_ip = strdup(htmlquoted(ip));
	hostenv_svc = strdup(htmlquoted(svc));
	hostenv_color = strdup(color);

	/* Reset per-host ack-cookie cache whenever host/svc context changes. */
	have_ackcookie = 0;
	ackcookie_val  = 0;
}

void sethostenv_report(time_t reportstart, time_t reportend, double repwarn, double reppanic)
{
	if (hostenv_repwarn == NULL) hostenv_repwarn = malloc(10);
	if (hostenv_reppanic == NULL) hostenv_reppanic = malloc(10);

	hostenv_reportstart = reportstart;
	hostenv_reportend = reportend;

	snprintf(hostenv_repwarn, 10, "%.2f", repwarn);
	snprintf(hostenv_reppanic, 10, "%.2f", reppanic);
}

void sethostenv_snapshot(time_t snapshot)
{
	hostenv_snapshot = snapshot;
}

void sethostenv_histlog(char *histtime)
{
	if (hostenv_logtime) xfree(hostenv_logtime);
	hostenv_logtime = strdup(histtime);
}

void sethostenv_template(char *dir)
{
	if (hostenv_templatedir) xfree(hostenv_templatedir);
	hostenv_templatedir = strdup(dir);
}

void sethostenv_refresh(int n)
{
	hostenv_refresh = n;
}

void sethostenv_pagepath(char *s)
{
	if (!s) return;
	if (hostenv_pagepath) xfree(hostenv_pagepath);
	hostenv_pagepath = strdup(s);
}

void sethostenv_filter(char *hostptn, char *pageptn, char *ipptn, char *classptn)
{
	int err;
	PCRE2_SIZE errofs;

	if (hostpattern_text) xfree(hostpattern_text);
	if (hostpattern) { pcre2_code_free(hostpattern); hostpattern = NULL; }
	if (pagepattern_text) xfree(pagepattern_text);
	if (pagepattern) { pcre2_code_free(pagepattern); pagepattern = NULL; }
	if (ippattern_text) xfree(ippattern_text);
	if (ippattern) { pcre2_code_free(ippattern); ippattern = NULL; }
	if (classpattern_text) xfree(classpattern_text);
	if (classpattern) { pcre2_code_free(classpattern); classpattern = NULL; }

	/* Setup the pattern to match names against */
	if (hostptn) {
		hostpattern_text = strdup(hostptn);
		hostpattern = pcre2_compile(hostptn, strlen(hostptn), PCRE2_CASELESS, &err, &errofs, NULL);
	}
	if (pageptn) {
		pagepattern_text = strdup(pageptn);
		pagepattern = pcre2_compile(pageptn, strlen(pageptn), PCRE2_CASELESS, &err, &errofs, NULL);
	}
	if (ipptn) {
		ippattern_text = strdup(ipptn);
		ippattern = pcre2_compile(ipptn, strlen(ipptn), PCRE2_CASELESS, &err, &errofs, NULL);
	}
	if (classptn) {
		classpattern_text = strdup(classptn);
		classpattern = pcre2_compile(classptn, strlen(classptn), PCRE2_CASELESS, &err, &errofs, NULL);
	}
}

static listpool_t *find_listpool(char *listname)
{
	listpool_t *pool = NULL;

	if (!listname) listname = "";
	for (pool = listpoolhead; (pool && strcmp(pool->name, listname)); pool = pool->next);
	if (!pool) {
		pool = (listpool_t *)calloc(1, sizeof(listpool_t));
		pool->name = strdup(listname);
		pool->next = listpoolhead;
		listpoolhead = pool;
	}

	return pool;
}

void sethostenv_clearlist(char *listname)
{
	listpool_t *pool = NULL;
	listrec_t *zombie;

	pool = find_listpool(listname);
	while (pool->listhead) {
		zombie = pool->listhead;
		pool->listhead = pool->listhead->next;

		xfree(zombie->name); xfree(zombie->val); xfree(zombie);
	}
}

void sethostenv_addtolist(char *listname, char *name, char *val, char *extra, int selected)
{
	listpool_t *pool = NULL;
	listrec_t *newitem = (listrec_t *)calloc(1, sizeof(listrec_t));

	pool = find_listpool(listname);
	newitem->name = strdup(name);
	newitem->val = strdup(val);
	newitem->extra = (extra ? strdup(extra) : NULL);
	newitem->selected = selected;
	if (pool->listtail) {
		pool->listtail->next = newitem;
		pool->listtail = newitem;
	}
	else {
		pool->listhead = pool->listtail = newitem;
	}
}

static int critackttprio = 0;
static char *critackttgroup = NULL;
static char *critackttextra = NULL;
static char *ackinfourl = NULL;
static char *critackdocurl = NULL;

void sethostenv_critack(int prio, char *ttgroup, char *ttextra, char *infourl, char *docurl)
{
	critackttprio = prio;
	if (critackttgroup) xfree(critackttgroup); critackttgroup = strdup((ttgroup && *ttgroup) ? ttgroup : "&nbsp;");
	if (critackttextra) xfree(critackttextra); critackttextra = strdup((ttextra && *ttextra) ? ttextra : "&nbsp;");
	if (ackinfourl) xfree(ackinfourl); ackinfourl = strdup(infourl);
	if (critackdocurl) xfree(critackdocurl); critackdocurl = strdup((docurl && *docurl) ? docurl : "");
}

static char *criteditupdinfo = NULL;
static int criteditprio = -1;
static char *criteditgroup = NULL;
static time_t criteditstarttime = 0;
static time_t criteditendtime = 0;
static char *criteditextra = NULL;
static char *criteditslawkdays = NULL;
static char *criteditslastart = NULL;
static char *criteditslaend = NULL;
static char **criteditclonelist = NULL;
static int criteditclonesize = 0;

void sethostenv_critedit(char *updinfo, int prio, char *group, time_t starttime, time_t endtime, char *crittime, char *extra)
{
	char *p;

	if (criteditupdinfo) xfree(criteditupdinfo);
	criteditupdinfo = strdup(updinfo);

	criteditprio = prio;
	criteditstarttime = starttime;
	criteditendtime = endtime;

	if (criteditgroup) xfree(criteditgroup);
	criteditgroup = strdup(group ? group : "");

	if (criteditextra) xfree(criteditextra);
	criteditextra = strdup(extra ? extra : "");

	if (criteditslawkdays) xfree(criteditslawkdays);
	criteditslawkdays = criteditslastart = criteditslaend = NULL;

	if (crittime) {
		criteditslawkdays = strdup(crittime);
		p = strchr(criteditslawkdays, ':');
		if (p) {
			*p = '\0';
			criteditslastart = p+1;

			p = strchr(criteditslastart, ':');
			if (p) {
				*p = '\0';
				criteditslaend = p+1;
			}
		}

		if (criteditslawkdays && (!criteditslastart || !criteditslaend)) {
			xfree(criteditslawkdays);
			criteditslawkdays = criteditslastart = criteditslaend = NULL;
		}
	}
}

void sethostenv_critclonelist_clear(void)
{
	int i;

	if (criteditclonelist) {
		for (i=0; (criteditclonelist[i]); i++) xfree(criteditclonelist[i]);
		xfree(criteditclonelist);
	}
	criteditclonelist = malloc(sizeof(char *));
	criteditclonelist[0] = NULL;
	criteditclonesize = 0;
}

void sethostenv_critclonelist_add(char *hostname)
{
	char *p;

	criteditclonelist = (char **)realloc(criteditclonelist, (criteditclonesize + 2)*sizeof(char *));
	criteditclonelist[criteditclonesize] = strdup(hostname);
	p = criteditclonelist[criteditclonesize];
	criteditclonelist[++criteditclonesize] = NULL;

	p += (strlen(p) - 1);
	if (*p == '=') *p = '\0';
}


void sethostenv_backsecs(int seconds)
{
	backdays = seconds / 86400; seconds -= backdays*86400;
	backhours = seconds / 3600; seconds -= backhours*3600;
	backmins = seconds / 60; seconds -= backmins*60;
	backsecs = seconds;
}

void sethostenv_eventtime(time_t starttime, time_t endtime)
{
	*hostenv_eventtimestart = *hostenv_eventtimeend = '\0';
	if (starttime) strftime(hostenv_eventtimestart, sizeof(hostenv_eventtimestart), "%Y/%m/%d@%H:%M:%S", localtime(&starttime));
	if (endtime) strftime(hostenv_eventtimeend, sizeof(hostenv_eventtimeend), "%Y/%m/%d@%H:%M:%S", localtime(&endtime));
}

char *wkdayselect(char wkday, char *valtxt, int isdefault)
{
	static char result[100];
	char *selstr;

	if (!criteditslawkdays) {
		if (isdefault) selstr = " selected";
		else selstr = "";
	}
	else {
		if (strchr(criteditslawkdays, wkday)) selstr = " selected";
		else selstr = "";
	}

	snprintf(result, sizeof(result), "<option value=\"%c\"%s>%s</option>\n", wkday, selstr, valtxt);

	return result;
}


static void *wanted_host(char *hostname)
{
	void *hinfo = hostinfo(hostname);
	int result;
	pcre2_match_data *ovector;

	if (!hinfo) return NULL;

	ovector = pcre2_match_data_create(30, NULL);
	if (hostpattern) {
		result = pcre2_match(hostpattern, hostname, strlen(hostname), 0, 0,
				ovector, NULL);
		if (result < 0) {
			pcre2_match_data_free(ovector);
			return NULL;
		}
	}

	if (pagepattern && hinfo) {
		char *pname = xmh_item(hinfo, XMH_PAGEPATH);
		result = pcre2_match(pagepattern, pname, strlen(pname), 0, 0,
				ovector, NULL);
		if (result < 0) {
			pcre2_match_data_free(ovector);
			return NULL;
		}
	}

	if (ippattern && hinfo) {
		char *hostip = xmh_item(hinfo, XMH_IP);
		result = pcre2_match(ippattern, hostip, strlen(hostip), 0, 0,
				ovector, NULL);
		if (result < 0) {
			pcre2_match_data_free(ovector);
			return NULL;
		}
	}

	if (classpattern && hinfo) {
		char *hostclass = xmh_item(hinfo, XMH_CLASS);
		if (!hostclass) return NULL;

		result = pcre2_match(classpattern, hostclass, strlen(hostclass), 0, 0,
				ovector, NULL);
		if (result < 0) {
			pcre2_match_data_free(ovector);
			return NULL;
		}
	}
	pcre2_match_data_free(ovector);

	return hinfo;
}


static void fetch_board(void)
{
	static int haveboard = 0;
	char *walk, *eoln;
	sendreturn_t *sres;

	if (haveboard) return;

	sres = newsendreturnbuf(1, NULL);
	if (sendmessage("xymondboard fields=hostname,testname,disabletime,dismsg,lastchange,line1",
			NULL, XYMON_TIMEOUT, sres) != XYMONSEND_OK) {
		freesendreturnbuf(sres);
		return;
	}

	haveboard = 1;
	statusboard = getsendreturnstr(sres, 1);
	freesendreturnbuf(sres);

	hostnames = xtreeNew(strcasecmp);
	testnames = xtreeNew(strcasecmp);
	walk = statusboard;
	while (walk) {
		eoln = strchr(walk, '\n'); if (eoln) *eoln = '\0';
		if (strlen(walk) && (strncmp(walk, "summary|", 8) != 0)) {
			char *buf, *hname = NULL, *tname = NULL;
			treerec_t *newrec;

			buf = strdup(walk);

			hname = gettok(buf, "|");

			if (hname && wanted_host(hname) && hostinfo(hname)) {
				newrec = (treerec_t *)malloc(sizeof(treerec_t));
				newrec->name = strdup(hname);
				newrec->flag = 0;
				xtreeAdd(hostnames, newrec->name, newrec);

				tname = gettok(NULL, "|");
				if (tname) {
					newrec = (treerec_t *)malloc(sizeof(treerec_t));
					newrec->name = strdup(tname);
					newrec->flag = 0;
					xtreeAdd(testnames, strdup(tname), newrec);
				}
			}

			xfree(buf);
		}

		if (eoln) {
			*eoln = '\n';
			walk = eoln + 1;
		}
		else
			walk = NULL;
	}

	sres = newsendreturnbuf(1, NULL);
	if (sendmessage("schedule", NULL, XYMON_TIMEOUT, sres) != XYMONSEND_OK) {
		freesendreturnbuf(sres);
		return;
	}

	scheduleboard = getsendreturnstr(sres, 1);
	freesendreturnbuf(sres);
}

static void fetch_ackcookie(void)
{
	char cmd[1024];
	sendreturn_t *sres;
	char *resp, *p;

	if (have_ackcookie) return;
	have_ackcookie = 1;

	if (!hostenv_host || !*hostenv_host || !hostenv_svc || !*hostenv_svc) return;

	/*
	 * Fetch the xymond ack cookie for this specific host+service.
	 *
	 * The cookie is an integer xymond assigns when a test enters alert state.
	 * acknowledge.c requires it as the NUMBER_1 form field; without it the
	 * xymondack message cannot be constructed and the ack is silently dropped
	 * (see acknowledge.c: "if (!awalk->acknum) ... NO ACK sent").
	 *
	 * Note: hostenv_host/svc are HTML-quoted, but hostnames never contain
	 * HTML-special characters so the regex is safe to build directly.
	 */
	snprintf(cmd, sizeof(cmd),
		"xymondboard fields=hostname,testname,cookie host=^%s$ test=^%s$",
		hostenv_host, hostenv_svc);

	sres = newsendreturnbuf(1, NULL);
	if (sendmessage(cmd, NULL, XYMON_TIMEOUT, sres) != XYMONSEND_OK) {
		freesendreturnbuf(sres);
		return;
	}

	resp = getsendreturnstr(sres, 0);
	if (resp) {
		/* Response format: hostname|testname|cookie\n */
		p = strchr(resp, '|'); if (p) p++;  /* skip hostname field */
		p = strchr(p,   '|'); if (p) p++;  /* skip testname field */
		if (p && *p) ackcookie_val = atoi(p);
	}
	freesendreturnbuf(sres);
}

static char *eventreport_timestring(time_t timestamp)
{
	static char result[20];

	strftime(result, sizeof(result), "%Y/%m/%d@%H:%M:%S", localtime(&timestamp));
	return result;
}

static void build_pagepath_dropdown(FILE *output)
{
	void * ptree;
	void *hwalk;
	xtreePos_t handle;

	ptree = xtreeNew(strcmp);

	for (hwalk = first_host(); (hwalk); hwalk = next_host(hwalk, 0)) {
		char *path = xmh_item(hwalk, XMH_PAGEPATH);
		char *ptext;

		handle = xtreeFind(ptree, path);
		if (handle != xtreeEnd(ptree)) continue;

		ptext = xmh_item(hwalk, XMH_PAGEPATHTITLE);
		xtreeAdd(ptree, ptext, path);
	}

	for (handle = xtreeFirst(ptree); (handle != xtreeEnd(ptree)); handle = xtreeNext(ptree, handle)) {
		fprintf(output, "<option value=\"%s\">%s</option>\n", (char *)xtreeData(ptree, handle), xtreeKey(ptree, handle));
	}

	xtreeDestroy(ptree);
}

char *xymonbody(char *id)
{
	static void * bodystorage;
	static int firsttime = 1;
	xtreePos_t handle;
	bodystorage_t *bodyelement;

	strbuffer_t *rawdata, *parseddata;
	char *envstart, *envend, *outpos;
	char *idtag, *idval;
	int idtaglen;

	if (firsttime) {
		bodystorage = xtreeNew(strcmp);
		firsttime = 0;
	}

	idtaglen = strspn(id, "ABCDEFGHIJKLMNOPQRSTUVWXYZ");
	idtag = (char *)malloc(idtaglen + 1);
	strncpy(idtag, id, idtaglen);
	*(idtag+idtaglen) = '\0';

	handle = xtreeFind(bodystorage, idtag);
	if (handle != xtreeEnd(bodystorage)) {
		bodyelement = (bodystorage_t *)xtreeData(bodystorage, handle);
		xfree(idtag);
		return STRBUF(bodyelement->txt);
	}

	rawdata = newstrbuffer(0);
	idval = xgetenv(idtag);
	if (idval == NULL) return "";

	if (strncmp(idval, "file:", 5) == 0) {
		FILE *fd;
		strbuffer_t *inbuf = newstrbuffer(0);

		fd = stackfopen(idval+5, "r", NULL);
		if (fd != NULL) {
			while (stackfgets(inbuf, NULL)) addtostrbuffer(rawdata, inbuf);
			stackfclose(fd);
		}

		freestrbuffer(inbuf);
	}
	else {
		addtobuffer(rawdata, idval);
	}

	/* Output the body data, but expand any environment variables along the way */
	parseddata = newstrbuffer(0);
	outpos = STRBUF(rawdata);
	while (*outpos) {
		envstart = strchr(outpos, '$');
		if (envstart) {
			char savechar;
			char *envval = NULL;

			*envstart = '\0';
			addtobuffer(parseddata, outpos);

			envstart++;
			envend = envstart + strspn(envstart, "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789");
			savechar = *envend; *envend = '\0';
			if (*envstart) envval = xgetenv(envstart);

			*envend = savechar;
			outpos = envend;

			if (envval) {
				addtobuffer(parseddata, envval);
			}
			else {
				addtobuffer(parseddata, "$");
				addtobuffer(parseddata, envstart);
			}
		}
		else {
			addtobuffer(parseddata, outpos);
			outpos += strlen(outpos);
		}
	}

	freestrbuffer(rawdata);

	bodyelement = (bodystorage_t *)calloc(1, sizeof(bodystorage_t));
	bodyelement->id = idtag;
	bodyelement->txt = parseddata;
	xtreeAdd(bodystorage, bodyelement->id, bodyelement);

	return STRBUF(bodyelement->txt);
}

typedef struct distest_t {
	char *name;
	char *cause;
	time_t until;
	time_t disstarted;
	int color;
	struct distest_t *next;
} distest_t;

typedef struct dishost_t {
	char *name;
	struct distest_t *tests;
	struct dishost_t *next;
} dishost_t;

/* Strip KEY=VALUE lines from buf, calling setenv() for each match.
 * Lines are removed in-place so output_parsed never sees them.
 * Used to let per-page inc files declare env vars (e.g. XYMONBODYCLASS). */
static void strip_assignments(char *buf)
{
	char *p = buf;
	while (*p) {
		char *nl  = strchr(p, '\n');
		char *eol = nl ? nl : (p + strlen(p));
		char *eq  = (char *)memchr(p, '=', (size_t)(eol - p));
		if (eq && eq > p) {
			int valid = 1;
			char *c;
			for (c = p; c < eq; c++) {
				if (!isupper((unsigned char)*c) && *c != '_' &&
				    !(c > p && isdigit((unsigned char)*c)))
					{ valid = 0; break; }
			}
			if (valid) {
				char val[4096];
				int vlen = (int)(eol - (eq + 1));
				*eq = '\0';
				if (vlen > 0 && vlen < (int)sizeof(val)) {
					memcpy(val, eq + 1, (size_t)vlen);
					val[vlen] = '\0';
				} else {
					val[0] = '\0';
				}
				setenv(p, val, 1);
				*eq = '=';
				if (nl) memmove(p, nl + 1, strlen(nl + 1) + 1);
				else    { *p = '\0'; break; }
				continue;
			}
		}
		p = nl ? nl + 1 : (p + strlen(p));
	}
}

void output_parsed(FILE *output, char *templatedata, int bgcolor, time_t selectedtime)
{
	char	*t_start, *t_next;
	char	savechar;
	time_t	now = getcurrenttime(NULL);
	time_t  yesterday = getcurrenttime(NULL) - 86400;
	struct  tm *nowtm;

	for (t_start = templatedata, t_next = strchr(t_start, '&'); (t_next); ) {
		/* Copy from t_start to t_next unchanged */
		*t_next = '\0'; t_next++;
		fprintf(output, "%s", t_start);

		/* Find token */
		t_start = t_next;
		/* Don't include lower-case letters - reserve those for eg "&nbsp;" */
		t_next += strspn(t_next, "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_");
		savechar = *t_next; *t_next = '\0';

		if ((strcmp(t_start, "XYMWEBDATE") == 0) || (strcmp(t_start, "BBDATE") == 0)) {
			char *datefmt = xgetenv("XYMONDATEFORMAT");
			char datestr[100];

			MEMDEFINE(datestr);

			/*
			 * If no XYMONDATEFORMAT setting, use a format string that
			 * produces output similar to that from ctime()
			 */
			if (datefmt == NULL) datefmt = "%a %b %d %H:%M:%S %Y\n";

			if (hostenv_reportstart != 0) {
				char starttime[20], endtime[20];

				MEMDEFINE(starttime); MEMDEFINE(endtime);

				strftime(starttime, sizeof(starttime), "%b %d %Y", localtime(&hostenv_reportstart));
				strftime(endtime, sizeof(endtime), "%b %d %Y", localtime(&hostenv_reportend));
				if (strcmp(starttime, endtime) == 0)
					fprintf(output, "%s", starttime);
				else
					fprintf(output, "%s - %s", starttime, endtime);

				MEMUNDEFINE(starttime); MEMUNDEFINE(endtime);
			}
			else if (hostenv_snapshot != 0) {
				strftime(datestr, sizeof(datestr), datefmt, localtime(&hostenv_snapshot));
				fprintf(output, "%s", datestr);
			}
			else {
				strftime(datestr, sizeof(datestr), datefmt, localtime(&now));
				fprintf(output, "%s", datestr);
			}

			MEMUNDEFINE(datestr);
		}

		else if ((strcmp(t_start, "XYMWEBBACKGROUND") == 0) || (strcmp(t_start, "BBBACKGROUND") == 0)) {
			fprintf(output, "%s", colorname(bgcolor));
		}
		else if (strcmp(t_start, "XYMWEBSTATUS") == 0) {
			fprintf(output, "status-%s", colorname(bgcolor));
		}
		else if (strcmp(t_start, "XYMONSTYLESSHEET") == 0) {
			char *wwwurl = xgetenv("XYMONSERVERWWWURL");
			char *theme  = xgetenv("XYMONTHEME");
			if (!theme || !*theme) theme = "default";
			fprintf(output, "%s/themes/%s/xymon.css",
				(wwwurl && *wwwurl) ? wwwurl : "/xymon", theme);
		}
		else if (strcmp(t_start, "XYMONEXTERNALS") == 0) {
			char *exturl = xgetenv("XYMONEXTERNALS");
			if (exturl && *exturl) {
				fprintf(output, "%s", exturl);
			} else {
				char *wwwurl = xgetenv("XYMONSERVERWWWURL");
				fprintf(output, "%s/externals",
					(wwwurl && *wwwurl) ? wwwurl : "/xymon");
			}
		}
		else if (strcmp(t_start, "XYMONHISTORYBUTTON") == 0) {
			char *show = xgetenv("XYMONSHOWHISTORY");
			if (show && *show) {
				char *cgiurl   = xgetenv("CGIBINURL");
				/* hostenv_* vars are already HTML-quoted; don't call htmlquoted()
				 * again — it uses a static buffer so two calls in one fprintf()
				 * would both point at the same memory (the last value written). */
				char *histhost = hostenv_hikey ? hostenv_hikey : hostenv_host;
				if (!cgiurl || !*cgiurl) cgiurl = "/cgi-bin";
				fprintf(output,
					"<form class=\"d-inline m-0\" action=\"%s/history.sh\">",
					cgiurl);
				fprintf(output,
					"<button type=\"submit\""
					" class=\"btn btn-outline-secondary btn-sm\">"
					"<i class=\"fa-solid fa-clock-rotate-left\"></i>"
					"<span class=\"d-none d-sm-inline\">History</span></button>");
				fprintf(output,
					"<input type=\"hidden\" name=\"HISTFILE\" value=\"%s.%s\">",
					histhost, hostenv_svc);
				fprintf(output,
					"<input type=\"hidden\" name=\"ENTRIES\" value=\"50\">"
					"<input type=\"hidden\" name=\"IP\" value=\"%s\">",
					hostenv_ip);
				fprintf(output,
					"<input type=\"hidden\" name=\"DISPLAYNAME\" value=\"%s\">",
					hostenv_host);
				fprintf(output, "</form>");
			}
		}
		else if (strcmp(t_start, "XYMONCOLORFILTER") == 0) {
			fprintf(output,
				"<div class=\"btn-group xymon-color-filter\""
				" role=\"group\" aria-label=\"Filter by status color\">"
				"<input type=\"checkbox\" class=\"btn-check xymon-filter-btn\""
				" id=\"filter-red\" autocomplete=\"off\">"
				"<label class=\"btn btn-sm btn-outline-secondary\" for=\"filter-red\">"
				"<i class=\"fa-solid fa-circle xymon-red\"></i></label>"
				"<input type=\"checkbox\" class=\"btn-check xymon-filter-btn\""
				" id=\"filter-yellow\" autocomplete=\"off\">"
				"<label class=\"btn btn-sm btn-outline-secondary\" for=\"filter-yellow\">"
				"<i class=\"fa-solid fa-circle xymon-yellow\"></i></label>"
				"<input type=\"checkbox\" class=\"btn-check xymon-filter-btn\""
				" id=\"filter-green\" autocomplete=\"off\">"
				"<label class=\"btn btn-sm btn-outline-secondary\" for=\"filter-green\">"
				"<i class=\"fa-solid fa-circle xymon-green\"></i></label>"
				"<input type=\"checkbox\" class=\"btn-check xymon-filter-btn\""
				" id=\"filter-blue\" autocomplete=\"off\">"
				"<label class=\"btn btn-sm btn-outline-secondary\" for=\"filter-blue\">"
				"<i class=\"fa-solid fa-circle xymon-blue\"></i></label>"
				"<input type=\"checkbox\" class=\"btn-check xymon-filter-btn\""
				" id=\"filter-purple\" autocomplete=\"off\">"
				"<label class=\"btn btn-sm btn-outline-secondary\" for=\"filter-purple\">"
				"<i class=\"fa-solid fa-circle xymon-purple\"></i></label>"
				"<input type=\"checkbox\" class=\"btn-check xymon-filter-btn\""
				" id=\"filter-clear\" autocomplete=\"off\">"
				"<label class=\"btn btn-sm btn-outline-secondary\" for=\"filter-clear\">"
				"<i class=\"fa-regular fa-circle xymon-clear\"></i></label>"
				"</div>");
		}
		else if (strcmp(t_start, "XYMONCOLORFILTERNOGREEN") == 0) {
			fprintf(output,
				"<div class=\"btn-group xymon-color-filter\""
				" role=\"group\" aria-label=\"Filter by status color\">"
				"<input type=\"checkbox\" class=\"btn-check xymon-filter-btn\""
				" id=\"filter-red\" autocomplete=\"off\">"
				"<label class=\"btn btn-sm btn-outline-secondary\" for=\"filter-red\">"
				"<i class=\"fa-solid fa-circle xymon-red\"></i></label>"
				"<input type=\"checkbox\" class=\"btn-check xymon-filter-btn\""
				" id=\"filter-yellow\" autocomplete=\"off\">"
				"<label class=\"btn btn-sm btn-outline-secondary\" for=\"filter-yellow\">"
				"<i class=\"fa-solid fa-circle xymon-yellow\"></i></label>"
				"<input type=\"checkbox\" class=\"btn-check xymon-filter-btn\""
				" id=\"filter-blue\" autocomplete=\"off\">"
				"<label class=\"btn btn-sm btn-outline-secondary\" for=\"filter-blue\">"
				"<i class=\"fa-solid fa-circle xymon-blue\"></i></label>"
				"<input type=\"checkbox\" class=\"btn-check xymon-filter-btn\""
				" id=\"filter-purple\" autocomplete=\"off\">"
				"<label class=\"btn btn-sm btn-outline-secondary\" for=\"filter-purple\">"
				"<i class=\"fa-solid fa-circle xymon-purple\"></i></label>"
				"<input type=\"checkbox\" class=\"btn-check xymon-filter-btn\""
				" id=\"filter-clear\" autocomplete=\"off\">"
				"<label class=\"btn btn-sm btn-outline-secondary\" for=\"filter-clear\">"
				"<i class=\"fa-regular fa-circle xymon-clear\"></i></label>"
				"</div>");
		}
		else if (strcmp(t_start, "XYMONDISABLEBUTTON") == 0) {
			if (hostenv_host && *hostenv_host && hostenv_svc && *hostenv_svc) {
				fprintf(output,
					"<button type=\"button\""
					" class=\"btn btn-outline-warning btn-sm\""
					" data-bs-toggle=\"modal\""
					" data-bs-target=\"#xymon-disable-modal\">"
					"<i class=\"fa-solid fa-bell-slash\"></i>"
					"<span class=\"d-none d-sm-inline\">Disable</span></button>");
			}
		}
		else if (strcmp(t_start, "XYMONACKNOWLEDGEBUTTON") == 0) {
			fetch_ackcookie();
			/* Only render when an active alert cookie exists — no cookie means
			 * the test is not in alert state and there is nothing to acknowledge. */
			if (ackcookie_val) {
				fprintf(output,
					"<button type=\"button\""
					" class=\"btn btn-outline-success btn-sm\""
					" data-bs-toggle=\"modal\""
					" data-bs-target=\"#xymon-ack-modal\">"
					"<i class=\"fa-solid fa-circle-check\"></i>"
					"<span class=\"d-none d-sm-inline\">Ack</span></button>");
			}
		}
		else if (strcmp(t_start, "XYMONACKCOOKIE") == 0) {
			fetch_ackcookie();
			/* NUMBER_1 hidden field in the ack modal form — see fetch_ackcookie()
			 * for why this cookie is mandatory in the acknowledge.sh POST. */
			fprintf(output, "%d", ackcookie_val);
		}
		else if ((strcmp(t_start, "XYMWEBCOLOR") == 0) || (strcmp(t_start, "BBCOLOR") == 0))
			fprintf(output, "%s", hostenv_color);
		else if ((strcmp(t_start, "XYMWEBSVC") == 0) || (strcmp(t_start, "BBSVC") == 0))
			fprintf(output, "%s", hostenv_svc);
		else if ((strcmp(t_start, "XYMWEBHOST") == 0) || (strcmp(t_start, "BBHOST") == 0))
			fprintf(output, "%s", hostenv_host);
		else if ((strcmp(t_start, "XYMWEBHIKEY") == 0) || (strcmp(t_start, "BBHIKEY") == 0))
			fprintf(output, "%s", (hostenv_hikey ? hostenv_hikey : hostenv_host));
		else if ((strcmp(t_start, "XYMWEBIP") == 0) || (strcmp(t_start, "BBIP") == 0))
			fprintf(output, "%s", hostenv_ip);
		else if ((strcmp(t_start, "XYMWEBIPNAME") == 0) || (strcmp(t_start, "BBIPNAME") == 0)) {
			if (strcmp(hostenv_ip, "0.0.0.0") == 0)  fprintf(output, "%s", hostenv_host);
			else fprintf(output, "%s", hostenv_ip);
		}
		else if ((strcmp(t_start, "XYMONREPWARN") == 0) || (strcmp(t_start, "BBREPWARN") == 0))
			fprintf(output, "%s", hostenv_repwarn);
		else if ((strcmp(t_start, "XYMONREPPANIC") == 0) || (strcmp(t_start, "BBREPPANIC") == 0))
			fprintf(output, "%s", hostenv_reppanic);
		else if (strcmp(t_start, "LOGTIME") == 0) 	 fprintf(output, "%s", (hostenv_logtime ? hostenv_logtime : ""));
		else if ((strcmp(t_start, "XYMWEBREFRESH") == 0) || (strcmp(t_start, "BBREFRESH") == 0))
			fprintf(output, "%d", hostenv_refresh);
		else if ((strcmp(t_start, "XYMWEBPAGEPATH") == 0) || (strcmp(t_start, "BBPAGEPATH") == 0))
			fprintf(output, "%s", (hostenv_pagepath ? hostenv_pagepath : ""));

		else if (strcmp(t_start, "REPMONLIST") == 0) {
			int i;
			struct tm monthtm;
			char mname[20];
			char *selstr;

			MEMDEFINE(mname);

			nowtm = localtime(&selectedtime);
			for (i=1; (i <= 12); i++) {
				if (i == (nowtm->tm_mon + 1)) selstr = " selected"; else selstr = "";
				monthtm.tm_mon = (i-1); monthtm.tm_mday = 1; monthtm.tm_year = nowtm->tm_year;
				monthtm.tm_hour = monthtm.tm_min = monthtm.tm_sec = monthtm.tm_isdst = 0;
				strftime(mname, sizeof(mname)-1, "%B", &monthtm);
				fprintf(output, "<option value=\"%d\"%s>%s</option>\n", i, selstr, mname);
			}

			MEMUNDEFINE(mname);
		}
		else if (strcmp(t_start, "MONLIST") == 0) {
			int i;
			struct tm monthtm;
			char mname[20];

			MEMDEFINE(mname);

			nowtm = localtime(&selectedtime);
			for (i=1; (i <= 12); i++) {
				monthtm.tm_mon = (i-1); monthtm.tm_mday = 1; monthtm.tm_year = nowtm->tm_year;
				monthtm.tm_hour = monthtm.tm_min = monthtm.tm_sec = monthtm.tm_isdst = 0;
				strftime(mname, sizeof(mname)-1, "%B", &monthtm);
				fprintf(output, "<option value=\"%d\">%s</option>\n", i, mname);
			}

			MEMUNDEFINE(mname);
		}
		else if (strcmp(t_start, "REPWEEKLIST") == 0) {
			int i;
			char weekstr[5];
			int weeknum;
			char *selstr;

			nowtm = localtime(&selectedtime);
			strftime(weekstr, sizeof(weekstr)-1, "%V", nowtm); weeknum = atoi(weekstr);
			for (i=1; (i <= 53); i++) {
				if (i == weeknum) selstr = " selected"; else selstr = "";
				fprintf(output, "<option value=\"%d\"%s>%d</option>\n", i, selstr, i);
			}
		}
		else if (strcmp(t_start, "REPDAYLIST") == 0) {
			int i;
			char *selstr;

			nowtm = localtime(&selectedtime);
			for (i=1; (i <= 31); i++) {
				if (i == nowtm->tm_mday) selstr = " selected"; else selstr = "";
				fprintf(output, "<option value=\"%d\"%s>%d</option>\n", i, selstr, i);
			}
		}
		else if (strcmp(t_start, "DAYLIST") == 0) {
			int i;

			nowtm = localtime(&selectedtime);
			for (i=1; (i <= 31); i++) {
				fprintf(output, "<option value=\"%d\">%d</option>\n", i, i);
			}
		}
		else if (strcmp(t_start, "REPYEARLIST") == 0) {
			int i;
			char *selstr;
			int beginyear, endyear;

			nowtm = localtime(&selectedtime);
			beginyear = nowtm->tm_year + 1900 - 5;
			endyear = nowtm->tm_year + 1900;

			for (i=beginyear; (i <= endyear); i++) {
				if (i == (nowtm->tm_year + 1900)) selstr = " selected"; else selstr = "";
				fprintf(output, "<option value=\"%d\"%s>%d</option>\n", i, selstr, i);
			}
		}
		else if (strcmp(t_start, "FUTUREYEARLIST") == 0) {
			int i;
			char *selstr;
			int beginyear, endyear;

			nowtm = localtime(&selectedtime);
			beginyear = nowtm->tm_year + 1900;
			endyear = nowtm->tm_year + 1900 + 5;

			for (i=beginyear; (i <= endyear); i++) {
				if (i == (nowtm->tm_year + 1900)) selstr = " selected"; else selstr = "";
				fprintf(output, "<option value=\"%d\"%s>%d</option>\n", i, selstr, i);
			}
		}
		else if (strcmp(t_start, "YEARLIST") == 0) {
			int i;
			int beginyear, endyear;

			nowtm = localtime(&selectedtime);
			beginyear = nowtm->tm_year + 1900;
			endyear = nowtm->tm_year + 1900 + 5;

			for (i=beginyear; (i <= endyear); i++) {
				fprintf(output, "<option value=\"%d\">%d</option>\n", i, i);
			}
		}
		else if (strcmp(t_start, "REPHOURLIST") == 0) { 
			int i; 
			struct tm *nowtm = localtime(&yesterday); 
			char *selstr;

			for (i=0; (i <= 24); i++) {
				if (i == nowtm->tm_hour) selstr = " selected"; else selstr = "";
				fprintf(output, "<option value=\"%d\"%s>%d</option>\n", i, selstr, i);
			}
		}
		else if (strcmp(t_start, "HOURLIST") == 0) { 
			int i; 

			for (i=0; (i <= 24); i++) {
				fprintf(output, "<option value=\"%d\">%d</option>\n", i, i);
			}
		}
		else if (strcmp(t_start, "REPMINLIST") == 0) {
			int i;
			struct tm *nowtm = localtime(&yesterday);
			char *selstr;

			for (i=0; (i <= 59); i++) {
				if (i == nowtm->tm_min) selstr = " selected"; else selstr = "";
				fprintf(output, "<option value=\"%02d\"%s>%02d</option>\n", i, selstr, i);
			}
		}
		else if (strcmp(t_start, "MINLIST") == 0) {
			int i;

			for (i=0; (i <= 59); i++) {
				fprintf(output, "<option value=\"%02d\">%02d</option>\n", i, i);
			}
		}
		else if (strcmp(t_start, "REPSECLIST") == 0) {
			int i;
			char *selstr;

			for (i=0; (i <= 59); i++) {
				if (i == 0) selstr = " selected"; else selstr = "";
				fprintf(output, "<option value=\"%02d\"%s>%02d</option>\n", i, selstr, i);
			}
		}
		else if (strcmp(t_start, "HOSTFILTER") == 0) {
			if (hostpattern_text) fprintf(output, "%s", hostpattern_text);
		}
		else if (strcmp(t_start, "PAGEFILTER") == 0) {
			if (pagepattern_text) fprintf(output, "%s", pagepattern_text);
		}
		else if (strcmp(t_start, "IPFILTER") == 0) {
			if (ippattern_text) fprintf(output, "%s", ippattern_text);
		}
		else if (strcmp(t_start, "CLASSFILTER") == 0) {
			if (classpattern_text) fprintf(output, "%s", classpattern_text);
		}
		else if (strcmp(t_start, "HOSTLIST") == 0) {
			xtreePos_t handle;
			treerec_t *rec;

			fetch_board();

			for (handle = xtreeFirst(hostnames); (handle != xtreeEnd(hostnames)); handle = xtreeNext(hostnames, handle)) {
				rec = (treerec_t *)xtreeData(hostnames, handle);

				if (wanted_host(rec->name)) {
					fprintf(output, "<option value=\"%s\">%s</option>\n", rec->name, rec->name);
				}
			}
		}
		else if (strcmp(t_start, "JSHOSTLIST") == 0) {
			xtreePos_t handle;

			fetch_board();
			clearflags(testnames);

			fprintf(output, "var hosts = new Array();\n");
			fprintf(output, "hosts[\"ALL\"] = [ \"ALL\"");
			for (handle = xtreeFirst(testnames); (handle != xtreeEnd(testnames)); handle = xtreeNext(testnames, handle)) {
				treerec_t *rec = xtreeData(testnames, handle);
				fprintf(output, ", \"%s\"", rec->name);
			}
			fprintf(output, " ];\n");

			for (handle = xtreeFirst(hostnames); (handle != xtreeEnd(hostnames)); handle = xtreeNext(hostnames, handle)) {
				treerec_t *hrec = xtreeData(hostnames, handle);
				if (wanted_host(hrec->name)) {
					xtreePos_t thandle;
					treerec_t *trec;
					char *bwalk, *tname, *p;
					SBUF_DEFINE(key);
					SBUF_MALLOC(key, strlen(hrec->name) + 3);

					/* Setup the search key and find the first occurrence. */
					snprintf(key, key_buflen, "\n%s|", hrec->name);
					if (strncmp(statusboard, (key+1), strlen(key+1)) == 0)
						bwalk = statusboard;
					else {
						bwalk = strstr(statusboard, key);
						if (bwalk) bwalk++;
					}

					while (bwalk) {
						tname = bwalk + strlen(key+1);
						p = strchr(tname, '|'); if (p) *p = '\0';
						if ( (strcmp(tname, xgetenv("INFOCOLUMN")) != 0) &&
						     (strcmp(tname, xgetenv("TRENDSCOLUMN")) != 0) ) {
							thandle = xtreeFind(testnames, tname);
							if (thandle != xtreeEnd(testnames)) {
								trec = (treerec_t *)xtreeData(testnames, thandle);
								trec->flag = 1;
							}
						}
						if (p) *p = '|';

						bwalk = strstr(tname, key); if (bwalk) bwalk++;
					}

					fprintf(output, "hosts[\"%s\"] = [ \"ALL\"", hrec->name);
					for (thandle = xtreeFirst(testnames); (thandle != xtreeEnd(testnames)); thandle = xtreeNext(testnames, thandle)) {
						trec = (treerec_t *)xtreeData(testnames, thandle);
						if (trec->flag == 0) continue;

						trec->flag = 0;
						fprintf(output, ", \"%s\"", trec->name);
					}
					fprintf(output, " ];\n");
				}
			}
		}
		else if (strcmp(t_start, "TESTLIST") == 0) {
			xtreePos_t handle;
			treerec_t *rec;

			fetch_board();

			for (handle = xtreeFirst(testnames); (handle != xtreeEnd(testnames)); handle = xtreeNext(testnames, handle)) {
				rec = (treerec_t *)xtreeData(testnames, handle);
				fprintf(output, "<option value=\"%s\">%s</option>\n", rec->name, rec->name);
			}
		}
		else if (strcmp(t_start, "DISABLELIST") == 0) {
			char *walk, *eoln;
			dishost_t *dhosts = NULL, *hwalk, *hprev;
			distest_t *twalk;

			fetch_board();
			clearflags(testnames);

			walk = statusboard;
			while (walk) {
				eoln = strchr(walk, '\n'); if (eoln) *eoln = '\0';
				if (*walk) {
					char *buf, *hname, *tname, *dismsg, *p;
					time_t distime, disstarted;
					int discolor;
					xtreePos_t thandle;
					treerec_t *rec;

					buf = strdup(walk);
					hname = tname = dismsg = NULL; distime = disstarted = 0; discolor = COL_BLUE;

					hname = gettok(buf, "|");
					if (hname) tname = gettok(NULL, "|");
					if (tname) { p = gettok(NULL, "|"); if (p) distime = atol(p); }
					if (distime) dismsg = gettok(NULL, "|");
					if (dismsg) { p = gettok(NULL, "|"); if (p) disstarted = atol(p); }
					if (disstarted || dismsg) {
						/* line1 is last field; not pipe-encoded, split on \n only */
						p = gettok(NULL, "\n");
						if (p && *p) {
							char *sp = strchr(p, ' ');
							if (sp) *sp = '\0';
							int c = parse_color(p);
							if (c != -1) discolor = c;
							if (sp) *sp = ' ';
						}
					}

					if (hname && tname && (distime != 0) && dismsg && wanted_host(hname)) {
						nldecode(dismsg);
						hwalk = dhosts; hprev = NULL;
						while (hwalk && (strcasecmp(hname, hwalk->name) > 0)) {
							hprev = hwalk;
							hwalk = hwalk->next;
						}
						if (!hwalk || (strcasecmp(hname, hwalk->name) != 0)) {
							dishost_t *newitem = (dishost_t *) malloc(sizeof(dishost_t));
							newitem->name = strdup(hname);
							newitem->tests = NULL;
							newitem->next = hwalk;
							if (!hprev)
								dhosts = newitem;
							else 
								hprev->next = newitem;
							hwalk = newitem;
						}
						twalk = (distest_t *) malloc(sizeof(distest_t));
						twalk->name = strdup(tname);
						twalk->cause = strdup(dismsg);
						twalk->until = distime;
						twalk->disstarted = disstarted;
						twalk->color = discolor;
						twalk->next = hwalk->tests;
						hwalk->tests = twalk;

						thandle = xtreeFind(testnames, tname);
						if (thandle != xtreeEnd(testnames)) {
							rec = xtreeData(testnames, thandle);
							rec->flag = 1;
						}
					}

					xfree(buf);
				}

				if (eoln) {
					*eoln = '\n';
					walk = eoln+1;
				}
				else {
					walk = NULL;
				}
			}

			if (dhosts) {
				fprintf(output,
					"<div class=\"table-responsive\">"
					"<table class=\"table table-sm table-hover xymon-disable-table\">\n"
					"<thead class=\"table-dark\"><tr>"
					"<th></th><th>Host</th><th>Test</th><th>Disabled By</th>"
					"<th>Since</th><th>Until</th><th>Reason</th><th></th>"
					"</tr></thead>\n<tbody>\n");
				for (hwalk = dhosts; hwalk; hwalk = hwalk->next) {
					for (twalk = hwalk->tests; twalk; twalk = twalk->next) {
						char *reason, *reasonend, untilbuf[64], sinbuf[64], bybuf[128], *nl;

						/* Extract "Disabled by: ..." from cause */
						strncpy(bybuf, "—", sizeof(bybuf));
						char *byline = strstr(twalk->cause, "Disabled by: ");
						if (byline) {
							byline += strlen("Disabled by: ");
							char *byend = strchr(byline, '\n');
							if (!byend) byend = byline + strlen(byline);
							while (byend > byline && (*(byend-1) == ' ' || *(byend-1) == '\r')) byend--;
							int bylen = (int)(byend - byline);
							if (bylen >= (int)sizeof(bybuf)) bylen = sizeof(bybuf) - 1;
							strncpy(bybuf, byline, bylen);
							bybuf[bylen] = '\0';
						}

						/* Format "since" (when disabled) from lastchange */
						strncpy(sinbuf, "—", sizeof(sinbuf));
						if (twalk->disstarted > 0) {
							char *t = ctime(&twalk->disstarted);
							snprintf(sinbuf, sizeof(sinbuf), "%s", t ? t : "");
							nl = strchr(sinbuf, '\n'); if (nl) *nl = '\0';
						}

						/* Format "until" (re-enable time) */
						if (twalk->until == -1) {
							snprintf(untilbuf, sizeof(untilbuf), "until OK");
						} else {
							char *t = ctime(&twalk->until);
							snprintf(untilbuf, sizeof(untilbuf), "%s", t ? t : "");
							nl = strchr(untilbuf, '\n'); if (nl) *nl = '\0';
						}

						/* Extract reason text */
						reason = strstr(twalk->cause, "Reason: ");
						if (reason) {
							reason += strlen("Reason: ");
						} else {
							reason = twalk->cause;
							reason += strspn(reason, "0123456789 ");
						}
						reasonend = reason + strcspn(reason, "\n");

						fprintf(output, "<tr>\n<td class=\"text-center\">%s</td>\n",
							coloricon(twalk->color, 0, 1));
						fprintf(output,
							"<td class=\"fw-bold\">%s</td>\n"
							"<td><a href=\"%s/svcstatus.sh?HOST=%s&amp;SERVICE=%s\">%s</a></td>\n"
							"<td class=\"small\">%s</td>\n"
							"<td class=\"text-muted small text-nowrap\">%s</td>\n"
							"<td class=\"text-muted small text-nowrap\">%s</td>\n"
							"<td class=\"small\">%.*s</td>\n"
							"<td class=\"text-end\">"
							"<form method=\"post\" action=\"%s/enadis.sh\" class=\"d-inline\">"
							"<input name=\"hostname\" type=\"hidden\" value=\"%s\">"
							"<input name=\"enabletest\" type=\"hidden\" value=\"%s\">"
							"<button name=\"go\" type=\"submit\" value=\"enable\" "
							"class=\"btn btn-sm btn-outline-success\">Enable</button>"
							"</form></td>\n"
							"</tr>\n",
							hwalk->name,
							xgetenv("CGIBINURL"), hwalk->name, twalk->name, twalk->name,
							bybuf, sinbuf, untilbuf,
							(int)(reasonend - reason), reason,
							xgetenv("SECURECGIBINURL"),
							hwalk->name, twalk->name);
					}
				}
				fprintf(output, "</tbody></table></div>\n");
			}
			else {
				fprintf(output, "<p class=\"text-muted\">No tests currently disabled.</p>\n");
			}
		}
		else if (strcmp(t_start, "SCHEDULELIST") == 0) {
			char *walk, *eoln;
			int gotany = 0;
			strbuffer_t *rows = newstrbuffer(0);

			fetch_board();

			walk = scheduleboard;
			while (walk) {
				eoln = strchr(walk, '\n'); if (eoln) *eoln = '\0';
				if (*walk) {
					int id = 0;
					time_t executiontime = 0;
					char *sender = NULL, *cmd = NULL, *buf, *p, *cmdeoln;
					char rowbuf[2048];

					buf = strdup(walk);
					p = gettok(buf, "|");
					if (p) { id = atoi(p); p = gettok(NULL, "|"); }
					if (p) { executiontime = atoi(p); p = gettok(NULL, "|"); }
					if (p) { sender = p; p = gettok(NULL, "|"); }
					if (p) { cmd = p; }

					if (id && executiontime && sender && cmd) {
						char timebuf[64], *nl;
						char *t = ctime(&executiontime);
						gotany = 1;
						nldecode(cmd);
						snprintf(timebuf, sizeof(timebuf), "%s", t ? t : "");
						nl = strchr(timebuf, '\n'); if (nl) *nl = '\0';

						addtobuffer(rows, "<tr>\n<td class=\"text-muted small\">");
						addtobuffer(rows, timebuf);
						addtobuffer(rows, "</td>\n<td class=\"small font-monospace\">");
						p = cmd;
						while ((cmdeoln = strchr(p, '\n')) != NULL) {
							*cmdeoln = '\0';
							addtobuffer(rows, htmlquoted(p));
							addtobuffer(rows, "<br>");
							p = (cmdeoln + 1);
						}
						if (*p) addtobuffer(rows, htmlquoted(p));
						snprintf(rowbuf, sizeof(rowbuf),
							"</td>\n"
							"<td class=\"text-end\">"
							"<form method=\"post\" action=\"%s/enadis.sh\" class=\"d-inline\">"
							"<input name=\"canceljob\" type=\"hidden\" value=\"%d\">"
							"<button name=\"go\" type=\"submit\" value=\"cancel\" "
							"class=\"btn btn-sm btn-outline-danger\">Cancel</button>"
							"</form></td>\n</tr>\n",
							xgetenv("SECURECGIBINURL"), id);
						addtobuffer(rows, rowbuf);
					}
					xfree(buf);
				}

				if (eoln) {
					*eoln = '\n';
					walk = eoln+1;
				}
				else {
					walk = NULL;
				}
			}

			if (gotany) {
				fprintf(output,
					"<h6 class=\"text-muted xymon-section-label\">Scheduled actions</h6>\n"
					"<div class=\"table-responsive\">"
					"<table class=\"table table-sm table-hover xymon-schedule-table\">\n"
					"<thead class=\"table-dark\"><tr>"
					"<th>Scheduled at</th><th>Action</th><th></th>"
					"</tr></thead>\n<tbody>\n");
				fprintf(output, "%s", grabstrbuffer(rows));
				fprintf(output, "</tbody></table></div>\n");
			}
			freestrbuffer(rows);
		}

		else if (strncmp(t_start, "GENERICLIST", strlen("GENERICLIST")) == 0) {
			listpool_t *pool = find_listpool(t_start + strlen("GENERICLIST"));
			listrec_t *walk;

			for (walk = pool->listhead; (walk); walk = walk->next)
				fprintf(output, "<option value=\"%s\"%s %s>%s</option>\n", 
					walk->val, (walk->selected ? " selected" : ""), (walk->extra ? walk->extra : ""),
					walk->name);
		}

		else if (strcmp(t_start, "CRITACKTTPRIO") == 0) fprintf(output, "%d", critackttprio);
		else if (strcmp(t_start, "CRITACKTTGROUP") == 0) fprintf(output, "%s", critackttgroup);
		else if (strcmp(t_start, "CRITACKTTEXTRA") == 0) fprintf(output, "%s", critackttextra);
		else if (strcmp(t_start, "CRITACKINFOURL") == 0) fprintf(output, "%s", ackinfourl);
		else if (strcmp(t_start, "CRITACKDOCURL") == 0) fprintf(output, "%s", critackdocurl);

		else if (strcmp(t_start, "CRITEDITUPDINFO") == 0) {
			fprintf(output, "%s", criteditupdinfo);
		}

		else if (strcmp(t_start, "CRITEDITPRIOLIST") == 0) {
			int i;
			char *selstr;

			for (i=1; (i <= 3); i++) {
				selstr = ((i == criteditprio) ? " selected" : "");
				fprintf(output, "<option value=\"%d\"%s>%d</option>\n", i, selstr, i);
			}
		}

		else if (strcmp(t_start, "CRITEDITCLONELIST") == 0) {
			int i;
			for (i=0; (criteditclonelist[i]); i++) 
				fprintf(output, "<option value=\"%s\">%s</option>\n", 
					criteditclonelist[i], criteditclonelist[i]);
		}

		else if (strcmp(t_start, "CRITEDITGROUP") == 0) {
			fprintf(output, "%s", criteditgroup);
		}

		else if (strcmp(t_start, "CRITEDITEXTRA") == 0) {
			fprintf(output, "%s", criteditextra);
		}

		else if (strcmp(t_start, "CRITEDITWKDAYS") == 0) {
			fprintf(output, "%s", wkdayselect('*', "All days", 1));
			fprintf(output, "%s", wkdayselect('W', "Mon-Fri", 0));
			fprintf(output, "%s", wkdayselect('1', "Monday", 0));
			fprintf(output, "%s", wkdayselect('2', "Tuesday", 0));
			fprintf(output, "%s", wkdayselect('3', "Wednesday", 0));
			fprintf(output, "%s", wkdayselect('4', "Thursday", 0));
			fprintf(output, "%s", wkdayselect('5', "Friday", 0));
			fprintf(output, "%s", wkdayselect('6', "Saturday", 0));
			fprintf(output, "%s", wkdayselect('0', "Sunday", 0));
		}

		else if (strcmp(t_start, "CRITEDITSTART") == 0) {
			int i, curr;
			char *selstr;

			curr = (criteditslastart ? (atoi(criteditslastart) / 100) : 0);
			for (i=0; (i <= 23); i++) {
				selstr = ((i == curr) ? " selected" : "");
				fprintf(output, "<option value=\"%02i00\"%s>%02i:00</option>\n", i, selstr, i);
			}
		}

		else if (strcmp(t_start, "CRITEDITEND") == 0) {
			int i, curr;
			char *selstr;

			curr = (criteditslaend ? (atoi(criteditslaend) / 100) : 24);
			for (i=1; (i <= 24); i++) {
				selstr = ((i == curr) ? " selected" : "");
				fprintf(output, "<option value=\"%02i00\"%s>%02i:00</option>\n", i, selstr, i);
			}
		}

		else if (strncmp(t_start, "CRITEDITDAYLIST", 13) == 0) {
			time_t t = ((*(t_start+13) == '1') ? criteditstarttime : criteditendtime);
			char *defstr = ((*(t_start+13) == '1') ? "Now" : "Never");
			int i;
			char *selstr;
			struct tm *tm;

			tm = localtime(&t);

			selstr = ((t == 0) ? " selected" : "");
			fprintf(output, "<option value=\"0\"%s>%s</option>\n", selstr, defstr);

			for (i=1; (i <= 31); i++) {
				selstr = ( (t && (tm->tm_mday == i)) ? " selected" : "");
				fprintf(output, "<option value=\"%d\"%s>%d</option>\n", i, selstr, i);
			}
		}

		else if (strncmp(t_start, "CRITEDITMONLIST", 13) == 0) {
			time_t t = ((*(t_start+13) == '1') ? criteditstarttime : criteditendtime);
			char *defstr = ((*(t_start+13) == '1') ? "Now" : "Never");
			int i;
			char *selstr;
			struct tm tm;
			time_t now;
			struct tm nowtm;
			struct tm monthtm;
			char mname[20];

			memcpy(&tm, localtime(&t), sizeof(tm));

			now = getcurrenttime(NULL);
			memcpy(&nowtm, localtime(&now), sizeof(tm));

			selstr = ((t == 0) ? " selected" : "");
			fprintf(output, "<option value=\"0\"%s>%s</option>\n", selstr, defstr);

			for (i=1; (i <= 12); i++) {
				selstr = ( (t && (tm.tm_mon == (i -1))) ? " selected" : "");
				monthtm.tm_mon = (i-1); monthtm.tm_mday = 1; monthtm.tm_year = nowtm.tm_year;
				monthtm.tm_hour = monthtm.tm_min = monthtm.tm_sec = monthtm.tm_isdst = 0;
				strftime(mname, sizeof(mname)-1, "%B", &monthtm);
				fprintf(output, "<option value=\"%d\"%s>%s</option>\n", i, selstr, mname);
			}
		}

		else if (strncmp(t_start, "CRITEDITYEARLIST", 14) == 0) {
			time_t t = ((*(t_start+14) == '1') ? criteditstarttime : criteditendtime);
			char *defstr = ((*(t_start+14) == '1') ? "Now" : "Never");
			int i;
			char *selstr;
			struct tm tm;
			time_t now;
			struct tm nowtm;
			int beginyear, endyear;

			memcpy(&tm, localtime(&t), sizeof(tm));

			now = getcurrenttime(NULL);
			memcpy(&nowtm, localtime(&now), sizeof(tm));

			beginyear = nowtm.tm_year + 1900;
			endyear = nowtm.tm_year + 1900 + 5;

			selstr = ((t == 0) ? " selected" : "");
			fprintf(output, "<option value=\"0\"%s>%s</option>\n", selstr, defstr);

			for (i=beginyear; (i <= endyear); i++) {
				selstr = ( (t && (tm.tm_year == (i - 1900))) ? " selected" : "");
				fprintf(output, "<option value=\"%d\"%s>%d</option>\n", i, selstr, i);
			}
		}

		else if (strcmp(t_start, "CRITEDITSTART_DATE") == 0) {
			if (criteditstarttime > 0) {
				char buf[11];
				struct tm *tm = localtime(&criteditstarttime);
				strftime(buf, sizeof(buf), "%Y-%m-%d", tm);
				fprintf(output, "%s", buf);
			}
		}

		else if (strcmp(t_start, "CRITEDITEND_DATE") == 0) {
			if (criteditendtime > 0) {
				char buf[11];
				struct tm *tm = localtime(&criteditendtime);
				strftime(buf, sizeof(buf), "%Y-%m-%d", tm);
				fprintf(output, "%s", buf);
			}
		}

		else if (hostenv_hikey && ( (strncmp(t_start, "XMH_", 4) == 0) || (strncmp(t_start, "BBH_", 4) == 0) )) {
			void *hinfo = hostinfo(hostenv_hikey);
			if (hinfo) {
				char *s;

				if (strncmp(t_start, "BBH_", 4) == 0) memmove(t_start, "XMH_", 4); /* For compatibility */
				s = xmh_item_byname(hinfo, t_start);

				if (!s) {
					fprintf(output, "&%s", t_start);
				}
				else {
					fprintf(output, "%s", s);
				}
			}
		}

		else if (strncmp(t_start, "BACKDAYS", 8) == 0) {
			fprintf(output, "%d", backdays);
		}

		else if (strncmp(t_start, "BACKHOURS", 9) == 0) {
			fprintf(output, "%d", backhours);
		}

		else if (strncmp(t_start, "BACKMINS", 8) == 0) {
			fprintf(output, "%d", backmins);
		}

		else if (strncmp(t_start, "BACKSECS", 8) == 0) {
			fprintf(output, "%d", backsecs);
		}

		else if (strncmp(t_start, "EVENTLASTMONTHBEGIN", 19) == 0) {
			time_t t = getcurrenttime(NULL);
			struct tm *tm = localtime(&t);

			tm->tm_mon -= 1;
			tm->tm_mday = 1;
			tm->tm_hour = tm->tm_min = tm->tm_sec = 0;
			tm->tm_isdst = -1;
			t = mktime(tm);
			fprintf(output, "%s", eventreport_timestring(t));
		}
		else if (strncmp(t_start, "EVENTCURRMONTHBEGIN", 19) == 0) {
			time_t t = getcurrenttime(NULL);
			struct tm *tm = localtime(&t);
			tm->tm_mday = 1;
			tm->tm_hour = tm->tm_min = tm->tm_sec = 0;
			tm->tm_isdst = -1;
			t = mktime(tm);
			fprintf(output, "%s", eventreport_timestring(t));
		}

		else if (strncmp(t_start, "EVENTLASTWEEKBEGIN", 18) == 0) {
			time_t t = getcurrenttime(NULL);
			struct tm *tm = localtime(&t);
			int weekstart = atoi(xgetenv("WEEKSTART"));

			if (tm->tm_wday == weekstart) { /* Do nothing */ }
			else if (tm->tm_wday > weekstart) tm->tm_mday -= (tm->tm_wday - weekstart);
			else tm->tm_mday += (weekstart - tm->tm_wday) - 7;

			tm->tm_mday -= 7;
			tm->tm_hour = tm->tm_min = tm->tm_sec = 0;
			tm->tm_isdst = -1;
			t = mktime(tm);
			fprintf(output, "%s", eventreport_timestring(t));
		}
		else if (strncmp(t_start, "EVENTCURRWEEKBEGIN", 18) == 0) {
			time_t t = getcurrenttime(NULL);
			struct tm *tm = localtime(&t);
			int weekstart = atoi(xgetenv("WEEKSTART"));

			if (tm->tm_wday == weekstart) { /* Do nothing */ }
			else if (tm->tm_wday > weekstart) tm->tm_mday -= (tm->tm_wday - weekstart);
			else tm->tm_mday += (weekstart - tm->tm_wday) - 7;

			tm->tm_hour = tm->tm_min = tm->tm_sec = 0;
			tm->tm_isdst = -1;
			t = mktime(tm);
			fprintf(output, "%s", eventreport_timestring(t));
		}

		else if (strncmp(t_start, "EVENTLASTYEARBEGIN", 18) == 0) {
			time_t t = getcurrenttime(NULL);
			struct tm *tm = localtime(&t);

			tm->tm_year -= 1;
			tm->tm_mon = 0;
			tm->tm_mday = 1;
			tm->tm_hour = tm->tm_min = tm->tm_sec = 0;
			tm->tm_isdst = -1;
			t = mktime(tm);
			fprintf(output, "%s", eventreport_timestring(t));
		}
		else if (strncmp(t_start, "EVENTCURRYEARBEGIN", 18) == 0) {
			time_t t = getcurrenttime(NULL);
			struct tm *tm = localtime(&t);

			tm->tm_mon = 0;
			tm->tm_mday = 1;
			tm->tm_hour = tm->tm_min = tm->tm_sec = 0;
			tm->tm_isdst = -1;
			t = mktime(tm);
			fprintf(output, "%s", eventreport_timestring(t));
		}
		else if (strncmp(t_start, "EVENTYESTERDAY", 14) == 0) {
			time_t t = getcurrenttime(NULL);
			struct tm *tm = localtime(&t);

			tm->tm_mday -= 1;
			tm->tm_hour = tm->tm_min = tm->tm_sec = 0;
			tm->tm_isdst = -1;
			t = mktime(tm);
			fprintf(output, "%s", eventreport_timestring(t));
		}
		else if (strncmp(t_start, "EVENTTODAY", 10) == 0) {
			time_t t = getcurrenttime(NULL);
			struct tm *tm = localtime(&t);

			tm->tm_hour = tm->tm_min = tm->tm_sec = 0;
			tm->tm_isdst = -1;
			t = mktime(tm);
			fprintf(output, "%s", eventreport_timestring(t));
		}
		else if (strncmp(t_start, "EVENTNOW", 8) == 0) {
			time_t t = getcurrenttime(NULL);
			fprintf(output, "%s", eventreport_timestring(t));
		}

		else if (strncmp(t_start, "PAGEPATH_DROPDOWN", 17) == 0) {
			build_pagepath_dropdown(output);
		}
		else if (strncmp(t_start, "EVENTSTARTTIME", 8) == 0) {
			fprintf(output, "%s", hostenv_eventtimestart);
		}
		else if (strncmp(t_start, "EVENTENDTIME", 8) == 0) {
			fprintf(output, "%s", hostenv_eventtimeend);
		}

		else if (strncmp(t_start, "XYMONBODY", 9) == 0) {
			char *bodytext = xymonbody(t_start);
			fprintf(output, "%s", bodytext);
		}

		else if (*t_start && (savechar == ';')) {
			/* A "&xxx;" is probably an HTML escape - output unchanged. */
			fprintf(output, "&%s", t_start);
		}

		else if (*t_start && (strncmp(t_start, "SELECT_", 7) == 0)) {
			/*
			 * Special for getting the SELECTED tag into list boxes.
			 * Cannot use xgetenv because it complains for undefined
			 * environment variables.
			 */
			char *val = getenv(t_start);

			fprintf(output, "%s", (val ? val : ""));
		}

		else if (strcmp(t_start, "PAGEHEAD") == 0 ||
			 strcmp(t_start, "XYMONPAGEHEADER") == 0 ||
			 strcmp(t_start, "PAGEFOOTER") == 0) {
			/* Load webfiles/${XYMONTEMPLATE}/<head|body_header|footer>_inc. */
			char *tmpl = getenv("XYMONTEMPLATE");
			const char *td = hostenv_templatedir ? hostenv_templatedir : xgetenv("XYMONHOME");
			const char *subname;
			char partpath[PATH_MAX];
			int pfd;
			struct stat pst;
			int found = 0;

			if      (strcmp(t_start, "PAGEHEAD")        == 0) subname = "head";
			else if (strcmp(t_start, "XYMONPAGEHEADER") == 0) subname = "body_header";
			else                                               subname = "footer";

			if (tmpl && td) {
				snprintf(partpath, sizeof(partpath), "%s/web/%s/%s_inc",
					 td, tmpl, subname);
				pfd = open(partpath, O_RDONLY);
				if (pfd >= 0) {
					found = 1;
					if (fstat(pfd, &pst) == 0 && pst.st_size > 0) {
						char *pb = (char *)malloc((size_t)pst.st_size + 1);
						if (pb) {
							ssize_t n = read(pfd, pb, (size_t)pst.st_size);
							if (n > 0) {
								pb[n] = '\0';
								strip_assignments(pb);
								output_parsed(output, pb, bgcolor, selectedtime);
							}
							free(pb);
						}
					}
					close(pfd);
				}
			}
		}

		else if (strlen(t_start) && xgetenv(t_start)) {
			fprintf(output, "%s", xgetenv(t_start));
		}

		else fprintf(output, "&%s", t_start);		/* No substitution - copy all unchanged. */
			
		*t_next = savechar; t_start = t_next; t_next = strchr(t_start, '&');
	}

	/* Remainder of file */
	fprintf(output, "%s", t_start);
}


/* Pre-load KEY=VALUE assignments from all *_inc files so any inc-file variable
 * overrides (e.g. XYMONBODYCLASS) are applied before the main template renders. */
static void preload_inc_assignments(const char *tmpl, const char *td)
{
	const char *parts[] = {"head", "body", "footer"};
	int i;
	for (i = 0; i < 3; i++) {
		char p[PATH_MAX]; int fd; struct stat st; char *buf; ssize_t n;
		snprintf(p, sizeof(p), "%s/web/%s/%s_inc", td, tmpl, parts[i]);
		fd = open(p, O_RDONLY);
		if (fd < 0) continue;
		if (fstat(fd, &st) == 0 && st.st_size > 0) {
			buf = (char *)malloc((size_t)st.st_size + 1);
			if (buf) {
				n = read(fd, buf, (size_t)st.st_size);
				if (n > 0) { buf[n] = '\0'; strip_assignments(buf); }
				free(buf);
			}
		}
		close(fd);
	}
}

void headfoot(FILE *output, char *template, char *pagepath, char *head_or_foot, int bgcolor)
{
	int	fd;
	char 	filename[PATH_MAX];
	SBUF_DEFINE(bulletinfile);
	struct  stat st;
	char	*templatedata;
	char	*hfpath;
	int	have_pagepath = (hostenv_pagepath != NULL);

	MEMDEFINE(filename);

	if (xgetenv("XYMONDREL") == NULL) {
		SBUF_DEFINE(xymondrel);
		SBUF_MALLOC(xymondrel, 12+strlen(VERSION));
		snprintf(xymondrel, xymondrel_buflen, "XYMONDREL=%s", VERSION);
		putenv(xymondrel);
	}

	/* Tell PAGE* token handlers which per-page subdir to use.
	 * Default body class to template name; head_inc may override. */
	setenv("XYMONTEMPLATE", template, 1);
	setenv("XYMONBODYCLASS", template, 1);

	/* Set XYMONPAGETITLE from static table or host/svc context */
	{
		static const struct { const char *t; const char *p; } titles[] = {
			{"acknowledge",     "Acknowledge Alert"},
			{"acknowledgements","Acknowledgement Log"},
			{"ackinfo",         "Acknowledge Alert"},
			{"chpasswd",        "Change Password"},
			{"columndoc",       "Column Info"},
			{"confreport",      "Configuration Report"},
			{"critedit",        "Critical Systems Editor"},
			{"critical",        "Critical Systems"},
			{"critmulti",       "Critical Systems"},
			{"divider",         "Xymon"},
			{"event",           "Eventlog"},
			{"findhost",        "Find Host"},
			{"ghosts",          "Ghost Clients"},
			{"graphs",          "Graphs"},
			{"hostgraphs",      "Metrics Report"},
			{"hostlist",        "List of Hosts"},
			{"maint",           "Maintenance"},
			{"maintact",        "Maintenance"},
			{"notify",          "Notification Log"},
			{"perfdata",        "Performance Data"},
			{"repnormal",       "Availability Report"},
			{"report",          "Availability Report"},
			{"snapshot",        "Snapshot Report"},
			{"snapcritical",    "Snapshot"},
			{"snapnongreen",    "Snapshot"},
			{"snapnormal",      "Snapshot Report"},
			{"stdcritical",     "Critical Systems"},
			{"stdnongreen",     "Non-green Systems"},
			{"stdnormal",       "Status"},
			{"topchanges",      "Top Changes"},
			{"useradm",         "Manage Users"},
			{"xymonnormal",     "Current Status"},
			{NULL, NULL}
		};
		int i; char buf[512]; const char *title = NULL;
		for (i = 0; titles[i].t; i++)
			if (strcmp(template, titles[i].t) == 0) { title = titles[i].p; break; }
		if (title) {
			/* xymonnormal: pagegen.c sets XYMONPAGETITLE from page->title before
			 * calling headfoot() — use overwrite=0 so that takes precedence. */
			setenv("XYMONPAGETITLE", title, strcmp(template, "xymonnormal") != 0);
		} else if (hostenv_host && hostenv_svc) {
			snprintf(buf, sizeof(buf), "%s \xe2\x80\x94 %s", hostenv_host, hostenv_svc);
			setenv("XYMONPAGETITLE", buf, 1);
		} else if (hostenv_host) {
			setenv("XYMONPAGETITLE", hostenv_host, 1);
		} else {
			setenv("XYMONPAGETITLE", template, 1);
		}
	}

	/* Pre-pass: load KEY=VALUE assignments from all *_inc files so any
	 * inc-file overrides take effect before the main template is rendered. */
	{
		const char *td_pre = hostenv_templatedir ? hostenv_templatedir : xgetenv("XYMONHOME");
		if (td_pre) preload_inc_assignments(template, td_pre);
	}

	/*
	 * "pagepath" is the relative path for this page, e.g. 
	 * - for the top-level page it is ""
	 * - for a page, it is "pagename/"
	 * - for a subpage, it is "pagename/subpagename/"
	 *
	 * We allow header/footer files named template_PAGE_header or template_PAGE_SUBPAGE_header
	 * so we need to scan for an existing file - starting with the
	 * most detailed one, and working up towards the standard "web/template_TYPE" file.
	 */

	hfpath = strdup(pagepath); 
	/* Trim off excess trailing slashes */
	if (*hfpath) {
		while (*(hfpath + strlen(hfpath) - 1) == '/') *(hfpath + strlen(hfpath) - 1) = '\0';
	}
	fd = -1;

	if (!have_pagepath) hostenv_pagepath = strdup(hfpath);

	while ((fd == -1) && strlen(hfpath)) {
		char *p;
		char *elemstart;

		if (hostenv_templatedir) {
			snprintf(filename, sizeof(filename), "%s/", hostenv_templatedir);
		}
		else {
			snprintf(filename, sizeof(filename), "%s/web/", xgetenv("XYMONHOME"));
		}

		p = strchr(hfpath, '/'); elemstart = hfpath;
		while (p) {
			*p = '\0';
			strncat(filename, elemstart, (sizeof(filename) - strlen(filename)));
			strncat(filename, "_", (sizeof(filename) - strlen(filename)));
			*p = '/';
			p++;
			elemstart = p; p = strchr(elemstart, '/');
		}
		strncat(filename, elemstart, (sizeof(filename) - strlen(filename)));
		strncat(filename, "_", (sizeof(filename) - strlen(filename)));
		strncat(filename, head_or_foot, (sizeof(filename) - strlen(filename)));

		dbgprintf("Trying header/footer file '%s'\n", filename);
		fd = open(filename, O_RDONLY);

		if (fd == -1) {
			p = strrchr(hfpath, '/');
			if (p == NULL) p = hfpath;
			*p = '\0';
		}
	}
	xfree(hfpath);

	/* Subdir layout: web/${template}/${head_or_foot} */
	if (fd == -1) {
		if (hostenv_templatedir) {
			snprintf(filename, sizeof(filename), "%s/%s/%s",
				 hostenv_templatedir, template, head_or_foot);
		} else {
			snprintf(filename, sizeof(filename), "%s/web/%s/%s",
				 xgetenv("XYMONHOME"), template, head_or_foot);
		}
		dbgprintf("Trying subdir template '%s'\n", filename);
		fd = open(filename, O_RDONLY);
	}

	/* Legacy flat layout: web/${template}_${head_or_foot} */
	if (fd == -1) {
		if (hostenv_templatedir) {
			snprintf(filename, sizeof(filename), "%s/%s_%s",
				 hostenv_templatedir, template, head_or_foot);
		} else {
			snprintf(filename, sizeof(filename), "%s/web/%s_%s",
				 xgetenv("XYMONHOME"), template, head_or_foot);
		}
		dbgprintf("Trying flat template '%s'\n", filename);
		fd = open(filename, O_RDONLY);
	}

	/* Master page fallback: web/shared/${head_or_foot} — header/footer only */
	if (fd == -1 && (strcmp(head_or_foot, "header") == 0 || strcmp(head_or_foot, "footer") == 0)) {
		if (hostenv_templatedir) {
			snprintf(filename, sizeof(filename), "%s/shared/%s",
				 hostenv_templatedir, head_or_foot);
		} else {
			snprintf(filename, sizeof(filename), "%s/web/shared/%s",
				 xgetenv("XYMONHOME"), head_or_foot);
		}
		dbgprintf("Trying shared master template '%s'\n", filename);
		fd = open(filename, O_RDONLY);
	}

	if (fd != -1) {
		int n;

		fstat(fd, &st);
		templatedata = (char *) malloc(st.st_size + 1); *templatedata = '\0';
		n = read(fd, templatedata, st.st_size);
		if (n > 0) templatedata[n] = '\0';
		close(fd);

		strip_assignments(templatedata);
		/* Make XYMWEBDATE available as $VAR in xymonmenu.cfg navbar.
		 * setenv(..., 0): don't override if htmllog.c pre-set it to logtime. */
		{
			char _xwdate[64];
			time_t _now = getcurrenttime(NULL);
			strftime(_xwdate, sizeof(_xwdate)-1, "%a %b %d %H:%M:%S", localtime(&_now));
			setenv("XYMWEBDATE", _xwdate, 0);
		}
		output_parsed(output, templatedata, bgcolor, getcurrenttime(NULL));

		xfree(templatedata);
	}
	else {
		fprintf(output, "<HTML><BODY> \n <HR size=4> \n <BR>%s is either missing or invalid, please create this file with your custom header<BR> \n<HR size=4>", htmlquoted(filename));
	}

	/* Check for bulletin files */
	SBUF_MALLOC(bulletinfile, strlen(xgetenv("XYMONHOME")) + strlen("/web/bulletin_") + strlen(head_or_foot)+1);
	snprintf(bulletinfile, bulletinfile_buflen, "%s/web/bulletin_%s", xgetenv("XYMONHOME"), head_or_foot);
	fd = open(bulletinfile, O_RDONLY);
	if (fd != -1) {
		int n;

		fstat(fd, &st);
		templatedata = (char *) malloc(st.st_size + 1); *templatedata = '\0';
		n = read(fd, templatedata, st.st_size);
		templatedata[n] = '\0';
		close(fd);
		strip_assignments(templatedata);
		output_parsed(output, templatedata, bgcolor, getcurrenttime(NULL));
		xfree(templatedata);
	}

	if (!have_pagepath) {
		xfree(hostenv_pagepath); hostenv_pagepath = NULL;
	}

	xfree(bulletinfile);

	MEMUNDEFINE(filename);
}

void showform(FILE *output, char *headertemplate, char *formtemplate, int color, time_t seltime, 
	      char *pretext, char *posttext)
{
	/* Present the query form */
	int formfile = -1;
	char formfn[PATH_MAX];

	/* Subdir layout: web/${headertemplate}/form or web/${headertemplate}/form_* */
	if (headertemplate) {
		/* Strip "headertemplate_" prefix from formtemplate to get the local name.
		 * e.g. headertemplate="report", formtemplate="report_form_daily" → "form_daily" */
		const char *suffix = formtemplate;
		size_t pfxlen = strlen(headertemplate);
		if (strncmp(formtemplate, headertemplate, pfxlen) == 0 && formtemplate[pfxlen] == '_')
			suffix = formtemplate + pfxlen + 1;
		snprintf(formfn, sizeof(formfn), "%s/web/%s/%s",
			 xgetenv("XYMONHOME"), headertemplate, suffix);
		formfile = open(formfn, O_RDONLY);
	}

	/* Legacy flat path: web/${formtemplate} */
	if (formfile < 0) {
		snprintf(formfn, sizeof(formfn), "%s/web/%s", xgetenv("XYMONHOME"), formtemplate);
		formfile = open(formfn, O_RDONLY);
	}

	if (formfile >= 0) {
		char *inbuf;
		struct stat st;
		int n;

		fstat(formfile, &st);
		inbuf = (char *) malloc(st.st_size + 1); *inbuf = '\0';
		n = read(formfile, inbuf, st.st_size);
		inbuf[n] = '\0';
		close(formfile);

		if (headertemplate) headfoot(output, headertemplate, (hostenv_pagepath ? hostenv_pagepath : ""), "header", color);
		if (pretext) fprintf(output, "<center><strong><big>%s</big></strong></center>\n", pretext);
		output_parsed(output, inbuf, color, seltime);
		if (posttext) fprintf(output, "%s", posttext);
		if (headertemplate) headfoot(output, headertemplate, (hostenv_pagepath ? hostenv_pagepath : ""), "footer", color);

		xfree(inbuf);
	}
}

