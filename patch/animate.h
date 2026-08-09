#include <time.h>
#include <sys/select.h>

static void animplace(Client *c, int x, int y);
static void animfrom(Client *c, int *x, int *y);
static void animsnapshot(Monitor *m);
static void animlaunch(Monitor *m);
static int animstep(void);
static int animskip(void);
static void animpark(Client *c);
static void animforget(Client *c);
static int animoverride(Client *c, int *x, int *y);
static void animslidecapture(Monitor *m);
static void animslidego(Monitor *m, int dir);
static int animoutgoing(Client *c, int *x, int *y);

/* What a window is doing this movement. MOVE is the ordinary case: it stays on
 * screen and travels. IN and OUT only happen when the workspace changes -- one
 * set rises into view while the other leaves the way it came. */
enum { AnimMove, AnimIn, AnimOut };

/* A window on its way from where it was to where the layout has put it. */
typedef struct {
	Client *c;
	int kind;
	int fx, fy; /* where the motion started */
	int tx, ty; /* where the layout says it belongs */
	int cx, cy; /* where it is this frame */
} Anim;
