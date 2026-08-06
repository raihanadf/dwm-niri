static void destroycontainer(Monitor *m);
static void detachcontainer(Client *c);
static void reparentclient(Client *c, Window parent, int x, int y);
static int iscontained(Client *c);
static int mappedwin(Window w);
static Bool isunmapof(Display *d, XEvent *e, XPointer arg);
static int hasfullscreen(Monitor *m);
static void updatecontainer(Monitor *m);
static void updateclientparent(Client *c);

/* c->x and c->y are always root coordinates; a client parented into a
 * container is placed relative to that container instead. */
#define CONTAINERX(C, X) ((C)->parent == root ? (X) : (X) - (C)->mon->wx)
#define CONTAINERY(C, Y) ((C)->parent == root ? (Y) : (Y) - (C)->mon->wy)
