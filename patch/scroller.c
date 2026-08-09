/* A niri-style scrolling layout.
 *
 * Windows live on an infinite horizontal strip of columns. A column holds one
 * or more windows stacked vertically and has a fixed width, so opening a new
 * window never resizes the ones already there -- the strip just grows to the
 * right. The monitor is a viewport onto that strip, scrolled by m->scrollx so
 * that the focused column is always fully visible.
 *
 * Columns are identified by Client.col. Indices are kept contiguous by
 * normalizecols(), so "the column to the left" is simply col - 1.
 *
 * The layout calls resizeclient() rather than resize(): applysizehints() would
 * clamp any window lying outside the monitor back inside it, which is exactly
 * the placement a scrolling strip depends on.
 */

/* Members of the strip. nexttiled() skips anything flagged floating, and a
 * fullscreen client is flagged floating -- but it still owns its column, or
 * you could navigate away from it and never find your way back. */
static Client *
nextcol(Client *c)
{
	for (; c && (!ISVISIBLE(c) || (c->isfloating && !c->isfullscreen)); c = c->next);
	return c;
}

int
colcount(Monitor *m)
{
	Client *c;
	int max = -1;

	for (c = nextcol(m->clients); c; c = nextcol(c->next))
		if (ISVISIBLE(c) && c->col > max)
			max = c->col;
	return max + 1;
}

/* Compact column indices down to 0..n-1, preserving their order. Done in two
 * passes through negative values so that renumbering cannot collide with an
 * index that has not been visited yet. */
void
normalizecols(Monitor *m)
{
	Client *c;
	int cols[64], n = 0, i, j, t;

	if (scanning)
		return; /* indices are still being restored; see scan() */

	for (c = nextcol(m->clients); c; c = nextcol(c->next)) {
		if (!ISVISIBLE(c))
			continue;
		for (i = 0; i < n && cols[i] != c->col; i++);
		if (i == n && n < (int)LENGTH(cols))
			cols[n++] = c->col;
	}
	for (i = 0; i < n; i++)
		for (j = i + 1; j < n; j++)
			if (cols[j] < cols[i]) { t = cols[i]; cols[i] = cols[j]; cols[j] = t; }

	for (c = nextcol(m->clients); c; c = nextcol(c->next))
		if (ISVISIBLE(c))
			for (i = 0; i < n; i++)
				if (cols[i] == c->col) { c->col = -(i + 1); break; }
	for (c = nextcol(m->clients); c; c = nextcol(c->next))
		if (ISVISIBLE(c) && c->col < 0)
			c->col = -c->col - 1;
}

/* Column width presets, as a fraction of the viewport. The last one fills it
 * completely, which is how you give one window the whole screen without
 * leaving the layout. */
static const float colfacts[] = { 0.33, 0.50, 0.67, 1.00 };

/* Keep the focused column in the middle of the viewport rather than merely
 * on-screen, so focus lands where the eye already is. */
static const int centerfocused = 1;

int
colwidthof(Monitor *m, int col)
{
	Client *c = colclient(m, col);
	float f = c ? c->cwfact : colfacts[1];
	int w = (m->ww - 2 * m->gappov) * f;

	return MAX(w, bh);
}

/* Left edge of a column in strip coordinates. Columns may differ in width, so
 * this is a running total rather than col * width. */
int
colx(Monitor *m, int col)
{
	int i, x = 0;

	for (i = 0; i < col; i++)
		x += colwidthof(m, i) + m->gappiv;
	return x;
}

Client *
colclient(Monitor *m, int col)
{
	Client *c;

	for (c = nextcol(m->clients); c; c = nextcol(c->next))
		if (ISVISIBLE(c) && c->col == col)
			return c;
	return NULL;
}

/* Scroll the viewport the minimum distance needed to bring col fully in. */
void
scrolltocol(Monitor *m, int col)
{
	int view = m->ww - 2 * m->gappov;
	int left = colx(m, col);
	int cw = colwidthof(m, col);
	int total = colx(m, colcount(m)) - m->gappiv;

	if (centerfocused && cw < view)
		m->scrollx = left - (view - cw) / 2;
	else if (left - m->scrollx < 0)
		m->scrollx = left;
	else if (left + cw - m->scrollx > view)
		m->scrollx = left + cw - view;

	if (total > view) {
		/* never scroll past either end of the strip */
		if (m->scrollx > total - view)
			m->scrollx = total - view;
		if (m->scrollx < 0)
			m->scrollx = 0;
	} else
		m->scrollx = 0; /* it all fits: start hard against the left edge */
}

void
scroller(Monitor *m)
{
	Client *c;
	unsigned int n;
	int oh, ov, ih, iv;
	int ncols, i, x, y, cw, nin, hh, rest;

	getgaps(m, &oh, &ov, &ih, &iv, &n);
	if (n == 0)
		return;

	normalizecols(m);
	ncols = colcount(m);

	if (m->sel && ISVISIBLE(m->sel) && !m->sel->isfloating)
		scrolltocol(m, m->sel->col);
	else
		scrolltocol(m, 0);

	for (i = 0; i < ncols; i++) {
		for (nin = 0, c = nextcol(m->clients); c; c = nextcol(c->next))
			if (c->col == i && !c->isfullscreen)
				nin++;
		if (!nin)
			continue;

		cw = colwidthof(m, i);
		x = m->wx + ov + colx(m, i) - m->scrollx;
		hh = (m->wh - 2 * oh - ih * (nin - 1)) / nin;
		rest = (m->wh - 2 * oh - ih * (nin - 1)) - hh * nin;
		y = m->wy + oh;

		for (c = nextcol(m->clients); c; c = nextcol(c->next)) {
			if (c->col != i || c->isfullscreen)
				continue; /* fullscreen keeps its own geometry */
			/* resizeclient(), not resize(): see the note at the top */
			resizeclient(c, x, y, cw - 2 * c->bw,
				hh + (rest > 0 ? 1 : 0) - 2 * c->bw);
			y += HEIGHT(c) + ih;
			rest--;
		}
	}
}

/* A new window opens in a column of its own, immediately to the right of the
 * focused one, pushing the rest of the strip along. Existing windows keep
 * their size -- that is the whole point of the layout. */
void
insertcolumn(Client *c)
{
	Monitor *m = c->mon;
	Client *o;
	int at;

	/* Tracked in every layout, not just the scroller: otherwise windows
	 * opened under tile would all share column 0 and collapse into a single
	 * stack the moment you switched over. */
	at = (m->sel && m->sel != c) ? m->sel->col + 1 : colcount(m);
	c->cwfact = colfacts[1]; /* half width by default */
	for (o = nextcol(m->clients); o; o = nextcol(o->next))
		if (o != c && ISVISIBLE(o) && o->col >= at)
			o->col++;
	c->col = at;
}

/* Only one window can own the screen at a time. A fullscreen client sits at
 * the root, above the container, and nothing inside the container can ever
 * stack over it -- so leaving one behind would hide whatever you just focused
 * and let a second fullscreen window pile on top of it. */
static void
dropfullscreen(Monitor *m, Client *keep)
{
	Client *c;

	/* not nexttiled(): a fullscreen client is flagged floating */
	for (c = m->clients; c; c = c->next)
		if (c != keep && ISVISIBLE(c) && (c->isfullscreen || c->wantfullscreen)) {
			c->wantfullscreen = 0;
			if (c->isfullscreen)
				setfullscreen(c, 0);
		}
}

/* Fullscreen follows focus.
 *
 * X cannot draw one window filling the screen and sitting in its column at the
 * same time, so a window is only actually fullscreen while it is the focused
 * one. Move away and it drops back into the strip, where you can see it beside
 * whatever you moved to; come back and it fills the screen again. The intent
 * is remembered in wantfullscreen, so the state survives the round trip. */
static void
syncfullscreen(Monitor *m, Client *sel)
{
	static int syncing = 0;
	Client *c;

	if (!m || m->lt[m->sellt]->arrange != scroller)
		return;
	/* the overview shows the strip; nothing may seize the screen while it
	 * is up, or the window you are looking for is the one you cannot see */
	if (overviewshown)
		return;
	/* setfullscreen() re-arranges, so guard against coming back round */
	if (syncing)
		return;
	syncing = 1;

	for (c = m->clients; c; c = c->next) {
		if (!ISVISIBLE(c))
			continue;
		if (c == sel && c->wantfullscreen && !c->isfullscreen)
			setfullscreen(c, 1);
		else if (c != sel && c->isfullscreen)
			setfullscreen(c, 0); /* wantfullscreen stays set */
	}
	syncing = 0;
}

/* Column layout across a restart.
 *
 * seamless_restart's client word is full of flags for patches this build does
 * not have, so rather than squat on those bits the scroller keeps its own
 * property. Widths are stored as hundredths, which is finer than anything the
 * keys can produce.
 *
 * |0000000|00000000|0|00000000
 * |       |        | |-- col
 * |       |        |-- wantfullscreen
 * |       |-- cwfact * 100
 * |-- oldcwfact * 100
 */
void
setclientscroller(Client *c)
{
	uint32_t data[] = {
		(c->col & 0xFF)
		| (c->wantfullscreen & 0x1) << 8
		| ((uint32_t)(c->cwfact * 100) & 0xFF) << 9
		| ((uint32_t)(c->oldcwfact * 100) & 0xFF) << 17
	};

	XChangeProperty(dpy, c->win, clientatom[ClientScroller], XA_CARDINAL, 32,
		PropModeReplace, (unsigned char *)data, 1);
}

int
getclientscroller(Client *c)
{
	Atom v = getatomprop(c, clientatom[ClientScroller], AnyPropertyType);
	unsigned int f;

	if (v == None)
		return 0;
	c->col = v & 0xFF;
	c->wantfullscreen = (v >> 8) & 0x1;
	f = (v >> 9) & 0xFF;
	c->cwfact = f ? f / 100.0 : colfacts[1];
	f = (v >> 17) & 0xFF;
	c->oldcwfact = f / 100.0;
	return 1;
}

void
focuscol(const Arg *arg)
{
	Monitor *m = selmon;
	Client *c;
	int want, ncols = colcount(m);

	if (!m->sel || ncols == 0)
		return;
	want = m->sel->col + arg->i;
	if (want < 0 || want >= ncols)
		return;
	if ((c = colclient(m, want))) {
		focus(c);
		arrange(m);
	}
}

void
focusincol(const Arg *arg)
{
	Monitor *m = selmon;
	Client *c, *prev = NULL, *target = NULL;
	int col;

	if (!m->sel)
		return;
	col = m->sel->col;

	for (c = nextcol(m->clients); c; c = nextcol(c->next)) {
		if (!ISVISIBLE(c) || c->col != col)
			continue;
		if (arg->i > 0 && prev == m->sel) { target = c; break; }
		if (arg->i < 0 && c == m->sel) { target = prev; break; }
		prev = c;
	}
	if (target) {
		focus(target);
		arrange(m);
	}
}

/* Move the focused window's whole column one place left or right by swapping
 * column indices with the neighbour. */
void
movecol(const Arg *arg)
{
	Monitor *m = selmon;
	Client *c;
	int from, to, ncols = colcount(m);

	if (!m->sel || ncols < 2)
		return;
	from = m->sel->col;
	to = from + arg->i;
	if (to < 0 || to >= ncols)
		return;

	for (c = nextcol(m->clients); c; c = nextcol(c->next)) {
		if (!ISVISIBLE(c))
			continue;
		if (c->col == from)
			c->col = to;
		else if (c->col == to)
			c->col = from;
	}
	arrange(m);
}

/* Exchange two clients' places in the client list. Order within a column is
 * client-list order, so this is what moves a window up or down a stack. */
static void
listswap(Monitor *m, Client *a, Client *b)
{
	Client **pa, **pb, *t;

	if (a == b)
		return;
	for (pa = &m->clients; *pa && *pa != a; pa = &(*pa)->next);
	for (pb = &m->clients; *pb && *pb != b; pb = &(*pb)->next);
	if (!*pa || !*pb)
		return;

	if (a->next == b) {
		a->next = b->next; b->next = a; *pa = b;
	} else if (b->next == a) {
		b->next = a->next; a->next = b; *pb = a;
	} else {
		*pa = b; *pb = a;
		t = a->next; a->next = b->next; b->next = t;
	}
}

/* arg->i > 0 consumes the next column's first window into this one;
 * arg->i < 0 expels the focused window back out into a column of its own. */
void
consumeexpel(const Arg *arg)
{
	Monitor *m = selmon;
	Client *c, *victim;
	int col, nin = 0;

	if (!m->sel || m->lt[m->sellt]->arrange != scroller)
		return;
	col = m->sel->col;

	if (arg->i > 0) {
		if (!(victim = colclient(m, col + 1)))
			return;
		victim->col = col;
	} else {
		for (c = nextcol(m->clients); c; c = nextcol(c->next))
			if (ISVISIBLE(c) && c->col == col)
				nin++;
		if (nin < 2)
			return; /* already alone in its column */
		for (c = nextcol(m->clients); c; c = nextcol(c->next))
			if (ISVISIBLE(c) && c->col > col)
				c->col++;
		m->sel->col = col + 1;
	}
	arrange(m);
}

void
moveincol(const Arg *arg)
{
	Monitor *m = selmon;
	Client *c, *prev = NULL, *target = NULL;
	int col;

	if (!m->sel || m->lt[m->sellt]->arrange != scroller)
		return;
	col = m->sel->col;

	for (c = nextcol(m->clients); c; c = nextcol(c->next)) {
		if (!ISVISIBLE(c) || c->col != col)
			continue;
		if (arg->i > 0 && prev == m->sel) { target = c; break; }
		if (arg->i < 0 && c == m->sel) { target = prev; break; }
		prev = c;
	}
	if (!target)
		return;
	listswap(m, m->sel, target);
	arrange(m);
}

/* Cycle the focused column through the width presets. The widest fills the
 * viewport, which is the "give this window everything" case. */
void
setcolfact(const Arg *arg)
{
	Monitor *m = selmon;
	Client *c;
	float cur, next;
	int col, i;

	if (!m->sel || m->lt[m->sellt]->arrange != scroller)
		return;
	col = m->sel->col;
	cur = m->sel->cwfact;

	if (arg->i >= 0) {
		next = colfacts[0];
		for (i = 0; i < (int)LENGTH(colfacts); i++)
			if (colfacts[i] > cur + 0.001) { next = colfacts[i]; break; }
	} else {
		next = colfacts[LENGTH(colfacts) - 1];
		for (i = LENGTH(colfacts) - 1; i >= 0; i--)
			if (colfacts[i] < cur - 0.001) { next = colfacts[i]; break; }
	}

	/* every window in the column shares its width */
	for (c = nextcol(m->clients); c; c = nextcol(c->next))
		if (ISVISIBLE(c) && c->col == col)
			c->cwfact = next;
	arrange(m);
}

/* Widen or narrow the focused column by hand, for when the presets are not
 * the size you wanted. */
void
setcolwidth(const Arg *arg)
{
	Monitor *m = selmon;
	Client *c;
	float f;
	int col;

	if (!m->sel || m->lt[m->sellt]->arrange != scroller)
		return;
	col = m->sel->col;
	f = m->sel->cwfact + arg->f;
	f = MAX(0.15, MIN(1.0, f));

	for (c = nextcol(m->clients); c; c = nextcol(c->next))
		if (ISVISIBLE(c) && c->col == col)
			c->cwfact = f;
	arrange(m);
}

/* Toggle the focused column between filling the whole workspace and whatever
 * width it had before. Unlike fullscreen this stays inside the layout: the bar
 * and the gaps remain, the window keeps its border, and the rest of the strip
 * is still there to scroll back to. */
void
maximizecol(const Arg *arg)
{
	Monitor *m = selmon;
	Client *c;
	int col, restore;
	float target;

	if (!m->sel || m->lt[m->sellt]->arrange != scroller)
		return;
	col = m->sel->col;
	restore = m->sel->cwfact >= 1.0 - 0.001;
	target = m->sel->oldcwfact > 0.01 ? m->sel->oldcwfact : colfacts[1];

	for (c = nextcol(m->clients); c; c = nextcol(c->next)) {
		if (!ISVISIBLE(c) || c->col != col)
			continue;
		if (restore)
			c->cwfact = target;
		else {
			c->oldcwfact = c->cwfact;
			c->cwfact = 1.0;
		}
	}
	arrange(m);
}

/* Fullscreening a window releases any other one, so they can never pile up.
 * Navigating away no longer cancels fullscreen -- restack() lifts the
 * container over it instead. */
void
scrolltogglefullscr(const Arg *arg)
{
	if (!selmon->sel)
		return;
	selmon->sel->wantfullscreen = selmon->lt[selmon->sellt]->arrange == scroller
		&& !selmon->sel->isfullscreen;
	if (selmon->sel->wantfullscreen)
		dropfullscreen(selmon, selmon->sel);
	togglefullscr(arg);
}

/* Rescue. A window can end up floating from a rule, a client request, or an
 * old drag, which puts it outside the strip entirely -- and if the usual
 * toggle happens to be grabbed by another client there is no way back. Pull
 * every visible floater into the layout at once. */
void
gathercolumns(const Arg *arg)
{
	Monitor *m = selmon;
	Client *c;
	int at;

	if (m->lt[m->sellt]->arrange != scroller)
		return;

	/* colcount() first, while the floaters are still skipped, so `at` is a
	 * column index no tiled window is using. */
	at = colcount(m);
	for (c = m->clients; c; c = c->next)
		if (ISVISIBLE(c) && c->isfloating && !c->isfullscreen) {
			c->isfloating = c->oldstate = 0;
			c->col = at++;
		}
	focus(NULL);
	arrange(m);
}

/* dwm's togglefloating, plus a column of its own when the window rejoins the
 * strip. A floating window is skipped by normalizecols(), so its index goes
 * stale while it is out and would otherwise land on top of whichever column
 * was renumbered into its place. */
void
scrolltogglefloating(const Arg *arg)
{
	Monitor *m = selmon;
	Client *c = m->sel;
	int wasfloating;

	if (!c)
		return;
	wasfloating = c->isfloating;
	togglefloating(arg);
	if (wasfloating && !c->isfloating && m->lt[m->sellt]->arrange == scroller) {
		c->col = colcount(m); /* strictly beyond every existing column */
		arrange(m);
	}
}

/* Workspaces.
 *
 * niri stacks workspaces vertically: the strip scrolls left and right, and the
 * workspaces you move between run up and down. dwm's tags are a bitmask rather
 * than a list, but the pertag patch already keeps a linear index for the single
 * tag in view -- m->pertag->curtag, 1..NUMTAGS -- and that is exactly the
 * stack. Everything below treats it as one, so h/l and j/k are two axes of the
 * same space rather than two unrelated concepts.
 *
 * Column indices come out per-workspace for free: colcount(), colclient() and
 * normalizecols() all filter on ISVISIBLE, so every workspace numbers its own
 * columns 0..n-1 and a window arriving from elsewhere only has to be handed an
 * index in the numbering it is joining.
 */

/* One past the last workspace holding anything. niri creates and discards
 * workspaces as you use them, always keeping one empty at the bottom; a fixed
 * bitmask cannot grow, but stopping navigation here gives the same feel -- you
 * can always step down into a fresh workspace, and you never wade through eight
 * empty ones on the way back up. */
int
lastworkspace(Monitor *m)
{
	Client *c;
	int i, last = 0;

	for (c = m->clients; c; c = c->next)
		for (i = NUMTAGS; i > last; i--)
			if (c->tags & (1 << (i - 1))) {
				last = i;
				break;
			}
	if (!last)
		return 1; /* nothing anywhere: one workspace, and it is empty */
	return MIN(last + 1, NUMTAGS);
}

/* How many workspaces the bar draws: everything reachable by j/k, plus the one
 * in view if you jumped past the end of that by number. The rest do not exist
 * yet as far as niri is concerned, and drawing nine boxes when two are in use
 * says otherwise. */
int
shownworkspaces(Monitor *m)
{
	return MAX(lastworkspace(m), (int)m->pertag->curtag);
}

/* Resolve a direction into a workspace index, or 0 when there is nowhere to go.
 * Navigation clamps rather than wraps: the stack has a top and a bottom, and
 * falling off one end into the other loses you your place. */
static int
workspacetarget(Monitor *m, int dir)
{
	int cur = m->pertag->curtag, want;

	if (cur < 1)
		cur = 1; /* the view-all pseudo-tag; treat it as the first */
	want = cur + dir;
	if (want < 1 || want > MAX(lastworkspace(m), cur))
		return 0;
	return want;
}

void
focusworkspace(const Arg *arg)
{
	int t = workspacetarget(selmon, arg->i);

	if (t)
		view(&((Arg) { .ui = 1 << (t - 1) }));
}

/* Move the focused window -- or its whole column -- to workspace t, and follow
 * it there.
 *
 * It lands at the same column index it held on the workspace it left, clamped
 * to the end of the destination strip. Push a window down and it stays roughly
 * under your eye instead of reappearing at some far edge, which is the whole
 * point of the two axes lining up.
 */
static void
sendtoworkspace(int t, int wholecol)
{
	Monitor *m = selmon;
	Client *c, *sel = m->sel;
	unsigned int tags;
	int srccol, want, cols[64], n = 0, i, j, tmp;

	if (!sel || t < 1 || t > NUMTAGS)
		return;
	tags = 1 << (t - 1);
	if (sel->tags == tags)
		return;

	/* A floating window has no column to preserve; it just changes hands. */
	if (sel->isfloating && !sel->isfullscreen) {
		sel->tags = tags;
		sel->switchtag = 0;
		view(&((Arg) { .ui = tags }));
		focus(sel);
		arrange(m);
		return;
	}
	srccol = sel->col;

	/* Distinct column indices already on the destination. It is not the
	 * workspace in view, so normalizecols() has not touched it and these may
	 * be anything at all -- only their order carries meaning. */
	for (c = m->clients; c; c = c->next) {
		if (!(c->tags & tags) || (c->isfloating && !c->isfullscreen))
			continue;
		for (i = 0; i < n && cols[i] != c->col; i++);
		if (i == n && n < (int)LENGTH(cols))
			cols[n++] = c->col;
	}
	for (i = 0; i < n; i++)
		for (j = i + 1; j < n; j++)
			if (cols[j] < cols[i]) { tmp = cols[i]; cols[i] = cols[j]; cols[j] = tmp; }

	want = MIN(srccol, n);

	/* Compact the destination to 0..n-1 and open a gap at `want`. Without the
	 * gap the arrival would share an index with a column that is already
	 * there, and normalizecols() would read that as one column holding two
	 * windows -- a merge, not a move. Two passes through negative values, as
	 * in normalizecols(), so renumbering cannot collide with an index that
	 * has not been visited yet. */
	for (c = m->clients; c; c = c->next) {
		if (!(c->tags & tags) || (c->isfloating && !c->isfullscreen))
			continue;
		for (i = 0; i < n; i++)
			if (cols[i] == c->col) {
				c->col = -((i < want ? i : i + 1) + 1);
				break;
			}
	}
	for (c = m->clients; c; c = c->next)
		if ((c->tags & tags) && c->col < 0)
			c->col = -c->col - 1;

	/* Take the window, or everything stacked in its column with it. Tested
	 * against the source workspace, which is still the one in view. */
	for (c = m->clients; c; c = c->next) {
		if (!ISVISIBLE(c) || (c->isfloating && !c->isfullscreen))
			continue;
		if (c != sel && !(wholecol && c->col == srccol))
			continue;
		c->tags = tags;
		c->switchtag = 0;
		c->col = want;
	}

	/* The gap left behind on the source closes when it is next arranged. */
	view(&((Arg) { .ui = tags }));
	focus(sel);
	arrange(m);
}

void
movetoworkspace(const Arg *arg)
{
	sendtoworkspace(workspacetarget(selmon, arg->i), 0);
}

void
movecoltoworkspace(const Arg *arg)
{
	sendtoworkspace(workspacetarget(selmon, arg->i), 1);
}

/* dwm's tag(), but landing where the directional keys would put it and taking
 * you along, so that Shift+2 and Shift+j differ only in where they stop. */
void
tagworkspace(const Arg *arg)
{
	int i;

	if (!(arg->ui & TAGMASK))
		return;
	for (i = 0; !(arg->ui & 1 << i); i++);
	sendtoworkspace(i + 1, 0);
}

/* Lay out an arbitrary workspace the way scroller() would, in strip
 * coordinates: x from the left end of the strip, y from the top of the work
 * area, with the scroll offset left out.
 *
 * The windows are not touched, and deliberately not consulted either. A
 * workspace that is not in view has stale geometry -- showhide() only shoves
 * its windows aside, and a window moved there has not been arranged since --
 * so anything wanting to draw a workspace it is not looking at has to work the
 * layout out rather than read it off the clients.
 *
 * Pass max = 0 to measure a workspace without listing it.
 */
int
stripgeom(Monitor *m, unsigned int tags, StripItem *out, int max, int *total)
{
	Client *c;
	unsigned int dummy;
	int cols[64], ncols = 0, n = 0, i, j, t;
	int oh, ov, ih, iv, x = 0, y, cw, nin, hh, rest;
	float f;

	getgaps(m, &oh, &ov, &ih, &iv, &dummy);
	*total = 0;

	for (c = m->clients; c; c = c->next) {
		if (!ONSTRIP(c, tags))
			continue;
		for (i = 0; i < ncols && cols[i] != c->col; i++);
		if (i == ncols && ncols < (int)LENGTH(cols))
			cols[ncols++] = c->col;
	}
	for (i = 0; i < ncols; i++)
		for (j = i + 1; j < ncols; j++)
			if (cols[j] < cols[i]) { t = cols[i]; cols[i] = cols[j]; cols[j] = t; }

	for (i = 0; i < ncols; i++) {
		f = 0;
		for (c = m->clients; c; c = c->next)
			if (ONSTRIP(c, tags) && c->col == cols[i]) { f = c->cwfact; break; }
		cw = MAX((int)((m->ww - 2 * ov) * (f > 0 ? f : colfacts[1])), bh);

		for (nin = 0, c = m->clients; c; c = c->next)
			if (ONSTRIP(c, tags) && c->col == cols[i] && !c->isfullscreen)
				nin++;

		if (nin) {
			hh = (m->wh - 2 * oh - ih * (nin - 1)) / nin;
			rest = (m->wh - 2 * oh - ih * (nin - 1)) - hh * nin;
			y = 0;
			for (c = m->clients; c; c = c->next) {
				if (!ONSTRIP(c, tags) || c->col != cols[i] || c->isfullscreen)
					continue;
				if (n < max) {
					out[n].c = c;
					out[n].x = x;
					out[n].y = y;
					out[n].w = cw;
					out[n].h = hh + (rest > 0 ? 1 : 0);
					n++;
				}
				y += hh + (rest > 0 ? 1 : 0) + ih;
				rest--;
			}
		}
		x += cw + iv;
	}
	*total = x > 0 ? x - iv : 0;
	return n;
}

/* Layout-aware wrappers, so one keymap serves both the scroller and the
 * ordinary layouts. */
void
scrollfocus(const Arg *arg)
{
	Arg f;

	if (selmon->lt[selmon->sellt]->arrange == scroller)
		focuscol(arg);
	else {
		f.f = arg->i < 0 ? -0.05 : +0.05;
		setmfact(&f);
	}
}

void
scrollfocusstack(const Arg *arg)
{
	if (selmon->lt[selmon->sellt]->arrange == scroller)
		focusincol(arg);
	else
		focusstack(arg);
}

void
scrollmove(const Arg *arg)
{
	if (selmon->lt[selmon->sellt]->arrange == scroller)
		movecol(arg);
}
