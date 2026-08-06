/* Per-monitor container windows.
 *
 * Each monitor owns an override-redirect window covering its window area.
 * Tiled clients are reparented into it, which makes X clip them to the
 * monitor boundary for free: a layout may then place a window partially or
 * wholly outside the viewport without it bleeding onto the neighbouring
 * monitor, and without the hidden part swallowing pointer events.
 *
 * Client geometry stays in root coordinates throughout dwm -- recttomon(),
 * applysizehints(), the mouse handlers and the synthetic ConfigureNotify sent
 * by configure() all keep working unchanged. The translation into
 * container-relative coordinates happens only where we talk to X, in
 * resizeclient() and showhide().
 */

int
iscontained(Client *c)
{
	/* Floating and fullscreen clients stay children of the root window so
	 * that they can be dragged across monitors and cover the bar. */
	return !c->isfloating && !c->isfullscreen && c->mon->lt[c->mon->sellt]->arrange;
}

int
mappedwin(Window w)
{
	XWindowAttributes wa;
	int mapped = 0;

	XSetErrorHandler(xerrordummy);
	if (XGetWindowAttributes(dpy, w, &wa))
		mapped = wa.map_state != IsUnmapped;
	XSync(dpy, False);
	XSetErrorHandler(xerror);
	return mapped;
}

/* Move a window to a new parent, keeping it mapped.
 *
 * XReparentWindow unmaps a mapped window and remaps it afterwards. Both
 * halves need help:
 *
 *   - the automatic remap is a MapWindow request against the new parent, so
 *     the SubstructureRedirect we hold there turns it into a MapRequest.
 *     maprequest() drops those for already managed windows, and during
 *     cleanup() the event loop is gone entirely, so the window would simply
 *     stay unmapped. We map it back ourselves.
 *
 *   - the automatic unmap produces UnmapNotify events that unmapnotify()
 *     would read as the client withdrawing itself, and unmanage it. One
 *     unmap reaches us more than once -- dwm selects StructureNotify on the
 *     window and SubstructureNotify on both the root window and every
 *     container, and each of those is a separate delivery -- so a counter
 *     cannot balance it. Sync and drain instead: after XSync every event our
 *     own request generated is already queued, and nothing else has had the
 *     chance to unmap this window in between.
 */
/* XCheckTypedWindowEvent is no good here: it matches on xany.window, which for
 * an UnmapNotify aliases the "event" field (who the event was reported to),
 * not "window" (what was unmapped). That catches the StructureNotify copy and
 * misses the SubstructureNotify copy the parent delivers. Match the real
 * field. */
static Bool
isunmapof(Display *d, XEvent *e, XPointer arg)
{
	return e->type == UnmapNotify && e->xunmap.window == *(Window *)arg;
}

static void
reparentclient(Client *c, Window parent, int x, int y)
{
	XEvent ev;
	int mapped = mappedwin(c->win);

	XReparentWindow(dpy, c->win, parent, x, y);
	if (mapped)
		XMapWindow(dpy, c->win);
	XSync(dpy, False);
	while (XCheckIfEvent(dpy, &ev, isunmapof, (XPointer)&c->win));
	c->parent = parent;
}

void
updateclientparent(Client *c)
{
	Monitor *m = c->mon;
	Window want;

	want = iscontained(c) && m->container != None ? m->container : root;
	if (c->parent == want)
		return;

	if (want == root) {
		/* Outside a container there is no clipping, so a window the
		 * scroller had parked off the strip would be stranded where no
		 * monitor can reach it. Pull it back into view -- but never a
		 * fullscreen client, whose geometry is deliberately the whole
		 * monitor including the bar area. */
		if (!c->isfullscreen) {
			c->x = MAX(m->wx, MIN(c->x, m->wx + m->ww - WIDTH(c)));
			c->y = MAX(m->wy, MIN(c->y, m->wy + m->wh - HEIGHT(c)));
		}
		reparentclient(c, root, c->x, c->y);
	} else
		reparentclient(c, want, c->x - m->wx, c->y - m->wy);
}

void
detachcontainer(Client *c)
{
	if (c->parent == root || c->parent == None)
		return;
	reparentclient(c, root, c->x, c->y);
}

int
hasfullscreen(Monitor *m)
{
	Client *c;

	for (c = m->clients; c; c = c->next)
		if (c->isfullscreen && ISVISIBLE(c))
			return 1;
	return 0;
}

void
updatecontainer(Monitor *m)
{
	XSetWindowAttributes wa;

	if (m->ww <= 0 || m->wh <= 0)
		return;

	if (m->container == None) {
		wa.override_redirect = True;
		/* ParentRelative lets the root wallpaper show through the
		 * container instead of the container painting over it. */
		wa.background_pixmap = ParentRelative;
		/* Reparented clients escape the root window's redirect, so the
		 * container has to claim it or they could move themselves. */
		wa.event_mask = SubstructureRedirectMask|SubstructureNotifyMask;
		m->container = XCreateWindow(dpy, root, m->wx, m->wy, m->ww, m->wh, 0,
			DefaultDepth(dpy, screen), InputOutput, DefaultVisual(dpy, screen),
			CWOverrideRedirect|CWBackPixmap|CWEventMask, &wa);
		/* below the bar and below any floating client */
		XLowerWindow(dpy, m->container);
		XMapWindow(dpy, m->container);
	} else
		XMoveResizeWindow(dpy, m->container, m->wx, m->wy, m->ww, m->wh);
}

void
destroycontainer(Monitor *m)
{
	Monitor *om;
	Client *c;

	if (m->container == None)
		return;

	/* Destroying a parent destroys its children along with it. On monitor
	 * removal updategeom() hands the clients to another monitor and only
	 * then calls cleanupmon(), so they are still parented here while
	 * already listed elsewhere -- search every list, not just this one. */
	for (c = m->clients; c; c = c->next)
		if (c->parent == m->container)
			detachcontainer(c);
	for (om = mons; om; om = om->next)
		for (c = om->clients; c; c = c->next)
			if (c->parent == m->container)
				detachcontainer(c);

	XDestroyWindow(dpy, m->container);
	m->container = None;
}
