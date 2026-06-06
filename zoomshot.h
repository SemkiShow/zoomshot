#ifndef ZOOMSHOT_H_
#define ZOOMSHOT_H_

#include "config.h"
#include <raylib.h>
#include <stdio.h>

typedef struct _GMainLoop GMainLoop;

typedef enum
{
    Mode_Screenshot,
    Mode_Zoom,
} Mode;

typedef enum
{
    Tool_Select,
    Tool_Move,
    Tool_Pencil,
    Tool_Eraser,
    Tool_Rectangle,
    Tool_LaserPointer,
    Tool_Pixelate,
    Tool_ColorPicker,
    Tool_Line,
    Tool_Arrow,
} Tool;

typedef enum
{
    SelectState_Resize,
    SelectState_Move,
} SelectState;

typedef struct
{
#if CAPTURE_METHOD == CAPTURE_METHOD_PORTAL
    GMainLoop* startup_loop;
#endif
    Texture screenshot;

    bool loop;
    Mode mode;
    Camera2D camera;
    Vector2 last_mouse_pos;
    Tool tool;
    Vector2 tool_start;
    float tool_thickness;
    Color tool_color;
    Rectangle selection;
    SelectState select_state;
    size_t pixelate_seed;
    float color_picker_timer;

    RenderTexture2D canvas;
    RenderTexture2D mask;

    Image history[MAX_UNDO];
    size_t history_count;
    size_t history_top;
    size_t history_head;
} State;

#define CHECK_NULL(val)                                                                            \
    do                                                                                             \
    {                                                                                              \
        if (!(val))                                                                                \
        {                                                                                          \
            nob_log(NOB_ERROR, #val " is NULL\n");                                                 \
            return;                                                                                \
        }                                                                                          \
    } while (0)

Rectangle fix_rec(Rectangle rec);
Color invert_color(Color color);

#endif // ZOOMSHOT_H_
