/* Skippy-xd
 *
 * Copyright (C) 2004 Hyriand <hyriand@thegraveyard.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "skippy.h"

void
tooltip_destroy(Tooltip *tt)
{
	session_t * const ps = tt->mainwin->ps;

	if(tt->text)
		free(tt->text);
	if(tt->font)
		XftFontClose(ps->dpy, tt->font);
	if(tt->draw)
		XftDrawDestroy(tt->draw);
	if(tt->color.pixel != None)
		XftColorFree(ps->dpy,
		             tt->mainwin->visual,
		             tt->mainwin->colormap,
		             &tt->color);
	if(tt->background.pixel != None)
		XftColorFree(ps->dpy,
		             tt->mainwin->visual,
		             tt->mainwin->colormap,
		             &tt->background);
	if(tt->border.pixel != None)
		XftColorFree(ps->dpy,
		             tt->mainwin->visual,
		             tt->mainwin->colormap,
		             &tt->border);
	if(tt->outline.pixel != None)
		XftColorFree(ps->dpy,
		             tt->mainwin->visual,
		             tt->mainwin->colormap,
		             &tt->outline);
	if(tt->window != None)
		XDestroyWindow(ps->dpy, tt->window);
	
	free(tt);
}

Tooltip *
tooltip_create(MainWin *mw) {
	session_t * const ps = mw->ps;
	const char *tmp;
	long int tmp_l;
	
	Tooltip *tt = allocchk(malloc(sizeof(Tooltip)));
	
	tt->mainwin = mw;
	tt->window = None;
	tt->font = 0;
	tt->draw = 0;
	tt->text = 0;
	tt->color.pixel = tt->background.pixel = tt->border.pixel = tt->outline.pixel = None;
	
	{
		XSetWindowAttributes attr = {
			.override_redirect = True,
			.border_pixel = None,
			.background_pixel = None,
			.event_mask = ExposureMask,
			.colormap = mw->colormap,
		};
		
		tt->window = XCreateWindow(ps->dpy,
				ps->o.pseudoTrans ? mw->window : ps->root,
				ps->o.pseudoTrans ? mw->x : 0, ps->o.pseudoTrans ? mw->y : 0,
				1, 1, 0,
				mw->depth, InputOutput, mw->visual,
				CWBorderPixel|CWBackPixel|CWOverrideRedirect|CWEventMask|CWColormap,
				&attr);
	}

	if (!tt->window) {
		printfef(false, "(): WARNING: Couldn't create tooltip window.");
		tooltip_destroy(tt);
		return 0;
	}
	wm_wid_set_info(ps, tt->window, "skippy-xd label", _NET_WM_WINDOW_TYPE_TOOLTIP);

	tmp = ps->o.tooltip_border;
	if(! XftColorAllocName(ps->dpy, mw->visual, mw->colormap, tmp, &tt->border))
	{
		printfef(false, "(): WARNING: Invalid color '%s'.\n", tmp);
		tooltip_destroy(tt);
		return 0;
	}

	tmp = ps->o.tooltip_background;
	if(! XftColorAllocName(ps->dpy, mw->visual, mw->colormap, tmp, &tt->background))
	{
		printfef(false, "(): WARNING: Invalid color '%s'.\n", tmp);
		tooltip_destroy(tt);
		return 0;
	}

	tmp = ps->o.tooltip_backgroundHighlight;
	if(! XftColorAllocName(ps->dpy, mw->visual, mw->colormap, tmp, &tt->backgroundHighlight))
	{
		printfef(false, "(): WARNING: Invalid color '%s'.\n", tmp);
		tooltip_destroy(tt);
		return 0;
	}

	tmp_l = alphaconv(ps->o.tooltip_opacity);
	tt->background.color.alpha = tmp_l;
	tt->backgroundHighlight.color.alpha = tmp_l;
	tt->border.color.alpha = tmp_l;
	
	tmp = ps->o.tooltip_text;
	if(! XftColorAllocName(ps->dpy, mw->visual, mw->colormap, tmp, &tt->color))
	{
		printfef(false, "(): WARNING: Couldn't allocate color '%s'.\n", tmp);
		tooltip_destroy(tt);
		return 0;
	}
	
	tmp = ps->o.tooltip_textOutline;
	if(strcasecmp(tmp, "none") != 0)
	{
		if(! XftColorAllocName(ps->dpy, mw->visual, mw->colormap, tmp, &tt->outline))
		{
			printfef(false, "(): WARNING: Couldn't allocate color '%s'.\n", tmp);
			tooltip_destroy(tt);
			return 0;
		}
	}
	
	tt->draw = XftDrawCreate(ps->dpy, tt->window, mw->visual, mw->colormap);
	if(! tt->draw)
	{
		printfef(false, "(): WARNING: Couldn't create Xft draw surface.\n");
		tooltip_destroy(tt);
		return 0;
	}
	
	tt->font = XftFontOpenName(ps->dpy, ps->screen, ps->o.tooltip_font);
	if(! tt->font)
	{
		printfef(false, "(): WARNING: Couldn't open Xft font.\n");
		tooltip_destroy(tt);
		return 0;
	}
	
	tt->font_height = tt->font->ascent + tt->font->descent;
	
	// Set tooltip window input region to empty to prevent disgusting
	// racing situations
	{
		XserverRegion region = XFixesCreateRegion(ps->dpy, NULL, 0);
		XFixesSetWindowShapeRegion(ps->dpy, tt->window, ShapeInput, 0, 0, region);
		XFixesDestroyRegion(ps->dpy, region);
	}

	return tt;
}

void
tooltip_map(Tooltip *tt, ClientWin *cw, FcChar8 *text, int len)
{
	session_t * const ps = tt->mainwin->ps;
	unsigned int max_width = cw->mini.width * ps->o.tooltip_width;
	if (max_width < 40)
		max_width = 40;

	// keep the full, untruncated title
	if (tt->text)
		free(tt->text);
	tt->text = (FcChar8 *)malloc(len + 1);
	memcpy(tt->text, text, len);
	tt->text[len] = '\0';
	tt->text_len = len;

	// word-wrap the title into multiple lines that each fit within max_width
	tt->nlines = 0;
	unsigned int maxpix = 0;
	int pos = 0;
	XGlyphInfo ext;
	while (pos < len && tt->nlines < TOOLTIP_MAX_LINES) {
		while (pos < len && tt->text[pos] == ' ')	// skip leading spaces
			pos++;
		if (pos >= len)
			break;

		int lstart = pos, lend = pos, scan = pos;
		while (scan < len) {
			int wend = scan;			// next word = [scan, wend)
			while (wend < len && tt->text[wend] != ' ')
				wend++;
			XftTextExtentsUtf8(ps->dpy, tt->font,
					tt->text + lstart, wend - lstart, &ext);
			if (ext.width <= max_width) {
				lend = wend;
				scan = wend;
				while (scan < len && tt->text[scan] == ' ')
					scan++;
			}
			else {
				if (lend == lstart) {	// single word too long -> hard-break
					int take = wend - lstart;
					while (take > 1) {
						XftTextExtentsUtf8(ps->dpy, tt->font,
								tt->text + lstart, take, &ext);
						if (ext.width <= max_width)
							break;
						take--;
					}
					lend = lstart + take;
				}
				break;
			}
		}

		tt->lines[tt->nlines].off = lstart;
		tt->lines[tt->nlines].len = lend - lstart;
		XftTextExtentsUtf8(ps->dpy, tt->font,
				tt->text + lstart, lend - lstart, &ext);
		if (ext.width > maxpix)
			maxpix = ext.width;
		tt->nlines++;
		pos = lend;
	}
	if (tt->nlines == 0) {			// fallback: whole string on one line
		tt->lines[0].off = 0;
		tt->lines[0].len = len;
		tt->nlines = 1;
		XftTextExtentsUtf8(ps->dpy, tt->font, tt->text, len, &ext);
		maxpix = ext.width;
	}

	int line_h = tt->font_height + 2;
	tt->width = maxpix + 8;
	tt->height = tt->nlines * line_h + 4;
	XResizeWindow(ps->dpy, tt->window, tt->width, tt->height);
	tooltip_move(tt, cw);

	XMapWindow(ps->dpy, tt->window);
	XRaiseWindow(ps->dpy, tt->window);
}

// Show arbitrary text as a single-line box, centred horizontally on cx with its
// top at ty (absolute mainwin coordinates). Used for the search-query display.
void
tooltip_show_at(Tooltip *tt, int cx, int ty, FcChar8 *text, int len)
{
	session_t * const ps = tt->mainwin->ps;

	if (tt->text)
		free(tt->text);
	tt->text = (FcChar8 *)malloc(len + 1);
	memcpy(tt->text, text, len);
	tt->text[len] = '\0';
	tt->text_len = len;

	tt->nlines = 1;
	tt->lines[0].off = 0;
	tt->lines[0].len = len;

	XGlyphInfo ext;
	XftTextExtentsUtf8(ps->dpy, tt->font, text, len, &ext);
	int line_h = tt->font_height + 2;
	tt->width  = ext.width + 16;
	tt->height = line_h + 8;
	XResizeWindow(ps->dpy, tt->window, tt->width, tt->height);

	int x = cx - (int) tt->width / 2;
	x = MIN(MAX(0, x), tt->mainwin->x + tt->mainwin->width - (int) tt->width);
	XMoveWindow(ps->dpy, tt->window, x, ty);

	XMapWindow(ps->dpy, tt->window);
	XRaiseWindow(ps->dpy, tt->window);
	tooltip_draw(tt, true);
	XFlush(ps->dpy);
}

void
tooltip_move(Tooltip *tt, ClientWin *cw) {

	session_t *ps = tt->mainwin->ps;
	int x = ps->o.tooltip_offsetX,
		y = ps->o.tooltip_offsetY;

    x += cw->mini.x + cw->mini.width/2 - tt->width / 2;
    // attach the label INSIDE the window, near its bottom edge, so it is
    // clearly tied to its own window and never intrudes into neighbours
    y += cw->mini.y + cw->mini.height - tt->height;

	x = MIN(MAX(0, x), tt->mainwin->x + tt->mainwin->width - tt->width);
	y = MIN(MAX(0, y), tt->mainwin->y + tt->mainwin->height - tt->height);
	
	XMoveWindow(tt->mainwin->ps->dpy, tt->window, x, y);
}

void
tooltip_unmap(Tooltip *tt)
{
	XUnmapWindow(tt->mainwin->ps->dpy, tt->window);
	if(tt->text)
		free(tt->text);
	tt->text = 0;
	tt->text_len = 0;
}

void
tooltip_draw(Tooltip *tt, bool focused)
{
	if (!tt || !tt->text)
		return;

	XftDrawRect(tt->draw, &tt->border, 0, 0, tt->width, 1);
	XftDrawRect(tt->draw, &tt->border, 0, 1, 1, tt->height - 2);
	XftDrawRect(tt->draw, &tt->border, 0, tt->height - 1, tt->width, 1);
	XftDrawRect(tt->draw, &tt->border, tt->width - 1, 1, 1, tt->height - 2);

	if (focused)
		XftDrawRect(tt->draw, &tt->backgroundHighlight, 1, 1, tt->width - 2, tt->height - 2);
	else
		XftDrawRect(tt->draw, &tt->background, 1, 1, tt->width - 2, tt->height - 2);

	int base_x = 4;
	int line_h = tt->font_height + 2;

	for (int n = 0; n < tt->nlines; n++) {
		FcChar8 *ltext = tt->text + tt->lines[n].off;
		int llen = tt->lines[n].len;
		int base_y = 2 + n * line_h + tt->font->ascent;

		if (tt->outline.pixel != None) {
			for (int dx = -1; dx <= 1; dx++) {
				for (int dy = -1; dy <= 1; dy++) {
					if (dx == 0 && dy == 0)
						continue;
					XftDrawStringUtf8(tt->draw, &tt->outline, tt->font,
							base_x + dx, base_y + dy,
							ltext, llen);
				}
			}
		}

		XftDrawStringUtf8(tt->draw, &tt->color, tt->font,
				base_x, base_y,
				ltext, llen);
	}
}
