#include <X11/extensions/Xcomposite.h>
#include <X11/extensions/Xrender.h>

static int compositeok(void);
static void redirectcontainer(Monitor *m);
static void drawthumb(Picture dst, Window win, int dx, int dy, int dw, int dh);
static void drawoverview(Monitor *m);
static void hideoverview(void);
static void toggleoverview(const Arg *arg);

static Window overviewwin = None;
static int overviewshown = 0;
static int havecomposite = 0;
