# zoomshot

A simple zoomer/screenshotter app for Linux.

## Quick Start

Install dependencies:  
Fedora: `sudo dnf install libX11-devel glib2-devel libportal-devel`

For copying the screenshot to the clipboard you'll need `wl-copy` on Wayland or `xclip` on X11.

```bash
cc -o nob nob.c
./nob
```
