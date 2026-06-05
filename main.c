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

void reset_camera(State* state)
{
    CHECK_NULL(state);

    state->camera = (Camera2D){0};
    switch (state->mode)
    {
    case Mode_Screenshot:
        state->camera.zoom = 1.0f;
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

void screenshot_mode(State* state)
{
    CHECK_NULL(state);

    state->mode = Mode_Screenshot;
    state->tool = Tool_Select;
    reset_camera(state);
}

void zoom_mode(State* state)
{
    CHECK_NULL(state);

    state->mode = Mode_Zoom;
    state->tool = Tool_Move;
    reset_camera(state);
}

State state_new()
{
    State state = {0};
#if CAPTURE_METHOD == CAPTURE_METHOD_PORTAL
    state.startup_loop = g_main_loop_new(NULL, FALSE);
#endif
    state.loop = true;
    state.tool_thickness = INITIAL_THICKNESS;
    state.tool_color = RED;
    state.pixelate_seed = time(0);
    SetRandomSeed(state.pixelate_seed);
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

void process_input(State* state)
{
    CHECK_NULL(state);

    Vector2 mouse_pos = GetMousePosition();
    Vector2 mouse_world_pos = GetScreenToWorld2D(mouse_pos, state->camera);

    float wheel = GetMouseWheelMove();
    if (wheel != 0.0f) zoom_tool(state);

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
            SetRandomSeed(state->pixelate_seed);
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

    if (IsKeyPressed(KEY_ZERO))
    {
        reset_camera(state);
        state->camera.zoom = 1.0f;
    }
    if (IsKeyPressed(KEY_S))
    {
        if (IsKeyDown(KEY_LEFT_CONTROL))
        {
            Image screenshot = take_screenshot(*state);
            save_screenshot(screenshot);
            UnloadImage(screenshot);
        }
        else
        {
            state->mode = Mode_Screenshot;
            if (state->tool == Tool_Move) state->tool = Tool_Select;
        }
    }
    if (IsKeyPressed(KEY_Z))
    {
        if (IsKeyDown(KEY_LEFT_CONTROL))
        {
            if (IsKeyDown(KEY_LEFT_SHIFT))
                redo(state);
            else
                undo(state);
        }
        else
        {
            state->mode = Mode_Zoom;
            if (state->tool == Tool_Select) state->tool = Tool_Move;
        }
    }
    if (IsKeyPressed(KEY_V))
    {
        state->mode = Mode_Screenshot;
        state->tool = Tool_Select;
    }
    if (IsKeyPressed(KEY_M)) state->tool = Tool_Move;
    if (IsKeyPressed(KEY_P)) state->tool = Tool_Pencil;
    if (IsKeyPressed(KEY_E)) state->tool = Tool_Eraser;
    if (IsKeyPressed(KEY_R)) state->tool = Tool_Rectangle;
    if (IsKeyPressed(KEY_I)) state->tool = Tool_LaserPointer;
    if (IsKeyPressed(KEY_X)) state->tool = Tool_Pixelate;
    if (IsKeyPressed(KEY_L)) state->tool = Tool_Line;
    if (IsKeyPressed(KEY_A)) state->tool = Tool_Arrow;
    if (IsKeyDown(KEY_LEFT_BRACKET))
    {
        state->tool_thickness -= THICKNESS_SPEED * GetFrameTime();
        state->tool_thickness = fmaxf(0, state->tool_thickness);

        BeginMode2D(state->camera);
        DrawCircleLinesV(mouse_world_pos, state->tool_thickness / 2, state->tool_color);
        EndMode2D();
    }
    if (IsKeyDown(KEY_RIGHT_BRACKET))
    {
        state->tool_thickness += THICKNESS_SPEED * GetFrameTime();
        state->tool_thickness = fminf(MAX_THICKNESS, state->tool_thickness);

        BeginMode2D(state->camera);
        DrawCircleLinesV(mouse_world_pos, state->tool_thickness / 2, state->tool_color);
        EndMode2D();
    }
    if (IsKeyPressed(KEY_C))
    {
        if (IsKeyDown(KEY_LEFT_CONTROL))
        {
            Image screenshot = take_screenshot(*state);
            copy_image(screenshot);
            UnloadImage(screenshot);
        }
        else
        {
            state->tool = Tool_ColorPicker;
        }
    }
    if (IsKeyPressed(KEY_ENTER))
    {
        Image screenshot = take_screenshot(*state);
        save_screenshot(screenshot);
        copy_image(screenshot);
        UnloadImage(screenshot);
        state->loop = false;
    }

    if (state->mode == Mode_Screenshot)
    {
        BeginTextureMode(state->mask);

        DrawRectangle(0, 0, state->mask.texture.width, state->mask.texture.height,
                      ColorAlpha(GRAY, 0.75));

        BeginBlendMode(BLEND_SUBTRACT_COLORS);
        DrawRectangleRec(fix_rec(state->selection), (Color){255, 255, 255, 0});
        EndBlendMode();

        EndTextureMode();
    }
}

void usage(const char* program)
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
    flags |= FLAG_WINDOW_TRANSPARENT;
    SetConfigFlags(flags);

    InitWindow(800, 600, "zoomshot");

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
                           state.canvas.texture.width,
                           -state.canvas.texture.height,
                       },
                       (Vector2){0, 0}, WHITE);

        EndMode2D();

        process_input(&state);

        state.last_mouse_pos = mouse_pos;

        BeginMode2D(state.camera);

        DrawTextureRec(state.mask.texture,
                       (Rectangle){
                           0,
                           0,
                           state.mask.texture.width,
                           -state.mask.texture.height,
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
            Rectangle copyButton = {0, 0, 0, 30};
            copyButton.width = MeasureText(label, copyButton.height);

            if (state.color_picker_timer <= 0 && CheckCollisionPointRec(mouse_pos, copyButton))
            {
                SetClipboardText(label);
                state.color_picker_timer = COLOR_PICKER_POPUP_DURATION;
            }

            DrawRectangleRec(copyButton, state.tool_color);
            DrawText(label, 0, 0, copyButton.height, invert_color(state.tool_color));

            nob_temp_rewind(mark);
        }

        EndDrawing();
    }

    CloseWindow();

    return 0;
}
