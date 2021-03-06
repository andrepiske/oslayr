/* 
*
* Copyright (c) 2013 André Diego Piske
* 
* Permission is hereby granted, free of charge, to any person obtaining a copy of
* this software and associated documentation files (the "Software"), to deal in
* the Software without restriction, including without limitation the rights to
* use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
* of the Software, and to permit persons to whom the Software is furnished to do
* so, subject to the following conditions:
* 
* The above copyright notice and this permission notice shall be included in all
* copies or substantial portions of the Software.
* 
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
* FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
* COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
* IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
* CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/select.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>
#include <lua.h>
#include <lauxlib.h>
#include <errno.h>
#include <signal.h>
// #include <pthread.h>

#define OSLAYR_EXECTYPE "oslayr_exec"

struct s_exec {
    pid_t pid;
    int pin[2];
    int pout[2];
    int perr[2];
    void *bufferout;
    size_t bosize; // , boused;
    int closed : 1;
    // int do_bufferout;
    // pthread_mutex *bo_mx;
};

static struct s_exec*
_get_exec(lua_State *L) {
    return (struct s_exec*)luaL_checkudata(L, 1, OSLAYR_EXECTYPE);
}

static void
_exec_assert_bs(struct s_exec *se, size_t amount) {
    if (se->bosize >= amount)
        return;
    se->bosize = amount;
    if (se->bufferout)
        free(se->bufferout);
    se->bufferout = malloc(amount);
}

/*
static int 
_exec_read_with_bufferout(lua_State *L, struct s_exec *se, size_t amount) {
    size_t maxread;
    pthread_mutex_lock(se->bo_mx);
    maxread = 
    pthread_mutex_unlock(se->bo_mx);
    return 0;
}
*/

static int
_f_read_out(lua_State *L) {
    struct s_exec *se = _get_exec(L);
    size_t amount = 1024;
    ssize_t r;

    if (lua_isnumber(L, 2))
        amount = (size_t)lua_tonumber(L, 2);
    
    if (!amount)
        return 0;

    /*
    if (se->do_bufferout)
        return _exec_read_with_bufferout(L, se, amount);
    */

    _exec_assert_bs(se, amount);

    r = read(se->pout[0], (void*)se->bufferout, amount);
    lua_pushnumber(L, r);
    if (r <= 0)
        return 1;

    lua_pushlstring(L, se->bufferout, r);
    return 2;
}

static int
_f_poll(lua_State *L) {
    struct s_exec *se = _get_exec(L);
    double timeout = 0.0;

    if (lua_isnumber(L, 2)) {
        timeout = (double)lua_tonumber(L, 2);
        if (timeout < 0.0)
            return luaL_error(L, "Timeout must be non-negative");
    }

    unsigned long int to_us = (unsigned long int)(timeout * 1000.0);

    struct timeval tv;
    tv.tv_sec = to_us / 1000000;
    tv.tv_usec = to_us % 1000000;
    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(se->pout[0], &read_fds);
    FD_SET(se->perr[0], &read_fds);
    const int fd = se->pout[0] > se->perr[0] ? se->pout[0] : se->perr[0];
    const int e = select(fd + 1, &read_fds, 0, 0, &tv);
    if (e < 0)
        return luaL_error(L, "select() error");

    lua_pushboolean(L, FD_ISSET(se->pout[0], &read_fds) ? 1 : 0);
    lua_pushboolean(L, FD_ISSET(se->perr[0], &read_fds) ? 1 : 0);
    return 2;
}
        
// read(f, read limit, stdout or stderr, non-blocking?)
static int
_f_read(lua_State *L) {
    struct s_exec *se = _get_exec(L);
    int read_limit = 1024;
    int fd = se->pout[0];
    int is_blocking = 1;

    if (!lua_isnil(L, 2)) {
        read_limit = (int)lua_tonumber(L, 2);
        if (read_limit < 0)
            return luaL_error(L, "Read limit must be non-negative");
    }

    if (!lua_isnil(L, 3)) {
        const int e = (int)lua_tonumber(L, 3);
        if (e == 2)
            fd = se->perr[0];
        else if (e != 1)
            return luaL_error(L, "Invalid read source (must be 1 or 2)");
    }

    if (lua_toboolean(L, 4))
        is_blocking = 0;

    if (!is_blocking) {
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 0;
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(fd, &read_fds);
        const int e = select(fd + 1, &read_fds, 0, 0, &tv);
        if (e < 0)
            return luaL_error(L, "select() error");
        if (!e) {
            lua_pushboolean(L, 0);
            return 1;
        }
    }

    _exec_assert_bs(se, (size_t)read_limit);

    const ssize_t total_read = read(fd, se->bufferout, read_limit);
    lua_pushboolean(L, 1);
    lua_pushnumber(L, (lua_Number)total_read);
    if (total_read)
        lua_pushlstring(L, (const char*)se->bufferout, (size_t)total_read);

    return total_read ? 3 : 2;
}

static int
_f_put(lua_State *L) {
    struct s_exec *se = _get_exec(L);
    const char *data;
    size_t datalen = 0, w;
    size_t offset=0, len=(size_t)-1;

    if (!lua_isstring(L, 2))
        return luaL_error(L, "Second argument must be a string");

    if (lua_isnumber(L, 3))
        offset = (size_t)lua_tonumber(L, 3);
    if (lua_isnumber(L, 4))
        len = (size_t)lua_tonumber(L, 4);

    data = lua_tolstring(L, 2, &datalen);
    if (offset > datalen)
        return luaL_error(L, "Invalid offset");

    if (len == (size_t)-1)
        len = datalen;
    if (len + offset > datalen)
        len = datalen - offset;

    w = write(se->pin[1], data+(ptrdiff_t)offset, len);
    
    lua_pushnumber(L, (lua_Number)w);
    return 1;
}

static int
_f_get_pipesize(lua_State *L) {
    struct s_exec *se = _get_exec(L);
    int s_in, s_out, s_err;
    s_in = fcntl(se->pin[1], 1032); // Linux Kernel >2.6 only, 1032==F_GETPIPE_SZ
    s_out = fcntl(se->pout[0], 1032);
    s_err = fcntl(se->perr[0], 1032);
    lua_pushnumber(L, (lua_Number)s_in);
    lua_pushnumber(L, (lua_Number)s_out);
    lua_pushnumber(L, (lua_Number)s_err);
    return 3;
}

static int
_f_waitend(lua_State *L) {
    struct s_exec *se = _get_exec(L);
    int status = 0;
    if (!se->pid)
        return 0;

    waitpid(se->pid, &status, 0);
    se->pid = 0;
    if (!(WIFEXITED(status)))
        return 0;
    lua_pushnumber(L, (lua_Number)(WEXITSTATUS(status)));
    return 1;
}


static int
_f_close(lua_State *L) {
    struct s_exec *se = _get_exec(L);
    if (se->closed)
        return luaL_error(L, "You may not call close() twice");

    if (se->pin[1] >= 0) close(se->pin[1]);
    if (se->pout[0] >= 0) close(se->pout[0]);
    if (se->perr[0] >= 0) close(se->perr[0]);
    if (se->bufferout)
        free(se->bufferout);
    se->bufferout = 0;
    se->bosize = 0;
    se->pin[1] = se->pout[0] = se->perr[0] = -1;
    se->closed = 1;
    return 0;
}

static int
_f_closeinput(lua_State *L) {
    struct s_exec *se = _get_exec(L);
    if (se->pin[1] >= 0)
        close(se->pin[1]);

    se->pin[1] = -1;
    return 0;
}


/*static int
exec_fin_bufferout
*/

/*
static int
_f_set_bufferout(lua_State *L) {
    struct s_exec *se = _get_exec(L);
    size_t bosize = (size_t)luaL_checknumber(L, 2);

    if (!bosize) {
        exec_fin_bufferout(se);
    }

    se->do_bufferout = 1;
    return 0;
}
*/

static void
exec_initmetatable(lua_State *L) {
    const luaL_Reg lr[] = {
        {"out", _f_read_out},
        // {"bufferout", _f_set_bufferout},
        {"read", _f_read},
        {"poll", _f_poll},
        {"put", _f_put},
        {"close", _f_close},
        {"close_input", _f_closeinput},
        {"waitend", _f_waitend},
        {"getpipesize", _f_get_pipesize},
        {0, 0}
    };
    lua_pushstring(L, "__index");
    luaL_newmetatable(L, OSLAYR_EXECTYPE);
    luaL_register(L, 0, lr);
    lua_rawset(L, -1);
}

// no resources are freed here on failure. hmmmm... so wrong that is
static int
exec_openprocess(const char *exname, const char**cmds, struct s_exec *se) {
    volatile char *erflag;
    char dummy;
    int pe[2];

    if (pipe(se->pin) || pipe(se->pout) || pipe(se->perr))
        return -1;

    if (pipe(pe) || fcntl(pe[1], F_SETFD, (int)FD_CLOEXEC))
        return -1;

    erflag = (volatile char*)mmap(0, 1, PROT_WRITE | PROT_READ,
        MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (!erflag)
        return -1;

    *erflag = 0;

    se->pid = fork();
    if (se->pid == -1)
        return -1;
        
    if (!se->pid) {
        char *const *ncmds = (char*const *)cmds; // it's a copy, it's another process.
        close(pe[0]);
        close(0),
        close(1),
        close(2),
        dup2(se->pin[0], 0);
        dup2(se->pout[1], 1);
        dup2(se->perr[1], 2);
        close(se->pin[1]);
        close(se->pout[0]);
        close(se->perr[0]);
        execvp(exname, ncmds);
        *erflag = 1;
        close(se->pin[0]);
        close(se->pout[1]);
        close(se->perr[1]);
        close(pe[1]);
        exit(1);
    }
    close(pe[1]);
    close(se->pin[0]);
    close(se->pout[1]);
    close(se->perr[1]);
    if (0 != read(pe[0], &dummy, 1)) {
        // wtf has happened we reached here?
        kill(se->pid, SIGTERM);
        return -1;
    }
    close(pe[0]);
    if (*erflag) {
        // execve has failed, free everything and quit.
        close(se->pin[1]);
        close(se->pout[0]);
        close(se->perr[0]);
        munmap((void*)erflag, 1);
        return 1;
    }

    // ok, we are up and running
    munmap((void*)erflag, 1);
    return 0;
}

static int
_f_exec(lua_State *L) {
    const char *exec_name;
    const char *cmds[128];
    int i, rv;
    struct s_exec *se;
    exec_name = luaL_checkstring(L, 1);
    if (!lua_istable(L, 2))
        return luaL_error(L, "Second argument must be a table (may not be nil)");

    for (i = 0;; ++i) {
        lua_pushnumber(L, i+1);
        lua_gettable(L, 2);
        if (!lua_isstring(L, -1))
            break;
        cmds[i] = lua_tostring(L, -1);
        lua_pop(L, 1);
    }
    cmds[i] = 0;

    se = (struct s_exec*)lua_newuserdata(L, sizeof(struct s_exec));
    rv = se ? exec_openprocess(exec_name, cmds, se) : -1;
    if (rv != 0) {
        lua_pushnumber(L, rv);
    } else {
        luaL_newmetatable(L, OSLAYR_EXECTYPE);
        lua_setmetatable(L, -2);
        se->bufferout = 0;
        se->bosize = 0;
        se->closed = 0;
        // se->boused = 0;
        // se->do_bufferout = 0;
        // se->bo_mx = 0;
    }

    return 1;
}

static int
_f_exec_n_pipe(lua_State *L) {
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
    { "exec", _f_exec },
    {0, 0},
};

int luaopen_oslayr(lua_State *L) {
    exec_initmetatable(L);
    luaL_register(L, "oslayr", g_lreg);
    return 1;
}


