#ifndef TOOLS_H_
#define TOOLS_H_

#include "zoomshot.h"

void save_action(State* state);
void undo(State* state);
void redo(State* state);

void zoom_tool(State* state);
void move_tool(State* state);
void pencil_tool(State* state);
void eraser_tool(State* state);
void rectangle_tool(State* state, bool write);
void laser_pointer_tool(State* state);
void pixelate_tool(State* state, bool write);
void color_picker_tool(State* state);
void line_tool(State* state, bool write);
void arrow_tool(State* state, bool write);

#endif // TOOLS_H_
