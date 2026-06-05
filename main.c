#include "config.h"
#include <raylib.h>
#include <raymath.h>
#include <stdio.h>

#if CAPTURE_METHOD == CAPTURE_METHOD_PORTAL
#include <glib.h>
#include <libportal/portal-helpers.h>
#include <libportal/screenshot.h>
#endif

#define NOB_IMPLEMENTATION
#include "nob.h"

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
    char* screenshot_filename;
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
        state->camera.zoom = 1.0f;
        // Wait for GetMousePosition
        while (Vector2Equals(GetMousePosition(), (Vector2){0, 0}))
        {
            BeginDrawing();
            ClearBackground(BLANK);

            BeginMode2D(state->camera);
            DrawTexture(state->screenshot, 0, 0, WHITE);
            EndMode2D();

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

#if CAPTURE_METHOD == CAPTURE_METHOD_PORTAL
static void on_screenshot_ready(GObject* source_object, GAsyncResult* res, gpointer user_data)
{
    XdpPortal* portal = XDP_PORTAL(source_object);
    GError* error = NULL;
    char* filename = NULL;
    State* state = (State*)user_data;
    char* uri = xdp_portal_take_screenshot_finish(portal, res, &error);

    if (uri == NULL)
    {
        nob_log(NOB_ERROR, "Error taking screenshot: %s\n", error->message);
        g_clear_error(&error);
        goto defer;
    }

    nob_log(NOB_INFO, "Screenshot successfully taken!");

    filename = g_filename_from_uri(uri, NULL, &error);

    if (filename == NULL)
    {
        nob_log(NOB_ERROR, "Failed to parse URI path: %s", error->message);
        g_clear_error(&error);
        goto defer;
    }

    state->screenshot_filename = strdup(filename);

defer:
    if (uri) g_free(uri);
    if (filename) g_free(filename);
    if (state->startup_loop) g_main_loop_quit(state->startup_loop);
}
#endif

#if CAPTURE_METHOD == CAPTURE_METHOD_PORTAL
void get_screen_portal(State* state)
{
    CHECK_NULL(state);

    XdpPortal* portal = xdp_portal_new();
    xdp_portal_take_screenshot(portal, NULL, XDP_SCREENSHOT_FLAG_NONE, false, on_screenshot_ready,
                               state);

    // Wait for the screenshot to be taken
    g_main_loop_run(state->startup_loop);
    g_main_loop_unref(state->startup_loop);
    state->startup_loop = NULL;
    g_object_unref(portal);

    state->screenshot = LoadTexture(state->screenshot_filename);
}
#endif

#if CAPTURE_METHOD == CAPTURE_METHOD_GRIM
void get_screen_grim(State* state)
{
    CHECK_NULL(state);

    FILE* pipe = popen("grim -t ppm -", "r");
    unsigned char* buffer = NULL;
    if (!pipe)
    {
        nob_log(NOB_ERROR, "Failed to run grim pipe");
        goto defer;
    }

    char magic[16];
    int width = 0, height = 0, max_color = 0;

    if (fscanf(pipe, "%15s\n%d %d\n%d\n", magic, &width, &height, &max_color) != 4)
    {
        nob_log(NOB_ERROR, "Failed to parse PPM header from grim");
        goto defer;
    }

    if (strcmp(magic, "P6") != 0)
    {
        nob_log(NOB_ERROR, "Unexpected PPM format: %s", magic);
        goto defer;
    }

    size_t buf_len = (size_t)width * (size_t)height * 3;
    unsigned char* buf = malloc(buf_len);
    if (!buf)
    {
        nob_log(NOB_ERROR, "Failed to allocate memory for raw pixel buffer");
        goto defer;
    }

    size_t bytes_read = fread(buf, sizeof(unsigned char), buf_len, pipe);

    if (bytes_read != buf_len)
    {
        nob_log(NOB_ERROR, "Pipe closed early. Expected %zu bytes, got %zu", buf_len, bytes_read);
        goto defer;
    }

    Image image = {
        .data = buf,
        .width = width,
        .height = height,
        .mipmaps = 1,
        .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8,
    };

    state->screenshot = LoadTextureFromImage(image);

    UnloadImage(image);

defer:
    if (pipe) pclose(pipe);
    if (buffer) free(buffer);
}
#endif

void get_screen(State* state)
{
    CHECK_NULL(state);

#if CAPTURE_METHOD == CAPTURE_METHOD_PORTAL
    get_screen_portal(state);
#elif CAPTURE_METHOD == CAPTURE_METHOD_GRIM
    get_screen_grim(state);
#endif
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

Image take_screenshot(State state)
{
    Rectangle selection = fix_rec(state.selection);

    // Clamp left
    if (selection.x < 0)
    {
        selection.width += selection.x;
        selection.x = 0;
    }
    if (selection.y < 0)
    {
        selection.height += selection.y;
        selection.y = 0;
    }

    // Clamp right
    if (selection.x + selection.width > state.screenshot.width)
    {
        selection.width = fmaxf(0, state.screenshot.width - selection.x);
    }
    if (selection.y + selection.height > state.screenshot.height)
    {
        selection.height = fmaxf(0, state.screenshot.height - selection.y);
    }

    RenderTexture2D texture = LoadRenderTexture(selection.width, selection.height);

    BeginTextureMode(texture);

    ClearBackground(BLANK);

    DrawTexture(state.screenshot, -selection.x, -selection.y, WHITE);

    DrawTextureRec(state.canvas.texture,
                   (Rectangle){
                       selection.x,
                       -selection.y,
                       state.canvas.texture.width,
                       -state.canvas.texture.height,
                   },
                   (Vector2){0, 0}, WHITE);

    EndTextureMode();

    Image image = LoadImageFromTexture(texture.texture);
    ImageFlipVertical(&image);
    return image;
}

void save_screenshot(Image image)
{
    char timeString[100];
    time_t rawTime;
    struct tm* timeInfo;
    time(&rawTime);
    timeInfo = localtime(&rawTime);
    strftime(timeString, sizeof(timeString), TIME_FORMAT, timeInfo);

    size_t mark = nob_temp_save();
    const char* path =
        nob_temp_sprintf("%s/%s", getenv("HOME"), nob_temp_sprintf(SCREENSHOT_PATH, timeString));
    ExportImage(image, path);
    nob_temp_rewind(mark);
}

bool copy_image(Image image)
{
    bool result = true;

    int dataSize = 0;
    unsigned char* pngData = ExportImageToMemory(image, ".png", &dataSize);
    FILE* clipboardPipe = NULL;

    if (pngData == NULL)
    {
        nob_log(NOB_ERROR, "Failed to ExportImageToMemory");
        return_defer(false);
    }

    const char* displayServer = getenv("XDG_SESSION_TYPE");
    if (displayServer != NULL && strcmp(displayServer, "wayland") == 0)
    {
        clipboardPipe = popen("wl-copy --type image/png", "w");
    }
    else
    {
        clipboardPipe = popen("xclip -selection clipboard -t image/png", "w");
    }

    if (clipboardPipe == NULL)
    {
        nob_log(NOB_ERROR, "Failed to locate native Linux clipboard tools (wl-copy/xclip).");
        return_defer(false);
    }

    fwrite(pngData, sizeof(pngData[0]), dataSize, clipboardPipe);

defer:
    if (pngData) MemFree(pngData);
    if (clipboardPipe) pclose(clipboardPipe);
    return result;
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

    if (IsKeyPressed(KEY_ZERO)) reset_camera(state);
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
