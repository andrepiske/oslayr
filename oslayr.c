#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <lua.h>
#include <lauxlib.h>
#include <errno.h>

int _f_exec_n_pipe(lua_State *L) {
    int i, pa[2];
    pid_t childpid;
    size_t re;
    const char *ex_name = luaL_checkstring(L, 2);
    const int red_stderr = lua_toboolean(L, 1);
    char *cmd[128];
    volatile char *err_flag;

    char *buffer = 0;
    size_t buffer_sz=0, total_read=0;

    for (i = 0; lua_isstring(L, i+3); ++i)
        cmd[i] = strdup(lua_tostring(L, i+3));
    cmd[i] = 0;

    pipe(pa);
    err_flag = (volatile char*)mmap(0, 1, PROT_READ | PROT_WRITE,
        MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    *err_flag = 0;

    childpid = fork();
    if (!childpid) {
        close(pa[0]);
        dup2(pa[1], 1);
        if (red_stderr)
            dup2(pa[1], 2);
        else
            close(2);
        close(pa[1]);
        execvp(ex_name, cmd);
        *err_flag = 1;
        close(1);
        if (red_stderr)
            close(2);
        exit(1);
    }

    close(pa[1]);
    for (;;) {
        const size_t will = 1024;
        if (total_read + will >= buffer_sz) {
            buffer_sz += will * 4;
            buffer = realloc(buffer, buffer_sz);
        }
        re = read(pa[0], (void*)&buffer[total_read], will);
        if (!re) {
            if (*err_flag) {
                munmap((void*)err_flag, 1);
                close(pa[0]);
                free(buffer);
                return 0;
            }
        }
        if (re <= 0)
            break;
        total_read += re;
    }
    buffer[total_read] = 0;
    lua_pushstring(L, buffer);
    free(buffer);
    munmap((void*)err_flag, 1);
    close(pa[0]);
    return 1;
}

static const luaL_Reg g_lreg[] = {
    { "exec_n_pipe", _f_exec_n_pipe },
    {0, 0},
};

int luaopen_oslayr(lua_State *L) {
    luaL_register(L, "oslayr", g_lreg);
    return 1;
}


