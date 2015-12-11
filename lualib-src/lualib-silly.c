#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/time.h>
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#include "silly_env.h"
#include "silly_message.h"
#include "silly_worker.h"
#include "silly_socket.h"
#include "silly_malloc.h"
#include "silly_timer.h"

static void
_register_cb(lua_State *L, void *key)
{
        lua_pushlightuserdata(L, key);
        lua_insert(L, -2);
        lua_settable(L, LUA_REGISTRYINDEX);
}

static void
_exit(lua_State *L)
{
        int err;
        int type;
        lua_pushlightuserdata(L, _exit);
        lua_gettable(L, LUA_REGISTRYINDEX);
        type = lua_type(L, -1);
        if (type == LUA_TFUNCTION) {
                err = lua_pcall(L, 0, 0, 0);
                if (err != 0)
                        fprintf(stderr, "_process_exit call fail:%s\n", lua_tostring(L, -1));
        } else {
                fprintf(stderr, "_process_exit invalid type:%d\n", type);
        }
}

static void
_timer_entry(lua_State *L, struct silly_message *m)
{
        int type;
        int err;
        struct silly_message_timer *tm = (struct silly_message_timer *)(m + 1);

        lua_pushlightuserdata(L, _timer_entry);
        lua_gettable(L, LUA_REGISTRYINDEX);
        type = lua_type(L, -1);
        if (type == LUA_TFUNCTION) {
                lua_pushinteger(L, tm->session);
                err = lua_pcall(L, 1, 0, 0);
                if (err != 0)
                        fprintf(stderr, "timer handler call fail:%s\n", lua_tostring(L, -1));

        } else if (type != LUA_TNIL) {
                fprintf(stderr, "_process_timer invalid type:%d\n", type);
        } else {
                fprintf(stderr, "_process_timer2 invalid type:%d\n", type);
        }

        return ;
}

static void
_socket_entry(lua_State *L, struct silly_message *m)
{
        int err;
        
        struct silly_message_socket *sm = (struct silly_message_socket *)(m + 1);
        lua_pushlightuserdata(L, _socket_entry);
        lua_gettable(L, LUA_REGISTRYINDEX);
        lua_pushinteger(L, m->type);
        lua_pushinteger(L, sm->sid);
        lua_pushinteger(L, sm->portid);
        lua_pushlightuserdata(L, m);
        err = lua_pcall(L, 4, 0, 0);
        if (err != 0)
                fprintf(stderr, "_socket_entry call failed:%s\n", lua_tostring(L, -1));

        return ;
}

static int
_lworkid(lua_State *L)
{
        int workid;
        struct silly_worker *w;
        w = lua_touserdata(L, lua_upvalueindex(1));
        workid = silly_worker_getid(w);

        lua_pushinteger(L, workid);

        return 1;
}

static int
_lgetenv(lua_State *L)
{
        const char *key = luaL_checkstring(L, 1);
        const char *value = silly_env_get(key);
        if (value)
                lua_pushstring(L, value);
        else
                lua_pushnil(L);

        return 1;
}

static int
_lsetenv(lua_State *L)
{
        const char *key = luaL_checkstring(L, 1);
        const char *value = luaL_checkstring(L, 2);

        silly_env_set(key, value);

        return 0;
}

static int
_lexit(lua_State *L)
{
        _register_cb(L, _exit);
        return 0;
}

static int
_ltimer_entry(lua_State *L)
{
        _register_cb(L, _timer_entry);
        return 0;
}

static int
_ltimer_add(lua_State *L)
{
        int err;
        int workid;
        int time;
        uint64_t session;
        struct silly_worker *w;
        w = lua_touserdata(L, lua_upvalueindex(1));
 
        time = luaL_checkinteger(L, 1);
        session = luaL_checkinteger(L, 2);

        workid = silly_worker_getid(w);

        err = timer_add(time, workid, session);

        lua_pushinteger(L, err);

        return 1;
}

static int
_ltimer_now(lua_State *L)
{
        uint32_t ms = timer_now();
        lua_pushinteger(L, ms);
        return 1;
}



static int
_lsocket_entry(lua_State *L)
{
        _register_cb(L, _socket_entry);
        return 0;
}


static int
_lsocket_connect(lua_State *L)
{
        const char *ip;
        int port;
        int err;
        int workid;
        struct silly_worker *w;
        
        w = lua_touserdata(L, lua_upvalueindex(1));
        workid = silly_worker_getid(w);


        ip = luaL_checkstring(L, 1);
        port = luaL_checkinteger(L, 2);

        err = silly_socket_connect(ip, port, workid);

        lua_pushinteger(L, err);

        return 1;
}

static int
_lsocket_close(lua_State *L)
{
        int err;
        int sid;

        sid = luaL_checkinteger(L, 1);

        err = silly_socket_close(sid);

        lua_pushinteger(L, err);

        return 1;
}

static int
_lsocket_send(lua_State *L)
{
        int sid;
        uint8_t *buff;
        int size;
 
        sid = luaL_checkinteger(L, 1);
        buff = luaL_checkudata(L, 2, "silly_socket_packet");
        size = luaL_checkinteger(L, 3);

        silly_socket_send(sid, buff, size);

        return 0;
}

static int
_ldrop(lua_State *L)
{
        struct silly_message *m = (struct silly_message *)lua_touserdata(L, 1);
        if (m->type == SILLY_SOCKET_DATA) {
                struct silly_message_socket *sm = (struct silly_message_socket *)(m + 1);
                assert(sm->data);
                silly_free(sm->data);
        }
        return 0;
}

static void
_process_msg(lua_State *L, struct silly_message *msg)
{

        switch (msg->type) {
        case SILLY_TIMER_EXECUTE:
                //fprintf(stderr, "silly_worker:_process:%d\n", w->workid);
                _timer_entry(L, msg);
                break;
        case SILLY_SOCKET_ACCEPT:
        case SILLY_SOCKET_CLOSE:
        case SILLY_SOCKET_CONNECTED:
        case SILLY_SOCKET_DATA:
                //fprintf(stderr, "silly_worker:_process:socket\n");
                _socket_entry(L, msg);
                break;
        default:
                fprintf(stderr, "silly_worker:_process:unknow message type:%d\n", msg->type);
                assert(0);
                break;
        }
}

int luaopen_silly(lua_State *L)
{
        luaL_Reg tbl[] = {
                //core
                {"workid",      _lworkid},
                {"getenv",      _lgetenv},
                {"setenv",      _lsetenv},
                {"exit",        _lexit},
                //timer
                {"timerentry",  _ltimer_entry},
                {"timeradd",    _ltimer_add},
                {"timernow",    _ltimer_now},
                //socket
                {"socketentry",         _lsocket_entry},
                {"socketconnect",       _lsocket_connect},
                {"socketclose",         _lsocket_close},
                {"socketsend",          _lsocket_send},
                {"dropmessage",          _ldrop},
                //end
                {NULL, NULL},
        };
 
        luaL_checkversion(L);


        luaL_newmetatable(L, "silly_socket_data");
        luaL_newmetatable(L, "silly_socket_packet");

        lua_rawgeti(L, LUA_REGISTRYINDEX, LUA_RIDX_MAINTHREAD);
        lua_State *m = lua_tothread(L, -1);
        lua_pop(L, 1);

        lua_pushlightuserdata(L, (void *)m);
        lua_gettable(L, LUA_REGISTRYINDEX);
        struct silly_worker *w = lua_touserdata(L, -1);
        assert(w);

        silly_worker_message(w, _process_msg);
        silly_worker_exit(w, _exit);

        luaL_newlibtable(L, tbl);
        lua_pushlightuserdata(L, (void *)w);
        luaL_setfuncs(L, tbl, 1);
        

        return 1;
}

