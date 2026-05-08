#include "os.hpp"

#include <stdint.h>

extern "C"
{
#include <lua/lauxlib.h>
#include <lua/lua.h>
#include <lua/lualib.h>
}

#ifdef _WIN32
#include <win32/file.h>
#include <win32/io.h>
#include <win32/process.h>
#include <win32/threads.h>
#elif defined(__linux__) || defined(__APPLE__)
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include "fs.hpp"
#include "lua_env.hpp"
#include "mem.hpp"
#include "string.hpp"

namespace os
{
	int execute(lua_State* L)
	{
		int top = lua_gettop(L);
		luaL_argcheck(L, lua_isstring(L, 1), 1, "'string' expected");
		if (top == 2)
			luaL_argcheck(L, lua_isstring(L, 2), 2, "'string' expected");
#ifdef _WIN32
		wchar_t* wworking_dir = nullptr;
		wchar_t* wcmd = nullptr;

		if (top == 2)
		{
			char const* working_dir = lua_tostring(L, 1);
			uint32_t    working_dir_size = strlen(working_dir);
			if (!fs::is_absolute(working_dir))
			{
				char* new_working_dir =
					lua::resolve_path_from_script(L, working_dir, working_dir_size);

				wworking_dir = char_to_wchar(new_working_dir);
				tfree(new_working_dir);
			}
			else
				wworking_dir = char_to_wchar(working_dir);

			wcmd = char_to_wchar(lua_tostring(L, 2));
		}
		else
			wcmd = char_to_wchar(lua_tostring(L, 1));

		DWORD  return_code = UINT32_MAX;
		HANDLE stdout_read = nullptr;
		HANDLE stdout_write = nullptr;
		HANDLE stderr_read = nullptr;
		HANDLE stderr_write = nullptr;

		SECURITY_ATTRIBUTES sa;
		sa.nLength = sizeof(SECURITY_ATTRIBUTES);
		sa.bInheritHandle = TRUE;
		sa.lpSecurityDescriptor = nullptr;

		CreatePipe(&stdout_read, &stdout_write, &sa, 0);
		CreatePipe(&stderr_read, &stderr_write, &sa, 0);

		SetHandleInformation(stdout_read, HANDLE_FLAG_INHERIT, 0);
		SetHandleInformation(stderr_read, HANDLE_FLAG_INHERIT, 0);

		STARTUPINFOW info;
		memset(&info, 0, sizeof(STARTUPINFOW));
		info.cb = sizeof(STARTUPINFOW);
		info.hStdOutput = stdout_write;
		info.hStdError = stderr_write;
		info.dwFlags |= STARTF_USESTDHANDLES;

		PROCESS_INFORMATION handles;
		memset(&handles, 0, sizeof(PROCESS_INFORMATION));

		if (CreateProcessW(nullptr, wcmd, nullptr, nullptr, true, 0, nullptr,
		                   wworking_dir, &info, &handles))
		{
			CloseHandle(handles.hThread);
			CloseHandle(stdout_write);
			CloseHandle(stderr_write);

			// Read from pipes while process is running to prevent blocking
			char     tmp_buf[512];
			DWORD    readlen;
			char*    stdout_buf = tmalloc<char>(513);
			char*    stderr_buf = tmalloc<char>(513);
			uint32_t stdout_buf_size = 512;
			uint32_t stderr_buf_size = 512;
			uint32_t stdout_written = 0;
			uint32_t stderr_written = 0;
			memset(stdout_buf, 0, 513);
			memset(stderr_buf, 0, 513);

			bool process_done = false;
			while (!process_done)
			{
				DWORD wait_result = WaitForSingleObject(handles.hProcess, 0);
				if (wait_result == WAIT_OBJECT_0)
					process_done = true;

				// Read from stdout pipe
				while (
					ReadFile(stdout_read, tmp_buf, sizeof(tmp_buf), &readlen, nullptr) &&
					readlen > 0)
				{
					if (stdout_written + readlen > stdout_buf_size)
					{
						stdout_buf = trealloc(stdout_buf, stdout_buf_size + 512);
						stdout_buf_size += 512;
					}
					memcpy(stdout_buf + stdout_written, tmp_buf, readlen);
					stdout_written += readlen;
				}

				// Read from stderr pipe
				while (
					ReadFile(stderr_read, tmp_buf, sizeof(tmp_buf), &readlen, nullptr) &&
					readlen > 0)
				{
					if (stderr_written + readlen > stderr_buf_size)
					{
						stderr_buf = trealloc(stderr_buf, stderr_buf_size + 512);
						stderr_buf_size += 512;
					}
					memcpy(stderr_buf + stderr_written, tmp_buf, readlen);
					stderr_written += readlen;
				}

				if (!process_done)
					WaitForSingleObject(handles.hProcess, 1000);
			}

			GetExitCodeProcess(handles.hProcess, &return_code);
			CloseHandle(handles.hProcess);

			stdout_buf[stdout_written] = '\0';
			stderr_buf[stderr_written] = '\0';

			lua_newtable(L);
			lua_pushinteger(L, return_code);
			lua_setfield(L, -2, "code");

			if (stdout_written > 0)
			{
				lua_pushstring(L, stdout_buf);
				lua_setfield(L, -2, "stdout");
			}

			if (stderr_written > 0)
			{
				lua_pushstring(L, stderr_buf);
				lua_setfield(L, -2, "stderr");
			}

			tfree(stdout_buf);
			tfree(stderr_buf);
		}
		else
		{
			return_code = GetLastError();
			lua_newtable(L);
			lua_pushinteger(L, return_code);
			lua_setfield(L, -2, "code");

			CloseHandle(stdout_read);
			CloseHandle(stdout_write);
			CloseHandle(stderr_read);
			CloseHandle(stderr_write);
		}

		CloseHandle(stdout_read);
		CloseHandle(stderr_read);
		tfree(wworking_dir);
		tfree(wcmd);
#elif defined(__linux__) || defined(__APPLE__)
		char*       working_dir = nullptr;
		char const* cmd = nullptr;

		if (top == 2)
		{
			char const* lua_working_dir = lua_tostring(L, 1);
			uint32_t    working_dir_size = strlen(lua_working_dir);
			if (!fs::is_absolute(lua_working_dir))
			{
				char* new_working_dir =
					lua::resolve_path_from_script(L, lua_working_dir, working_dir_size);

				working_dir = new_working_dir;
			}
			else
			{
				working_dir = tmalloc<char>(working_dir_size + 1);
				strcpy(working_dir, lua_working_dir);
			}

			cmd = lua_tostring(L, 2);
		}
		else
			cmd = lua_tostring(L, 1);

		int stdout_fd[2];
		int stderr_fd[2];
		pipe(stdout_fd);
		pipe(stderr_fd);
		pid_t pid;

		pid = fork();
		switch (pid)
		{
			case -1:
			{
				close(stdout_fd[0]);
				close(stdout_fd[1]);
				close(stderr_fd[0]);
				close(stderr_fd[1]);

				lua_pushinteger(L, -1);
				break;
			}

			case 0:
			{
				close(stdout_fd[0]);
				close(stderr_fd[0]);

				dup2(stdout_fd[1], 1);
				dup2(stderr_fd[1], 2);

				close(stdout_fd[1]);
				close(stderr_fd[1]);

				chdir(working_dir);
				execl("/bin/sh", "sh", "-c", cmd, nullptr);
				break;
			}
		}
		tfree(working_dir);

		int status;
		waitpid(pid, &status, 0);

		lua_newtable(L);
		if (WIFEXITED(status))
			lua_pushinteger(L, WEXITSTATUS(status));
		else
			lua_pushinteger(L, -1);

		lua_setfield(L, -2, "code");

		close(stdout_fd[1]);
		close(stderr_fd[1]);

		char     tmp_buf[512] {'\0'};
		uint32_t written {0};

		char*    out_buf = tmalloc<char>(513);
		uint32_t buf_size = 512;
		memset(out_buf, 0, 513);

		ssize_t readlen;
		while ((readlen = read(stdout_fd[0], tmp_buf, sizeof(tmp_buf))) != 0)
		{
			if (written + readlen > buf_size)
			{
				out_buf = trealloc(out_buf, buf_size + 512);
				buf_size += 512;
			}
			strncpy(out_buf + written, tmp_buf, readlen);
			written += readlen;
			out_buf[written] = '\0';
		}

		if (written > 0)
		{
			lua_pushstring(L, out_buf);
			lua_setfield(L, -2, "stdout");
		}

		written = 0;
		while ((readlen = read(stderr_fd[0], tmp_buf, sizeof(tmp_buf))) != 0)
		{
			if (written + readlen > buf_size)
			{
				out_buf = trealloc(out_buf, buf_size + 512);
				buf_size += 512;
			}
			strncpy(out_buf + written, tmp_buf, readlen);
			written += readlen;
			out_buf[written] = '\0';
		}

		if (written > 0)
		{
			lua_pushstring(L, out_buf);
			lua_setfield(L, -2, "stderr");
		}
		tfree(out_buf);
#endif
		return 1;
	}

	int copy_file(lua_State* L)
	{
		luaL_argcheck(L, lua_isstring(L, 1), 1, "'string' expected");
		luaL_argcheck(L, lua_isstring(L, 2), 2, "'string' expected");

		char const* src_path = lua_tostring(L, 1);
		char const* dst_path = lua_tostring(L, 2);
		char*       resolved_src_path = nullptr;
		char*       resolved_dst_path = nullptr;

		if (!fs::is_absolute(src_path))
		{
			resolved_src_path = lua::resolve_path_from_script(L, src_path);
		}
		else
		{
			resolved_src_path = tmalloc<char>(strlen(src_path) + 1);
			strcpy(resolved_src_path, src_path);
		}

		if (!fs::is_absolute(dst_path))
		{
			resolved_dst_path = lua::resolve_path_from_script(L, dst_path);
		}
		else
		{
			resolved_dst_path = tmalloc<char>(strlen(dst_path) + 1);
			strcpy(resolved_dst_path, dst_path);
		}

		bool res = fs::copy_file(resolved_src_path, resolved_dst_path, true);

		tfree(resolved_src_path);
		tfree(resolved_dst_path);

		lua_pushboolean(L, res);

		return 1;
	}

	int create_directory(lua_State* L)
	{
		luaL_argcheck(L, lua_isstring(L, 1), 1, "'string' expected");
		if (lua_gettop(L) == 2)
			luaL_argcheck(L, lua_isboolean(L, 2), 2, "'bool' expected");

		char const* path = lua_tostring(L, 1);
		bool        recursive = lua_gettop(L) == 2 ? lua_toboolean(L, 2) : true;

		char* resolved_path = nullptr;
		if (!fs::is_absolute(path))
		{
			resolved_path = lua::resolve_path_from_script(L, path);
		}
		else
		{
			resolved_path = tmalloc<char>(strlen(path) + 1);
			strcpy(resolved_path, path);
		}

		bool success = true;
		if (recursive)
		{
			char*    path_frag = tmalloc<char>(strlen(resolved_path) + 1);
			uint32_t path_frag_len = 0;
			path_frag[0] = '\0';
			while (path_frag_len < strlen(resolved_path))
			{
				path_frag[path_frag_len] = resolved_path[path_frag_len];
				if (path_frag[path_frag_len] == '/' || path_frag[path_frag_len] == '\\')
				{
					path_frag[path_frag_len + 1] = '\0';
					success &= fs::create_dir(path_frag);
				}
				++path_frag_len;
			}
			tfree(path_frag);
		}
		else
		{
			success = fs::create_dir(resolved_path);
		}

		lua_pushboolean(L, success);

		return 1;
	}
} // namespace os
