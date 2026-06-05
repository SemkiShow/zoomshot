#include "screenshot.h"
#include "nob.h"
#include <math.h>

#if CAPTURE_METHOD == CAPTURE_METHOD_PORTAL
#include <glib.h>
#include <libportal/portal-helpers.h>
#include <libportal/screenshot.h>
#endif

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
static void get_screen_portal(State* state)
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
static void get_screen_grim(State* state)
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
