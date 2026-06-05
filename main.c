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

typedef struct
{
    GMainLoop* startup_loop;
    Texture screenshot;
    char* filename;
    Mode mode;
    RenderTexture2D canvas;
    Camera2D camera;
    Vector2 last_mouse_pos;
} State;

State state_new()
{
    State state = {0};
    state.startup_loop = g_main_loop_new(NULL, FALSE);
    state.camera.zoom = 1.0f;
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

    state->filename = strdup(filename);

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
    printf("-z, --zoom          Start the the zoom mode\n");
    printf("-s, --screenshot    Start the the screenshot mode (default)\n");
}

int main(int argc, char* argv[])
{
    XdpPortal* portal = xdp_portal_new();
    State state = state_new();

    xdp_portal_take_screenshot(portal, NULL, XDP_SCREENSHOT_FLAG_NONE, NULL, on_screenshot_ready,
                               &state);

    // Wait for the screenshot to be taken
    g_main_loop_run(state.startup_loop);
    g_main_loop_unref(state.startup_loop);
    state.startup_loop = NULL;

    const char* program = nob_shift_args(&argc, &argv);
    while (argc)
    {
        char* flag = nob_shift_args(&argc, &argv);
        if (strcmp(flag, "-h") == 0 || strcmp(flag, "--help") == 0)
        {
            usage(program);
            exit(0);
        }
        else if (strcmp(flag, "-z") == 0 || strcmp(flag, "--zoom") == 0)
        {
            state.mode = Mode_Zoom;
        }
        else if (strcmp(flag, "-s") == 0 || strcmp(flag, "--screenshot") == 0)
        {
            state.mode = Mode_Screenshot;
        }
        else
        {
            printf("Unknown flag: %s\n", flag);
            usage(program);
            exit(0);
        }
    }

    unsigned int flags = 0;
    flags |= FLAG_VSYNC_HINT;
    flags |= FLAG_FULLSCREEN_MODE;
    flags |= FLAG_WINDOW_UNDECORATED;
    flags |= FLAG_WINDOW_TOPMOST;
    SetConfigFlags(flags);

    InitWindow(800, 600, "zoomshot");

    state.screenshot = LoadTexture(state.filename);
    state.canvas = LoadRenderTexture(state.screenshot.width, state.screenshot.height);

    while (!WindowShouldClose())
    {
        BeginDrawing();

        ClearBackground(BLACK);

        Vector2 mouse_pos = GetMousePosition();

        float wheel = GetMouseWheelMove();
        if (wheel != 0.0f)
        {
            Vector2 mouse_world_pos = GetScreenToWorld2D(mouse_pos, state.camera);
            state.camera.target = mouse_world_pos;
            state.camera.offset = mouse_pos;
            state.camera.zoom += wheel * MOUSE_WHEEL_SENSITIVITY;
            if (state.camera.zoom < MIN_ZOOM) state.camera.zoom = MIN_ZOOM;
        }

        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT))
        {
            if (state.mode == Mode_Zoom || IsKeyDown(KEY_LEFT_SHIFT))
            {
                Vector2 delta = Vector2Subtract(mouse_pos, state.last_mouse_pos);
                state.camera.target.x -= delta.x / state.camera.zoom;
                state.camera.target.y -= delta.y / state.camera.zoom;
            }
            else
            {
                Vector2 current_world_mouse = GetScreenToWorld2D(mouse_pos, state.camera);
                Vector2 last_world_mouse = GetScreenToWorld2D(state.last_mouse_pos, state.camera);

                BeginTextureMode(state.canvas);
                DrawLineEx(last_world_mouse, current_world_mouse, PENCIL_THICCNESS, RED);
                EndTextureMode();
            }
        }

        if (IsMouseButtonDown(MOUSE_BUTTON_MIDDLE))
        {
            Vector2 delta = Vector2Subtract(mouse_pos, state.last_mouse_pos);
            state.camera.target.x -= delta.x / state.camera.zoom;
            state.camera.target.y -= delta.y / state.camera.zoom;
        }

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

        state.last_mouse_pos = mouse_pos;

        EndDrawing();
    }

    g_object_unref(portal);
    CloseWindow();

    return 0;
}
