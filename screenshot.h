#ifndef SCREENSHOT_H_
#define SCREENSHOT_H_

#include "zoomshot.h"

void get_screen(State* state);
Image take_screenshot(State state);
void save_screenshot(Image image);
bool copy_image(Image image);

#endif // SCREENSHOT_H_
