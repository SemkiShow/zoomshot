#include "config.h"
#include <glib.h>
#include <libportal/portal-helpers.h>
#include <libportal/screenshot.h>
#include <raylib.h>
#include <raymath.h>
#include <stdio.h>

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
} Tool;

typedef struct
{
    GMainLoop* startup_loop;
    char* screenshot_filename;
    Texture screenshot;
    bool loop;
    Mode mode;
    Tool tool;
    float tool_thickness;
    Color tool_color;
    Camera2D camera;
    RenderTexture2D canvas;
    RenderTexture2D mask;
    Vector2 last_mouse_pos;
    Rectangle selection;
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
        state->camera.zoom = INITIAL_ZOOM;
        state->camera.target = (Vector2){
            GetRenderWidth() / INITIAL_ZOOM,
            GetRenderHeight() / INITIAL_ZOOM,
        };
        state->camera.offset = (Vector2){
            GetRenderWidth() / INITIAL_ZOOM,
            GetRenderHeight() / INITIAL_ZOOM,
        };
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
    state.startup_loop = g_main_loop_new(NULL, FALSE);
    state.loop = true;
    state.tool_thickness = INITIAL_THICKNESS;
    state.tool_color = RED;
    screenshot_mode(&state);
    return state;
}

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

void usage(const char* program)
{
    printf("Usage: %s [OPTION...]\n", program);
    printf("A simple zoomer/screenshotter app for Linux.\n");
    printf("\n");
    printf("-h, --help          Print this help message\n");
    printf("-s, --screenshot    Start the the screenshot mode (default)\n");
    printf("-z, --zoom          Start the the zoom mode\n");
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

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
    {
        DrawCircleV(last_world_mouse, state->tool_thickness / 2.0f, state->tool_color);
    }

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
    DrawLineEx(last_world_mouse, current_world_mouse, state->tool_thickness,
               (Color){255, 255, 255, 0});
    EndBlendMode();
    EndTextureMode();
}

Image take_screenshot(State state)
{
    Rectangle selection = state.selection;

    // Fix negative sizes
    if (selection.width < 0)
    {
        selection.x += selection.width;
        selection.width *= -1;
    }
    if (selection.height < 0)
    {
        selection.y += selection.height;
        selection.height *= -1;
    }

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

int main(int argc, char* argv[])
{
    State state = state_new();

    XdpPortal* portal = xdp_portal_new();
    xdp_portal_take_screenshot(portal, NULL, XDP_SCREENSHOT_FLAG_NONE, NULL, on_screenshot_ready,
                               &state);

    // Wait for the screenshot to be taken
    g_main_loop_run(state.startup_loop);
    g_main_loop_unref(state.startup_loop);
    state.startup_loop = NULL;

    unsigned int flags = 0;
    flags |= FLAG_VSYNC_HINT;
    flags |= FLAG_FULLSCREEN_MODE;
    flags |= FLAG_WINDOW_UNDECORATED;
    SetConfigFlags(flags);

    InitWindow(800, 600, "zoomshot");

    state.screenshot = LoadTexture(state.screenshot_filename);
    state.canvas = LoadRenderTexture(state.screenshot.width, state.screenshot.height);
    state.mask = LoadRenderTexture(state.screenshot.width, state.screenshot.height);

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

        float wheel = GetMouseWheelMove();
        if (wheel != 0.0f) zoom_tool(&state);

        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT))
        {
            if (IsKeyDown(KEY_LEFT_SHIFT))
                move_tool(&state);
            else
            {
                switch (state.tool)
                {
                case Tool_Move:
                    move_tool(&state);
                    break;
                case Tool_Select:
                {
                    Vector2 mouse_world_pos = GetScreenToWorld2D(mouse_pos, state.camera);
                    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
                    {
                        state.selection.x = mouse_world_pos.x;
                        state.selection.y = mouse_world_pos.y;
                    }
                    state.selection.width = mouse_world_pos.x - state.selection.x;
                    state.selection.height = mouse_world_pos.y - state.selection.y;
                }
                break;
                case Tool_Pencil:
                    pencil_tool(&state);
                    break;
                case Tool_Eraser:
                    eraser_tool(&state);
                    break;
                }
            }
        }

        if (IsMouseButtonDown(MOUSE_BUTTON_MIDDLE)) move_tool(&state);

        if (IsKeyPressed(KEY_ZERO)) reset_camera(&state);
        if (IsKeyPressed(KEY_S))
        {
            if (IsKeyDown(KEY_LEFT_CONTROL))
            {
                Image screenshot = take_screenshot(state);
                save_screenshot(screenshot);
                UnloadImage(screenshot);
            }
            else
            {
                state.mode = Mode_Screenshot;
                state.tool = Tool_Select;
            }
        }
        if (IsKeyPressed(KEY_Z))
        {
            state.mode = Mode_Zoom;
            state.tool = Tool_Move;
        }
        if (IsKeyPressed(KEY_V)) state.tool = Tool_Select;
        if (IsKeyPressed(KEY_M)) state.tool = Tool_Move;
        if (IsKeyPressed(KEY_P)) state.tool = Tool_Pencil;
        if (IsKeyPressed(KEY_E)) state.tool = Tool_Eraser;
        if (IsKeyDown(KEY_LEFT_BRACKET))
        {
            state.tool_thickness -= THICKNESS_SPEED * GetFrameTime();
            state.tool_thickness = fmaxf(0, state.tool_thickness);
        }
        if (IsKeyDown(KEY_RIGHT_BRACKET))
        {
            state.tool_thickness += THICKNESS_SPEED * GetFrameTime();
            state.tool_thickness = fminf(MAX_THICKNESS, state.tool_thickness);
        }
        if (IsKeyPressed(KEY_C))
        {
            if (IsKeyDown(KEY_LEFT_CONTROL))
            {
                Image screenshot = take_screenshot(state);
                copy_image(screenshot);
                UnloadImage(screenshot);
            }
        }
        if (IsKeyPressed(KEY_ENTER))
        {
            Image screenshot = take_screenshot(state);
            save_screenshot(screenshot);
            copy_image(screenshot);
            UnloadImage(screenshot);
            state.loop = false;
        }

        if (state.mode == Mode_Screenshot)
        {
            BeginTextureMode(state.mask);

            ClearBackground(BLANK);
            DrawRectangle(0, 0, state.mask.texture.width, state.mask.texture.height,
                          ColorAlpha(GRAY, 0.75));

            BeginBlendMode(BLEND_SUBTRACT_COLORS);
            Rectangle selection = state.selection;
            if (selection.width < 0)
            {
                selection.x += selection.width;
                selection.width *= -1;
            }
            if (selection.height < 0)
            {
                selection.y += selection.height;
                selection.height *= -1;
            }
            DrawRectangleRec(selection, (Color){255, 255, 255, 0});
            EndBlendMode();

            EndTextureMode();
        }

        state.last_mouse_pos = mouse_pos;

        BeginDrawing();

        ClearBackground(BLACK);

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

        if (state.mode == Mode_Screenshot)
        {
            DrawTextureRec(state.mask.texture,
                           (Rectangle){
                               0,
                               0,
                               state.mask.texture.width,
                               -state.mask.texture.height,
                           },
                           (Vector2){0, 0}, WHITE);
        }

        EndMode2D();

        EndDrawing();
    }

    g_object_unref(portal);
    CloseWindow();

    return 0;
}
