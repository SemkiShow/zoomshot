#define NOB_IMPLEMENTATION
#define NOB_EXPERIMENTAL_DELETE_OLD
#include "nob.h"

void raylib(Nob_Cmd* cmd)
{
    nob_cmd_append(cmd, "-I.", "-I./raylib-6.0_linux_amd64/include");
    nob_cmd_append(cmd, "-L./raylib-6.0_linux_amd64/lib", "-l:libraylib.a", "-lm", "-lX11");
}

#define LIBPORTAL_FLAGS_PATH "libportal-flags.txt"
bool libportal(Nob_Cmd* cmd)
{
    bool result = true;

    Nob_Cmd portal_cmd = {0};

    nob_cmd_append(&portal_cmd, "pkg-config", "--cflags", "--libs", "libportal", "glib-2.0");
    if (!nob_cmd_run(&portal_cmd, .stdout_path = LIBPORTAL_FLAGS_PATH)) return_defer(false);

    Nob_String_Builder flags = {0};
    nob_read_entire_file(LIBPORTAL_FLAGS_PATH, &flags);
    Nob_String_View flags_sv = nob_sb_to_sv(flags);
    flags_sv = nob_sv_trim(flags_sv);
    size_t mark = nob_temp_save();
    while (flags_sv.count > 0)
    {
        Nob_String_View flag = nob_sv_chop_by_delim(&flags_sv, ' ');
        nob_cmd_append(cmd, nob_temp_sv_to_cstr(flag));
    }
    nob_temp_rewind(mark);

    nob_delete_file(LIBPORTAL_FLAGS_PATH);

defer:
    nob_cmd_free(portal_cmd);
    nob_sb_free(flags);
    return result;
}

int main(int argc, char* argv[])
{
    NOB_GO_REBUILD_URSELF(argc, argv);

    Nob_Cmd cmd = {0};
    nob_cmd_append(&cmd, "cc", "-Wall", "-Wextra", "-o", "zoomshot");
    nob_cmd_append(&cmd, "main.c");
    raylib(&cmd);
    if (!libportal(&cmd)) return 1;
    if (!nob_cmd_run(&cmd)) return 1;

    return 0;
}
