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
	for (i = 0; i < nanim; i++)
		if (anims[i].c == c) {
			*x = anims[i].cx;
			*y = anims[i].cy;
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
			animfrom(c, &next[n].fx, &next[n].fy);
			next[n].cx = next[n].fx;
			next[n].cy = next[n].fy;
			n++;
		}
	}

	/* Anything that was moving and is not in the new set lands where the
	 * layout already believes it is, rather than freezing part-way. */
	for (i = 0; i < nanim; i++) {
		for (j = 0; j < n && next[j].c != anims[i].c; j++);
		if (j == n)
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
		/* Left the strip while the layout ran: showhide() has already put it
		 * wherever it belongs, and it is not ours to move. */
		if (!ISVISIBLE(c) || c->isfloating || c->isfullscreen)
			continue;
		anims[i].tx = c->x;
		anims[i].ty = c->y;
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
		/* e == 1 put everything exactly on its target already */
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
