/* Artificial smooth motion.
 *
 * There is no compositor here, so nothing is being interpolated on our behalf:
 * the animation is a timer that walks the windows themselves from where they
 * were to where the layout has decided they go. dwm's model is left alone --
 * c->x and c->y hold the real, final geometry from the moment the layout runs,
 * and only the X windows lag behind for a few frames. Anything that reads a
 * client's position while it glides sees the truth, not the frame.
 *
 * Position only, deliberately. Every intermediate size would reach the client
 * as a real ConfigureNotify, and a terminal asked to reflow ninety times a
 * second is a bad price for a prettier resize. Sizes land at once; positions
 * glide. In a scrolling layout almost all the motion is translation anyway --
 * columns keep their width and slide -- so this is very nearly free.
 *
 * Retargeting rather than queueing: a new layout while one is in flight starts
 * a fresh motion from wherever the windows have got to. Holding a key down
 * tracks the strip continuously instead of playing a backlog of animations.
 */

#define MAXANIM 96

static Anim anims[MAXANIM];
static int nanim = 0;
static int animrunning = 0;
static int animpending = 0; /* between the snapshot and the launch */
static struct timespec animt0;

/* Workspace changes need to know who was on screen a moment ago, because by the
 * time the layout runs the tag has already changed and the old set is no longer
 * visible to anything that asks. */
static struct { Client *c; int x, y; } animprev[MAXANIM];
static int nanimprev = 0;
static int slidedir = 0;         /* +1 leaving downwards, -1 upwards, 0 not a slide */
static Monitor *slidemon = NULL;

/* Put a window away where showhide() would have: off to the side, still mapped,
 * costing nothing until its workspace comes back. */
static void
animpark(Client *c)
{
	XMoveWindow(dpy, c->win, WIDTH(c) * -2, CONTAINERY(c, c->y));
}

static long
animelapsed(void)
{
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	return (now.tv_sec - animt0.tv_sec) * 1000
		+ (now.tv_nsec - animt0.tv_nsec) / 1000000;
}

/* Talk to X without touching the client: this is the one place a window is
 * somewhere its Client says it is not. */
static void
animplace(Client *c, int x, int y)
{
	XMoveWindow(dpy, c->win, CONTAINERX(c, x), CONTAINERY(c, y));
}

/* Where the window is on screen right now -- mid-flight if it is already
 * moving, otherwise wherever the layout last left it. */
static void
animfrom(Client *c, int *x, int *y)
{
	int i;

	for (i = 0; i < nanim; i++)
		if (anims[i].c == c) {
			*x = anims[i].cx;
			*y = anims[i].cy;
			return;
		}
	*x = c->x;
	*y = c->y;
}

/* While the layout is running it asks X to put each window at its destination.
 * The animation is about to walk it there from where it already is, so answer
 * with the position it should really be given this instant -- otherwise it is
 * drawn at the destination and yanked back a millisecond later, which is a
 * visible flinch at the start of every movement. */
int
animoverride(Client *c, int *x, int *y)
{
	int i;

	if (!animpending)
		return 0;
	for (i = 0; i < nanim; i++) {
		if (anims[i].c != c)
			continue;
		if (anims[i].kind == AnimIn) {
			/* resizeclient() has already written the destination into the
			 * client, so this is the arriving window's starting point: a
			 * screen away, on the side we are coming from. */
			*x = c->x;
			*y = c->y + slidedir * slidemon->wh;
		} else {
			*x = anims[i].cx;
			*y = anims[i].cy;
		}
		return 1;
	}
	return 0;
}

/* showhide() is about to park a window that has left the workspace. If it is
 * sliding out, it needs to stay where the animation has it until the movement
 * finishes -- parking it now would make the old workspace vanish rather than
 * leave. */
int
animoutgoing(Client *c, int *x, int *y)
{
	int i;

	if (!animrunning && !animpending)
		return 0;
	for (i = 0; i < nanim; i++)
		if (anims[i].c == c && anims[i].kind == AnimOut) {
			*x = anims[i].cx;
			*y = anims[i].cy;
			return 1;
		}
	return 0;
}

/* Remember where everything is while the outgoing workspace is still the one on
 * screen. Called before the tag changes; animslidego() decides afterwards
 * whether it was a movement worth animating. */
void
animslidecapture(Monitor *m)
{
	Client *c;

	nanimprev = 0;
	slidedir = 0;
	slidemon = m;
	if (animskip())
		return;
	for (c = m->clients; c; c = c->next) {
		if (!ISVISIBLE(c) || c->isfloating || c->isfullscreen)
			continue;
		if (nanimprev >= MAXANIM)
			break;
		animprev[nanimprev].c = c;
		animfrom(c, &animprev[nanimprev].x, &animprev[nanimprev].y);
		nanimprev++;
	}
}

void
animslidego(Monitor *m, int dir)
{
	slidedir = dir > 0 ? 1 : dir < 0 ? -1 : 0;
	slidemon = m;
	if (!slidedir)
		nanimprev = 0;
}

static int
wasonscreen(Client *c, int *x, int *y)
{
	int i;

	for (i = 0; i < nanimprev; i++)
		if (animprev[i].c == c) {
			*x = animprev[i].x;
			*y = animprev[i].y;
			return 1;
		}
	return 0;
}

static int
animskip(void)
{
	/* Not during startup, when windows are being adopted rather than moved,
	 * and not under the overview, which is drawing the strip from the
	 * clients' real geometry and would show them lagging. */
	return !animated || scanning || overviewshown;
}

/* Record where everything is, before the layout moves it. */
void
animsnapshot(Monitor *m)
{
	Anim next[MAXANIM];
	Monitor *mm;
	Client *c;
	int n = 0, i, j;

	if (animskip())
		return;

	for (mm = mons; mm; mm = mm->next) {
		if (m && mm != m)
			continue;
		for (c = mm->clients; c; c = c->next) {
			if (!ISVISIBLE(c) || c->isfloating || c->isfullscreen)
				continue;
			if (n >= MAXANIM)
				break;
			next[n].c = c;
			next[n].kind = AnimMove;
			if (slidedir && !wasonscreen(c, &next[n].fx, &next[n].fy)) {
				/* not here a moment ago: it is arriving with the workspace */
				next[n].kind = AnimIn;
				next[n].fx = next[n].fy = 0; /* set once the layout has run */
			} else
				animfrom(c, &next[n].fx, &next[n].fy);
			next[n].cx = next[n].fx;
			next[n].cy = next[n].fy;
			n++;
		}
	}

	/* Anything already on its way out keeps going. A layout can run again
	 * mid-slide -- a focus change alone will do it -- and dropping these would
	 * park them half-way, so the old workspace would vanish rather than
	 * finish leaving. They resume from where they have got to. */
	for (i = 0; i < nanim && n < MAXANIM; i++) {
		if (anims[i].kind != AnimOut)
			continue;
		for (j = 0; j < n && next[j].c != anims[i].c; j++);
		if (j < n)
			continue;
		next[n] = anims[i];
		next[n].fx = anims[i].cx;
		next[n].fy = anims[i].cy;
		n++;
	}

	/* Whoever was on screen and is not any more leaves the way we came,
	 * instead of blinking out. showhide() has already been told to leave them
	 * alone; animstep() parks them when they are off the edge. */
	if (slidedir)
		for (i = 0; i < nanimprev && n < MAXANIM; i++) {
			c = animprev[i].c;
			for (j = 0; j < n && next[j].c != c; j++);
			if (j < n || !c->mon || (m && c->mon != m))
				continue;
			next[n].c = c;
			next[n].kind = AnimOut;
			next[n].fx = next[n].cx = animprev[i].x;
			next[n].fy = next[n].cy = animprev[i].y;
			next[n].tx = animprev[i].x;
			next[n].ty = animprev[i].y - slidedir * c->mon->wh;
			n++;
		}

	/* Anything that was moving and is not in the new set lands where it
	 * belongs rather than freezing part-way -- parked if it had already left
	 * the workspace, since putting it back on screen would strand it there. */
	for (i = 0; i < nanim; i++) {
		for (j = 0; j < n && next[j].c != anims[i].c; j++);
		if (j < n)
			continue;
		if (anims[i].kind == AnimOut || !ISVISIBLE(anims[i].c))
			animpark(anims[i].c);
		else
			animplace(anims[i].c, anims[i].c->x, anims[i].c->y);
	}

	memcpy(anims, next, n * sizeof *anims);
	nanim = n;
	animpending = 1;
}

/* The layout has run and the windows are already at their destinations. Work
 * out which of them actually travelled, put those back where they started, and
 * start the clock. */
void
animlaunch(Monitor *m)
{
	int i, keep = 0;
	Client *c;

	animpending = 0;
	if (animskip()) {
		nanim = 0;
		animrunning = 0;
		return;
	}

	for (i = 0; i < nanim; i++) {
		c = anims[i].c;
		if (anims[i].kind == AnimOut) {
			/* target was fixed at snapshot time: a screen away, upwind */
			anims[keep++] = anims[i];
			continue;
		}
		/* Left the strip while the layout ran: showhide() has already put it
		 * wherever it belongs, and it is not ours to move. */
		if (!ISVISIBLE(c) || c->isfloating || c->isfullscreen)
			continue;
		anims[i].tx = c->x;
		anims[i].ty = c->y;
		if (anims[i].kind == AnimIn) {
			anims[i].fx = c->x;
			anims[i].fy = c->y + slidedir * c->mon->wh;
			anims[i].cx = anims[i].fx;
			anims[i].cy = anims[i].fy;
			anims[keep++] = anims[i];
			continue;
		}
		/* A window that barely moved is not worth a frame of anyone's time.
		 * It was held back at its old position while the layout ran, so put
		 * it where it actually belongs before dropping it. */
		if (abs(anims[i].tx - anims[i].fx) < animminpx
		&& abs(anims[i].ty - anims[i].fy) < animminpx) {
			animplace(c, c->x, c->y);
			continue;
		}
		anims[keep++] = anims[i];
	}
	nanim = keep;
	nanimprev = 0;
	/* The slide is decided: every arriving window has its starting point and
	 * every leaving one its destination. Clear the direction now, or the next
	 * layout to run -- and a focus change alone will run one -- would read the
	 * whole workspace as arriving all over again and restart the movement. */
	slidedir = 0;

	if (!nanim) {
		animrunning = 0;
		return;
	}
	/* No need to place anything: animoverride() kept every one of them at its
	 * starting position throughout the layout. */
	clock_gettime(CLOCK_MONOTONIC, &animt0);
	animrunning = 1;
}

/* One frame. Returns whether there is still motion to draw, which is what the
 * event loop uses to decide between a frame deadline and an idle wait. */
int
animstep(void)
{
	double t, e;
	int i;

	if (!animrunning)
		return 0;

	t = animduration ? (double)animelapsed() / animduration : 1.0;
	if (t >= 1.0)
		t = 1.0;
	/* Ease out: quick to leave, gentle to arrive. The eye reads the start of
	 * a movement as the response and the end as the result, so the response
	 * wants to be immediate and the result wants to settle. */
	e = 1.0 - (1.0 - t) * (1.0 - t) * (1.0 - t);

	for (i = 0; i < nanim; i++) {
		anims[i].cx = anims[i].fx + (int)((anims[i].tx - anims[i].fx) * e);
		anims[i].cy = anims[i].fy + (int)((anims[i].ty - anims[i].fy) * e);
		animplace(anims[i].c, anims[i].cx, anims[i].cy);
	}

	if (t >= 1.0) {
		/* e == 1 put everything exactly on its target already. The ones that
		 * left are now a screen off the edge; park them properly so they cost
		 * nothing while their workspace is away. */
		for (i = 0; i < nanim; i++)
			if (anims[i].kind == AnimOut)
				animpark(anims[i].c);
		animrunning = 0;
		nanim = 0;
	}
	return animrunning;
}

/* A client can be destroyed mid-flight, and the table holds pointers. */
void
animforget(Client *c)
{
	int i, keep = 0;

	for (i = 0; i < nanim; i++)
		if (anims[i].c != c)
			anims[keep++] = anims[i];
	nanim = keep;
	if (!nanim)
		animrunning = 0;
}
