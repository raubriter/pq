#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/select.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/keysym.h>
#include "config.h"

#define MAXIN 8
#define RECORD "record.txt"

enum { RUN, DONE };

static Display *dpy;
static Window win;
static GC gc;
static XFontStruct *fontc;
static XFontStruct *fontt;
static int sw, sh, running = 1, state = RUN;
static time_t start, end;
static int best = -1, pen;
static unsigned long red;

static const int tables[] = TABLES;
static int ntables;
static int ncomb;
static int *ca;
static int *cb;
static int *pool;
static int np, pos;

static char in[MAXIN + 1];
static int inlen, wrong;

static void
load(void)
{
	FILE *f = fopen(RECORD, "r");

	if (f) {
		if (fscanf(f, "%d", &best) != 1)
			best = -1;
		fclose(f);
	}
}

static void
save(int s)
{
	FILE *f = fopen(RECORD, "w");

	if (f) {
		fprintf(f, "%d\n", s);
		fclose(f);
	}
}

static void
fmt(char *b, int s)
{
	if (s < 0)
		s = 0;
	snprintf(b, 16, "%02d:%02d:%02d",
	         s / 3600, (s % 3600) / 60, s % 60);
}

static int
digit(KeySym k)
{
	if (k >= XK_0 && k <= XK_9)
		return k - XK_0;
	if (k >= XK_KP_0 && k <= XK_KP_9)
		return k - XK_KP_0;
	return -1;
}

static XFontStruct *
loadfont(int size)
{
	char pat[64];
	XFontStruct *f;

	snprintf(pat, sizeof(pat),
	         "-*-*-bold-r-*-*-%d-*-*-*-*-*-*-*", size);
	f = XLoadQueryFont(dpy, pat);
	if (!f)
		f = XLoadQueryFont(dpy, "fixed");
	return f;
}

static void
draw_timer(void)
{
	int sc = DefaultScreen(dpy);
	time_t now = end > 0 ? end : time(NULL);
	char b[16], r[32];
	int h, w, x, rw, rx;

	XSetFont(dpy, gc, fontt->fid);
	fmt(b, (int)(now - start) + pen);
	h = fontt->ascent + fontt->descent + 16;
	w = XTextWidth(fontt, b, strlen(b)) + 16;
	x = sw - w - 20;
	XSetForeground(dpy, gc, BlackPixel(dpy, sc));
	XFillRectangle(dpy, win, gc, x, 20, w, h);
	XSetForeground(dpy, gc, WhitePixel(dpy, sc));
	XDrawString(dpy, win, gc, x + 8, 28 + fontt->ascent, b, strlen(b));
	if (best < 0)
		return;
	fmt(b, best);
	snprintf(r, sizeof(r), "recorde: %s", b);
	rw = XTextWidth(fontt, r, strlen(r));
	rx = x - 20 - rw;
	XSetForeground(dpy, gc, BlackPixel(dpy, sc));
	XDrawString(dpy, win, gc, rx, 28 + fontt->ascent, r, strlen(r));
}

static void
draw(void)
{
	int sc = DefaultScreen(dpy);
	int i, a, b, qw, x, tw;
	char q[32];

	XSetFont(dpy, gc, fontc->fid);
	XSetForeground(dpy, gc, WhitePixel(dpy, sc));
	XFillRectangle(dpy, win, gc, 0, 0, sw, sh);
	XSetForeground(dpy, gc, BlackPixel(dpy, sc));
	if (state == DONE) {
		tw = XTextWidth(fontc, "FIM", 3);
		XDrawString(dpy, win, gc, (sw - tw) / 2, sh / 2, "FIM", 3);
	} else {
		i = pool[pos];
		a = ca[i];
		b = cb[i];
		snprintf(q, sizeof(q), "%d x %d = ", a, b);
		qw = XTextWidth(fontc, q, strlen(q));
		x = (sw - qw - XTextWidth(fontc, in, inlen)) / 2;
		XDrawString(dpy, win, gc, x, sh / 2, q, strlen(q));
		XSetForeground(dpy, gc, wrong ? red : BlackPixel(dpy, sc));
		XDrawString(dpy, win, gc, x + qw, sh / 2, in, inlen);
		XSetForeground(dpy, gc, BlackPixel(dpy, sc));
		XDrawString(dpy, win, gc,
		            x + qw + XTextWidth(fontc, in, inlen),
		            sh / 2, "_", 1);
	}
	draw_timer();
}

static void
enter(void)
{
	int i = pool[pos], s;

	if (inlen > 0 && atoi(in) == ca[i] * cb[i]) {
		pool[pos] = pool[--np];
		if (!np) {
			end = time(NULL);
			s = (int)(end - start) + pen;
			if (best < 0 || s < best) {
				best = s;
				save(s);
			}
			state = DONE;
		} else if (pos >= np) {
			pos = 0;
		}
		in[0] = 0;
		inlen = 0;
		wrong = 0;
	} else {
		wrong = 1;
		pen += PENALTY;
	}
}

static void
skip(void)
{
	pos = (pos + 1) % np;
	in[0] = 0;
	inlen = 0;
	wrong = 0;
}

static void
initcomb(void)
{
	int i, j, k = 0, d, t;

	ntables = sizeof(tables) / sizeof(tables[0]);
	ncomb = MIRRORED ? ntables * ntables
	                 : ntables * (ntables + 1) / 2;
	ca = malloc(ncomb * sizeof(int));
	cb = malloc(ncomb * sizeof(int));
	pool = malloc(ncomb * sizeof(int));
	for (i = 0; i < ntables; i++) {
		for (j = 0; j < ntables; j++) {
			if (!MIRRORED && i > j)
				continue;
			ca[k] = tables[i];
			cb[k] = tables[j];
			k++;
		}
	}
	np = ncomb;
	for (i = 0; i < ncomb; i++)
		pool[i] = i;
	for (i = ncomb - 1; i > 0; i--) {
		d = rand() % (i + 1);
		t = pool[i];
		pool[i] = pool[d];
		pool[d] = t;
	}
}

int
main(void)
{
	int sc, xfd, d;
	KeySym k;
	XEvent ev;
	XColor c;
	Colormap cm;
	XSetWindowAttributes swa;
	time_t last = 0;
	fd_set fds;
	struct timeval tv;

	srand(time(NULL));
	load();
	initcomb();

	dpy = XOpenDisplay(NULL);
	if (!dpy)
		return 1;
	sc = DefaultScreen(dpy);
	sw = DisplayWidth(dpy, sc);
	sh = DisplayHeight(dpy, sc);

	cm = DefaultColormap(dpy, sc);
	if (XParseColor(dpy, cm, "red", &c) && XAllocColor(dpy, cm, &c))
		red = c.pixel;
	else
		red = BlackPixel(dpy, sc);

	swa.override_redirect = True;
	swa.background_pixel = WhitePixel(dpy, sc);
	swa.border_pixel = BlackPixel(dpy, sc);
	win = XCreateWindow(dpy, RootWindow(dpy, sc), 0, 0, sw, sh, 0,
	                    CopyFromParent, InputOutput, CopyFromParent,
	                    CWOverrideRedirect | CWBackPixel | CWBorderPixel,
	                    &swa);
	XSelectInput(dpy, win, ExposureMask | KeyPressMask | StructureNotifyMask);
	XMapRaised(dpy, win);
	XGrabKeyboard(dpy, win, True, GrabModeAsync, GrabModeAsync, CurrentTime);

	gc = XCreateGC(dpy, win, 0, NULL);
	fontc = loadfont(FONTSIZE_CONTENT);
	fontt = loadfont(FONTSIZE_TIMER);
	if (!fontc || !fontt)
		return 1;

	start = time(NULL);
	draw();

	xfd = ConnectionNumber(dpy);
	while (running) {
		while (XPending(dpy)) {
			XNextEvent(dpy, &ev);
			if (ev.type == Expose) {
				draw();
				continue;
			}
			if (ev.type != KeyPress)
				continue;
			k = XLookupKeysym(&ev.xkey, 0);
			if (k == XK_Escape) {
				running = 0;
				continue;
			}
			if (state == DONE)
				continue;
			if (k == XK_Return || k == XK_KP_Enter) {
				enter();
			} else if (k == XK_space) {
				skip();
			} else if (k == XK_BackSpace) {
				if (inlen)
					in[--inlen] = 0;
				wrong = 0;
			} else {
				d = digit(k);
				if (d < 0 || inlen >= MAXIN)
					continue;
				in[inlen++] = '0' + d;
				in[inlen] = 0;
				wrong = 0;
			}
			draw();
		}
		if (time(NULL) != last) {
			draw_timer();
			last = time(NULL);
		}
		tv.tv_sec = 1;
		tv.tv_usec = 0;
		FD_ZERO(&fds);
		FD_SET(xfd, &fds);
		select(xfd + 1, &fds, NULL, NULL, &tv);
	}
	XUngrabKeyboard(dpy, CurrentTime);
	XCloseDisplay(dpy);
	return 0;
}