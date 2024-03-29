/*
 * Astra Lua API
 * http://cesbo.com/astra
 *
 * Copyright (C) 2012-2013, Andrey Dyldin <and@cesbo.com>
 *                    2015, Artem Kharitonov <artem@sysert.ru>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <astra.h>
#include <luaapi/luaapi.h>

bool module_option_integer(lua_State *L, const char *name, int *integer)
{
    if (lua_type(L, MODULE_OPTIONS_IDX) != LUA_TTABLE)
        return false;

    lua_getfield(L, MODULE_OPTIONS_IDX, name);
    const int type = lua_type(L, -1);
    bool result = false;

    if (type == LUA_TNUMBER)
    {
        *integer = lua_tointeger(L, -1);
        result = true;
    }
    else if (type == LUA_TSTRING)
    {
        const char *str = lua_tostring(L, -1);
        *integer = atoi(str);
        result = true;
    }
    else if (type == LUA_TBOOLEAN)
    {
        *integer = lua_toboolean(L, -1);
        result = true;
    }

    lua_pop(L, 1);
    return result;
}

bool module_option_string(lua_State *L, const char *name, const char **string
                          , size_t *length)
{
    if (lua_type(L, MODULE_OPTIONS_IDX) != LUA_TTABLE)
        return false;

    lua_getfield(L, MODULE_OPTIONS_IDX, name);
    const int type = lua_type(L, -1);
    bool result = false;

    if (type == LUA_TSTRING)
    {
        if (length)
            *length = luaL_len(L, -1);
        *string = lua_tostring(L, -1);
        result = true;
    }

    lua_pop(L, 1);
    return result;
}

bool module_option_boolean(lua_State *L, const char *name, bool *boolean)
{
    if (lua_type(L, MODULE_OPTIONS_IDX) != LUA_TTABLE)
        return false;

    lua_getfield(L, MODULE_OPTIONS_IDX, name);
    const int type = lua_type(L, -1);
    bool result = false;

    if (type == LUA_TNUMBER)
    {
        *boolean = (lua_tointeger(L, -1) != 0) ? true : false;
        result = true;
    }
    else if (type == LUA_TSTRING)
    {
        const char *str = lua_tostring(L, -1);
        *boolean = (!strcmp(str, "true")
                    || !strcmp(str, "on")
                    || !strcmp(str, "1"));
        result = true;
    }
    else if (type == LUA_TBOOLEAN)
    {
        *boolean = lua_toboolean(L, -1);
        result = true;
    }

    lua_pop(L, 1);
    return result;
}
