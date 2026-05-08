#include "project.hpp"

extern "C"
{
#include <lua/lauxlib.h>
#include <lua/lua.h>
#include <lua/lualib.h>
}

#include "fs.hpp"
#include "lua_env.hpp"
#include "mem.hpp"
#include "string.hpp"

namespace prj
{
	namespace
	{
		void fill_prebuilt_project(lua_State* L, lua::input const& in, lua::output& out)
		{
			if (in.static_libraries_size || in.static_library_directories_size)
			{
				uint32_t link_options_str_size {0};
				uint32_t link_options_str_capacity {0};
				for (uint32_t i {0}; i < in.static_library_directories_size; ++i)
					link_options_str_capacity +=
						strlen(in.static_library_directories[i]) + 6 /*-L"../*/ +
						2 /*" */;

				link_options_str_capacity -= 1;
				char* link_options = tmalloc<char>(link_options_str_capacity + 1);
				for (uint32_t i {0}; i < in.static_library_directories_size; ++i)
				{
					uint32_t    pos = 0;
					uint32_t    len = 0;
					char const* path = nullptr;
					if (!fs::is_absolute(in.static_library_directories[i]))
					{
						path = lua::resolve_path_from_script(
							L, in.static_library_directories[i]);
						len = strlen(path);
					}
					else
					{
						path = in.static_library_directories[i];
						len = strlen(in.static_library_directories[i]);
					}

					if (link_options_str_capacity < link_options_str_size + len + 8)
					{
						while (link_options_str_capacity <
						       link_options_str_size + len + 8)
							link_options_str_capacity *= 2;

						link_options =
							trealloc(link_options, link_options_str_capacity + 1);
					}

					if (!fs::is_absolute(path))
					{
						strncpy(link_options + link_options_str_size, "-L\"../", 6);
						link_options_str_size += 6;
					}
					else
					{
						strncpy(link_options + link_options_str_size, "-L\"", 3);
						link_options_str_size += 3;
					}

					strncpy(link_options + link_options_str_size, path, len);
					link_options_str_size += len;

					if (in.static_libraries_size ||
					    i < in.static_library_directories_size - 1)
					{
						strncpy(link_options + link_options_str_size, "\" ", 2);
						link_options_str_size += 2;
					}
					else
					{
						strncpy(link_options + link_options_str_size, "\"", 1);
						link_options_str_size += 1;
					}

					if (!fs::is_absolute(in.static_library_directories[i]))
						tfree(path);
				}

				for (uint32_t i {0}; i < in.static_libraries_size; ++i)
				{
					uint32_t len = strlen(in.static_libraries[i]);
					if (link_options_str_capacity < link_options_str_size + len + 3)
					{
						while (link_options_str_capacity <
						       link_options_str_size + len + 3)
							link_options_str_capacity *= 2;

						link_options =
							trealloc(link_options, link_options_str_capacity + 1);
					}

					strncpy(link_options + link_options_str_size, "-l", 2);
					link_options_str_size += 2;

					strncpy(link_options + link_options_str_size, in.static_libraries[i],
					        len);
					link_options_str_size += len;

					if (i < in.static_libraries_size - 1)
					{
						strncpy(link_options + link_options_str_size, " ", 1);
						link_options_str_size += 1;
					}
				}

				link_options[link_options_str_size] = '\0';
				out.link_options = link_options;
			}
		}

		void
		fill_sources(char const* dir_filter, char const* file_filter, lua::output& out)
		{
			fs::list_files_res files = fs::list_files(dir_filter, file_filter);
			if (out.sources_capacity < out.sources_size + files.size)
			{
				if (!out.sources_capacity)
					out.sources_capacity = 1;
				else
					out.sources_capacity *= 2;
				lua::output::source* new_sources = trealloc(
					out.sources, out.sources_capacity * sizeof(lua::output::source));
				out.sources = new_sources;
			}
			for (uint32_t i {0}; i < files.size; ++i)
			{
				out.sources[out.sources_size + i].file = files.files[i];
				out.sources[out.sources_size + i].compile_options = nullptr;
			}
			out.sources_size += files.size;
			if (files.size)
				tfree(files.files);

			fs::list_dirs_res sub_dirs = fs::list_dirs(dir_filter);
			for (uint32_t i {0}; i < sub_dirs.size; ++i)
			{
				uint32_t dir_len = static_cast<uint32_t>(strlen(sub_dirs.dirs[i]));
				char*    filter = tmalloc<char>(dir_len + 2);
				strcpy(filter, sub_dirs.dirs[i]);
				strcpy(filter + dir_len, "/");
				fill_sources(filter, file_filter, out);

				tfree(filter);
				tfree(sub_dirs.dirs[i]);
			}
			if (sub_dirs.size)
				tfree(sub_dirs.dirs);
		}
	} // namespace

	int new_project(lua_State* L)
	{
		luaL_argcheck(L, lua_istable(L, 1), 1, "'table' expected");
		lua::input  in = lua::parse_input(L);
		lua::output out {0};

		out.name = in.name;
		in.name = nullptr;

		out.type = in.type;

		if (in.type == lua::project_type::prebuilt)
		{
			fill_prebuilt_project(L, in, out);
		}
		else
		{
			for (uint32_t i {0}; i < in.sources_size; ++i)
			{
				uint32_t source_len {static_cast<uint32_t>(strlen(in.sources[i]))};
				uint32_t pos {UINT32_MAX};

				if ((pos = str::rfind(in.sources[i], "**", source_len)) != UINT32_MAX)
				{
					char* filter = nullptr;
					if (fs::is_absolute(in.sources[i]))
					{
						filter = tmalloc<char>(pos + 1);
						strncpy(filter, in.sources[i], pos);
						filter[pos] = '\0';
					}
					else
					{
						filter = lua::resolve_path_from_script(L, in.sources[i], pos);
					}
					fill_sources(filter, in.sources[i] + pos + 2, out);
					tfree(filter);
				}
				else if ((pos = str::rfind(in.sources[i], "*", source_len)) != UINT32_MAX)
				{
					char* filter = nullptr;
					if (fs::is_absolute(in.sources[i]))
					{
						filter = tmalloc<char>(pos + 1);
						strncpy(filter, in.sources[i], pos);
						filter[pos] = '\0';
					}
					else
					{
						filter = lua::resolve_path_from_script(L, in.sources[i], pos);
					}
					fs::list_files_res files =
						fs::list_files(filter, in.sources[i] + pos + 1);
					if (out.sources_capacity < out.sources_size + files.size)
					{
						if (!out.sources_capacity)
							out.sources_capacity = 1;
						else
							out.sources_capacity *= 2;
						lua::output::source* new_sources =
							trealloc(out.sources,
						             out.sources_capacity * sizeof(lua::output::source));
						out.sources = new_sources;
					}
					for (uint32_t i {0}; i < files.size; ++i)
					{
						out.sources[out.sources_size + i].file = files.files[i];
						out.sources[out.sources_size + i].compile_options = nullptr;
					}
					out.sources_size += files.size;
					if (files.size)
						tfree(files.files);
					tfree(filter);
				}
				else
				{
					char const* source = nullptr;
					if (!fs::is_absolute(in.sources[i]))
					{
						char* new_source =
							lua::resolve_path_from_script(L, in.sources[i], source_len);
						source = new_source;
					}
					else
					{
						source = in.sources[i];
					}

					// TODO Decide if file should already exists at declaration time or
					// not
					// if (fs::file_exists(source))
					{
						if (out.sources_capacity < out.sources_size + 1)
						{
							if (!out.sources_capacity)
								out.sources_capacity = 1;
							else
								out.sources_capacity *= 2;
							lua::output::source* new_sources =
								trealloc(out.sources, out.sources_capacity *
							                              sizeof(lua::output::source));
							out.sources = new_sources;
						}
						out.sources[out.sources_size].file = source;
						out.sources[out.sources_size].compile_options = nullptr;
						++out.sources_size;

						if (fs::is_absolute(source))
							in.sources[i] = nullptr;
					}
				}
			}

			if (!out.sources_size)
				luaL_error(L, "sources cannot be empty");

			if (in.compile_options_size || in.includes_size || in.ext_includes_size)
			{
				uint32_t compile_options_str_size {0};
				uint32_t compile_options_str_capacity {0};
				char*    compile_options = tmalloc<char>(compile_options_str_capacity);
				for (uint32_t i {0}; i < in.compile_options_size; ++i)
				{
					uint32_t len {static_cast<uint32_t>(strlen(in.compile_options[i]))};
					if (compile_options_str_capacity < compile_options_str_size + len + 1)
					{
						if (!compile_options_str_capacity)
							compile_options_str_capacity = len + 1;

						while (compile_options_str_capacity <
						       compile_options_str_size + len + 1)
							compile_options_str_capacity *= 2;

						compile_options =
							trealloc(compile_options, compile_options_str_capacity + 1);
					}

					strncpy(compile_options + compile_options_str_size,
					        in.compile_options[i], len);
					compile_options_str_size += len;
					if (in.includes_size || in.ext_includes_size ||
					    i < in.compile_options_size - 1)
					{
						strncpy(compile_options + compile_options_str_size, " ", 1);
						compile_options_str_size += 1;
					}
				}

				for (uint32_t i {0}; i < in.includes_size; ++i)
				{
					uint32_t len {static_cast<uint32_t>(strlen(in.includes[i]))};
					if (fs::is_absolute(in.includes[i]))
					{
						if (compile_options_str_capacity <
						    compile_options_str_size + len + 3)
						{
							if (!compile_options_str_capacity)
								compile_options_str_capacity = len + 3;

							while (compile_options_str_capacity <
							       compile_options_str_size + len + 3)
								compile_options_str_capacity *= 2;

							compile_options = trealloc(compile_options,
							                           compile_options_str_capacity + 1);
						}
						strncpy(compile_options + compile_options_str_size, "-I\"", 3);
						compile_options_str_size += 3;
						strncpy(compile_options + compile_options_str_size,
						        in.includes[i], len);
						compile_options_str_size += len;
					}
					else
					{
						char const* include =
							lua::resolve_path_from_script(L, in.includes[i], len);
						len = strlen(include);
						if (compile_options_str_capacity <
						    compile_options_str_size + len + 6)
						{
							if (!compile_options_str_capacity)
								compile_options_str_capacity = len + 6;

							while (compile_options_str_capacity <
							       compile_options_str_size + len + 6)
								compile_options_str_capacity *= 2;

							compile_options = trealloc(compile_options,
							                           compile_options_str_capacity + 1);
						}
						strncpy(compile_options + compile_options_str_size, "-I\"../", 6);
						compile_options_str_size += 6;
						strncpy(compile_options + compile_options_str_size, include, len);
						compile_options_str_size += len;
						tfree(include);
					}

					if (in.ext_includes_size || i < in.includes_size - 1)
					{
						if (compile_options_str_capacity < compile_options_str_size + 2)
						{
							while (compile_options_str_capacity <
							       compile_options_str_size + 2)
								compile_options_str_capacity *= 2;

							compile_options = trealloc(compile_options,
							                           compile_options_str_capacity + 1);
						}
						strncpy(compile_options + compile_options_str_size, "\" ", 2);
						compile_options_str_size += 2;
					}
					else
					{
						if (compile_options_str_capacity < compile_options_str_size + 1)
						{
							while (compile_options_str_capacity <
							       compile_options_str_size + 1)
								compile_options_str_capacity *= 1;

							compile_options = trealloc(compile_options,
							                           compile_options_str_capacity + 1);
						}
						strncpy(compile_options + compile_options_str_size, "\"", 1);
						compile_options_str_size += 1;
					}
				}

				for (uint32_t i {0}; i < in.ext_includes_size; ++i)
				{
					uint32_t len {static_cast<uint32_t>(strlen(in.ext_includes[i]))};
					if (fs::is_absolute(in.ext_includes[i]))
					{
						if (compile_options_str_capacity <
						    compile_options_str_size + len + 3)
						{
							if (!compile_options_str_capacity)
								compile_options_str_capacity = len + 3;

							while (compile_options_str_capacity <
							       compile_options_str_size + len + 3)
								compile_options_str_capacity *= 2;

							compile_options = trealloc(compile_options,
							                           compile_options_str_capacity + 1);
						}
						strncpy(compile_options + compile_options_str_size, "-isystem\"",
						        9);
						compile_options_str_size += 9;
						strncpy(compile_options + compile_options_str_size,
						        in.ext_includes[i], len);
						compile_options_str_size += len;
					}
					else
					{
						char const* include =
							lua::resolve_path_from_script(L, in.ext_includes[i], len);
						len = strlen(include);
						if (compile_options_str_capacity <
						    compile_options_str_size + len + 6)
						{
							if (!compile_options_str_capacity)
								compile_options_str_capacity = len + 6;

							while (compile_options_str_capacity <
							       compile_options_str_size + len + 6)
								compile_options_str_capacity *= 2;

							compile_options = trealloc(compile_options,
							                           compile_options_str_capacity + 1);
						}
						strncpy(compile_options + compile_options_str_size,
						        "-isystem\"../", 12);
						compile_options_str_size += 12;
						strncpy(compile_options + compile_options_str_size, include, len);
						compile_options_str_size += len;
						tfree(include);
					}

					if (in.ext_includes_size || i < in.ext_includes_size - 1)
					{
						if (compile_options_str_capacity < compile_options_str_size + 2)
						{
							while (compile_options_str_capacity <
							       compile_options_str_size + 2)
								compile_options_str_capacity *= 2;

							compile_options = trealloc(compile_options,
							                           compile_options_str_capacity + 1);
						}
						strncpy(compile_options + compile_options_str_size, "\" ", 2);
						compile_options_str_size += 2;
					}
					else
					{
						if (compile_options_str_capacity < compile_options_str_size + 1)
						{
							while (compile_options_str_capacity <
							       compile_options_str_size + 1)
								compile_options_str_capacity *= 1;

							compile_options = trealloc(compile_options,
							                           compile_options_str_capacity + 1);
						}
						strncpy(compile_options + compile_options_str_size, "\"", 1);
						compile_options_str_size += 1;
					}
				}
				compile_options[compile_options_str_size] = '\0';
				out.compile_options = compile_options;
			}

			if (in.link_options_size)
			{
				uint32_t link_options_str_size {0};
				for (uint32_t i {0}; i < in.link_options_size; ++i)
					link_options_str_size += strlen(in.link_options[i]) + 1;

				char*    link_options = tmalloc<char>(link_options_str_size);
				uint32_t pos {0};
				for (uint32_t i {0}; i < in.link_options_size; ++i)
				{
					uint32_t len {static_cast<uint32_t>(strlen(in.link_options[i]))};
					strncpy(link_options + pos, in.link_options[i], len);
					pos += len;
					if (i < in.link_options_size - 1)
					{
						strncpy(link_options + pos, " ", 1);
						pos += 1;
					}
				}
				link_options[link_options_str_size - 1] = '\0';
				out.link_options = link_options;
			}

			if (in.deps)
			{
				uint32_t     out_deps_size = in.deps_size;
				uint32_t     out_deps_i = 0;
				lua::output* out_deps = tmalloc<lua::output>(out_deps_size);
				for (uint32_t i {0}; i < in.deps_size; ++i)
				{
					if (in.deps[i].type == lua::project_type::prebuilt)
					{
						if (in.deps[i].link_options)
						{
							char*    new_link_options = nullptr;
							uint32_t out_link_option_size = 0;
							if (out.link_options)
							{
								out_link_option_size = strlen(out.link_options);
								new_link_options =
									trealloc(const_cast<char*>(out.link_options),
								             out_link_option_size +
								                 strlen(in.deps[i].link_options) + 2);
								new_link_options[strlen(new_link_options)] = ' ';
								++out_link_option_size;
							}
							else
							{
								new_link_options =
									tmalloc<char>(strlen(in.deps[i].link_options) + 1);
							}
							strcpy(new_link_options + out_link_option_size,
							       in.deps[i].link_options);
							--out_deps_size;
							out.link_options = new_link_options;
						}

						lua::free_output(in.deps[i]);
					}
					else
					{
						out_deps[out_deps_i] = in.deps[i];
						++out_deps_i;
					}
				}

				out.deps = out_deps;
				out.deps_size = out_deps_size;

				tfree(in.deps);
				in.deps = nullptr;
				in.deps_size = 0;
			}
		}

		lua::free_input(in);
		lua::dump_output(L, out);
		lua::free_output(out);

		return 1;
	}
} // namespace prj
