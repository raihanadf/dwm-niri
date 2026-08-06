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
	if (!compositeok() || m->container == None)
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

	/* A window can be resized or destroyed between naming its pixmap and
	 * using it, which is the classic way this goes wrong. Errors here are
	 * never worth dying for. */
	XSetErrorHandler(xerrordummy);

	if (!XGetWindowAttributes(dpy, win, &wa) || wa.map_state != IsViewable
	|| !(fmt = XRenderFindVisualFormat(dpy, wa.visual))
	|| !(pm = XCompositeNameWindowPixmap(dpy, win))) {
		XSync(dpy, False);
		XSetErrorHandler(xerror);
		return;
	}

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
	XSync(dpy, False);
	XSetErrorHandler(xerror);
}

void
drawoverview(Monitor *m)
{
	XSetWindowAttributes wa;
	XRenderPictFormat *fmt;
	Picture dst;
	Client *c;
	int ncols, total, sx, sy, dx, dy, dw, dh, xoff, yoff, i;
	double scale;

	if (!overviewshown || m != selmon)
		return;

	ncols = (compositeok() && m->lt[m->sellt]->arrange == scroller)
		? colcount(m) : 0;
	if (ncols == 0) {
		if (overviewwin != None)
			XUnmapWindow(dpy, overviewwin);
		return;
	}

	if (overviewwin == None) {
		wa.override_redirect = True;
		wa.background_pixel = scheme[SchemeNorm][ColBg].pixel;
		wa.event_mask = ExposureMask;
		overviewwin = XCreateWindow(dpy, root, m->wx, m->wy, m->ww, m->wh, 0,
			DefaultDepth(dpy, screen), CopyFromParent, DefaultVisual(dpy, screen),
			CWOverrideRedirect|CWBackPixel|CWEventMask, &wa);
	} else
		XMoveResizeWindow(dpy, overviewwin, m->wx, m->wy, m->ww, m->wh);
	XMapRaised(dpy, overviewwin);
	XClearWindow(dpy, overviewwin);

	if (!(fmt = XRenderFindVisualFormat(dpy, DefaultVisual(dpy, screen))))
		return;
	dst = XRenderCreatePicture(dpy, overviewwin, fmt, 0, NULL);

	total = colx(m, ncols) - m->gappiv;
	if (total <= 0) {
		XRenderFreePicture(dpy, dst);
		return;
	}
	scale = MIN((double)(m->ww - 2 * OVERVIEWPAD) / total,
		(double)(m->wh - 2 * OVERVIEWPAD) / m->wh);
	xoff = (m->ww - total * scale) / 2;
	yoff = (m->wh - m->wh * scale) / 2;

	for (c = nextcol(m->clients); c; c = nextcol(c->next)) {
		if (c->isfullscreen)
			continue;
		/* undo the scroll to get the window's place on the strip */
		sx = c->x - m->wx - m->gappov + m->scrollx;
		sy = c->y - m->wy - m->gappoh;
		dx = xoff + sx * scale;
		dy = yoff + sy * scale;
		dw = c->w * scale;
		dh = c->h * scale;

		drawthumb(dst, c->win, dx, dy, dw, dh);

		XSetForeground(dpy, drw->gc, scheme[c == m->sel ? SchemeSel : SchemeNorm][ColFg].pixel);
		for (i = 1; i <= (c == m->sel ? 3 : 1); i++)
			XDrawRectangle(dpy, overviewwin, drw->gc,
				dx - i, dy - i, dw + 2 * i - 1, dh + 2 * i - 1);
	}

	XRenderFreePicture(dpy, dst);
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
	if (overviewshown)
		hideoverview();
	else {
		overviewshown = 1;
		arrange(selmon);
	}
}
