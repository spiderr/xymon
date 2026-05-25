/*----------------------------------------------------------------------------*/
/* Xymon monitor library.                                                     */
/*                                                                            */
/* This tool generates the report status log for a single status, with the    */
/* availability percentages etc needed for a report-mode view.                */
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

#include "libxymon.h"

char *stylenames[3] = { "crit", "nongr", "all" };

void generate_replog(FILE *htmlrep, FILE *textrep, char *textrepurl,
		     char *hostname, char *service, int color, int style,
		     char *ip, char *displayname,
		     time_t st, time_t end, double reportwarnlevel, double reportgreenlevel, int reportwarnstops, 
		     reportinfo_t *repinfo)
{
	replog_t *walk;
	if (!displayname) displayname = hostname;
	sethostenv(displayname, ip, service, colorname(color), hostname);
	sethostenv_report(st, end, reportwarnlevel, reportgreenlevel);

	headfoot(htmlrep, "replog", "", "header", color);

	fprintf(htmlrep, "<h3 class=\"replog-title\">%s - ", htmlquoted(displayname));
	fprintf(htmlrep, "%s</h3>\n", htmlquoted(service));
	fprintf(htmlrep, "<div class=\"table-responsive\"><table class=\"table table-sm table-dark replog-availability\">\n");
	fprintf(htmlrep, "<tr>\n");

	if (repinfo->withreport) {
		fprintf(htmlrep, "<td colspan=\"3\" class=\"text-center fw-bold\">Availability (24x7): %.2f%%</td>\n", repinfo->fullavailability);
		fprintf(htmlrep, "<td>&nbsp;</td>\n");
		fprintf(htmlrep, "<td colspan=\"3\" class=\"text-center fw-bold\">Availability (SLA): %.2f%%</td>\n", repinfo->reportavailability);
	}
	else {
		fprintf(htmlrep, "<td colspan=\"7\" class=\"text-center fw-bold\">Availability: %.2f%%</td>\n", repinfo->fullavailability);
	}
	fprintf(htmlrep, "</tr>\n");

	fprintf(htmlrep, "<tr>\n");
	fprintf(htmlrep, "<td>&nbsp;</td>\n");
	fprintf(htmlrep, "<td class=\"text-center\">%s</td>\n", coloricon(COL_GREEN,  0, 1));
	fprintf(htmlrep, "<td class=\"text-center\">%s</td>\n", coloricon(COL_YELLOW, 0, 1));
	fprintf(htmlrep, "<td class=\"text-center\">%s</td>\n", coloricon(COL_RED,    0, 1));
	fprintf(htmlrep, "<td class=\"text-center\">%s</td>\n", coloricon(COL_PURPLE, 0, 1));
	fprintf(htmlrep, "<td class=\"text-center\">%s</td>\n", coloricon(COL_CLEAR,  0, 1));
	fprintf(htmlrep, "<td class=\"text-center\">%s</td>\n", coloricon(COL_BLUE,   0, 1));
	fprintf(htmlrep, "</tr>\n");
	fprintf(htmlrep, "<tr>\n");
	fprintf(htmlrep, "<td><strong>24x7</strong></td>\n");
	fprintf(htmlrep, "<td class=\"text-center\"><strong>%.2f%%</strong></td>\n", repinfo->fullpct[COL_GREEN]);
	fprintf(htmlrep, "<td class=\"text-center\"><strong>%.2f%%</strong></td>\n", repinfo->fullpct[COL_YELLOW]);
	fprintf(htmlrep, "<td class=\"text-center\"><strong>%.2f%%</strong></td>\n", repinfo->fullpct[COL_RED]);
	fprintf(htmlrep, "<td class=\"text-center\"><strong>%.2f%%</strong></td>\n", repinfo->fullpct[COL_PURPLE]);
	fprintf(htmlrep, "<td class=\"text-center\"><strong>%.2f%%</strong></td>\n", repinfo->fullpct[COL_CLEAR]);
	fprintf(htmlrep, "<td class=\"text-center\"><strong>%.2f%%</strong></td>\n", repinfo->fullpct[COL_BLUE]);
	fprintf(htmlrep, "</tr>\n");
	if (repinfo->withreport) {
		fprintf(htmlrep, "<tr>\n");
		fprintf(htmlrep, "<td><strong>SLA (%.2f)</strong></td>\n", reportwarnlevel);
		fprintf(htmlrep, "<td class=\"text-center\"><strong>%.2f%%</strong></td>\n", repinfo->reportpct[COL_GREEN]);
		fprintf(htmlrep, "<td class=\"text-center\"><strong>%.2f%%</strong></td>\n", repinfo->reportpct[COL_YELLOW]);
		fprintf(htmlrep, "<td class=\"text-center\"><strong>%.2f%%</strong></td>\n", repinfo->reportpct[COL_RED]);
		fprintf(htmlrep, "<td class=\"text-center\">-</td>\n");
		fprintf(htmlrep, "<td class=\"text-center\"><strong>%.2f%%</strong></td>\n", repinfo->reportpct[COL_CLEAR]);
		fprintf(htmlrep, "<td class=\"text-center\">-</td>\n");
		fprintf(htmlrep, "</tr>\n");
	}
	fprintf(htmlrep, "<tr>\n");
	fprintf(htmlrep, "<td colspan=\"2\" class=\"text-center\"><strong>Event count</strong></td>\n");
	fprintf(htmlrep, "<td class=\"text-center\"><strong>%d</strong></td>\n", repinfo->count[COL_YELLOW]);
	fprintf(htmlrep, "<td class=\"text-center\"><strong>%d</strong></td>\n", repinfo->count[COL_RED]);
	fprintf(htmlrep, "<td class=\"text-center\"><strong>%d</strong></td>\n", repinfo->count[COL_PURPLE]);
	fprintf(htmlrep, "<td class=\"text-center\"><strong>%d</strong></td>\n", repinfo->count[COL_CLEAR]);
	fprintf(htmlrep, "<td class=\"text-center\"><strong>%d</strong></td>\n", repinfo->count[COL_BLUE]);
	fprintf(htmlrep, "</tr>\n");
	fprintf(htmlrep, "<tr>\n");
	fprintf(htmlrep, "<td colspan=\"7\" class=\"text-center\"><strong>[Total may not equal 100%%]</strong></td></tr>\n");

	if (strcmp(repinfo->fstate, "NOTOK") == 0) {
		fprintf(htmlrep, "<tr>\n");
		fprintf(htmlrep, "<td colspan=\"7\" class=\"text-center\"><strong>[History file contains invalid entries]</strong></td></tr>\n");
	}

	fprintf(htmlrep, "</table></div>\n");

	/* Text-based report start */
	if (textrep) {
		char text_starttime[20], text_endtime[20];

		fprintf(textrep, "Availability Report\n");

		strftime(text_starttime, sizeof(text_starttime), "%b %d %Y", localtime(&st));
		strftime(text_endtime, sizeof(text_endtime), "%b %d %Y", localtime(&end));
		if (strcmp(text_starttime, text_endtime) == 0)
			fprintf(textrep, "%s\n", text_starttime);
		else
			fprintf(textrep, "%s - %s\n", text_starttime, text_endtime);

		fprintf(textrep, "\n");
		fprintf(textrep, "\n");
		fprintf(textrep, "				%s - %s\n", displayname, service);
		fprintf(textrep, "\n");
		if (repinfo->withreport) {
			fprintf(textrep, "			Availability (24x7) :	%.2f%%\n", repinfo->fullavailability);
			fprintf(textrep, "			Availability (SLA)  :	%.2f%%\n", repinfo->reportavailability);
		}
		else {
			fprintf(textrep, "				Availability:	%.2f%%\n", repinfo->fullavailability);
		}
		fprintf(textrep, "			Green	Yellow	Red	Purple	Clear	Blue\n");
		fprintf(textrep, "		24x7	%.2f%%	%.2f%%	%.2f%%	%.2f%%	%.2f%%	%.2f%%\n",
			repinfo->fullpct[COL_GREEN], repinfo->fullpct[COL_YELLOW], repinfo->fullpct[COL_RED], 
			repinfo->fullpct[COL_PURPLE], repinfo->fullpct[COL_CLEAR], repinfo->fullpct[COL_BLUE]);
		if (repinfo->withreport) {
			fprintf(textrep, "		SLA	%.2f%%	%.2f%%	%.2f%%	   -  	%.2f%%	   -  \n",
				repinfo->reportpct[COL_GREEN], repinfo->reportpct[COL_YELLOW], 
				repinfo->reportpct[COL_RED], repinfo->reportpct[COL_CLEAR]);
		}
		fprintf(textrep, "		Events	%d	%d	%d	%d	%d	%d\n",
			repinfo->count[COL_GREEN], repinfo->count[COL_YELLOW], repinfo->count[COL_RED], 
			repinfo->count[COL_PURPLE], repinfo->count[COL_CLEAR], repinfo->count[COL_BLUE]);
		fprintf(textrep, "\n");
		fprintf(textrep, "\n");
		fprintf(textrep, "				Event logs for the given period\n");
		fprintf(textrep, "\n");
		fprintf(textrep, "Event Start			Event End			Status	Duration	(Seconds)	Cause\n");
		fprintf(textrep, "\n");
		fprintf(textrep, "\n");
	}


	fprintf(htmlrep, "<div class=\"table-responsive\"><table class=\"table table-sm table-dark table-striped replog-events\">\n");
	fprintf(htmlrep, "<tr>\n");
	fprintf(htmlrep, "<td colspan=\"5\" class=\"text-center fw-bold\">Event logs for the given period</td>\n");
	fprintf(htmlrep, "</tr>\n");
	fprintf(htmlrep, "<tr>\n");
	fprintf(htmlrep, "<th class=\"text-center\">Event Start</th>\n");
	fprintf(htmlrep, "<th class=\"text-center\">Event End</th>\n");
	fprintf(htmlrep, "<th class=\"text-center\">Status</th>\n");
	fprintf(htmlrep, "<th class=\"text-center\">Duration</th>\n");
	fprintf(htmlrep, "<th class=\"text-center\">Cause</th>\n");
	fprintf(htmlrep, "</tr>\n");

	for (walk = reploghead; (walk); walk = walk->next) {
		int wanted = 0;

		switch (style) {
		  case STYLE_CRIT: wanted = (walk->color == COL_RED); break;
		  case STYLE_NONGR: wanted = (walk->color != COL_GREEN); break;
		  case STYLE_OTHER: wanted = 1;
		}

		if (wanted) {
			char start[30];
			char end[30];
			time_t endtime;
			int angrygif = (repinfo->withreport && walk->affectssla);

			strftime(start, sizeof(start), "%a %b %d %H:%M:%S %Y", localtime(&walk->starttime));
			endtime = walk->starttime + walk->duration;
			strftime(end, sizeof(end), "%a %b %d %H:%M:%S %Y", localtime(&endtime));

			fprintf(htmlrep, "<tr>\n");
			fprintf(htmlrep, "<td class=\"text-nowrap\">%s</td>\n", start);
			fprintf(htmlrep, "<td class=\"text-nowrap text-end\">%s</td>\n", end);
			fprintf(htmlrep, "<td class=\"text-center\">");
			fprintf(htmlrep, "<a href=\"%s\">",
				histlogurl(hostname, service, 0, walk->timespec));
			fprintf(htmlrep, "%s", coloricon(walk->color, 0, !angrygif));
			fprintf(htmlrep, "</a></td>\n");

			fprintf(htmlrep, "<td class=\"text-center\">%s</td>\n", durationstr(walk->duration));
			fprintf(htmlrep, "<td>%s</td>\n", walk->cause);
			fprintf(htmlrep, "</tr>\n\n");


			/* And the text-report */
			if (textrep) {
				fprintf(textrep, "%s	%s	%s	%s		%u		",
					start, end, colorname(walk->color), 
					durationstr(walk->duration), (unsigned int)walk->duration);
				if (walk->cause) {
					char *p;

                        		for (p=walk->cause; (p && *p); ) {
						if (*p == '<') {
							p = strchr(p, '>');
							if (p) p++;
						}
						else if (*p != '\n') {
							fprintf(textrep, "%c", *p);
							p++;
						}
						else p++;
					}
					fprintf(textrep, "\n");
				}
			}
		}
	}

	fprintf(htmlrep, "<tr><td colspan=\"3\" class=\"text-end\"><strong>Time Critical/Offline (24x7):</strong></td>\n");
	fprintf(htmlrep, "<td colspan=\"2\" class=\"text-nowrap\">%s</td></tr>\n",
		durationstr(repinfo->fullduration[COL_RED]));

	if (style != STYLE_CRIT) {
		fprintf(htmlrep, "<tr><td colspan=\"3\" class=\"text-end\"><strong>Time Non-Critical (24x7):</strong></td>\n");
		fprintf(htmlrep, "<td colspan=\"2\" class=\"text-nowrap\">%s</td></tr>\n",
			durationstr(repinfo->fullduration[COL_YELLOW] + repinfo->fullduration[COL_PURPLE] +
				    repinfo->fullduration[COL_CLEAR]  + repinfo->fullduration[COL_BLUE]));
	}

	if (repinfo->withreport) {
		fprintf(htmlrep, "<tr><td colspan=\"3\" class=\"text-end\"><strong>Time Critical/Offline (SLA):</strong></td>\n");
		fprintf(htmlrep, "<td colspan=\"2\" class=\"text-nowrap\">%s</td></tr>\n",
			durationstr(repinfo->reportduration[COL_RED]));

		if (style != STYLE_CRIT) {
			fprintf(htmlrep, "<tr><td colspan=\"3\" class=\"text-end\"><strong>Time Non-Critical (SLA):</strong></td>\n");
			fprintf(htmlrep, "<td colspan=\"2\" class=\"text-nowrap\">%s</td></tr>\n",
				durationstr(repinfo->reportduration[COL_YELLOW]));
		}
	}


	/* And the text report ... */
	if (textrep) {
		fprintf(textrep, "\n");
		fprintf(textrep, "\n");
		fprintf(textrep, "			%s %s	(%lu secs)\n",
			"Time Critical/Offline (24x7):", durationstr(repinfo->fullduration[COL_RED]), repinfo->fullduration[COL_RED]);

		if (style != STYLE_CRIT) {
			fprintf(textrep, "			%s %s	(%lu secs)\n",
				"Time Non-Critical (24x7):", 
				durationstr(repinfo->fullduration[COL_YELLOW] + repinfo->fullduration[COL_PURPLE] +
					    repinfo->fullduration[COL_CLEAR]  + repinfo->fullduration[COL_BLUE]),
				(repinfo->fullduration[COL_YELLOW] + repinfo->fullduration[COL_PURPLE] + 
				 repinfo->fullduration[COL_CLEAR] + repinfo->fullduration[COL_BLUE]));
		}


		if (repinfo->withreport) {
			fprintf(textrep, "\n");
			fprintf(textrep, "\n");
			fprintf(textrep, "			%s %s	(%lu secs)\n",
				"Time Critical/Offline (SLA) :", durationstr(repinfo->reportduration[COL_RED]), repinfo->reportduration[COL_RED]);

			if (style != STYLE_CRIT) {
				fprintf(textrep, "			%s %s	(%lu secs)\n",
					"Time Non-Critical (SLA) :", 
					durationstr(repinfo->reportduration[COL_YELLOW]), repinfo->fullduration[COL_YELLOW]);
			}
		}
	}

	fprintf(htmlrep, "</table></div>\n");

	fprintf(htmlrep, "<p><a class=\"text-warning\" href=\"%s\">Click here for text-based availability report</a></p>\n", textrepurl);

	/* XYMONREPEXT extensions */
	do_extensions(htmlrep, "XYMONREPEXT", "rep");

	headfoot(htmlrep, "replog", "", "footer", color);
}

