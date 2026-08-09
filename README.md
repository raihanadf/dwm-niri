# dwm-niri

dwm with a scrolling layout and vertical workspaces, built to behave like
[niri](https://github.com/YaLTeR/niri) — on X11, in about 3000 lines of C, with
no compositor.

Windows live on an infinite horizontal strip of columns. Opening a window never
resizes the ones already there; the strip just grows and the screen scrolls
along it. Workspaces are a vertical stack, so `h`/`l` and `j`/`k` are two axes
of one space rather than two unrelated concepts.

## What is different from dwm

- **The scroller is the only layout.** There is nothing to switch to, so the
  layout keys, the cycler and the bar's layout symbol are all gone. Windows
  still float one at a time; what is gone is floating as a mode a whole
  workspace can be in.
- **One workspace at a time.** A window lives on exactly one workspace, so the
  combo and toggle tag bindings have no set left to add to.
- **Workspaces are a vertical stack.** `j`/`k` move down and up them, `Shift`
  brings the focused window along, and it keeps the column position it had.
- **Dynamic-feeling workspaces.** Navigation stops one past the last occupied
  workspace and the bar draws only that far, so you see the ones you use plus
  one empty, not nine fixed boxes. The underlying tag bitmask still caps this
  at 9 (31 if you raise `NUMTAGS`).
- **An overview of every workspace**, with live window contents, on `Mod+o`.
- **Animated movement**, without a compositor.

## Keys

`Mod` is Alt.

| Key | Action |
| --- | --- |
| `Mod+h` / `Mod+l` | focus the column left / right |
| `Mod+Shift+h` / `Mod+Shift+l` | move the column left / right |
| `Mod+j` / `Mod+k` | focus down / up: the rest of the column first, then the next workspace |
| `Mod+Shift+j` / `Mod+Shift+k` | move the window to the workspace below / above, and follow it |
| `Mod+Super+j` / `Mod+Super+k` | focus within the column, never leaving it |
| `Mod+Super+Shift+j` / `Mod+Super+Shift+k` | move the whole column to the workspace below / above |
| `Mod+1..9` / `Mod+Shift+1..9` | view workspace N / send the window there and follow |
| `Mod+[` / `Mod+]` | expel a window from its column / consume the next column into it |
| `Mod+Shift+[` / `Mod+Shift+]` | move the window up / down inside its column |
| `Mod+r` / `Mod+Shift+r` | cycle the column through the width presets |
| `Mod+=` / `Mod+-` | widen / narrow the column by hand |
| `Mod+w` | maximise the column to the full width, and back |
| `Mod+Shift+f` | fullscreen |
| `Mod+o` | overview of every workspace |
| `Mod+Shift+m` | strip minimap |
| `Mod+Shift+s` | pull every floating window back into the strip |
| `Mod+Shift+Space` | toggle floating |
| `Mod+,` / `Mod+.` | focus the previous / next monitor |
| `Mod+Shift+,` / `Mod+Shift+.` | send the window to the previous / next monitor |

`Mod+j`/`Mod+k` are context-aware: a column holding several windows is itself a
vertical list, so they walk that first and only step to the next workspace off
the end of it. A column of one goes straight to the workspace.

## Overview

`Mod+o` draws every reachable workspace as a row, with live window contents.

It works without a compositor because the Composite extension alone is enough:
redirecting the container's subwindows in *Automatic* mode leaves the server
drawing everything to the screen as before, while additionally keeping each
window's full contents in offscreen storage — at the window's real size, however
much of it is clipped or however far off-screen it sits.

Every row draws at one scale and is anchored by its screen frame rather than its
strip, so the eye runs down the workspaces while longer strips overflow to
either side. The frame marks the slice each workspace is scrolled to.

Click a window to go to it; click anywhere else in a row to go to that
workspace.

## Animation

A timer walks the windows from where they were to where the layout put them,
easing out over `animduration` milliseconds. dwm's model is untouched: `c->x`
and `c->y` hold the real geometry from the moment the layout runs, and only the
X windows lag behind for a few frames.

Position only, deliberately — every intermediate size would reach the client as
a real `ConfigureNotify`, and a terminal asked to reflow 90 times a second is a
bad price for a prettier resize. In a scrolling layout nearly all the motion is
translation anyway.

Moving between workspaces slides: the one you are leaving goes out the way you
came while the new one rises into view behind it, so up and down are something
you see rather than something you work out afterwards. The outgoing workspace
has to be captured before the tag changes, because a moment later nothing can
tell what was on screen.

The event loop waits on the X connection rather than blocking in `XNextEvent`: a
frame's timeout while something is moving, indefinitely otherwise. Input is
never queued behind an animation — a keystroke arriving mid-flight cuts the wait
short and retargets the movement from wherever the windows have got to, so
holding a key down tracks the strip instead of playing back a backlog.

Tunable in `config.h`:

```c
static const int animated              = 1;   /* 0 turns it off entirely */
static const unsigned int animduration = 130; /* ms per movement */
static const unsigned int animfps      = 90;  /* frames per second while moving */
static const int animminpx             = 6;   /* ignore movements smaller than this */
```

## Building

```sh
make
sudo make install
```

Needs Xlib, Xft, fontconfig, Xinerama, and — for the overview — Xcomposite,
Xrender, Xdamage. Without Composite the overview simply reports itself
unavailable; everything else works.

Configuration is `config.h`, edited and recompiled, as in any dwm.

## Credit

Built on [dwm](https://dwm.suckless.org/) and the
[flexipatch](https://github.com/bakkeby/dwm-flexipatch) patch layout. Layout
behaviour is modelled on [niri](https://github.com/YaLTeR/niri).
