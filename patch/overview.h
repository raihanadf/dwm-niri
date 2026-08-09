#include <X11/extensions/Xcomposite.h>
#include <X11/extensions/Xrender.h>

/* Where a thumbnail, and where a workspace's row, ended up on the overlay --
 * kept so a click can be resolved back to what was drawn there. */
typedef struct {
	Client *c;
	int ws, x, y, w, h;
} OvSlot;

typedef struct {
	int ws, y, h;
} OvRow;

static int compositeok(void);
static int overviewclick(XButtonPressedEvent *ev);
static void redirectcontainer(Monitor *m);
static void drawthumb(Picture dst, Window win, int dx, int dy, int dw, int dh);
static void drawoverview(Monitor *m);
static void hideoverview(void);
static void toggleoverview(const Arg *arg);

static Window overviewwin = None;
static int overviewshown = 0;
static int havecomposite = 0;
