#include "lua_env.hpp"

#include <stdlib.h>
#include <string.h>

#include "generator.hpp"
#include "mem.hpp"
#include "net.hpp"
#include "os.hpp"
#include "project.hpp"
#include "state.hpp"
#include "string.hpp"

#include "fs.hpp"

namespace lua
{

	namespace
	{
		void* lua_alloc(void* ud, void* ptr, [[maybe_unused]] size_t osize, size_t nsize)
		{
			if (!nsize)
			{
				if (ptr)
					tfree(ptr);
				return nullptr;
			}
			else
			{
				if (!ptr)
					return malloc(nsize);

				return realloc(ptr, nsize);
			}
		}

		void collect_files(lua_State*  L,
		                   uint32_t&   res_idx,
		                   char const* dir_filter,
		                   char const* file_filter)
		{
			fs::list_files_res files = fs::list_files(dir_filter, file_filter);
			for (uint32_t i {0}; i < files.size; ++i)
			{
				lua_pushstring(L, files.files[i]);
				lua_rawseti(L, -2, res_idx++);
			}
			if (files.size)
				tfree(files.files);

			fs::list_dirs_res sub_dirs = fs::list_dirs(dir_filter);
			for (uint32_t i {0}; i < sub_dirs.size; ++i)
			{
				uint32_t dir_len = static_cast<uint32_t>(strlen(sub_dirs.dirs[i]));
				char*    filter = tmalloc<char>(dir_len + 2);
				strcpy(filter, sub_dirs.dirs[i]);
				strcpy(filter + dir_len, "/");
				collect_files(L, res_idx, filter, file_filter);

				tfree(filter);
				tfree(sub_dirs.dirs[i]);
			}
			if (sub_dirs.size)
				tfree(sub_dirs.dirs);
		}

		int32_t collect_files(lua_State* L)
		{
			luaL_argcheck(L, lua_isstring(L, 1), 1, "'string' expected");
			char const* lua_filter = lua_tostring(L, 1);
			uint32_t    lua_filter_len = strlen(lua_filter);

			lua_newtable(L);
			uint32_t res_idx = 1;

			uint32_t pos = UINT32_MAX;
			if ((pos = str::rfind(lua_filter, "**", lua_filter_len)) != UINT32_MAX)
			{
				char* filter = nullptr;
				if (fs::is_absolute(lua_filter))
				{
					filter = tmalloc<char>(pos + 1);
					strncpy(filter, lua_filter, pos);
					filter[pos] = '\0';
				}
				else
				{
					filter = lua::resolve_path_from_script(L, lua_filter, pos);
				}
				collect_files(L, res_idx, filter, lua_filter + pos + 2);
				tfree(filter);
			}
			else if ((pos = str::rfind(lua_filter, "*", lua_filter_len)) != UINT32_MAX)
			{
				char* filter = nullptr;
				if (fs::is_absolute(lua_filter))
				{
					filter = tmalloc<char>(pos + 1);
					strncpy(filter, lua_filter, pos);
					filter[pos] = '\0';
				}
				else
				{
					filter = lua::resolve_path_from_script(L, lua_filter, pos);
				}
				fs::list_files_res files = fs::list_files(filter, lua_filter + pos + 1);
				for (uint32_t i {0}; i < files.size; ++i)
				{
					lua_pushstring(L, files.files[i]);
					lua_rawseti(L, -2, res_idx++);
					tfree(files.files[i]);
				}
				if (files.size)
					tfree(files.files);
				tfree(filter);
			}
			return 1;
		}

		int32_t resolve_path(lua_State* L)
		{
			luaL_argcheck(L, lua_isstring(L, 1), 1, "'string' expected");
			char const* lua_path = lua_tostring(L, 1);
			uint32_t    lua_path_len = strlen(lua_path);

			char* resolved = lua::resolve_path_from_script(L, lua_path, lua_path_len);
			lua_pushstring(L, resolved);
			tfree(resolved);
			return 1;
		}

		int32_t configurations(lua_State* L)
		{
			luaL_argcheck(L, lua_istable(L, 1), 1, "'array' expected");

			g.config_size = lua_rawlen(L, 1);

			luaL_argcheck(L, g.config_size > 0, 1,
			              "expecting at least one configuration");

			g.configs = tmalloc<char const*>(g.config_size);

			for (uint32_t i {0}; i < g.config_size; ++i)
			{
				lua_rawgeti(L, 1, i + 1);
				char const* lua_str = lua_tostring(L, -1);
				char*       str = tmalloc<char>(strlen(lua_str) + 1);
				strcpy(str, lua_str);
				g.configs[i] = str;
				lua_pop(L, 1);
			}

			if (!g.config_param)
				g.config_param = g.configs[0];
			return 0;
		}

		int32_t add_pre_build_cmd(lua_State* L)
		{
			luaL_argcheck(L, lua_istable(L, 1), 1, "'table' expected");
			luaL_argcheck(L, lua_istable(L, 2), 2, "'table' expected");

			lua_getfield(L, 1, "pre_build_cmds");
			uint32_t len = lua_rawlen(L, -1);
			if (lua_isnil(L, -1))
			{
				lua_pop(L, 1);
				lua_newtable(L);
			}

			lua_newtable(L);
			lua_getfield(L, 2, "input");
			if (!lua_isnil(L, -1) && !lua_isstring(L, -1) && !lua_istable(L, -1))
				luaL_error(L, "'input': string or string array expected");
			else if (lua_isstring(L, -1))
			{
				char const* lua_input = lua_tostring(L, -1);
				if (strlen(lua_input))
				{
					if (fs::is_absolute(lua_input))
					{
						lua_pushstring(L, lua_input);
						lua_setfield(L, -3, "input");
					}
					else
					{
						char* input = resolve_path_from_script(L, lua_input);
						lua_pushstring(L, input);
						lua_setfield(L, -3, "input");
						tfree(input);
					}
				}
				lua_pop(L, 1);
			}
			else if (lua_istable(L, -1))
			{
				lua_newtable(L);
				uint32_t len = lua_rawlen(L, -2);
				for (uint32_t i {0}; i < len; ++i)
				{
					lua_rawgeti(L, -2, i + 1);
					if (!lua_isstring(L, -1))
						luaL_error(L, "'input': string or string array expected");
					char const* lua_input = lua_tostring(L, -1);
					if (strlen(lua_input))
					{
						if (fs::is_absolute(lua_input))
						{
							lua_pushstring(L, lua_input);
							lua_rawseti(L, -3, i + 1);
						}
						else
						{
							char* input = resolve_path_from_script(L, lua_input);
							lua_pushstring(L, input);
							lua_rawseti(L, -3, i + 1);
							tfree(input);
						}
					}
					lua_pop(L, 1);
				}
				lua_setfield(L, -3, "input");
				lua_pop(L, 1);
			}
			else
				lua_pop(L, 1);

			lua_getfield(L, 2, "output");
			if (lua_isnil(L, -1))
				luaL_error(L, "missing key: 'output'");
			else if (lua_isstring(L, -1))
			{
				char const* lua_output = lua_tostring(L, -1);
				if (strlen(lua_output))
				{
					if (fs::is_absolute(lua_output))
					{
						lua_pushstring(L, lua_output);
						lua_setfield(L, -3, "output");
					}
					else
					{
						char* output = resolve_path_from_script(L, lua_output);
						lua_pushstring(L, output);
						lua_setfield(L, -3, "output");
						tfree(output);
					}
				}
				lua_pop(L, 1);
			}
			else if (lua_istable(L, -1))
			{
				lua_newtable(L);
				uint32_t len = lua_rawlen(L, -2);
				for (uint32_t i {0}; i < len; ++i)
				{
					lua_rawgeti(L, -2, i + 1);
					if (!lua_isstring(L, -1))
						luaL_error(L, "'output': string or string array expected");
					char const* lua_output = lua_tostring(L, -1);
					if (strlen(lua_output))
					{
						if (fs::is_absolute(lua_output))
						{
							lua_pushstring(L, lua_output);
							lua_rawseti(L, -3, i + 1);
						}
						else
						{
							char* output = resolve_path_from_script(L, lua_output);
							lua_pushstring(L, output);
							lua_rawseti(L, -3, i + 1);
							tfree(output);
						}
					}
					lua_pop(L, 1);
				}
				lua_setfield(L, -3, "output");
				lua_pop(L, 1);
			}
			else
				lua_pop(L, 1);

			lua_getfield(L, 2, "cmd");
			if (lua_isnil(L, -1))
				luaL_error(L, "missing key: 'cmd'");
			else if (!lua_isstring(L, -1))
				luaL_error(L, "'cmd': string expected");
			else
			{
				char const* lua_cmd = lua_tostring(L, -1);
				if (!strlen(lua_cmd))
					luaL_error(L, "cmd cannot be empty");
				lua_pushstring(L, lua_cmd);
				lua_setfield(L, -3, "cmd");
				lua_pop(L, 1);
			}

			lua_rawseti(L, 3, len + 1);
			lua_setfield(L, 1, "pre_build_cmds");

			return 0;
		}

		int32_t add_pre_build_copy(lua_State* L)
		{
			luaL_argcheck(L, lua_istable(L, 1), 1, "'table' expected");
			luaL_argcheck(L, lua_istable(L, 2), 2, "'table' expected");

			lua_getfield(L, 1, "pre_build_cmds");
			uint32_t len = lua_rawlen(L, -1);
			if (lua_isnil(L, -1))
			{
				lua_pop(L, 1);
				lua_newtable(L);
			}

			char* input = nullptr;
			char* output = nullptr;
			lua_newtable(L);
			lua_getfield(L, 2, "input");
			if (lua_isnil(L, -1))
				luaL_error(L, "missing key: 'input'");
			else if (!lua_isstring(L, -1))
				luaL_error(L, "'input': string expected");
			else
			{
				char const* lua_input = lua_tostring(L, -1);
				if (!strlen(lua_input))
					luaL_error(L, "input cannot be empty");
				input = resolve_path_from_script(L, lua_input);
				lua_pushstring(L, input);
				lua_setfield(L, -3, "input");
				lua_pop(L, 1);
			}

			lua_getfield(L, 2, "output");
			if (lua_isnil(L, -1))
				luaL_error(L, "missing key: 'output'");
			else if (!lua_isstring(L, -1))
				luaL_error(L, "'output': string expected");
			else
			{
				output = resolve_path_from_script(L, lua_tostring(L, -1));
				lua_pushstring(L, output);
				lua_setfield(L, -3, "output");
				lua_pop(L, 1);
			}

			tfree(output);
			tfree(input);

			lua_rawseti(L, 3, len + 1);
			lua_setfield(L, 1, "pre_build_cmds");
			return 0;
		}

		int32_t add_post_build_cmd(lua_State* L)
		{
			luaL_argcheck(L, lua_istable(L, 1), 1, "'table' expected");
			luaL_argcheck(L, lua_istable(L, 2), 2, "'table' expected");

			lua_getfield(L, 1, "post_build_cmds");
			uint32_t len = lua_rawlen(L, -1);
			if (lua_isnil(L, -1))
			{
				lua_pop(L, 1);
				lua_newtable(L);
			}

			lua_newtable(L);
			lua_getfield(L, 2, "input");
			if (!lua_isnil(L, -1) && !lua_isstring(L, -1) && !lua_istable(L, -1))
				luaL_error(L, "'input': string or string array expected");
			else if (lua_isstring(L, -1))
			{
				char const* lua_input = lua_tostring(L, -1);
				if (strlen(lua_input))
				{
					char* input = resolve_path_from_script(L, lua_input);
					lua_pushstring(L, input);
					lua_setfield(L, -3, "input");
					tfree(input);
				}
				lua_pop(L, 1);
			}
			else if (lua_istable(L, -1))
			{
				lua_newtable(L);
				uint32_t len = lua_rawlen(L, -2);
				for (uint32_t i {0}; i < len; ++i)
				{
					lua_rawgeti(L, -2, i + 1);
					if (!lua_isstring(L, -1))
						luaL_error(L, "'input': string or string array expected");
					char const* lua_input = lua_tostring(L, -1);
					if (strlen(lua_input))
					{
						char* input = resolve_path_from_script(L, lua_input);
						lua_pushstring(L, input);
						lua_rawseti(L, -3, i + 1);
						tfree(input);
					}
					lua_pop(L, 1);
				}
				lua_setfield(L, -3, "input");
				lua_pop(L, 1);
			}
			else
				lua_pop(L, 1);

			lua_getfield(L, 2, "output");
			if (lua_isnil(L, -1))
				luaL_error(L, "missing key: 'output'");
			else if (lua_isstring(L, -1))
			{
				char const* lua_output = lua_tostring(L, -1);
				if (strlen(lua_output))
				{
					char* output = resolve_path_from_script(L, lua_output);
					lua_pushstring(L, output);
					lua_setfield(L, -3, "output");
					tfree(output);
				}
				lua_pop(L, 1);
			}
			else if (lua_istable(L, -1))
			{
				lua_newtable(L);
				uint32_t len = lua_rawlen(L, -2);
				for (uint32_t i {0}; i < len; ++i)
				{
					lua_rawgeti(L, -2, i + 1);
					if (!lua_isstring(L, -1))
						luaL_error(L, "'output': string or string array expected");
					char const* lua_output = lua_tostring(L, -1);
					if (strlen(lua_output))
					{
						char* output = resolve_path_from_script(L, lua_output);
						lua_pushstring(L, output);
						lua_rawseti(L, -3, i + 1);
						tfree(output);
					}
					lua_pop(L, 1);
				}
				lua_setfield(L, -3, "output");
				lua_pop(L, 1);
			}
			else
				lua_pop(L, 1);

			lua_getfield(L, 2, "cmd");
			if (lua_isnil(L, -1))
				luaL_error(L, "missing key: 'cmd'");
			else if (!lua_isstring(L, -1))
				luaL_error(L, "'cmd': string expected");
			else
			{
				char const* lua_cmd = lua_tostring(L, -1);
				if (!strlen(lua_cmd))
					luaL_error(L, "cmd cannot be empty");
				lua_pushstring(L, lua_cmd);
				lua_setfield(L, -3, "cmd");
				lua_pop(L, 1);
			}

			lua_rawseti(L, 3, len + 1);
			lua_setfield(L, 1, "post_build_cmds");

			return 0;
		}

		int32_t add_post_build_copy(lua_State* L)
		{
			luaL_argcheck(L, lua_istable(L, 1), 1, "'table' expected");
			luaL_argcheck(L, lua_istable(L, 2), 2, "'table' expected");

			lua_getfield(L, 1, "post_build_cmds");
			uint32_t len = lua_rawlen(L, -1);
			if (lua_isnil(L, -1))
			{
				lua_pop(L, 1);
				lua_newtable(L);
			}

			char* input = nullptr;
			char* output = nullptr;
			lua_newtable(L);
			lua_getfield(L, 2, "input");
			if (lua_isnil(L, -1))
				luaL_error(L, "missing key: 'input'");
			else if (!lua_isstring(L, -1))
				luaL_error(L, "'input': string expected");
			else
			{
				char const* lua_input = lua_tostring(L, -1);
				if (!strlen(lua_input))
					luaL_error(L, "input cannot be empty");
				input = resolve_path_from_script(L, lua_input);
				lua_pushstring(L, input);
				lua_setfield(L, -3, "input");
				lua_pop(L, 1);
			}

			lua_getfield(L, 2, "output");
			if (lua_isnil(L, -1))
				luaL_error(L, "missing key: 'output'");
			else if (!lua_isstring(L, -1))
				luaL_error(L, "'output': string expected");
			else
			{
				output = resolve_path_from_script(L, lua_tostring(L, -1));
				lua_pushstring(L, output);
				lua_setfield(L, -3, "output");
				lua_pop(L, 1);
			}

			tfree(output);
			tfree(input);

			lua_rawseti(L, 3, len + 1);
			lua_setfield(L, 1, "post_build_cmds");
			return 0;
		}

		int32_t platform(lua_State* L)
		{
#ifdef _WIN32
			lua_pushliteral(L, "windows");
#elif defined(__linux__)
			lua_pushliteral(L, "linux");
#elif defined(__APPLE__)
			lua_pushliteral(L, "mac");
#else
			luaL_error(L, "Unknown platform");
#endif
			return 1;
		}

		int32_t need_generate(lua_State* L)
		{
			lua_Debug info;
			lua_getstack(L, 1, &info);
			lua_getinfo(L, "Sl", &info);

			lua_pushboolean(L, strcmp(g.file, info.short_src) == 0);

			return 1;
		}

		int32_t get_build_dir(lua_State* L)
		{
			char* build_dir = resolve_path_to_script(L, "build/");

			lua_pushstring(L, build_dir);

			tfree(build_dir);
			return 1;
		}
	} // namespace

	void create()
	{
		lua_State* L = lua_newstate(lua_alloc, nullptr);
		luaL_openlibs(L);

		lua_getglobal(L, "os");
		lua_pushcclosure(L, os::execute, 0);
		lua_setfield(L, -2, "execute");
		lua_pushcclosure(L, os::copy_file, 0);
		lua_setfield(L, -2, "copy_file");

		lua_newtable(L);

		lua_pushcclosure(L, prj::new_project, 0);
		lua_setfield(L, -2, "project");

		lua_newtable(L);
		for (uint32_t i {0}; i < project_type::count; ++i)
		{
			lua_newtable(L);
			lua_pushinteger(L, i);
			lua_setfield(L, -2, "__project_type_enum_value");
			lua_setfield(L, -2, project_type_names[i]);
		}
		lua_setfield(L, -2, "project_type");

		lua_pushcclosure(L, collect_files, 0);
		lua_setfield(L, -2, "collect_files");

		lua_pushcclosure(L, resolve_path, 0);
		lua_setfield(L, -2, "resolve_path");

		lua_pushcclosure(L, add_pre_build_cmd, 0);
		lua_setfield(L, -2, "add_pre_build_cmd");
		lua_pushcclosure(L, add_pre_build_copy, 0);
		lua_setfield(L, -2, "add_pre_build_copy");
		lua_pushcclosure(L, add_post_build_cmd, 0);
		lua_setfield(L, -2, "add_post_build_cmd");
		lua_pushcclosure(L, add_post_build_copy, 0);
		lua_setfield(L, -2, "add_post_build_copy");

		lua_pushcclosure(L, gen::ninja_generator, 0);
		lua_setfield(L, -2, "generate");

		lua_pushcclosure(L, configurations, 0);
		lua_setfield(L, -2, "configurations");

		lua_pushcclosure(L, platform, 0);
		lua_setfield(L, -2, "platform");

		lua_pushcclosure(L, need_generate, 0);
		lua_setfield(L, -2, "need_generate");

		lua_pushcclosure(L, get_build_dir, 0);
		lua_setfield(L, -2, "get_build_dir");

		lua_setglobal(L, "mg");

		lua_newtable(L);
		lua_pushcclosure(L, net::download, 0);
		lua_setfield(L, -2, "download");

		lua_setglobal(L, "net");

		g.L = L;
	}

	void destroy()
	{
		lua_close(g.L);
		if (g.config_size)
		{
			for (uint32_t i {0}; i < g.config_size; ++i)
				tfree(g.configs[i]);
			tfree(g.configs);
		}
	}

	int32_t run_file(char const* filename)
	{
		g.file = filename;
		if (luaL_dofile(g.L, filename))
		{
			printf("%s", lua_tostring(g.L, -1));
			g.file = nullptr;
			return 1;
		}
		g.file = nullptr;

		return 0;
	}

	char* resolve_path_from_script(lua_State* L, char const* path, uint32_t len)
	{
		if (len == UINT32_MAX)
			len = strlen(path);
		if (fs::is_absolute(path))
		{
			char* new_path = tmalloc<char>(len + 1);
			strncpy(new_path, path, len);
			new_path[len] = '\0';
			return new_path;
		}

		lua_Debug info;
		lua_getstack(L, 1, &info);
		lua_getinfo(L, "Sl", &info);

		uint32_t pos = 0;
		if (str::starts_with(info.short_src, "./") ||
		    str::starts_with(info.short_src, ".\\"))
			pos = 2;
		else
		{
			char* cwd = fs::get_cwd();
			if (str::starts_with(info.short_src, cwd))
			{
				if (str::ends_with(cwd, "/") || str::ends_with(cwd, "\\"))
					pos = strlen(cwd);
				else
					pos = strlen(cwd) + 1;
			}
			else if (str::find(info.short_src, "/"))
			{
				char* new_path = tmalloc<char>(len + 1);
				strncpy(new_path, path, len);
				new_path[len] = '\0';
				tfree(cwd);
				return new_path;
			}
			else
			{
				// TODO Generate absolute path
				char* new_path = tmalloc<char>(len + 1);
				strncpy(new_path, path, len);
				new_path[len] = '\0';
				tfree(cwd);
				return new_path;
			}
			tfree(cwd);
		}

		struct str_view
		{
			char const* str {nullptr};
			uint32_t    len {0};
		};

		str_view* path_fragments = tmalloc<str_view>(4);
		uint32_t  path_fragments_len = 0;
		uint32_t  path_fragments_cap = 4;

		uint32_t find_pos = str::find(info.short_src + pos, "/");
		uint32_t src_len {static_cast<uint32_t>(strlen(info.short_src))};
		while (find_pos != UINT32_MAX && (find_pos + pos < src_len))
		{
			str_view frag {info.short_src + pos, find_pos + 1};
			if (path_fragments_len == path_fragments_cap)
			{
				path_fragments_cap *= 2;
				path_fragments = trealloc(path_fragments, path_fragments_cap);
			}

			path_fragments[path_fragments_len] = frag;
			++path_fragments_len;

			pos += find_pos + 1;
			find_pos = str::find(info.short_src + pos, "/");
		}

		if (str::starts_with(path, "./") || str::starts_with(path, ".\\"))
			pos = 2;
		else
			pos = 0;

		find_pos = str::find(path + pos, "/");
		while (find_pos != UINT32_MAX && (find_pos + pos < len))
		{
			str_view frag {path + pos, find_pos + 1};
			if (path_fragments_len == path_fragments_cap)
			{
				path_fragments_cap *= 2;
				path_fragments = trealloc(path_fragments, path_fragments_cap);
			}

			path_fragments[path_fragments_len] = frag;
			++path_fragments_len;

			pos += find_pos + 1;
			find_pos = str::find(path + pos, "/");
		}

		if (path_fragments_len == path_fragments_cap)
		{
			path_fragments_cap *= 2;
			path_fragments = trealloc(path_fragments, path_fragments_cap);
		}

		if (len - pos > 0)
		{
			str_view last_path_frag {path + pos, len - pos};
			path_fragments[path_fragments_len] = last_path_frag;
			++path_fragments_len;
		}

		uint32_t res_path_cap {len};
		uint32_t res_path_len {0};
		char*    res_path = tmalloc<char>(res_path_cap + 1);

		int32_t frag_pos {0};
		bool    need_add {false};
		for (uint32_t i {0}; i < path_fragments_len; ++i)
		{
			need_add = false;

			str_view* frag = path_fragments + i;
			if (strncmp(frag->str, "../", frag->len) == 0)
			{
				if (frag_pos > 0)
				{
					uint32_t last_frag = str::rfind(res_path, "/", res_path_len - 1);
					if (last_frag == UINT32_MAX)
						res_path_len = 0;
					else
						res_path_len = last_frag;
				}
				else
				{
					need_add = true;
				}

				--frag_pos;
			}
			else
			{
				++frag_pos;
				need_add = true;
			}

			if (need_add)
			{
				if (res_path_len + frag->len > res_path_cap)
				{
					while (res_path_len + frag->len > res_path_cap)
						res_path_cap *= 2;
					res_path = trealloc(res_path, res_path_cap + 1);
				}

				strncpy(res_path + res_path_len, frag->str, frag->len);
				res_path_len += frag->len;
			}
		}

		tfree(path_fragments);

		res_path[res_path_len] = '\0';
		return res_path;
	}

	// TODO re-verify func
	char* resolve_path_to_script(lua_State* L, char const* path, uint32_t len)
	{
		if (len == UINT32_MAX)
			len = strlen(path);
		if (fs::is_absolute(path))
		{
			char* new_path = tmalloc<char>(len + 1);
			strncpy(new_path, path, len);
			new_path[len] = '\0';
			return new_path;
		}

		lua_Debug info;
		lua_getstack(L, 1, &info);
		lua_getinfo(L, "Sl", &info);

		uint32_t pos = 0;
		if (str::starts_with(info.short_src, "./") ||
		    str::starts_with(info.short_src, ".\\"))
			pos = 2;
		else
		{
			char* cwd = fs::get_cwd();
			if (str::starts_with(info.short_src, cwd))
			{
				if (str::ends_with(cwd, "/") || str::ends_with(cwd, "\\"))
					pos = strlen(cwd);
				else
					pos = strlen(cwd) + 1;
			}
			else if (str::find(info.short_src, "/"))
			{
				char* new_path = tmalloc<char>(len + 1);
				strncpy(new_path, path, len);
				new_path[len] = '\0';
				tfree(cwd);
				return new_path;
			}
			else
			{
				// TODO Generate absolute path
				char* new_path = tmalloc<char>(len + 1);
				strncpy(new_path, path, len);
				new_path[len] = '\0';
				tfree(cwd);
				return new_path;
			}
			tfree(cwd);
		}

		uint32_t res_path_capacity = len;
		uint32_t res_path_size = 0;
		char*    res_path = tmalloc<char>(res_path_capacity + 1);

		uint32_t current_script_dir_count = 0;
		uint32_t path_dir_count = 0;

		for (uint32_t i {pos}; i < strlen(info.short_src); ++i)
			if (info.short_src[i] == '/' || info.short_src[i] == '\\')
				++current_script_dir_count;

		while (current_script_dir_count > 0)
		{
			if (res_path_capacity < res_path_size + 3 /*../*/)
			{
				while (res_path_capacity < res_path_size + 3)
					res_path_capacity *= 2;
				res_path = trealloc(res_path, res_path_capacity + 1);
			}

			strncpy(res_path + res_path_size, "../", 3);
			res_path_size += 3;
			--current_script_dir_count;
			uint32_t find_pos = str::find(info.short_src + pos, "/");
			if (find_pos == UINT32_MAX)
				find_pos = str::find(info.short_src + pos, "\\");
			pos += find_pos + 1;
		}

		uint32_t find_pos = str::rfind(info.short_src + pos, "/");
		if (find_pos == UINT32_MAX)
			find_pos = str::rfind(info.short_src + pos, "\\");

		if (find_pos != UINT32_MAX)
		{
			if (res_path_capacity < res_path_size + find_pos + 1 + len)
			{
				while (res_path_capacity < res_path_size + find_pos + 1 + len)
					res_path_capacity *= 2;
				res_path = trealloc(res_path, res_path_capacity + 1);
			}

			strncpy(res_path + res_path_size, info.short_src + pos, find_pos + 1);
			res_path_size += find_pos + 1;
		}
		else if (res_path_capacity < res_path_size + len)
		{
			while (res_path_capacity < res_path_size + len)
				res_path_capacity *= 2;
			res_path = trealloc(res_path, res_path_capacity + 1);
		}
		strncpy(res_path + res_path_size, path, len);
		res_path[res_path_size + len] = '\0';
		return res_path;
	}

	// NOLINTBEGIN(clang-analyzer-unix.Malloc)

	namespace
	{
		bool
		parse_config_input(lua_State* L, char const* key, int32_t value_type, input& in)
		{
			if (strcmp(key, "sources") == 0)
			{
				if (value_type != LUA_TTABLE)
					luaL_error(L, "sources: expecting array");

				uint32_t len = lua_rawlen(L, -1);
				if (!len)
					return true;
				in.sources = trealloc(in.sources, in.sources_size + len);
				for (uint32_t i {in.sources_size}; i < in.sources_size + len; ++i)
				{
					lua_rawgeti(L, -1, i + 1);
					if (lua_isstring(L, -1))
					{
						char const* lua_str = lua_tostring(L, -1);
						char*       str = tmalloc<char>(strlen(lua_str) + 1);
						strcpy(str, lua_str);
						in.sources[i] = str;
					}
					else
						luaL_error(L, "sources: expecting string in array");
					lua_pop(L, 1);
				}
				in.sources_size += len;

				return true;
			}
			else if (strcmp(key, "includes") == 0)
			{
				if (value_type != LUA_TTABLE)
					luaL_error(L, "includes: expecting array");

				uint32_t len = lua_rawlen(L, -1);
				if (!len)
					return true;
				in.includes = trealloc(in.includes, in.includes_size + len);
				for (uint32_t i {in.includes_size}; i < in.includes_size + len; ++i)
				{
					lua_rawgeti(L, -1, i - in.includes_size + 1);
					if (lua_isstring(L, -1))
					{
						char const* lua_str = lua_tostring(L, -1);
						char*       str = tmalloc<char>(strlen(lua_str) + 1);
						strcpy(str, lua_str);
						in.includes[i] = str;
					}
					else
						luaL_error(L, "includes: expecting string in array");
					lua_pop(L, 1);
				}
				in.includes_size += len;

				return true;
			}
			else if (strcmp(key, "external_includes") == 0)
			{
				if (value_type != LUA_TTABLE)
					luaL_error(L, "external_includes: expecting array");

				uint32_t len = lua_rawlen(L, -1);
				if (!len)
					return true;
				in.ext_includes = trealloc(in.ext_includes, in.ext_includes_size + len);
				for (uint32_t i {in.ext_includes_size}; i < in.ext_includes_size + len;
				     ++i)
				{
					lua_rawgeti(L, -1, i - in.ext_includes_size + 1);
					if (lua_isstring(L, -1))
					{
						char const* lua_str = lua_tostring(L, -1);
						char*       str = tmalloc<char>(strlen(lua_str) + 1);
						strcpy(str, lua_str);
						in.ext_includes[i] = str;
					}
					else
						luaL_error(L, "external_includes: expecting string in array");
					lua_pop(L, 1);
				}
				in.ext_includes_size += len;

				return true;
			}
			else if (strcmp(key, "compile_options") == 0)
			{
				if (value_type != LUA_TTABLE)
					luaL_error(L, "compile_options: expecting array");

				uint32_t len = lua_rawlen(L, -1);
				if (!len)
					return true;
				in.compile_options =
					trealloc(in.compile_options, in.compile_options_size + len);
				for (uint32_t i {in.compile_options_size};
				     i < in.compile_options_size + len; ++i)
				{
					lua_rawgeti(L, -1, i - in.compile_options_size + 1);
					if (lua_isstring(L, -1))
					{
						char const* lua_str = lua_tostring(L, -1);
						char*       str = tmalloc<char>(strlen(lua_str) + 1);
						strcpy(str, lua_str);
						in.compile_options[i] = str;
					}
					else
						luaL_error(L, "compile_options: expecting string in array");
					lua_pop(L, 1);
				}
				in.compile_options_size += len;

				return true;
			}
			else if (strcmp(key, "link_options") == 0)
			{
				if (value_type != LUA_TTABLE)
					luaL_error(L, "link_options: expecting array");

				uint32_t len = lua_rawlen(L, -1);
				if (!len)
					return true;
				in.link_options = trealloc(in.link_options, in.link_options_size + len);
				for (uint32_t i {in.link_options_size}; i < in.link_options_size + len;
				     ++i)
				{
					lua_rawgeti(L, -1, i - in.link_options_size + 1);
					if (lua_isstring(L, -1))
					{
						char const* lua_str = lua_tostring(L, -1);
						char*       str = tmalloc<char>(strlen(lua_str) + 1);
						strcpy(str, lua_str);
						in.link_options[i] = str;
					}
					else
						luaL_error(L, "link_options: expecting string in array");
					lua_pop(L, 1);
				}

				in.link_options_size += len;

				return true;
			}
			else if (strcmp(key, "dependencies") == 0)
			{
				if (value_type != LUA_TTABLE)
					luaL_error(L, "dependencies: expecting array");

				uint32_t len = lua_rawlen(L, -1);
				if (!len)
					return true;
				in.deps = trealloc(in.deps, in.deps_size + len);
				for (uint32_t i {in.deps_size}; i < in.deps_size + len; ++i)
				{
					lua_rawgeti(L, -1, i - in.deps_size + 1);
					if (lua_istable(L, -1))
						in.deps[i] = parse_output(L);
					else
						luaL_error(L, "dependencies: expecting table in array");
					lua_pop(L, 1);
				}

				in.deps_size += len;

				return true;
			}
			else if (strcmp(key, "static_libraries") == 0 &&
			         in.type == project_type::prebuilt)
			{
				if (value_type != LUA_TTABLE)
					luaL_error(L, "static_libraries: expecting array");

				uint32_t len = lua_rawlen(L, -1);
				if (!len)
					return true;

				in.static_libraries =
					trealloc(in.static_libraries, in.static_libraries_size + len);
				for (uint32_t i {in.static_libraries_size};
				     i < in.static_libraries_size + len; ++i)
				{
					lua_rawgeti(L, -1, i - in.static_libraries_size + 1);
					if (lua_isstring(L, -1))
					{
						char const* lua_str = lua_tostring(L, -1);
						char*       str = tmalloc<char>(strlen(lua_str) + 1);
						strcpy(str, lua_str);
						in.static_libraries[i] = str;
					}
					else
						luaL_error(L, "static_libraries: expecting string in array");
					lua_pop(L, 1);
				}

				in.static_libraries_size += len;

				return true;
			}
			else if (strcmp(key, "static_library_directories") == 0 &&
			         in.type == project_type::prebuilt)
			{
				if (value_type != LUA_TTABLE)
					luaL_error(L, "static_library_directories: expecting array");

				uint32_t len = lua_rawlen(L, -1);
				if (!len)
					return true;

				in.static_library_directories =
					trealloc(in.static_library_directories,
				             in.static_library_directories_size + len);
				for (uint32_t i {in.static_library_directories_size};
				     i < in.static_library_directories_size + len; ++i)
				{
					lua_rawgeti(L, -1, i - in.static_library_directories_size + 1);
					if (lua_isstring(L, -1))
					{
						char const* lua_str = lua_tostring(L, -1);
						char*       str = tmalloc<char>(strlen(lua_str) + 1);
						strcpy(str, lua_str);
						in.static_library_directories[i] = str;
					}
					else
						luaL_error(
							L, "static_library_directories: expecting string in array");
					lua_pop(L, 1);
				}

				in.static_library_directories_size += len;

				return true;
			}

			return false;
		}
	} // namespace

	input parse_input(lua_State* L, int32_t idx)
	{
		input in {};
		in.type = project_type::count;

		if (idx != -1)
			lua_pushvalue(L, idx);
		// TODO manually read allowed configs before iterating on table, because lua_next
		// doesn't order the values in initialisation order

		lua_getfield(L, -1, "type");
		if (lua_type(L, -1) == LUA_TTABLE)
		{
			lua_getfield(L, -1, "__project_type_enum_value");
			if (lua_isnil(L, -1))
				luaL_error(L, "type: expecting project_type enum");
			in.type = static_cast<project_type>(lua_tointeger(L, -1));

			lua_pop(L, 1);
		}
		else if (lua_type(L, -1) != LUA_TNIL)
			luaL_error(L, "type: expecting project_type enum");
		else
		{
			luaL_error(L, "missing key: type");
		}
		lua_pop(L, 1);

		lua_pushnil(L);

		while (lua_next(L, -2))
		{
			char const* key = lua_tostring(L, -2);
			int32_t     value_type = lua_type(L, -1);
			if (strcmp(key, "name") == 0)
			{
				if (value_type != LUA_TSTRING)
					luaL_error(L, "name: expecting string");

				char const* lua_name = lua_tostring(L, -1);
				char*       name = tmalloc<char>(strlen(lua_name) + 1);
				strcpy(name, lua_name);
				in.name = name;
			}
			else if (strcmp(key, "type") == 0)
			{
			}
			else if (strcmp(key, g.config_param) == 0)
			{
				if (value_type != LUA_TTABLE)
				{
					luaL_error(L, "%s: expecting table", key);
				}
				else
				{
					lua_pushnil(L);

					while (lua_next(L, -2))
					{
						char const* config_key = lua_tostring(L, -2);
						int32_t     config_value_type = lua_type(L, -1);
						if (!parse_config_input(L, config_key, config_value_type, in))
						{
							luaL_where(L, 1);
							char const* src = lua_tostring(L, -1);
							printf("%s: Unknown key: %s\n", src, key);
							lua_pop(L, 1);
						}
						lua_pop(L, 1);
					}
				}
			}
			else if (!parse_config_input(L, key, value_type, in))
			{
				bool err {true};

				for (uint32_t i {0}; i < g.config_size; ++i)
				{
					if (strcmp(key, g.configs[i]) == 0)
					{
						err = false;
						break;
					}
				}

				if (err)
				{
					luaL_where(L, 1);
					char const* src = lua_tostring(L, -1);
					printf("%s: Unknown key: %s\n", src, key);
					lua_pop(L, 1);
				}
			}
			lua_pop(L, 1);
		}

		if (!in.name)
			luaL_error(L, "missing key: name");
		if (idx != -1)
			lua_pop(L, 1);

		return in;
	}

	// NOLINTEND(clang-analyzer-unix.Malloc)

	void free_input(input const& in)
	{
		if (in.name)
			tfree(in.name);

		if (in.sources)
		{
			for (uint32_t i {0}; i < in.sources_size; ++i)
				if (in.sources[i])
					tfree(in.sources[i]);
			tfree(in.sources);
		}

		if (in.includes)
		{
			for (uint32_t i {0}; i < in.includes_size; ++i)
				if (in.includes[i])
					tfree(in.includes[i]);
			tfree(in.includes);
		}

		if (in.ext_includes)
		{
			for (uint32_t i {0}; i < in.ext_includes_size; ++i)
				if (in.ext_includes[i])
					tfree(in.ext_includes[i]);
			tfree(in.ext_includes);
		}

		if (in.compile_options)
		{
			for (uint32_t i {0}; i < in.compile_options_size; ++i)
				if (in.compile_options[i])
					tfree(in.compile_options[i]);
			tfree(in.compile_options);
		}

		if (in.link_options)
		{
			for (uint32_t i {0}; i < in.link_options_size; ++i)
				if (in.link_options[i])
					tfree(in.link_options[i]);
			tfree(in.link_options);
		}

		if (in.deps)
		{
			for (uint32_t i {0}; i < in.deps_size; ++i)
				free_output(in.deps[i]);
			tfree(in.deps);
		}

		if (in.static_libraries)
		{
			for (uint32_t i {0}; i < in.static_libraries_size; ++i)
				if (in.static_libraries[i])
					tfree(in.static_libraries[i]);
			tfree(in.static_libraries);
		}

		if (in.static_library_directories)
		{
			for (uint32_t i {0}; i < in.static_library_directories_size; ++i)
				if (in.static_library_directories[i])
					tfree(in.static_library_directories[i]);
			tfree(in.static_library_directories);
		}
	}

	void dump_output(lua_State* L, output const& out)
	{
		lua_newtable(L);

		lua_pushstring(L, out.name);
		lua_setfield(L, -2, "name");

		lua_newtable(L);
		lua_pushinteger(L, static_cast<int32_t>(out.type));
		lua_setfield(L, -2, "__project_type_enum_value");
		lua_setfield(L, -2, "type");

		if (out.sources)
		{
			lua_newtable(L);
			for (uint32_t i {0}; i < out.sources_size; ++i)
			{
				lua_newtable(L);
				lua_pushstring(L, out.sources[i].file);
				lua_setfield(L, -2, "file");

				if (out.sources[i].compile_options)
				{
					lua_pushstring(L, out.sources[i].compile_options);
					lua_setfield(L, -2, "compile_options");
				}
				lua_rawseti(L, -2, i + 1);
			}
			lua_setfield(L, -2, "sources");
		}

		if (out.compile_options)
		{
			lua_pushstring(L, out.compile_options);
			lua_setfield(L, -2, "compile_options");
		}

		if (out.link_options)
		{
			lua_pushstring(L, out.link_options);
			lua_setfield(L, -2, "link_options");
		}

		if (out.deps)
		{
			lua_newtable(L);
			for (uint32_t i {0}; i < out.deps_size; ++i)
			{
				dump_output(L, out.deps[i]);
				lua_rawseti(L, -2, i + 1);
			}
			lua_setfield(L, -2, "dependencies");
		}

		if (out.pre_build_cmds)
		{
			lua_newtable(L);
			uint32_t lua_idx {0};
			for (uint32_t i {0}; i < out.pre_build_cmd_size; ++i)
			{
				custom_command* cmd = out.pre_build_cmds + i;
				lua_newtable(L);
				if (cmd->in_len != 0)
				{
					if (cmd->in_len == 1)
					{
						lua_pushstring(L, cmd->in[0]);
					}
					else
					{
						lua_newtable(L);
						uint32_t lua_idx2 {1};
						for (uint32_t j {0}; j < cmd->in_len; ++j)
						{
							lua_pushstring(L, cmd->in[j]);
							lua_rawseti(L, -2, lua_idx2);
							++lua_idx2;
						}
					}

					lua_setfield(L, -2, "input");
				}

				if (cmd->out_len != 0)
				{
					if (cmd->out_len == 1)
					{
						lua_pushstring(L, cmd->out[0]);
					}
					else
					{
						lua_newtable(L);
						uint32_t lua_idx2 {1};
						for (uint32_t j {0}; j < cmd->out_len; ++j)
						{
							lua_pushstring(L, cmd->out[j]);
							lua_rawseti(L, -2, lua_idx2);
							++lua_idx2;
						}
					}

					lua_setfield(L, -2, "output");
				}

				if (cmd->cmd)
				{
					lua_pushstring(L, cmd->cmd);
					lua_setfield(L, -2, "cmd");
				}

				lua_rawseti(L, -2, lua_idx);
				++lua_idx;
			}

			lua_setfield(L, -2, "pre_build_cmds");
		}

		if (out.post_build_cmds)
		{
			lua_newtable(L);
			uint32_t lua_idx {0};
			for (uint32_t i {0}; i < out.post_build_cmd_size; ++i)
			{
				custom_command* cmd = out.post_build_cmds + i;
				if (cmd->in_len != 0)
				{
					if (cmd->in_len == 1)
					{
						lua_pushstring(L, cmd->in[0]);
					}
					else
					{
						lua_newtable(L);
						uint32_t lua_idx2 {1};
						for (uint32_t j {0}; j < cmd->in_len; ++j)
						{
							lua_pushstring(L, cmd->in[j]);
							lua_rawseti(L, -2, lua_idx2);
							++lua_idx2;
						}
					}

					lua_setfield(L, -2, "input");
				}

				if (cmd->out_len != 0)
				{
					if (cmd->out_len == 1)
					{
						lua_pushstring(L, cmd->out[0]);
					}
					else
					{
						lua_newtable(L);
						uint32_t lua_idx2 {1};
						for (uint32_t j {0}; j < cmd->out_len; ++j)
						{
							lua_pushstring(L, cmd->out[j]);
							lua_rawseti(L, -2, lua_idx2);
							++lua_idx2;
						}
					}

					lua_setfield(L, -2, "output");
				}

				if (cmd->cmd)
				{
					lua_pushstring(L, cmd->cmd);
					lua_setfield(L, -2, "cmd");
				}

				lua_rawseti(L, -2, lua_idx);
				++lua_idx;
			}

			lua_setfield(L, -2, "post_build_cmds");
		}
	}

	// NOLINTBEGIN(clang-analyzer-unix.Malloc)

	output parse_output(lua_State* L, int32_t idx)
	{
		output out {};

		if (idx != -1)
			lua_pushvalue(L, idx);

		lua_pushnil(L);

		while (lua_next(L, -2))
		{
			char const* key = lua_tostring(L, -2);
			int32_t     value_type = lua_type(L, -1);
			if (strcmp(key, "name") == 0)
			{
				if (value_type != LUA_TSTRING)
					luaL_error(L, "name: expecting string");

				char const* lua_name = lua_tostring(L, -1);
				char*       name = tmalloc<char>(strlen(lua_name) + 1);
				strcpy(name, lua_name);
				out.name = name;
			}
			else if (strcmp(key, "type") == 0)
			{
				if (value_type != LUA_TTABLE)
				{
					luaL_error(L, "type: expecting project_type enum");
				}
				else
				{
					lua_getfield(L, -1, "__project_type_enum_value");
					if (lua_isnil(L, -1))
						luaL_error(L, "type: expecting project_type enum");
					out.type = static_cast<project_type>(lua_tointeger(L, -1));
				}
				lua_pop(L, 1);
			}
			else if (strcmp(key, "sources") == 0)
			{
				if (value_type != LUA_TTABLE)
					luaL_error(L, "sources: expecting array");

				uint32_t len = lua_rawlen(L, -1);
				if (len)
				{
					out.sources_size = out.sources_capacity = len;
					out.sources = tmalloc<output::source>(len);
					for (uint32_t i {0}; i < len; ++i)
					{
						lua_rawgeti(L, -1, i + 1);
						if (lua_istable(L, -1))
						{
							lua_getfield(L, -1, "file");
							if (lua_isstring(L, -1))
							{
								char const* lua_file = lua_tostring(L, -1);
								char*       file = tmalloc<char>(strlen(lua_file) + 1);
								strcpy(file, lua_file);
								out.sources[i].file = file;
							}
							lua_getfield(L, -2, "compile_options");
							if (lua_isstring(L, -1))
							{
								char const* lua_compile_options = lua_tostring(L, -1);
								char*       compile_options =
									tmalloc<char>(strlen(lua_compile_options) + 1);
								strcpy(compile_options, lua_compile_options);
								out.sources[i].compile_options = compile_options;
							}
							else
							{
								out.sources[i].compile_options = nullptr;
							}
							lua_pop(L, 2);
						}
						lua_pop(L, 1);
					}
				}
			}
			else if (strcmp(key, "compile_options") == 0)
			{
				if (value_type != LUA_TSTRING)
					luaL_error(L, "compile_options: expecting string");

				char const* lua_compile_options = lua_tostring(L, -1);
				char* compile_options = tmalloc<char>(strlen(lua_compile_options) + 1);
				strcpy(compile_options, lua_compile_options);
				out.compile_options = compile_options;
			}
			else if (strcmp(key, "link_options") == 0)
			{
				if (value_type != LUA_TSTRING)
					luaL_error(L, "link_options: expecting string");

				char const* lua_link_options = lua_tostring(L, -1);
				char*       link_options = tmalloc<char>(strlen(lua_link_options) + 1);
				strcpy(link_options, lua_link_options);
				out.link_options = link_options;
			}
			else if (strcmp(key, "dependencies") == 0)
			{
				if (value_type != LUA_TTABLE)
					luaL_error(L, "dependencies: expecting array");

				uint32_t len = lua_rawlen(L, -1);
				if (len)
				{
					out.deps_size = len;
					out.deps = tmalloc<output>(len);
					for (uint32_t i {0}; i < len; ++i)
					{
						lua_rawgeti(L, -1, i + 1);
						if (lua_istable(L, -1))
							out.deps[i] = parse_output(L);
						lua_pop(L, 1);
					}
				}
			}
			else if (strcmp(key, "pre_build_cmds") == 0)
			{
				if (value_type != LUA_TTABLE)
					luaL_error(L, "pre_build_cmds: expecting array");

				uint32_t len = lua_rawlen(L, -1);
				if (len)
				{
					out.pre_build_cmd_size = len;
					out.pre_build_cmds = tmalloc<custom_command>(len);
					for (uint32_t i {0}; i < len; ++i)
					{
						lua_rawgeti(L, -1, i + 1);
						if (lua_istable(L, -1))
						{
							lua_getfield(L, -1, "input");
							if (!lua_isnil(L, -1))
							{
								if (lua_isstring(L, -1))
								{
									char const* lua_input = lua_tostring(L, -1);
									if (strlen(lua_input))
									{
										char* input =
											tmalloc<char>(strlen(lua_input) + 1);
										strcpy(input, lua_input);
										out.pre_build_cmds[i].in =
											tmalloc<char const*>(1);
										out.pre_build_cmds[i].in_len = 1;
										out.pre_build_cmds[i].in[0] = input;
									}
								}
								else if (lua_istable(L, -1))
								{
									out.pre_build_cmds[i].in_len = lua_rawlen(L, -1);
									out.pre_build_cmds[i].in = tmalloc<char const*>(
										out.pre_build_cmds[i].in_len);
									for (uint32_t j {0}; j < out.pre_build_cmds[i].in_len;
									     ++j)
									{
										lua_rawgeti(L, -1, j + 1);
										char const* lua_input = lua_tostring(L, -1);
										char*       input =
											tmalloc<char>(strlen(lua_input) + 1);
										strcpy(input, lua_input);
										out.pre_build_cmds[i].in[j] = input;
										lua_pop(L, 1);
									}
								}
							}
							else
							{
								out.pre_build_cmds[i].in_len = 0;
								out.pre_build_cmds[i].in = nullptr;
							}
							lua_getfield(L, -2, "output");
							if (lua_isstring(L, -1))
							{
								char const* lua_output = lua_tostring(L, -1);
								if (strlen(lua_output))
								{
									char* output = tmalloc<char>(strlen(lua_output) + 1);
									strcpy(output, lua_output);
									out.pre_build_cmds[i].out = tmalloc<char const*>(1);
									out.pre_build_cmds[i].out_len = 1;
									out.pre_build_cmds[i].out[0] = output;
								}
							}
							else if (lua_istable(L, -1))
							{
								out.pre_build_cmds[i].out_len = lua_rawlen(L, -1);
								out.pre_build_cmds[i].out =
									tmalloc<char const*>(out.pre_build_cmds[i].out_len);
								for (uint32_t j {0}; j < out.pre_build_cmds[i].out_len;
								     ++j)
								{
									lua_rawgeti(L, -1, j + 1);
									char const* lua_output = lua_tostring(L, -1);
									char* output = tmalloc<char>(strlen(lua_output) + 1);
									strcpy(output, lua_output);
									out.pre_build_cmds[i].out[j] = output;
									lua_pop(L, 1);
								}
							}

							lua_getfield(L, -3, "cmd");
							if (!lua_isnil(L, -1))
							{
								char const* lua_cmd = lua_tostring(L, -1);
								char*       cmd = tmalloc<char>(strlen(lua_cmd) + 1);
								strcpy(cmd, lua_cmd);
								out.pre_build_cmds[i].cmd = cmd;
							}
							else
								out.pre_build_cmds[i].cmd = nullptr;
							lua_pop(L, 3);
						}
						lua_pop(L, 1);
					}
				}
			}
			else if (strcmp(key, "post_build_cmds") == 0)
			{
				if (value_type != LUA_TTABLE)
					luaL_error(L, "post_build_cmds: expecting array");

				uint32_t len = lua_rawlen(L, -1);
				if (len)
				{
					out.post_build_cmd_size = len;
					out.post_build_cmds = tmalloc<custom_command>(len);
					for (uint32_t i {0}; i < len; ++i)
					{
						lua_rawgeti(L, -1, i + 1);
						if (lua_istable(L, -1))
						{
							lua_getfield(L, -1, "input");
							if (!lua_isnil(L, -1))
							{
								if (lua_isstring(L, -1))
								{
									char const* lua_input = lua_tostring(L, -1);
									if (strlen(lua_input))
									{
										char* input =
											tmalloc<char>(strlen(lua_input) + 1);
										strcpy(input, lua_input);
										out.post_build_cmds[i].in =
											tmalloc<char const*>(1);
										out.post_build_cmds[i].in_len = 1;
										out.post_build_cmds[i].in[0] = input;
									}
								}
								else if (lua_istable(L, -1))
								{
									out.post_build_cmds[i].in_len = lua_rawlen(L, -1);
									out.post_build_cmds[i].in = tmalloc<char const*>(
										out.post_build_cmds[i].in_len);
									for (uint32_t j {0};
									     j < out.post_build_cmds[i].in_len; ++j)
									{
										lua_rawgeti(L, -1, j + 1);
										char const* lua_input = lua_tostring(L, -1);
										char*       input =
											tmalloc<char>(strlen(lua_input) + 1);
										strcpy(input, lua_input);
										out.post_build_cmds[i].in[j] = input;
										lua_pop(L, 1);
									}
								}
							}
							else
							{
								out.post_build_cmds[i].in_len = 0;
								out.post_build_cmds[i].in = nullptr;
							}
							lua_getfield(L, -2, "output");
							if (lua_isstring(L, -1))
							{
								char const* lua_output = lua_tostring(L, -1);
								if (strlen(lua_output))
								{
									char* output = tmalloc<char>(strlen(lua_output) + 1);
									strcpy(output, lua_output);
									out.post_build_cmds[i].out = tmalloc<char const*>(1);
									out.post_build_cmds[i].out_len = 1;
									out.post_build_cmds[i].out[0] = output;
								}
							}
							else if (lua_istable(L, -1))
							{
								out.post_build_cmds[i].out_len = lua_rawlen(L, -1);
								out.post_build_cmds[i].out =
									tmalloc<char const*>(out.post_build_cmds[i].out_len);
								for (uint32_t j {0}; j < out.post_build_cmds[i].out_len;
								     ++j)
								{
									lua_rawgeti(L, -1, j + 1);
									char const* lua_output = lua_tostring(L, -1);
									char* output = tmalloc<char>(strlen(lua_output) + 1);
									strcpy(output, lua_output);
									out.post_build_cmds[i].out[j] = output;
									lua_pop(L, 1);
								}
							}

							lua_getfield(L, -3, "cmd");
							if (!lua_isnil(L, -1))
							{
								char const* lua_cmd = lua_tostring(L, -1);
								char*       cmd = tmalloc<char>(strlen(lua_cmd) + 1);
								strcpy(cmd, lua_cmd);
								out.post_build_cmds[i].cmd = cmd;
							}
							else
								out.post_build_cmds[i].cmd = nullptr;
							lua_pop(L, 3);
						}
						lua_pop(L, 1);
					}
				}
			}
			lua_pop(L, 1);
		}

		if (idx != -1)
			lua_pop(L, 1);

		return out;
	}

	// NOLINTEND(clang-analyzer-unix.Malloc)

	void free_output(output const& out)
	{
		if (out.name)
			tfree(out.name);

		if (out.sources)
		{
			for (uint32_t i {0}; i < out.sources_size; ++i)
			{
				tfree(out.sources[i].file);
				if (out.sources[i].compile_options)
					tfree(out.sources[i].compile_options);
			}
			tfree(out.sources);
		}

		if (out.compile_options)
			tfree(out.compile_options);

		if (out.link_options)
			tfree(out.link_options);

		if (out.deps)
		{
			for (uint32_t i {0}; i < out.deps_size; ++i)
				free_output(out.deps[i]);
			tfree(out.deps);
		}

		if (out.pre_build_cmds)
		{
			for (uint32_t i {0}; i < out.pre_build_cmd_size; ++i)
			{
				for (uint32_t j {0}; j < out.pre_build_cmds[i].in_len; ++j)
					tfree(out.pre_build_cmds[i].in[j]);
				tfree(out.pre_build_cmds[i].in);
				for (uint32_t j {0}; j < out.pre_build_cmds[i].out_len; ++j)
					tfree(out.pre_build_cmds[i].out[j]);
				tfree(out.pre_build_cmds[i].out);
				tfree(out.pre_build_cmds[i].cmd);
			}
			tfree(out.pre_build_cmds);
		}

		if (out.post_build_cmds)
		{
			for (uint32_t i {0}; i < out.post_build_cmd_size; ++i)
			{
				for (uint32_t j {0}; j < out.post_build_cmds[i].in_len; ++j)
					tfree(out.post_build_cmds[i].in[j]);
				tfree(out.post_build_cmds[i].in);
				for (uint32_t j {0}; j < out.post_build_cmds[i].out_len; ++j)
					tfree(out.post_build_cmds[i].out[j]);
				tfree(out.post_build_cmds[i].out);
				tfree(out.post_build_cmds[i].cmd);
			}
			tfree(out.post_build_cmds);
		}
	}
} // namespace lua