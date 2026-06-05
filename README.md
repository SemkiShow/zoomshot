# zoomshot

A simple zoomer/screenshotter app for Linux.

## Quick Start

Install dependencies (glib2 and libportal are not needed if USE_GRIM is uncommented in config.h):  
Fedora: `sudo dnf install libX11-devel glib2-devel libportal-devel`

For copying the screenshot to the clipboard you'll need `wl-copy` on Wayland or `xclip` on X11.

```bash
cc -o nob nob.c
./nob
```
