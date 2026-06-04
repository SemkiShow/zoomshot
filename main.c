#include <raylib.h>
#include <stdio.h>

int main(void)
{
    unsigned int flags = 0;
    flags |= FLAG_VSYNC_HINT;
    flags |= FLAG_FULLSCREEN_MODE;
    flags |= FLAG_WINDOW_UNDECORATED;
    flags |= FLAG_WINDOW_TOPMOST;
    SetConfigFlags(flags);

    InitWindow(800, 600, "zoomshot");

    while (!WindowShouldClose())
    {
        BeginDrawing();

        ClearBackground(BLACK);

        DrawRectangle(0, 0, 100, 100, RED);

        EndDrawing();
    }

    CloseWindow();

    return 0;
}
