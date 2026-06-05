#include "tools.h"
#include "nob.h"
#include <math.h>
#include <raymath.h>

void save_action(State* state)
{
    CHECK_NULL(state);

    state->history_head = (state->history_head + 1) % MAX_UNDO;

    if (state->history[state->history_head].data != NULL)
    {
        UnloadImage(state->history[state->history_head]);
    }

    state->history[state->history_head] = LoadImageFromTexture(state->canvas.texture);

    if (state->history_count < MAX_UNDO) state->history_count++;
    state->history_top = state->history_count;
}

void undo(State* state)
{
    CHECK_NULL(state);

    if (state->history_count <= 1) return;
    state->history_head = (state->history_head - 1 + MAX_UNDO) % MAX_UNDO;
    state->history_count--;
    UpdateTexture(state->canvas.texture, state->history[state->history_head].data);
}

void redo(State* state)
{
    CHECK_NULL(state);

    if (state->history_count >= state->history_top) return;
    state->history_head = (state->history_head + 1) % MAX_UNDO;
    state->history_count++;
    UpdateTexture(state->canvas.texture, state->history[state->history_head].data);
}

void zoom_tool(State* state)
{
    CHECK_NULL(state);

    Vector2 mouse_pos = GetMousePosition();
    Vector2 mouse_world_pos = GetScreenToWorld2D(mouse_pos, state->camera);
    float wheel = GetMouseWheelMove();

    state->camera.target = mouse_world_pos;
    state->camera.offset = mouse_pos;
    state->camera.zoom *= pow(MOUSE_WHEEL_SENSITIVITY, wheel);
    if (state->camera.zoom < MIN_ZOOM) state->camera.zoom = MIN_ZOOM;
}

void move_tool(State* state)
{
    CHECK_NULL(state);

    Vector2 mouse_pos = GetMousePosition();

    Vector2 delta = Vector2Subtract(mouse_pos, state->last_mouse_pos);
    state->camera.target.x -= delta.x / state->camera.zoom;
    state->camera.target.y -= delta.y / state->camera.zoom;
}

void pencil_tool(State* state)
{
    CHECK_NULL(state);

    Vector2 mouse_pos = GetMousePosition();
    Vector2 current_world_mouse = GetScreenToWorld2D(mouse_pos, state->camera);
    Vector2 last_world_mouse = GetScreenToWorld2D(state->last_mouse_pos, state->camera);

    BeginTextureMode(state->canvas);

    DrawCircleV(last_world_mouse, state->tool_thickness / 2.0f, state->tool_color);
    DrawLineEx(last_world_mouse, current_world_mouse, state->tool_thickness, state->tool_color);
    DrawCircleV(current_world_mouse, state->tool_thickness / 2.0f, state->tool_color);

    EndTextureMode();
}

void eraser_tool(State* state)
{
    CHECK_NULL(state);

    Vector2 mouse_pos = GetMousePosition();
    Vector2 current_world_mouse = GetScreenToWorld2D(mouse_pos, state->camera);
    Vector2 last_world_mouse = GetScreenToWorld2D(state->last_mouse_pos, state->camera);

    BeginTextureMode(state->canvas);
    BeginBlendMode(BLEND_SUBTRACT_COLORS);

    DrawCircleV(last_world_mouse, state->tool_thickness / 2.0f, (Color){255, 255, 255, 0});
    DrawLineEx(last_world_mouse, current_world_mouse, state->tool_thickness,
               (Color){255, 255, 255, 0});
    DrawCircleV(current_world_mouse, state->tool_thickness / 2.0f, (Color){255, 255, 255, 0});

    EndBlendMode();
    EndTextureMode();
}

void rectangle_tool(State* state, bool write)
{
    CHECK_NULL(state);

    Vector2 mouse_pos = GetMousePosition();
    Vector2 mouse_world_pos = GetScreenToWorld2D(mouse_pos, state->camera);

    if (write)
        BeginTextureMode(state->canvas);
    else
        BeginMode2D(state->camera);

    Rectangle rec = {
        state->tool_start.x,
        state->tool_start.y,
        mouse_world_pos.x - state->tool_start.x,
        mouse_world_pos.y - state->tool_start.y,
    };
    DrawRectangleRec(fix_rec(rec), state->tool_color);

    if (write)
    {
        EndTextureMode();
        save_action(state);
    }
    else
        EndMode2D();
}

void laser_pointer_tool(State* state)
{
    CHECK_NULL(state);

    Vector2 mouse_pos = GetMousePosition();
    Vector2 current_world_mouse = GetScreenToWorld2D(mouse_pos, state->camera);
    Vector2 last_world_mouse = GetScreenToWorld2D(state->last_mouse_pos, state->camera);

    BeginMode2D(state->camera);

    DrawCircleV(last_world_mouse, state->tool_thickness / 2.0f, state->tool_color);
    DrawLineEx(last_world_mouse, current_world_mouse, state->tool_thickness, state->tool_color);
    DrawCircleV(current_world_mouse, state->tool_thickness / 2.0f, state->tool_color);

    EndMode2D();
}

void pixelate_tool(State* state, bool write)
{
    CHECK_NULL(state);

    Vector2 mouse_pos = GetMousePosition();
    Vector2 mouse_world_pos = GetScreenToWorld2D(mouse_pos, state->camera);

    if (write)
        BeginTextureMode(state->canvas);
    else
        BeginMode2D(state->camera);

    Rectangle rec = {
        state->tool_start.x,
        state->tool_start.y,
        mouse_world_pos.x - state->tool_start.x,
        mouse_world_pos.y - state->tool_start.y,
    };
    rec = fix_rec(rec);
    SetRandomSeed(state->pixelate_seed);
    for (float y = rec.y; y < rec.y + rec.height; y += PIXELATE_SIZE)
    {
        float height = y + PIXELATE_SIZE;
        if (y + height > rec.height) height = rec.y + rec.height - y;
        for (float x = rec.x; x < rec.x + rec.width; x += PIXELATE_SIZE)
        {
            float width = x + PIXELATE_SIZE;
            if (x + width > rec.width) width = rec.x + rec.width - x;
            int color = GetRandomValue(0, 100);
            DrawRectangle(x, y, width, height, (Color){color, color, color, 255});
        }
    }

    if (write)
    {
        EndTextureMode();
        save_action(state);
    }
    else
        EndMode2D();
}

void color_picker_tool(State* state)
{
    CHECK_NULL(state);

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
    {
        Image image = LoadImageFromTexture(state->screenshot);
        ImageFormat(&image, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);

        Vector2 mouse_pos = GetMousePosition();
        Vector2 mouse_world_pos = GetScreenToWorld2D(mouse_pos, state->camera);

        Clamp(mouse_world_pos.x, 0, image.width);
        Clamp(mouse_world_pos.y, 0, image.height);

        Color* colors = LoadImageColors(image);

        state->tool_color = colors[(int)mouse_world_pos.y * image.width + (int)mouse_world_pos.x];

        MemFree(colors);

        UnloadImage(image);
    }
}

void line_tool(State* state, bool write)
{
    CHECK_NULL(state);

    Vector2 mouse_pos = GetMousePosition();
    Vector2 current_world_mouse = GetScreenToWorld2D(mouse_pos, state->camera);

    if (write)
        BeginTextureMode(state->canvas);
    else
        BeginMode2D(state->camera);

    DrawCircleV(state->tool_start, state->tool_thickness / 2.0f, state->tool_color);
    DrawLineEx(state->tool_start, current_world_mouse, state->tool_thickness, state->tool_color);
    DrawCircleV(current_world_mouse, state->tool_thickness / 2.0f, state->tool_color);

    if (write)
    {
        EndTextureMode();
        save_action(state);
    }
    else
        EndMode2D();
}

void arrow_tool(State* state, bool write)
{
    CHECK_NULL(state);

    Vector2 mouse_pos = GetMousePosition();
    Vector2 current_world_mouse = GetScreenToWorld2D(mouse_pos, state->camera);

    if (write)
        BeginTextureMode(state->canvas);
    else
        BeginMode2D(state->camera);

    DrawCircleV(state->tool_start, state->tool_thickness / 2.0f, state->tool_color);
    DrawLineEx(state->tool_start, current_world_mouse, state->tool_thickness, state->tool_color);
    DrawCircleV(current_world_mouse, state->tool_thickness / 2.0f, state->tool_color);

#if ARROW_INVERT
    Vector2 arrow_pivot = state->tool_start;
    float start_angle = 0;
#else
    Vector2 arrow_pivot = current_world_mouse;
    float start_angle = 3.14f;
#endif

    Vector2 arrow1 = Vector2Add(
        Vector2Rotate(
            Vector2Scale(Vector2Normalize(Vector2Subtract(current_world_mouse, state->tool_start)),
                         ARROW_LENGTH),
            start_angle - ARROW_ANGLE),
        arrow_pivot);
    DrawCircleV(arrow1, state->tool_thickness / 2.0f, state->tool_color);
    DrawLineEx(arrow1, arrow_pivot, state->tool_thickness, state->tool_color);

    Vector2 arrow2 = Vector2Add(
        Vector2Rotate(
            Vector2Scale(Vector2Normalize(Vector2Subtract(current_world_mouse, state->tool_start)),
                         ARROW_LENGTH),
            start_angle + ARROW_ANGLE),
        arrow_pivot);
    DrawCircleV(arrow2, state->tool_thickness / 2.0f, state->tool_color);
    DrawLineEx(arrow2, arrow_pivot, state->tool_thickness, state->tool_color);

    if (write)
    {
        EndTextureMode();
        save_action(state);
    }
    else
        EndMode2D();
}
