/* A schematic view of the scroller's strip.
 *
 * The strip is longer than the screen, so it is easy to lose track of where you
 * are in it. This draws the whole thing to scale in a small overlay: a block
 * per column, split into sub-blocks that mirror the windows stacked inside it,
 * and a track along the bottom showing which slice of the strip is on screen.
 *
 * It reuses dwm's own Drw, so there are no new dependencies. drw_rect() only
 * paints the current scheme's fg or bg, so the blocks are filled through the GC
 * directly. Only foreground/background pairs are used: a theme guarantees those
 * contrast, whereas the border colours need not -- this config sets
 * normbordercolor to the same black as the background, which would render every
 * unfocused block invisible.
 */

#define MINIMAPH    46
#define MINIMAPPAD  6
#define MINIMAPBAR  4
#define MINIMAPGAP  2
#define MINIMAPMAXW 900

static void
mmfill(int x, int y, int w, int h, Clr c)
{
	if (w <= 0 || h <= 0)
		return;
	XSetForeground(drw->dpy, drw->gc, c.pixel);
	XFillRectangle(drw->dpy, drw->drawable, drw->gc, x, y, w, h);
}

static void
minimapgeom(Monitor *m, int *x, int *y, int *w, int *h)
{
	*w = MIN(m->ww - 8 * MINIMAPPAD, MINIMAPMAXW);
	*w = MAX(*w, MIN(m->ww, 160));
	*h = MINIMAPH;
	*x = m->wx + (m->ww - *w) / 2;
	*y = m->wy + m->wh - *h - 4 * MINIMAPPAD;
}

void
drawminimap(Monitor *m)
{
	XSetWindowAttributes wa;
	Client *c;
	int x, y, w, h, i, n, ncols, total, view;
	int cx, cw, by, bh, inner, track;
	float sc;

	if (!minimapshown || m != selmon)
		return;

	/* only meaningful in the scroller; the flag is kept so it comes back
	 * when you switch to that layout again */
	ncols = m->lt[m->sellt]->arrange == scroller ? colcount(m) : 0;
	if (ncols == 0) {
		if (minimapwin != None)
			XUnmapWindow(dpy, minimapwin);
		return;
	}

	minimapgeom(m, &x, &y, &w, &h);
	if (minimapwin == None) {
		wa.override_redirect = True;
		wa.background_pixel = scheme[SchemeNorm][ColBg].pixel;
		wa.event_mask = ExposureMask;
		minimapwin = XCreateWindow(dpy, root, x, y, w, h, 0,
			DefaultDepth(dpy, screen), CopyFromParent, DefaultVisual(dpy, screen),
			CWOverrideRedirect|CWBackPixel|CWEventMask, &wa);
	} else
		XMoveResizeWindow(dpy, minimapwin, x, y, w, h);
	XMapRaised(dpy, minimapwin);

	total = colx(m, ncols) - m->gappiv;
	view = m->ww - 2 * m->gappov;
	if (total <= 0)
		return;
	sc = (float)(w - 2 * MINIMAPPAD) / total;

	track = h - MINIMAPPAD - MINIMAPBAR;
	inner = track - 2 * MINIMAPPAD;

	mmfill(0, 0, w, h, scheme[SchemeNorm][ColBg]);
	mmfill(0, 0, w, 1, scheme[SchemeNorm][ColFg]);
	mmfill(0, h - 1, w, 1, scheme[SchemeNorm][ColFg]);
	mmfill(0, 0, 1, h, scheme[SchemeNorm][ColFg]);
	mmfill(w - 1, 0, 1, h, scheme[SchemeNorm][ColFg]);

	for (i = 0; i < ncols; i++) {
		cx = MINIMAPPAD + colx(m, i) * sc;
		cw = MAX(4, colwidthof(m, i) * sc - MINIMAPGAP);

		for (n = 0, c = nextcol(m->clients); c; c = nextcol(c->next))
			if (c->col == i)
				n++;
		if (!n)
			continue;

		/* split the block the same way the column itself is split */
		bh = (inner - MINIMAPGAP * (n - 1)) / n;
		by = MINIMAPPAD;
		for (c = nextcol(m->clients); c; c = nextcol(c->next)) {
			if (c->col != i)
				continue;
			mmfill(cx, by, cw, bh, c == m->sel ? scheme[SchemeSel][ColFg]
				: m->sel && m->sel->col == i ? scheme[SchemeSel][ColBg]
				: scheme[SchemeNorm][ColFg]);
			by += bh + MINIMAPGAP;
		}
	}

	/* which slice of the strip the viewport is showing */
	mmfill(MINIMAPPAD, track + MINIMAPBAR / 2, w - 2 * MINIMAPPAD, 1,
		scheme[SchemeNorm][ColFg]);
	mmfill(MINIMAPPAD + m->scrollx * sc, track, MAX(8, view * sc), MINIMAPBAR,
		scheme[SchemeSel][ColBg]);

	drw_map(drw, minimapwin, 0, 0, w, h);
}

void
hideminimap(void)
{
	minimapshown = 0;
	if (minimapwin != None)
		XUnmapWindow(dpy, minimapwin);
}

void
toggleminimap(const Arg *arg)
{
	if (minimapshown)
		hideminimap();
	else {
		minimapshown = 1;
		arrange(selmon);
	}
}
