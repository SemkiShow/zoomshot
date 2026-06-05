#include "config.h"
#include <glib.h>
#include <libportal/portal-helpers.h>
#include <libportal/screenshot.h>
#include <libportal/types.h>
#include <raylib.h>
#include <raymath.h>
#include <stdio.h>

#define NOB_IMPLEMENTATION
#include "nob.h"

typedef struct
{
    GMainLoop* startup_loop;
    Texture screenshot;
    char* filename;
    Camera2D camera;
    Vector2 last_mouse_pos;
} State;

State state_new()
{
    return (State){
        .startup_loop = g_main_loop_new(NULL, FALSE),
        .screenshot = {0},
        .filename = NULL,
        .camera =
            (Camera2D){
                .zoom = 1,
            },
        .last_mouse_pos = {0},
    };
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

int main(void)
{
    XdpPortal* portal = xdp_portal_new();
    State state = state_new();

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
    flags |= FLAG_WINDOW_TOPMOST;
    SetConfigFlags(flags);

    InitWindow(800, 600, "zoomshot");

    state.screenshot = LoadTexture(state.filename);

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
            Vector2 delta = Vector2Subtract(mouse_pos, state.last_mouse_pos);
            state.camera.target.x -= delta.x / state.camera.zoom;
            state.camera.target.y -= delta.y / state.camera.zoom;
        }

        BeginMode2D(state.camera);
        DrawTexture(state.screenshot, 0, 0, WHITE);
        EndMode2D();

        state.last_mouse_pos = mouse_pos;

        EndDrawing();
    }

    g_object_unref(portal);
    CloseWindow();

    return 0;
}
