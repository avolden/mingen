#pragma once

#include <stdint.h>

extern "C"
{
#include <lua/lauxlib.h>
#include <lua/lua.h>
#include <lua/lualib.h>
}

namespace lua
{
	void    create();
	void    destroy();
	int32_t run_file(char const* filename);

	char*
	resolve_path_from_script(lua_State* L, char const* path, uint32_t len = UINT32_MAX);
	char*
	resolve_path_to_script(lua_State* L, char const* path, uint32_t len = UINT32_MAX);

	enum project_type
	{
		sources,
		static_library,
		shared_library,
		executable,
		prebuilt,
		count
	};

	char const* const project_type_names[] {"sources", "static_library", "shared_library",
	                                        "executable", "prebuilt"};

	struct output;

	struct input
	{
		char const*  name;
		project_type type;

		char const** sources;
		uint32_t     sources_size;
		char const** includes;
		uint32_t     includes_size;
		char const** ext_includes;
		uint32_t     ext_includes_size;
		char const** compile_options;
		uint32_t     compile_options_size;

		output*  deps;
		uint32_t deps_size;

		char const** link_options;
		uint32_t     link_options_size;

		// Specific to prebuilt type
		char const** static_library_directories;
		uint32_t     static_library_directories_size;
		char const** static_libraries;
		uint32_t     static_libraries_size;

		// TODO dynamic_libraries to auto post_build_copy, only for prebuilt_input
	};

	struct custom_command
	{
		char const** in;
		uint32_t     in_len;
		char const** out;
		uint32_t     out_len;
		char const*  cmd;
	};

	struct output

	{
		struct source
		{
			char const* file;
			// Empty by default, but can be written manually in projects files
			char const* compile_options;
		};

		char const*  name;
		project_type type;

		source*  sources;
		uint32_t sources_capacity;
		uint32_t sources_size;

		char const* compile_options;
		char const* link_options;

		output*  deps;
		uint32_t deps_size;

		// TODO dynamic_libraries to auto post_build_copy, only for prebuilt_input

		custom_command* pre_build_cmds;
		uint32_t        pre_build_cmd_size;
		custom_command* post_build_cmds;
		uint32_t        post_build_cmd_size;
	};

	input parse_input(lua_State* L, int32_t idx = -1);
	void  free_input(input const& in);

	void   dump_output(lua_State* L, output const& out);
	output parse_output(lua_State* L, int32_t idx = -1);
	void   free_output(output const& out);
} // namespace lua