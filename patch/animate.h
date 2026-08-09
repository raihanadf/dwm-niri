#include <time.h>
#include <sys/select.h>

static void animplace(Client *c, int x, int y);
static void animfrom(Client *c, int *x, int *y);
static void animsnapshot(Monitor *m);
static void animlaunch(Monitor *m);
static int animstep(void);
static void animforget(Client *c);
static int animoverride(Client *c, int *x, int *y);

/* A window on its way from where it was to where the layout has put it. */
typedef struct {
	Client *c;
	int fx, fy; /* where the motion started */
	int tx, ty; /* where the layout says it belongs */
	int cx, cy; /* where it is this frame */
} Anim;
