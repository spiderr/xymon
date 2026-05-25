/*----------------------------------------------------------------------------*/
/* Xymon monitor library.                                                     */
/*                                                                            */
/* Shared HTML5 boilerplate emitter.  Writes the <head> section (DOCTYPE,    */
/* external asset links, theme CSS) and the closing JS + </body></html>.      */
/* Assets are served from $XYMONEXTERNALS (defaults to $XYMONSERVERWWWURL/   */
/* externals) so no external CDN requests are made.                           */
/* Theme CSS is served from $XYMONSERVERWWWURL/themes/$XYMONTHEME/xymon.css.  */
/*                                                                            */
/* Copyright (C) 2002-2011 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id$";

#include <stdio.h>
#include <string.h>
#include "libxymon.h"

void emit_html_header(FILE *output, const char *pagetitle)
{
	const char *theme  = xgetenv("XYMONTHEME");
	const char *wwwurl = xgetenv("XYMONSERVERWWWURL");
	const char *exturl = xgetenv("XYMONEXTERNALS");
	char extbuf[512];

	if (!theme)           theme  = "default";
	if (!wwwurl)          wwwurl = "/xymon";
	if (!exturl || !*exturl) {
		snprintf(extbuf, sizeof(extbuf), "%s/externals", wwwurl);
		exturl = extbuf;
	}

	fprintf(output,
		"<!DOCTYPE html>\n"
		"<html lang=\"en\" data-bs-theme=\"dark\">\n"
		"<head>\n"
		"<meta charset=\"utf-8\">\n"
		"<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
		"<link rel=\"stylesheet\" href=\"%s/bootstrap/css/bootstrap.min.css\">\n"
		"<link rel=\"stylesheet\" href=\"%s/fontawesome/css/all.min.css\">\n",
		exturl, exturl);
	fprintf(output,
		"<link rel=\"stylesheet\" href=\"%s/themes/%s/xymon.css\">\n"
		"<title>%s</title>\n"
		"</head>\n\n",
		wwwurl, theme,
		(pagetitle && *pagetitle) ? pagetitle : "Xymon");
}

void emit_html_footer(FILE *output)
{
	const char *exturl = xgetenv("XYMONEXTERNALS");
	const char *wwwurl = xgetenv("XYMONSERVERWWWURL");
	char extbuf[512];

	if (!exturl || !*exturl) {
		snprintf(extbuf, sizeof(extbuf), "%s/externals",
			(wwwurl && *wwwurl) ? wwwurl : "/xymon");
		exturl = extbuf;
	}

	fprintf(output,
		"\n<script src=\"%s/bootstrap/js/bootstrap.bundle.min.js\"></script>\n"
		"</body>\n"
		"</html>\n",
		exturl);
}
