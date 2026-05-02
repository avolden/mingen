#include "generator.hpp"

#include "fs.hpp"
#include "lua_env.hpp"
#include "mem.hpp"
#include "state.hpp"
#include "string.hpp"

extern "C"
{
#include <lua/lauxlib.h>
#include <lua/lua.h>
#include <lua/lualib.h>
}

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace gen
{
	namespace
	{
		char* unesc_str(char const* str)
		{
			uint32_t len = strlen(str);
			char*    unescaped = tmalloc<char>(len * 2);
			uint32_t pos = 0;
			for (uint32_t i {0}; i < len; ++i)
			{
				switch (str[i])
				{
					case '\a':
						strcpy(unescaped + pos, "\\a");
						pos += 2;
						break;
					case '\b':
						strcpy(unescaped + pos, "\\b");
						pos += 2;
						break;
					case '\f':
						strcpy(unescaped + pos, "\\f");
						pos += 2;
						break;
					case '\n':
						strcpy(unescaped + pos, "\\n");
						pos += 2;
						break;
					case '\r':
						strcpy(unescaped + pos, "\\r");
						pos += 2;
						break;
					case '\t':
						strcpy(unescaped + pos, "\\t");
						pos += 2;
						break;
					case '\v':
						strcpy(unescaped + pos, "\\v");
						pos += 2;
						break;
					case '\\':
						strcpy(unescaped + pos, "\\\\");
						pos += 2;
						break;
					case '\'':
						strcpy(unescaped + pos, "\\'");
						pos += 2;
						break;
					case '\"':
						strcpy(unescaped + pos, "\\\"");
						pos += 2;
						break;
					case '\?':
						strcpy(unescaped + pos, "\\\?");
						pos += 2;
						break;
					default:
						unescaped[pos] = str[i];
						++pos;
						break;
				}
			}
			unescaped[pos] = '\0';
			return unescaped;
		}

		void generate_db(lua::output* outs, uint32_t outs_size)
		{
			FILE* file = fopen("build/compile_commands.json", "w");
			char* cwd = fs::get_cwd();
			char* unesc_cwd = unesc_str(cwd);
			tfree(cwd);
			fwrite("[\n", 1, 2, file);
			for (uint32_t i {0}; i < outs_size; ++i)
			{
				for (uint32_t j {0}; j < outs[i].sources_size; ++j)
				{
					fwrite("	{\n", 1, 3, file);
#ifdef _WIN32
					fprintf(file, "		\"directory\": \"%s\\\\build\",\n", unesc_cwd);
#elif defined(__linux__) || defined(__APPLE__)
					fprintf(file, "		\"directory\": \"%s/build\",\n", unesc_cwd);
#endif
					char* unesc_options =
						unesc_str(outs[i].sources[j].compile_options
					                  ? outs[i].sources[j].compile_options
					                  : outs[i].compile_options);
					fprintf(file, "		\"command\": \"clang++ %s\",\n", unesc_options);
					tfree(unesc_options);
					fprintf(file, "		\"file\": \"../%s\"\n", outs[i].sources[j].file);
					if (i == outs_size - 1 && j == outs[i].sources_size - 1)
						fwrite("	}\n", 1, 3, file);
					else
						fwrite("	},\n", 1, 4, file);
				}
			}
			fwrite("]", 1, 1, file);

			tfree(unesc_cwd);
			fclose(file);
		}

		char** collect_objs(lua::output const& out)
		{
			char**   objs = tmalloc<char*>(out.sources_size);
			uint32_t path_start {UINT32_MAX};
			for (uint32_t i {0}; i < out.sources_size; ++i)
			{
				uint32_t file_start {str::rfind(out.sources[i].file, "/")};
				if (file_start == UINT32_MAX)
				{
					path_start = 0;
					break;
				}
				else
				{
					if (path_start == UINT32_MAX)
						path_start = file_start;

					if (strlen(out.sources[i].file) > path_start &&
					    strncmp(out.sources[0].file, out.sources[i].file, path_start) !=
					        0)
					{
						do
						{
							file_start = str::rfind(out.sources[i].file, "/", path_start);
						}
						while (file_start != UINT32_MAX &&
						       strncmp(out.sources[0].file, out.sources[i].file,
						               file_start) != 0);

						if (file_start == UINT32_MAX)
						{
							path_start = 0;
							break;
						}
					}
				}
			}

			if (path_start != 0)
				++path_start;

			for (uint32_t i {0}; i < out.sources_size; ++i)
			{
				objs[i] = tmalloc<char>(strlen(out.sources[i].file) - path_start + 3);
				strcpy(objs[i], out.sources[i].file + path_start);
				strcpy(objs[i] + strlen(out.sources[i].file) - path_start, ".o");
			}

			return objs;
		}

		void write_deps(lua::output const& out, FILE* file)
		{
			for (uint32_t i {0}; i < out.deps_size; ++i)
			{
				switch (out.deps[i].type)
				{
					case lua::project_type::sources:
					{
						char** objs = collect_objs(out.deps[i]);

						for (uint32_t j {0}; j < out.deps[i].sources_size; ++j)
							fprintf(file, "obj/%s/%s ", out.deps[i].name, objs[j]);

						for (uint32_t j {0}; j < out.deps[i].sources_size; ++j)
							tfree(objs[j]);
						tfree(objs);
						break;
					}
					case lua::project_type::shared_library:
					{
						fprintf(file, "lib/%s.a ", out.deps[i].name);
						break;
					}
					case lua::project_type::static_library:
					{
						fprintf(file, "lib/%s.a ", out.deps[i].name);
						break;
					}
					case lua::project_type::executable: [[fallthrough]];
					default: break;
				}

				write_deps(out.deps[i], file);
			}
		}

		char* get_ninja_cwd()
		{
			char* cwd = fs::get_cwd();
#ifdef _WIN32
			char* ninja_cwd = tmalloc<char>(strlen(cwd) + 2);
			ninja_cwd[0] = cwd[0];
			ninja_cwd[1] = '$';
			strcpy(ninja_cwd + 2, cwd + 1);
			tfree(cwd);
			return ninja_cwd;
#elif defined(__linux__) || defined(__APPLE__)
			return cwd;
#endif
		}

		void write_custom_command(lua::custom_command* cmds,
		                          uint32_t             cmd_size,
		                          FILE*                file,
		                          char const*          cmd_chain = nullptr)
		{
			// TODO handle null output

			for (uint32_t i {0}; i < cmd_size; ++i)
			{
				if (cmds[i].cmd)
				{
					for (uint32_t j {0}; j < cmds[i].out_len; ++j)
					{
						if (j == 0)
							fwrite("build", 1, 5, file);
						if (fs::is_absolute(cmds[i].out[j]))
							fprintf(file, " %s", cmds[i].out[j]);
						else if (str::starts_with(cmds[i].out[j], "build"))
							fprintf(file, " %s", cmds[i].out[j] + 6);
						else
							fprintf(file, " ../%s", cmds[i].out[j]);
					}
					fwrite(": cmd", 1, 5, file);
					for (uint32_t j {0}; j < cmds[i].in_len; ++j)
						if (fs::is_absolute(cmds[i].in[j]))
							fprintf(file, " %s", cmds[i].in[j]);
						else if (str::starts_with(cmds[i].in[j], "build"))
							fprintf(file, " %s", cmds[i].in[j] + 6);
						else
							fprintf(file, " ../%s", cmds[i].in[j]);

					if (i > 0 || cmd_chain)
						fwrite(" ||", 1, 3, file);
					if (i > 0)
					{
						if (fs::is_absolute(cmds[i - 1].out[0]))
							fprintf(file, " %s", cmds[i - 1].out[0]);
						else if (str::starts_with(cmds[i - 1].out[0], "build"))
							fprintf(file, " %s", cmds[i - 1].out[0] + 6);
						else
							fprintf(file, " ../%s", cmds[i - 1].out[0]);
					}
					if (cmd_chain)
						fprintf(file, " %s", cmd_chain);

					uint32_t in_pos = str::find(cmds[i].cmd, "${in}");
					uint32_t out_pos = str::find(cmds[i].cmd, "${out}");
					if (in_pos != UINT32_MAX || out_pos != UINT32_MAX)
					{
						uint32_t first_pos = 0;
						uint32_t second_pos = 0;
						if (in_pos < out_pos)
						{
							fprintf(file, "\n    cmd = %.*s", in_pos, cmds[i].cmd);
							first_pos = in_pos;
							second_pos = out_pos;
						}
						else
						{
							fprintf(file, "\n    cmd = %.*s", out_pos, cmds[i].cmd);
							first_pos = out_pos;
							second_pos = in_pos;
						}

						if (first_pos == in_pos)
							for (uint32_t j {0}; j < cmds[i].in_len; ++j)
								if (j == 0)
									fprintf(file, "%s", cmds[i].in[j]);
								else
									fprintf(file, " %s", cmds[i].in[j]);
						else
							for (uint32_t j {0}; j < cmds[i].out_len; ++j)
								if (j == 0)
									fprintf(file, "%s", cmds[i].out[j]);
								else
									fprintf(file, " %s", cmds[i].out[j]);

						if (second_pos != UINT32_MAX)
						{
							if (first_pos == in_pos)
							{
								fprintf(file, "%.*s", second_pos - first_pos - 5,
								        cmds[i].cmd + first_pos + 5 /*${in}*/);
								for (uint32_t j {0}; j < cmds[i].out_len; ++j)
									if (j == 0)
										fprintf(file, "%s", cmds[i].out[j]);
									else
										fprintf(file, " %s", cmds[i].out[j]);
								fprintf(file, "%s",
								        cmds[i].cmd + second_pos + 6 /*${out}*/);
							}
							else
							{
								fprintf(file, " %.*s", second_pos - first_pos - 6,
								        cmds[i].cmd + first_pos + 6 /*${out}*/);
								for (uint32_t j {0}; j < cmds[i].in_len; ++j)
									if (j == 0)
										fprintf(file, "%s", cmds[i].in[j]);
									else
										fprintf(file, " %s", cmds[i].in[j]);
								fprintf(file, "%s",
								        cmds[i].cmd + second_pos + 5 /*${in}*/);
							}
						}
						else
						{
							fprintf(file, "%s", cmds[i].cmd + first_pos + 5 /*${in}*/);
						}
						fwrite("\n", 1, 1, file);
					}
					else
						fprintf(file, "\n    cmd = %s\n", cmds[i].cmd);
				}
				else
				{
					fwrite("build ", 1, 6, file);
					if (fs::is_absolute(cmds[i].out[0]))
						fprintf(file, "%s: copy ", cmds[i].out[0]);
					else if (str::starts_with(cmds[i].out[0], "build"))
						fprintf(file, "%s: copy ", cmds[i].out[0] + 6);
					else
						fprintf(file, "../%s: copy ", cmds[i].out[0]);

					if (fs::is_absolute(cmds[i].in[0]))
						fprintf(file, "%s", cmds[i].in[0]);
					else if (str::starts_with(cmds[i].in[0], "build"))
						fprintf(file, "%s", cmds[i].in[0] + 6);
					else
						fprintf(file, "../%s", cmds[i].in[0]);

					if (i > 0 || cmd_chain)
						fwrite(" ||", 1, 3, file);

					if (i > 0)
					{
						if (fs::is_absolute(cmds[i - 1].out[0]))
							fprintf(file, " %s", cmds[i - 1].out[0]);
						else if (str::starts_with(cmds[i - 1].out[0], "build"))
							fprintf(file, " %s", cmds[i - 1].out[0] + 6);
						else
							fprintf(file, " ../%s", cmds[i - 1].out[0]);
					}
					if (cmd_chain)
						fprintf(file, " %s\n", cmd_chain);
					else
						fwrite("\n", 1, 1, file);
				}
			}

			if (cmd_size)
				fwrite("\n", 1, 1, file);
		}

		void generate(lua::output const& out, FILE* file)
		{
			char* cwd = get_ninja_cwd();

			write_custom_command(out.pre_build_cmds, out.pre_build_cmd_size, file);

			char** objs = collect_objs(out);
			for (uint32_t i {0}; i < out.sources_size; ++i)
			{
				if (fs::is_absolute(out.sources[i].file))
					fprintf(file, "build obj/%s/%s: cxx %s", out.name, objs[i],
					        out.sources[i].file);
				else if (str::starts_with(out.sources[i].file, "build"))
					fprintf(file, "build obj/%s/%s: cxx %s", out.name, objs[i],
					        out.sources[i].file + 6);
				else
					fprintf(file, "build obj/%s/%s: cxx ../%s", out.name, objs[i],
					        out.sources[i].file);

				if (out.pre_build_cmd_size)
				{
					if (fs::is_absolute(
							out.pre_build_cmds[out.pre_build_cmd_size - 1].out[0]))
					{
						fprintf(file, " || %s\n",
						        out.pre_build_cmds[out.pre_build_cmd_size - 1].out[0]);
					}
					else if (str::starts_with(
								 out.pre_build_cmds[out.pre_build_cmd_size - 1].out[0],
								 "build"))
					{
						fprintf(file, " || %s\n",
						        out.pre_build_cmds[out.pre_build_cmd_size - 1].out[0] +
						            6);
					}
					else
					{
						fprintf(file, " || ../%s\n",
						        out.pre_build_cmds[out.pre_build_cmd_size - 1].out[0]);
					}
				}

				else
					fwrite("\n", 1, 1, file);

				if (fs::is_absolute(out.sources[i].file))
				{
					fprintf(file, "    cxxflags = %s\n",
					        out.sources[i].compile_options
					            ? out.sources[i].compile_options
					            : out.compile_options);
				}
				else
				{
					// TODO absolute path ?
					fprintf(
						file, "    cxxflags = -fmacro-prefix-map=\"../=\" %s\n", /*cwd,*/
						out.sources[i].compile_options ? out.sources[i].compile_options
													   : out.compile_options);
				}
			}

			char* build_out = nullptr;
			switch (out.type)
			{
				case lua::project_type::executable:
				{
#ifdef _WIN32
					int32_t result = snprintf(nullptr, 0, "bin/%s.exe", out.name);
					build_out = tmalloc<char>(result + 1);
					snprintf(build_out, result + 1, "bin/%s.exe", out.name);
#elif defined(__linux__) || defined(__APPLE__)
					int32_t result = snprintf(nullptr, 0, "bin/%s", out.name);
					build_out = tmalloc<char>(result + 1);
					snprintf(build_out, result + 1, "bin/%s", out.name);
#endif

					fprintf(file, "build %s: link ", build_out);
					for (uint32_t i {0}; i < out.sources_size; ++i)
						fprintf(file, "obj/%s/%s ", out.name, objs[i]);

					write_deps(out, file);
					if (out.deps_size)
					{
						fwrite("|", 1, 1, file);
						for (uint32_t i {0}; i < out.deps_size; ++i)
							fprintf(file, " %s", out.deps[i].name);
					}
					else
					{
						fseek(file, -1, SEEK_CUR);
					}
					if (out.link_options)
						fprintf(file, "\n    lflags = %s\n\n", out.link_options);
					else
						fwrite("\n\n", 1, 2, file);

					break;
				}
				// TODO Verify implementation, to put static export library in lib, not
				// bin
				case lua::project_type::shared_library:
				{
#ifdef _WIN32
					int32_t result = snprintf(nullptr, 0, "bin/%s.dll", out.name);
					build_out = tmalloc<char>(result + 1);
					snprintf(build_out, result + 1, "bin/%s.dll", out.name);
#elif defined(__linux__) || defined(__APPLE__)
					int32_t result = snprintf(nullptr, 0, "bin/%s.so", out.name);
					build_out = tmalloc<char>(result + 1);
					snprintf(build_out, result, "bin/%s.so", out.name);
#endif

					fprintf(file, "build %s: link ", build_out);
					for (uint32_t i {0}; i < out.sources_size; ++i)
						fprintf(file, "obj/%s ", objs[i]);

					write_deps(out, file);
					if (out.deps_size)
					{
						fwrite("|", 1, 1, file);
						for (uint32_t i {0}; i < out.deps_size; ++i)
							fprintf(file, " %s", out.deps[i].name);
					}
					else
					{
						fseek(file, -1, SEEK_CUR);
					}
					if (out.link_options)
						fprintf(file, "\n    lflags = %s\n\n", out.link_options);
					else
						fwrite("\n\n", 1, 2, file);
					break;
				}
				case lua::project_type::static_library:
				{
					int32_t result = snprintf(nullptr, 0, "lib/%s.s", out.name);
					build_out = tmalloc<char>(result + 1);
					snprintf(build_out, result + 1, "lib/%s.a", out.name);

					fprintf(file, "build %s: lib ", build_out);
					for (uint32_t i {0}; i < out.sources_size; ++i)
						fprintf(file, "obj/%s/%s ", out.name, objs[i]);

					write_deps(out, file);
					if (out.deps_size)
					{
						fwrite("|", 1, 1, file);
						for (uint32_t i {0}; i < out.deps_size; ++i)
							fprintf(file, " %s", out.deps[i].name);
					}
					else
					{
						fseek(file, -1, SEEK_CUR);
					}
					fwrite("\n    lflags = rscu\n\n", 1, 19, file);

					break;
				}
				case lua::project_type::sources:
				{
					uint32_t result = 0;
					uint32_t name_len = strlen(out.name);
					for (uint32_t i {0}; i < out.sources_size; ++i)
						result += 4 /*obj/*/ + name_len + 1 /*/*/ + strlen(objs[i]) + 1;

					build_out = tmalloc<char>(result);
					memset(build_out, 0, result);
					uint32_t pos = 0;
					for (uint32_t i {0}; i < out.sources_size; ++i)
					{
						strncpy(build_out + pos, "obj/", 4);
						pos += 4;
						strncpy(build_out + pos, out.name, name_len);
						pos += name_len;
						build_out[pos] = '/';
						++pos;
						strncpy(build_out + pos, objs[i], strlen(objs[i]));
						pos += strlen(objs[i]);
						if (i != out.sources_size - 1)
						{
							build_out[pos] = ' ';
							++pos;
						}
						else
						{
							build_out[pos] = '\0';
							++pos;
						}
					}
				}
				default:
				{
					fwrite("\n", 1, 1, file);
					break;
				}
			}

			write_custom_command(out.post_build_cmds, out.post_build_cmd_size, file,
			                     build_out);

			if (build_out)
			{
				if (out.post_build_cmd_size)
				{
					if (fs::is_absolute(
							out.post_build_cmds[out.post_build_cmd_size - 1].out[0]))
					{
						fprintf(file, "build %s: phony %s\n\n", out.name,
						        out.post_build_cmds[out.post_build_cmd_size - 1].out[0]);
					}
					else if (str::starts_with(
								 out.post_build_cmds[out.post_build_cmd_size - 1].out[0],
								 "build"))
					{
						fprintf(file, "build %s: phony %s\n\n", out.name,
						        out.post_build_cmds[out.post_build_cmd_size - 1].out[0] +
						            6);
					}
					else
					{
						fprintf(file, "build %s: phony ../%s\n\n", out.name,
						        out.post_build_cmds[out.post_build_cmd_size - 1].out[0]);
					}
				}
				else
					fprintf(file, "build %s: phony %s\n\n", out.name, build_out);

				tfree(build_out);
			}

			for (uint32_t i {0}; i < out.sources_size; ++i)
				tfree(objs[i]);
			tfree(objs);
			tfree(cwd);
		}
	} // namespace

	int32_t ninja_generator(lua_State* L)
	{
		int32_t len = lua_rawlen(L, 1);

		luaL_argcheck(L, len > 0, 1, "generate must not be empty");

		// TODO allow customizing output directory

		if (!fs::dir_exists("build/"))
			fs::create_dir("build/");

		FILE* file = fopen("build/build.ninja", "w");

		if (!file)
			luaL_error(L, "failed to open file for write");

		// create rules
		constexpr char rules[] =
			R"(rule cxx
    description = Compiling ${in}
    deps = gcc
    depfile = ${out}.d
    command = clang++ -fdiagnostics-absolute-paths -fcolor-diagnostics -fansi-escape-codes ${cxxflags} -MMD -MF ${out}.d -c ${in} -o ${out}

rule lib
    description = Creating ${out}
    command = llvm-ar ${lflags} ${out} ${in}

rule link
    description = Creating ${out}
    command = clang++ ${lflags} ${in} -o ${out}

)";

#ifdef _WIN32
		constexpr char cmd_rule[] =
			R"(rule cmd
    description = Running ${cmd}
    command = cmd /c pushd .. && ${cmd}

rule copy
    description = Copying ${in} to ${out}
    command = %s cp ${in} ${out}

)";
		char* mingen_path = fs::get_current_executable_path();
		fprintf(file, cmd_rule, mingen_path);
#elif defined(__linux__) || defined(__APPLE__)
		constexpr char cmd_rule[] =
			R"(rule cmd
    description = Running ${cmd}
    command = pushd .. && ${cmd}

rule copy
    description = Copying ${in} to ${out}
    command = cp ${in} ${out}

)";
		fwrite(cmd_rule, 1, sizeof(cmd_rule) - 1, file);
#endif

		fwrite(rules, 1, sizeof(rules) - 1, file);

		uint32_t* original_outputs = tmalloc<uint32_t>(len);

		lua::output* outputs = tmalloc<lua::output>(len);
		uint32_t     outputs_size = 0;
		uint32_t     outputs_capacity = len;

		for (uint32_t i {0}; i < len; ++i)
		{
			lua_rawgeti(L, 1, i + 1);
			lua::output out = lua::parse_output(L);
			if (out.type == lua::project_type::prebuilt)
			{
				luaL_error(L,
				           "Cannot ask for prebuilt projet '%s' to be explicitly built",
				           out.name);
			}
			for (uint32_t j {0}; j < out.deps_size; ++j)
			{
				bool write {true};
				for (uint32_t k {0}; k < outputs_size; ++k)
				{
					if (strcmp(outputs[k].name, out.deps[j].name) == 0)
					{
						write = false;
						break;
					}
				}

				if (write)
				{
					if (outputs_capacity == outputs_size)
					{
						outputs = trealloc(outputs, outputs_capacity * 2);
						outputs_capacity *= 2;
					}
					outputs[outputs_size] = out.deps[j];
					++outputs_size;
				}
			}

			if (outputs_capacity == outputs_size)
			{
				outputs = trealloc(outputs, outputs_capacity * 2);
				outputs_capacity *= 2;
			}
			outputs[outputs_size] = out;
			original_outputs[i] = outputs_size;
			++outputs_size;

			lua_pop(L, 1);
		}

		if (g.gen_compile_db)
			generate_db(outputs, outputs_size);

		for (uint32_t i {0}; i < outputs_size; ++i)
			generate(outputs[i], file);

		fwrite("default", 1, 7, file);
		for (uint32_t i {0}; i < len; ++i)
			fprintf(file, " %s", outputs[original_outputs[i]].name);
		fwrite("\n", 1, 1, file);

		for (uint32_t i {0}; i < len; ++i)
			lua::free_output(outputs[original_outputs[i]]);
		tfree(outputs);
		tfree(original_outputs);
		fclose(file);
		return 0;
	}
} // namespace gen