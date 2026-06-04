#define NOB_IMPLEMENTATION
#define NOB_EXPERIMENTAL_DELETE_OLD
#include "nob.h"

void raylib(Nob_Cmd* cmd)
{
    nob_cmd_append(cmd, "-I.", "-I./raylib-6.0_linux_amd64/include");
    nob_cmd_append(cmd, "-L./raylib-6.0_linux_amd64/lib", "-l:libraylib.a", "-lm", "-lX11");
}

int main(int argc, char** argv)
{
    NOB_GO_REBUILD_URSELF(argc, argv);

    Nob_Cmd cmd = {0};
    nob_cmd_append(&cmd, "cc", "-Wall", "-Wextra", "-o", "zoomshot");
    nob_cmd_append(&cmd, "main.c");
    raylib(&cmd);
    if (!nob_cmd_run(&cmd)) return 1;

    return 0;
}
