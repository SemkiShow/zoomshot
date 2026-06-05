#ifndef CONFIG_H_
#define CONFIG_H_

#define CAPTURE_METHOD_PORTAL 0
#define CAPTURE_METHOD_GRIM 1 // Works only on wlroots
#define CAPTURE_METHOD CAPTURE_METHOD_PORTAL

#define MAX_UNDO 50

#define MOUSE_WHEEL_SENSITIVITY 1.1
#define INITIAL_ZOOM 2.0f
#define MIN_ZOOM 0.05f

#define INITIAL_THICKNESS 3
#define THICKNESS_SPEED 10
#define MIN_THICKNESS 1
#define MAX_THICKNESS 100

#define PIXELATE_SIZE 5
#define COLOR_PICKER_POPUP_DURATION 1
#define ARROW_ANGLE 20 * 3.14f / 180
#define ARROW_LENGTH 30
#define ARROW_INVERT 0

#define TIME_FORMAT "%Y-%m-%d_%H:%M:%S"
// %s is replaced with time in TIME_FORMAT format. SCREENSHOT_PATH is prefixed with $HOME/ on save
#define SCREENSHOT_PATH "Pictures/Screenshots/%s.png"

#endif // CONFIG_H_
