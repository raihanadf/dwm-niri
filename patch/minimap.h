static void mmfill(int x, int y, int w, int h, Clr c);
static void minimapgeom(Monitor *m, int *x, int *y, int *w, int *h);
static void drawminimap(Monitor *m);
static void hideminimap(void);
static void toggleminimap(const Arg *arg);

static Window minimapwin = None;
static int minimapshown = 0;
