#define NOB_IMPLEMENTATION
#define NOB_EXPERIMENTAL_DELETE_OLD
#include "nob.h"

int main(int argc, char** argv)
{
    NOB_GO_REBUILD_URSELF(argc, argv);

    Nob_Cmd cmd = {0};
    nob_cmd_append(&cmd, "cc", "-Wall", "-Wextra", "-o", "zoomshot");
    nob_cmd_append(&cmd, "main.c");
    if (!nob_cmd_run(&cmd)) return 1;

    return 0;
}
