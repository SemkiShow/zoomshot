#include "config.h"
#include "screenshot.h"
#include "tools.h"
#include "zoomshot.h"
#include <raylib.h>
#include <raymath.h>
#include <stdio.h>

#define NOB_IMPLEMENTATION
#include "nob.h"

#if CAPTURE_METHOD == CAPTURE_METHOD_PORTAL
#include <glib.h>
#endif

static void reset_camera(State* state)
{
    CHECK_NULL(state);

    state->camera = (Camera2D){0};
    switch (state->mode)
    {
    case Mode_Screenshot:
        state->camera.zoom = 1.0F;
        break;
    case Mode_Zoom:
    {
        // Wait for GetMousePosition
        while (Vector2Equals(GetMousePosition(), (Vector2){0, 0}))
        {
            BeginDrawing();
            ClearBackground(BLANK);
            DrawTexture(state->screenshot, 0, 0, WHITE);
            EndDrawing();
        }

        Vector2 mouse_pos = GetMousePosition();
        state->camera.target = mouse_pos;
        state->camera.offset = mouse_pos;
        state->camera.zoom = INITIAL_ZOOM;
    }
    break;
    }
}

static void screenshot_mode(State* state)
{
    CHECK_NULL(state);

    state->mode = Mode_Screenshot;
    state->tool = Tool_Select;
    reset_camera(state);
}

static void zoom_mode(State* state)
{
    CHECK_NULL(state);

    state->mode = Mode_Zoom;
    state->tool = Tool_Move;
    reset_camera(state);
}

static State state_new()
{
    State state = {0};
#if CAPTURE_METHOD == CAPTURE_METHOD_PORTAL
    state.startup_loop = g_main_loop_new(NULL, FALSE);
#endif
    state.loop = true;
    state.tool_thickness = INITIAL_THICKNESS;
    state.tool_color = RED;
    state.pixelate_seed = time(0);
    screenshot_mode(&state);
    return state;
}

Rectangle fix_rec(Rectangle rec)
{
    if (rec.width < 0)
    {
        rec.x += rec.width;
        rec.width *= -1;
    }
    if (rec.height < 0)
    {
        rec.y += rec.height;
        rec.height *= -1;
    }
    return rec;
}

Color invert_color(Color color)
{
    return (Color){
        255 - color.r,
        255 - color.g,
        255 - color.b,
        color.a,
    };
}

static void process_input(State* state)
{
    CHECK_NULL(state);

    Vector2 mouse_pos = GetMousePosition();
    Vector2 mouse_world_pos = GetScreenToWorld2D(mouse_pos, state->camera);

    float wheel = GetMouseWheelMove();
    if (wheel != 0.0F) zoom_tool(state);

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
    {
        state->tool_start.x = mouse_world_pos.x;
        state->tool_start.y = mouse_world_pos.y;
        if (state->tool == Tool_Select)
        {
            if (CheckCollisionPointRec(mouse_world_pos, fix_rec(state->selection)))
            {
                state->select_state = SelectState_Move;
            }
            else
            {
                state->select_state = SelectState_Resize;
                state->selection = (Rectangle){mouse_world_pos.x, mouse_world_pos.y, 0, 0};
            }
        }
    }

    if (IsMouseButtonDown(MOUSE_BUTTON_LEFT))
    {
        if (IsKeyDown(KEY_LEFT_SHIFT))
            move_tool(state);
        else
        {
            switch (state->tool)
            {
            case Tool_Move:
                move_tool(state);
                break;
            case Tool_Select:
                switch (state->select_state)
                {
                case SelectState_Resize:
                    state->selection.width = mouse_world_pos.x - state->selection.x;
                    state->selection.height = mouse_world_pos.y - state->selection.y;
                    break;
                case SelectState_Move:
                {
                    Vector2 delta = Vector2Subtract(
                        mouse_world_pos, GetScreenToWorld2D(state->last_mouse_pos, state->camera));
                    state->selection.x += delta.x;
                    state->selection.y += delta.y;
                }
                break;
                }
                break;
            case Tool_Pencil:
                pencil_tool(state);
                break;
            case Tool_Eraser:
                eraser_tool(state);
                break;
            case Tool_Rectangle:
                rectangle_tool(state, false);
                break;
            case Tool_LaserPointer:
                laser_pointer_tool(state);
                break;
            case Tool_Pixelate:
                pixelate_tool(state, false);
                break;
            case Tool_ColorPicker:
                color_picker_tool(state);
                break;
            case Tool_Line:
                line_tool(state, false);
                break;
            case Tool_Arrow:
                arrow_tool(state, false);
                break;
            }
        }
    }

    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
    {
        switch (state->tool)
        {
        case Tool_Select:
        case Tool_Move:
        case Tool_LaserPointer:
        case Tool_ColorPicker:
            break;
        case Tool_Pencil:
        case Tool_Eraser:
            save_action(state);
            break;
        case Tool_Rectangle:
            rectangle_tool(state, true);
            break;
        case Tool_Pixelate:
            pixelate_tool(state, true);
            state->pixelate_seed = time(0);
            break;
        case Tool_Line:
            line_tool(state, true);
            break;
        case Tool_Arrow:
            arrow_tool(state, true);
            break;
        }
    }

    if (IsMouseButtonDown(MOUSE_BUTTON_MIDDLE)) move_tool(state);

    if (IsKeyDown(KEY_LEFT_CONTROL))
    {
        if (state->mode == Mode_Screenshot && IsKeyPressed(KEY_S))
        {
            Image screenshot = take_screenshot(state);
            save_screenshot(screenshot);
            UnloadImage(screenshot);
            return;
        }
        if (IsKeyPressed(KEY_Z))
        {
            if (IsKeyDown(KEY_LEFT_SHIFT))
                redo(state);
            else
                undo(state);
            return;
        }
        if (state->mode == Mode_Screenshot && IsKeyPressed(KEY_C))
        {
            Image screenshot = take_screenshot(state);
            copy_image(screenshot);
            UnloadImage(screenshot);
            return;
        }
    }
    if (IsKeyPressed(RESET_KEY))
    {
        reset_camera(state);
        state->camera.zoom = 1.0F;
    }
    if (IsKeyPressed(SCREENSHOT_MODE_KEY))
    {
        state->mode = Mode_Screenshot;
        if (state->tool == Tool_Move) state->tool = Tool_Select;
    }
    if (IsKeyPressed(ZOOM_MODE_KEY))
    {
        state->mode = Mode_Zoom;
        if (state->tool == Tool_Select) state->tool = Tool_Move;
    }
    if (IsKeyPressed(SELECT_TOOL_KEY))
    {
        state->mode = Mode_Screenshot;
        state->tool = Tool_Select;
    }
    if (IsKeyPressed(MOVE_TOOL_KEY)) state->tool = Tool_Move;
    if (IsKeyPressed(PENCIL_TOOL_KEY)) state->tool = Tool_Pencil;
    if (IsKeyPressed(ERASER_TOOL_KEY)) state->tool = Tool_Eraser;
    if (IsKeyPressed(RECTANGLE_TOOL_KEY)) state->tool = Tool_Rectangle;
    if (IsKeyPressed(LASER_POINTER_TOOL_KEY)) state->tool = Tool_LaserPointer;
    if (IsKeyPressed(PIXELATE_TOOL_KEY)) state->tool = Tool_Pixelate;
    if (IsKeyPressed(COLOR_PICKER_TOOL_KEY)) state->tool = Tool_ColorPicker;
    if (IsKeyPressed(LINE_TOOL_KEY)) state->tool = Tool_Line;
    if (IsKeyPressed(ARROW_TOOL_KEY)) state->tool = Tool_Arrow;
    if (IsKeyDown(DECREASE_TOOL_THICKNESS_KEY))
    {
        state->tool_thickness -= THICKNESS_SPEED * GetFrameTime();
        state->tool_thickness = fmaxf(0, state->tool_thickness);

        BeginMode2D(state->camera);
        DrawCircleLinesV(mouse_world_pos, state->tool_thickness / 2, state->tool_color);
        EndMode2D();
    }
    if (IsKeyDown(INCREASE_TOOL_THICKNESS_KEY))
    {
        state->tool_thickness += THICKNESS_SPEED * GetFrameTime();
        state->tool_thickness = fminf(MAX_THICKNESS, state->tool_thickness);

        BeginMode2D(state->camera);
        DrawCircleLinesV(mouse_world_pos, state->tool_thickness / 2, state->tool_color);
        EndMode2D();
    }
    if (state->mode == Mode_Screenshot && IsKeyPressed(ACCEPT_KEY))
    {
        Image screenshot = take_screenshot(state);
        save_screenshot(screenshot);
        copy_image(screenshot);
        UnloadImage(screenshot);
        state->loop = false;
    }
}

static void usage(const char* program)
{
    printf("Usage: %s [OPTION...]\n", program);
    printf("A simple zoomer/screenshotter app for Linux.\n");
    printf("\n");
    printf("-h, --help          Print this help message\n");
    printf("-s, --screenshot    Start the the screenshot mode (default)\n");
    printf("-z, --zoom          Start the the zoom mode\n");
}

int main(int argc, char* argv[])
{
    State state = state_new();

    unsigned int flags = 0;
    flags |= FLAG_FULLSCREEN_MODE;
    flags |= FLAG_WINDOW_UNDECORATED;
    SetConfigFlags(flags);

    InitWindow(800, 600, "zoomshot");
    SetExitKey(REJECT_KEY);

    get_screen(&state);

    state.canvas = LoadRenderTexture(state.screenshot.width, state.screenshot.height);
    state.mask = LoadRenderTexture(state.screenshot.width, state.screenshot.height);
    save_action(&state);

    SetWindowState(FLAG_VSYNC_HINT);

    const char* program = nob_shift_args(&argc, &argv);
    while (argc)
    {
        const char* flag = nob_shift_args(&argc, &argv);
        if (strcmp(flag, "-h") == 0 || strcmp(flag, "--help") == 0)
        {
            usage(program);
            exit(0);
        }
        else if (strcmp(flag, "-s") == 0 || strcmp(flag, "--screenshot") == 0)
        {
            screenshot_mode(&state);
        }
        else if (strcmp(flag, "-z") == 0 || strcmp(flag, "--zoom") == 0)
        {
            zoom_mode(&state);
        }
        else
        {
            printf("Unknown flag: %s\n", flag);
            printf("\n");
            usage(program);
            exit(0);
        }
    }

    while (!WindowShouldClose() && state.loop)
    {
        Vector2 mouse_pos = GetMousePosition();

        BeginDrawing();

        ClearBackground(BLACK);

        BeginTextureMode(state.mask);
        ClearBackground(BLANK);
        EndTextureMode();

        BeginMode2D(state.camera);

        DrawTexture(state.screenshot, 0, 0, WHITE);

        DrawTextureRec(state.canvas.texture,
                       (Rectangle){
                           0,
                           0,
                           (float)state.canvas.texture.width,
                           (float)-state.canvas.texture.height,
                       },
                       (Vector2){0, 0}, WHITE);

        EndMode2D();

        process_input(&state);

        if (state.mode == Mode_Screenshot)
        {
            BeginTextureMode(state.mask);

            DrawRectangle(0, 0, state.mask.texture.width, state.mask.texture.height,
                          ColorAlpha(GRAY, 0.75F));

            BeginBlendMode(BLEND_SUBTRACT_COLORS);
            DrawRectangleRec(fix_rec(state.selection), (Color){255, 255, 255, 0});
            EndBlendMode();

            EndTextureMode();
        }

        state.last_mouse_pos = mouse_pos;

        BeginMode2D(state.camera);

        DrawTextureRec(state.mask.texture,
                       (Rectangle){
                           0,
                           0,
                           (float)state.mask.texture.width,
                           (float)-state.mask.texture.height,
                       },
                       (Vector2){0, 0}, WHITE);

        EndMode2D();

        if (state.tool == Tool_ColorPicker)
        {
            size_t mark = nob_temp_save();
            const char* label = NULL;
            if (state.color_picker_timer > 0)
            {
                label = "Copied!";
                state.color_picker_timer -= GetFrameTime();
            }
            else
            {
                label = nob_temp_sprintf("#%06x", ColorToInt(state.tool_color));
            }
            Rectangle copy_button = {0, 0, 0, 30};
            copy_button.width = (float)MeasureText(label, (int)copy_button.height);

            if (state.color_picker_timer <= 0 && CheckCollisionPointRec(mouse_pos, copy_button))
            {
                SetClipboardText(label);
                state.color_picker_timer = COLOR_PICKER_POPUP_DURATION;
            }

            DrawRectangleRec(copy_button, state.tool_color);
            DrawText(label, 0, 0, (int)copy_button.height, invert_color(state.tool_color));

            nob_temp_rewind(mark);
        }

        EndDrawing();
    }

    CloseWindow();

    return 0;
}
