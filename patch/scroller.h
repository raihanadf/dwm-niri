static void scroller(Monitor *m);
static void normalizecols(Monitor *m);
static Client *nextcol(Client *c);
static int colcount(Monitor *m);
static int colwidthof(Monitor *m, int col);
static int colx(Monitor *m, int col);
static void setcolfact(const Arg *arg);
static void setcolwidth(const Arg *arg);
static void maximizecol(const Arg *arg);
static void scrolltogglefullscr(const Arg *arg);
static void syncfullscreen(Monitor *m, Client *sel);
static void gathercolumns(const Arg *arg);
static void scrolltogglefloating(const Arg *arg);
static void dropfullscreen(Monitor *m, Client *keep);
static void scrolltocol(Monitor *m, int col);
static Client *colclient(Monitor *m, int col);
static void insertcolumn(Client *c);
static void setclientscroller(Client *c);
static int getclientscroller(Client *c);
static void focuscol(const Arg *arg);
static Client *colneighbour(Monitor *m, int dir);
static void focusincol(const Arg *arg);
static void movecol(const Arg *arg);
static void listswap(Monitor *m, Client *a, Client *b);
static void consumeexpel(const Arg *arg);
static void moveincol(const Arg *arg);
/* A window on the strip, in strip coordinates. */
typedef struct {
	Client *c;
	int x, y, w, h;
} StripItem;

/* Members of a workspace's strip: on `tags`, and not floating unless the
 * floating flag is only there because the window is fullscreen. */
#define ONSTRIP(C, TAGS) (((C)->tags & (TAGS)) \
	&& !((C)->isfloating && !(C)->isfullscreen))

static int lastworkspace(Monitor *m);
static int shownworkspaces(Monitor *m);
static int stripgeom(Monitor *m, unsigned int tags, StripItem *out, int max, int *total);
static int workspacetarget(Monitor *m, int dir);
static void focusworkspace(const Arg *arg);
static void sendtoworkspace(int t, int wholecol);
static void movetoworkspace(const Arg *arg);
static void movecoltoworkspace(const Arg *arg);
static void tagworkspace(const Arg *arg);
static void scrollfocus(const Arg *arg);
static void scrollfocusstack(const Arg *arg);
static void scrollmove(const Arg *arg);
