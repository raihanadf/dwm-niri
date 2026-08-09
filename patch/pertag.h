/* Defined here rather than in pertag.c so that the scroller, which is included
 * first and reads curtag as its workspace index, sees a complete type. */
struct Pertag {
	unsigned int curtag, prevtag; /* current and previous tag */
	int nmasters[NUMTAGS + 1]; /* number of windows in master area */
	const Layout *ltidxs[NUMTAGS + 1][2]; /* matrix of tags and layouts indexes  */
	float mfacts[NUMTAGS + 1]; /* mfacts per tag */
	unsigned int sellts[NUMTAGS + 1]; /* selected layouts */
	Client *prevzooms[NUMTAGS + 1]; /* store zoom information */
	int enablegaps[NUMTAGS + 1];
	unsigned int gaps[NUMTAGS + 1];
	int scrollxs[NUMTAGS + 1]; /* scroller: strip offset, per workspace */
};

static void pertagview(const Arg *arg);

