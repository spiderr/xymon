/*----------------------------------------------------------------------------*/
/* Xymon monitor library.                                                     */
/*                                                                            */
/* Copyright (C) 2002-2011 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef __HTMLHEADER_H__
#define __HTMLHEADER_H__

#include <stdio.h>

extern void emit_html_header(FILE *output, const char *pagetitle);
extern void emit_html_footer(FILE *output);

#endif
