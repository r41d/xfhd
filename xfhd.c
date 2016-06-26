/*

Copyright 1988, 1998  The Open Group

Permission to use, copy, modify, distribute, and sell this software and its
documentation for any purpose is hereby granted without fee, provided that
the above copyright notice appear in all copies and that both that
copyright notice and this permission notice appear in supporting
documentation.

The above copyright notice and this permission notice shall be included
in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE OPEN GROUP BE LIABLE FOR ANY CLAIM, DAMAGES OR
OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
OTHER DEALINGS IN THE SOFTWARE.

Except as contained in this notice, the name of The Open Group shall
not be used in advertising or otherwise to promote the sale, use or
other dealings in this Software without prior written authorization
from The Open Group.

*/

/*
 * xfhd - simple program for resizing to Full HD
 * Author:  Jim Fulton, MIT X Consortium; Dana Chee, Bellcore
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

#include <X11/Xos.h>
#include <X11/Xlib.h>
#include <X11/cursorfont.h>
#include <X11/Xproto.h>

#include <X11/Xmu/WinUtil.h>

static char *ProgramName;

#define SelectButtonAny (-1)
#define SelectButtonFirst (-2)

int RESOLUTION_X = 1920;
int RESOLUTION_Y = 1080;

static XID get_window_id ( Display *dpy, int screen, int button, const char *msg );
static Bool wm_state_set ( Display *dpy, Window win );
static Bool wm_running ( Display *dpy, int screenno );


static void _X_NORETURN Exit(int code, Display *dpy) {
	if (dpy) {
		XCloseDisplay (dpy);
	}
	exit (code);
}


static void _X_NORETURN usage(void) {
	const char *options =
"where options include:\n"
"    -display displayname    X server to contact\n"
"    -id resource            resource whose client is to be resized\n"
"    -x                      specify alternative X resolution"
"    -y                      specify alternative Y resolution"
"    -version                print version and exit\n"
"\n";

	fprintf (stderr, "usage:  %s [-option ...]\n%s", ProgramName, options);
	Exit (1, NULL);
}


int main(int argc, char *argv[]) {
	int i;				/* iterator, temp variable */
	Display *dpy = NULL;
	char *displayname = NULL;		/* name of server to contact */
	int screenno;			/* screen number of dpy */
	XID id = None;			/* resource to resize */
	char *button_name = NULL;		/* name of button for window select */
	int button;				/* button number or negative for all */

	ProgramName = argv[0];
	button = SelectButtonFirst;

	for (i = 1; i < argc; i++) {
		char *arg = argv[i];

		if (arg[0] == '-') {
			switch (arg[1]) {
			  case 'd':			/* -display displayname */
				if (++i >= argc) usage();
				displayname = argv[i];
				continue;
			  case 'i':			/* -id resourceid */
				if (++i >= argc) usage();
				id = strtoul(argv[i], NULL, 0);
				if (id == 0 || id >= 0xFFFFFFFFU) {
					fprintf (stderr, "%s:  invalid id \"%s\"\n",
						 ProgramName, argv[i]);
					Exit (1, dpy);
				}
				continue;
			  case 'x':
				if (++i >= argc) usage();
				RESOLUTION_X = strtoul(argv[i], NULL, 0);
				continue;
			  case 'y':
				if (++i >= argc) usage();
				RESOLUTION_Y = strtoul(argv[i], NULL, 0);
				continue;
			  case 'v':
				puts(PACKAGE_STRING);
				exit(0);
			  default:
				usage();
			}
		} else {
			usage();
		}
	}					/* end for */

	dpy = XOpenDisplay(displayname);
	if (!dpy) {
		fprintf (stderr, "%s:  unable to open display \"%s\"\n",
			 ProgramName, XDisplayName (displayname));
		Exit (1, dpy);
	}
	screenno = DefaultScreen(dpy);

	/*
	 * if no id was given, we need to choose a window
	 */
	if (id == None) {
		if (!button_name)
			button_name = XGetDefault(dpy, ProgramName, "Button");

		if (button >= 0 || button == SelectButtonFirst) {
			unsigned char pointer_map[256];	 /* 8 bits of pointer num */
			int count, j;
			unsigned int ub = (unsigned int) button;

			count = XGetPointerMapping (dpy, pointer_map, 256);
			if (count <= 0) {
				fprintf (stderr,
					"%s:  no pointer mapping, can't select window\n",
					ProgramName);
				Exit (1, dpy);
			}

			if (button >= 0) {			/* check button */
				for (j = 0; j < count; j++) {
					if (ub == (unsigned int) pointer_map[j])
						break;
				}
				if (j == count) {
					fprintf (stderr,
						"%s:  no button number %u in pointer map, can't select window\n",
						ProgramName, ub);
					Exit (1, dpy);
				}
			} else {				/* get first entry */
				button = (int) ((unsigned int) pointer_map[0]);
			}
		}
		if ((id = get_window_id (dpy, screenno, button,
					"the window whose client you wish to resize"))) {
			if (id == RootWindow(dpy,screenno))
				id = None;
			else {
				XID indicated = id;
				if ((id = XmuClientWindow(dpy, indicated)) == indicated) {
					/* Try not to resize the window manager when the user
					 * indicates an icon to xfhd.
					 */
					if (! wm_state_set(dpy, id) && wm_running(dpy, screenno))
						id = None;
				}
			}
		}
	}

	if (id != None) {
		printf ("%s:  resizing creator of resource 0x%lx\n", ProgramName, id);
		XSync (dpy, 0);			/* give xterm a chance */

		// Don't kill
		//XKillClient(dpy, id);

		// But resize!
		// XResizeWindow(display, w, width, height)
		//       Display *display;
		//       Window w;
		//       unsigned int width, height;
		XResizeWindow(dpy, id, RESOLUTION_X, RESOLUTION_Y);

		XSync (dpy, 0);
	}

	Exit (0, dpy);
	/*NOTREACHED*/
	return 0;
}


static XID get_window_id(Display *dpy, int screen, int button, const char *msg) {
	Cursor cursor;		/* cursor to use when selecting */
	Window root;		/* the current root */
	Window retwin = None;	/* the window that got selected */
	int retbutton = -1;		/* button used to select window */
	int pressed = 0;		/* count of number of buttons pressed */

#define MASK (ButtonPressMask | ButtonReleaseMask)

	root = RootWindow (dpy, screen);
	cursor = XCreateFontCursor (dpy, XC_pirate);
	if (cursor == None) {
		fprintf (stderr, "%s:  unable to create selection cursor\n", ProgramName);
		Exit (1, dpy);
	}

	printf ("Select %s with ", msg);
	if (button == -1)
		printf ("any button");
	else
		printf ("button %d", button);
	printf ("....\n");
	XSync (dpy, 0);			/* give xterm a chance */

	if (XGrabPointer (dpy, root, False, MASK, GrabModeSync, GrabModeAsync,
				  None, cursor, CurrentTime) != GrabSuccess) {
		fprintf (stderr, "%s:  unable to grab cursor\n", ProgramName);
		Exit (1, dpy);
	}

	/* from dsimple.c in xwininfo */
	while (retwin == None || pressed != 0) {
		XEvent event;

		XAllowEvents (dpy, SyncPointer, CurrentTime);
		XWindowEvent (dpy, root, MASK, &event);
		switch (event.type) {
		  case ButtonPress:
			if (retwin == None) {
			retbutton = event.xbutton.button;
			retwin = ((event.xbutton.subwindow != None) ?
				  event.xbutton.subwindow : root);
			}
			pressed++;
			continue;
		  case ButtonRelease:
			if (pressed > 0) pressed--;
			continue;
		}					/* end switch */
	}						/* end for */

	XUngrabPointer (dpy, CurrentTime);
	XFreeCursor (dpy, cursor);
	XSync (dpy, 0);

	return ((button == -1 || retbutton == button) ? retwin : None);
}


/* Return True if the property WM_STATE is set on the window, otherwise
 * return False.
 */
static Bool wm_state_set(Display *dpy, Window win) {
	Atom wm_state;
	Atom actual_type;
	int success;
	int actual_format;
	unsigned long nitems, remaining;
	unsigned char* prop = NULL;

	wm_state = XInternAtom(dpy, "WM_STATE", True);
	if (wm_state == None)
		return False;
	success = XGetWindowProperty(dpy, win, wm_state, 0L, 0L, False,
				 AnyPropertyType, &actual_type, &actual_format,
				 &nitems, &remaining, &prop);
	if (prop)
		XFree((char *) prop);
	return (success == Success && actual_type != None && actual_format);
}


/* Using a heuristic method, return True if a window manager is running,
 * otherwise, return False.
 */
static Bool wm_running(Display *dpy, int screenno) {
	XWindowAttributes	xwa;
	Status		status;

	status = XGetWindowAttributes(dpy, RootWindow(dpy, screenno), &xwa);
	return (status &&
		((xwa.all_event_masks & SubstructureRedirectMask) ||
		 (xwa.all_event_masks & SubstructureNotifyMask)));
}

