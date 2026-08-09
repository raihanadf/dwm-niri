/* A zoomed-out view of the whole strip, with live window contents.
 *
 * dwm is not a compositor and there is none running here, but the Composite
 * extension alone is enough: redirecting the container's subwindows in
 * Automatic mode leaves the server drawing them to the screen exactly as
 * before, while additionally keeping each window's full contents in offscreen
 * storage. Crucially that storage is the window's real size no matter how much
 * of it the container is clipping -- so a column scrolled far off the viewport
 * still has complete, current contents to sample. That is what makes an
 * overview possible at all.
 *
 * Each window is then named as a Pixmap, wrapped in an XRender Picture, scaled
 * with a transform, and composited into a full-screen overlay.
 */

#define OVERVIEWPAD 24
#define OVERVIEWGAP 14

int
compositeok(void)
{
	static int checked = 0;
	int ev, err;

	if (!checked) {
		checked = 1;
		havecomposite = XCompositeQueryExtension(dpy, &ev, &err)
			&& XRenderQueryExtension(dpy, &ev, &err);
		if (!havecomposite)
			fprintf(stderr, "dwm: no Composite/Render extension, "
				"the overview is unavailable\n");
	}
	return havecomposite;
}

void
redirectcontainer(Monitor *m)
{
	static int rootdone = 0;

	if (!compositeok())
		return;

	/* Floating and fullscreen clients stay children of the root rather than of
	 * a container, so redirecting only the containers left them with no
	 * offscreen storage at all -- which is why they never appeared in the
	 * overview. Automatic here too: the screen is painted exactly as before,
	 * we are only asking the server to keep a copy. */
	if (!rootdone) {
		XCompositeRedirectSubwindows(dpy, root, CompositeRedirectAutomatic);
		rootdone = 1;
	}

	if (m->container == None)
		return;
	/* Automatic, not Manual: we are not taking over painting the screen,
	 * only asking the server to keep a copy we can sample. Scoped to the
	 * container rather than the root so this composes with a real
	 * compositor later, should one ever be running. */
	XCompositeRedirectSubwindows(dpy, m->container, CompositeRedirectAutomatic);
}

void
drawthumb(Picture dst, Window win, int dx, int dy, int dw, int dh)
{
	XWindowAttributes wa;
	XRenderPictFormat *fmt;
	XRenderPictureAttributes pa;
	XTransform t;
	Pixmap pm;
	Picture src;

	if (dw <= 0 || dh <= 0)
		return;

	/* The error handler and the sync belong to the caller, around the whole
	 * set of thumbnails. A window can be resized or destroyed between naming
	 * its pixmap and using it -- errors here are never worth dying for -- but
	 * syncing per window turns every thumbnail into its own round trip, and
	 * the overview draws a great many of them. */
	if (!XGetWindowAttributes(dpy, win, &wa) || wa.map_state != IsViewable
	|| !(fmt = XRenderFindVisualFormat(dpy, wa.visual))
	|| !(pm = XCompositeNameWindowPixmap(dpy, win)))
		return;

	pa.subwindow_mode = IncludeInferiors; /* client-side child windows too */
	src = XRenderCreatePicture(dpy, pm, fmt, CPSubwindowMode, &pa);
	XRenderSetPictureFilter(dpy, src, FilterBilinear, NULL, 0);

	/* The transform maps destination back to source, so it scales up by the
	 * ratio we want to shrink by. */
	memset(&t, 0, sizeof t);
	t.matrix[0][0] = XDoubleToFixed((double)wa.width / dw);
	t.matrix[1][1] = XDoubleToFixed((double)wa.height / dh);
	t.matrix[2][2] = XDoubleToFixed(1.0);
	XRenderSetPictureTransform(dpy, src, &t);

	XRenderComposite(dpy, PictOpSrc, src, None, dst, 0, 0, 0, 0, dx, dy, dw, dh);

	XRenderFreePicture(dpy, src);
	XFreePixmap(dpy, pm);
}

/* What was drawn where, so a click can be turned back into a window. Filled on
 * every draw; the overview is the only thing that reads them. */
static OvSlot ovslots[128];
static int novslots;
static OvRow ovrows[NUMTAGS + 1];
static int novrows;

void
drawoverview(Monitor *m)
{
	XSetWindowAttributes wa;
	XRenderPictFormat *fmt;
	Picture dst;
	Client *c;
	StripItem items[64];
	int totals[NUMTAGS + 1];
	int nws, ws, nitems, widest = 0, rowh, rowy, xoff, yoff;
	int dx, dy, dw, dh, i, k, vx, vw, cur;
	double scale;

	if (!overviewshown || m != selmon)
		return;

	if (!compositeok() || m->lt[m->sellt]->arrange != scroller) {
		if (overviewwin != None)
			XUnmapWindow(dpy, overviewwin);
		return;
	}

	nws = shownworkspaces(m);
	cur = m->pertag->curtag;

	/* Measure every workspace first: they all draw at one scale, so that a
	 * window is the same size wherever it sits and the rows can be read
	 * against each other. */
	for (ws = 1; ws <= nws; ws++) {
		stripgeom(m, 1 << (ws - 1), NULL, 0, &totals[ws]);
		widest = MAX(widest, totals[ws]);
	}

	if (overviewwin == None) {
		wa.override_redirect = True;
		/* ParentRelative rather than a flat colour: the parent is the root
		 * window, so what shows through is the wallpaper, in the right place
		 * and at the right offset, with no compositor and nothing to keep in
		 * step when it changes. The same trick the containers use. */
		wa.background_pixmap = ParentRelative;
		wa.event_mask = ExposureMask|ButtonPressMask;
		/* the whole monitor, not the work area: the overview replaces the
		 * screen while it is up, bar included */
		overviewwin = XCreateWindow(dpy, root, m->mx, m->my, m->mw, m->mh, 0,
			DefaultDepth(dpy, screen), CopyFromParent, DefaultVisual(dpy, screen),
			CWOverrideRedirect|CWBackPixmap|CWEventMask, &wa);
	} else
		XMoveResizeWindow(dpy, overviewwin, m->mx, m->my, m->mw, m->mh);
	XMapRaised(dpy, overviewwin);
	XClearWindow(dpy, overviewwin);

	if (!(fmt = XRenderFindVisualFormat(dpy, DefaultVisual(dpy, screen))))
		return;
	dst = XRenderCreatePicture(dpy, overviewwin, fmt, 0, NULL);
	/* One handler and one sync for the whole set; see drawthumb() */
	XSetErrorHandler(xerrordummy);

	rowh = (m->mh - 2 * OVERVIEWPAD - (nws - 1) * OVERVIEWGAP) / nws;
	/* An empty workspace still gets a row, so it is somewhere you can see and
	 * aim at rather than a gap in the list. Scale to the widest strip, or to
	 * one screenful when nothing is wider than that. */
	scale = MIN((double)(m->mw - 2 * OVERVIEWPAD) / MAX(widest, m->ww),
		(double)rowh / m->wh);

	novslots = novrows = 0;

	for (ws = 1; ws <= nws; ws++) {
		rowy = OVERVIEWPAD + (ws - 1) * (rowh + OVERVIEWGAP);
		yoff = rowy + (rowh - m->wh * scale) / 2;
		/* Line the rows up by their screen rather than by their strip. Centre
		 * each strip on its own width and the frames stagger across the
		 * display, which reads as noise; anchoring the screens instead lets
		 * the eye run straight down the column of workspaces, with the strips
		 * running off to either side by however much they overflow. */
		vw = (m->ww - 2 * m->gappov) * scale;
		vx = (m->mw - vw) / 2;
		xoff = vx - (ws == cur ? m->scrollx : m->pertag->scrollxs[ws]) * scale;

		if (novrows < (int)LENGTH(ovrows)) {
			ovrows[novrows].ws = ws;
			ovrows[novrows].y = rowy;
			ovrows[novrows].h = rowh;
			novrows++;
		}

		nitems = stripgeom(m, 1 << (ws - 1), items, LENGTH(items), &totals[ws]);
		for (i = 0; i < nitems; i++) {
			dx = xoff + items[i].x * scale;
			dy = yoff + items[i].y * scale;
			dw = items[i].w * scale;
			dh = items[i].h * scale;

			drawthumb(dst, items[i].c->win, dx, dy, dw, dh);

			XSetForeground(dpy, drw->gc, scheme[items[i].c == m->sel
				? SchemeSel : SchemeNorm][ColFg].pixel);
			for (k = 1; k <= (items[i].c == m->sel ? 3 : 1); k++)
				XDrawRectangle(dpy, overviewwin, drw->gc,
					dx - k, dy - k, dw + 2 * k - 1, dh + 2 * k - 1);

			if (novslots < (int)LENGTH(ovslots)) {
				ovslots[novslots].c = items[i].c;
				ovslots[novslots].ws = ws;
				ovslots[novslots].x = dx;
				ovslots[novslots].y = dy;
				ovslots[novslots].w = dw;
				ovslots[novslots].h = dh;
				novslots++;
			}
		}

		/* Floating windows are not on the strip and do not scroll with it:
		 * they sit over the screen wherever they were put, so they are placed
		 * against the frame rather than the strip. Drawn after the tiled ones
		 * so they overlap here as they do in life. */
		for (c = m->clients; c; c = c->next) {
			if (!(c->tags & (1 << (ws - 1))) || !c->isfloating || c->isfullscreen)
				continue;
			dx = vx + (c->x - m->wx) * scale;
			dy = yoff + (c->y - m->wy) * scale;
			dw = c->w * scale;
			dh = c->h * scale;

			drawthumb(dst, c->win, dx, dy, dw, dh);

			XSetForeground(dpy, drw->gc, scheme[c == m->sel
				? SchemeSel : SchemeNorm][ColFg].pixel);
			for (k = 1; k <= (c == m->sel ? 3 : 1); k++)
				XDrawRectangle(dpy, overviewwin, drw->gc,
					dx - k, dy - k, dw + 2 * k - 1, dh + 2 * k - 1);

			if (novslots < (int)LENGTH(ovslots)) {
				ovslots[novslots].c = c;
				ovslots[novslots].ws = ws;
				ovslots[novslots].x = dx;
				ovslots[novslots].y = dy;
				ovslots[novslots].w = dw;
				ovslots[novslots].h = dh;
				novslots++;
			}
		}

		/* The screen itself, drawn over the strip: a frame around the slice
		 * this workspace is scrolled to. It gives an empty workspace
		 * something to be, and everywhere else it says which part of a strip
		 * too long to fit is the part you would actually see. Each workspace
		 * remembers its own offset, so this is where you would land rather
		 * than wherever the workspace in view happens to sit. */
		vx = (m->mw - vw) / 2;
		XSetForeground(dpy, drw->gc,
			scheme[ws == cur ? SchemeSel : SchemeNorm][ColFg].pixel);
		for (k = 0; k < (ws == cur ? 3 : 1); k++)
			XDrawRectangle(dpy, overviewwin, drw->gc, vx - k, yoff - k,
				vw + 2 * k, m->wh * scale + 2 * k);
	}

	XRenderFreePicture(dpy, dst);
	XSync(dpy, False);
	XSetErrorHandler(xerror);
}

/* Click a window to go to it, or anywhere else in a row to go to that
 * workspace. The overview is meant to be somewhere you act from, not just
 * something you look at. */
int
overviewclick(XButtonPressedEvent *ev)
{
	Monitor *m = selmon;
	int i, x, y;

	if (!overviewshown || overviewwin == None || ev->window != overviewwin)
		return 0;
	x = ev->x;
	y = ev->y;

	for (i = 0; i < novslots; i++)
		if (x >= ovslots[i].x && x < ovslots[i].x + ovslots[i].w
		&& y >= ovslots[i].y && y < ovslots[i].y + ovslots[i].h) {
			hideoverview();
			if (ovslots[i].ws != (int)m->pertag->curtag)
				view(&((Arg) { .ui = 1 << (ovslots[i].ws - 1) }));
			focus(ovslots[i].c);
			syncfullscreen(m, m->sel);
			arrange(m);
			return 1;
		}

	for (i = 0; i < novrows; i++)
		if (y >= ovrows[i].y && y < ovrows[i].y + ovrows[i].h) {
			hideoverview();
			if (ovrows[i].ws != (int)m->pertag->curtag)
				view(&((Arg) { .ui = 1 << (ovrows[i].ws - 1) }));
			syncfullscreen(m, m->sel);
			arrange(m);
			return 1;
		}

	hideoverview();
	syncfullscreen(m, m->sel);
	arrange(m);
	return 1;
}

void
hideoverview(void)
{
	overviewshown = 0;
	if (overviewwin != None)
		XUnmapWindow(dpy, overviewwin);
}

void
toggleoverview(const Arg *arg)
{
	Client *c;

	if (overviewshown) {
		hideoverview();
		syncfullscreen(selmon, selmon->sel); /* put fullscreen back */
	} else {
		overviewshown = 1;
		/* A fullscreen window owns the whole monitor rather than a slot on
		 * the strip, so it has nowhere to be drawn here. Drop it back into
		 * its column for the duration -- wantfullscreen is kept, so closing
		 * the overview restores it. */
		for (c = selmon->clients; c; c = c->next)
			if (c->isfullscreen && ISVISIBLE(c))
				setfullscreen(c, 0);
	}
	arrange(selmon);
}
