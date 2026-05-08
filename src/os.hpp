#pragma once

struct lua_State;

namespace os
{
	int execute(lua_State* L);
	int copy_file(lua_State* L);
	int create_directory(lua_State* L);
}
