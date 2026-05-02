#include "fs.hpp"

#ifdef _WIN32
#include <win32/file.h>
#include <win32/io.h>
#include <win32/misc.h>
#elif defined(__linux__) || defined(__APPLE__)
#include <dirent.h>
#include <fcntl.h>
#if defined(__linux__)
#include <sys/sendfile.h>
#elif defined(__APPLE__)
#include <copyfile.h>
#endif
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <utime.h>
#endif

#include "mem.hpp"
#include "string.hpp"

namespace fs
{
#ifdef _WIN32
	list_dirs_res list_dirs(char const* dir_filter)
	{
		uint32_t dirs_count {0};
		STACK_CHAR_TO_WCHAR(dir_filter, wdir_tmp)
		uint32_t tmp_len = wcslen(wdir_tmp);
		wchar_t* wdir = tmalloc<wchar_t>(tmp_len + 2);
		wcsncpy(wdir, wdir_tmp, tmp_len);
		wdir[tmp_len] = L'*';
		wdir[tmp_len + 1] = L'\0';

		WIN32_FIND_DATAW entry_data;

		HANDLE entry = FindFirstFileExW(wdir, FindExInfoBasic, &entry_data,
		                                FindExSearchNameMatch, nullptr, 0);

		if (entry == INVALID_HANDLE_VALUE)
		{
			tfree(wdir);
			return {nullptr, 0};
		}

		do
		{
			if (entry_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY &&
			    wcscmp(entry_data.cFileName, L".") != 0 &&
			    wcscmp(entry_data.cFileName, L"..") != 0)
			{
				++dirs_count;
			}
		}
		while (FindNextFileW(entry, &entry_data) != 0);

		if (!dirs_count)
		{
			tfree(wdir);
			return {nullptr, 0};
		}

		char**   dirs {tmalloc<char*>(dirs_count)};
		uint32_t i {0};
		entry = FindFirstFileExW(wdir, FindExInfoBasic, &entry_data,
		                         FindExSearchNameMatch, nullptr, 0);
		do
		{
			if (entry_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY &&
			    wcscmp(entry_data.cFileName, L".") != 0 &&
			    wcscmp(entry_data.cFileName, L"..") != 0)
			{
				STACK_WCHAR_TO_CHAR(entry_data.cFileName, dn)
				dirs[i] = tmalloc<char>(strlen(dir_filter) + strlen(dn) + 1);
				strncpy(dirs[i], dir_filter, strlen(dir_filter));
				strcpy(dirs[i] + strlen(dir_filter), dn);
				++i;
			}
		}
		while (FindNextFileW(entry, &entry_data) != 0);

		tfree(wdir);
		return {dirs, dirs_count};
	}

	list_files_res list_files(char const* dir_filter, char const* file_filter)
	{
		uint32_t files_count {0};
		STACK_CHAR_TO_WCHAR(dir_filter, wdir_tmp)
		uint32_t tmp_len = wcslen(wdir_tmp);
		wchar_t* wdir = tmalloc<wchar_t>(tmp_len + 2);
		wcsncpy(wdir, wdir_tmp, tmp_len);
		wdir[tmp_len] = L'*';
		wdir[tmp_len + 1] = L'\0';

		WIN32_FIND_DATAW entry_data;
		HANDLE           entry = FindFirstFileExW(wdir, FindExInfoBasic, &entry_data,
		                                          FindExSearchNameMatch, nullptr, 0);
		if (entry == INVALID_HANDLE_VALUE)
		{
			tfree(wdir);
			return {nullptr, 0};
		}

		do
		{
			STACK_WCHAR_TO_CHAR(entry_data.cFileName, fn)
			if (!(entry_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
			    (!file_filter || str::ends_with(fn, file_filter)))
			{
				++files_count;
			}
		}
		while (FindNextFileW(entry, &entry_data) != 0);

		if (!files_count)
		{
			tfree(wdir);
			return {nullptr, 0};
		}

		char**   files {tmalloc<char*>(files_count)};
		uint32_t i = 0;
		entry = FindFirstFileExW(wdir, FindExInfoBasic, &entry_data,
		                         FindExSearchNameMatch, nullptr, 0);
		do
		{
			STACK_WCHAR_TO_CHAR(entry_data.cFileName, fn)
			if (!(entry_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
			    (!file_filter || str::ends_with(fn, file_filter)))
			{
				files[i] = tmalloc<char>(strlen(dir_filter) + strlen(fn) + 1);
				strncpy(files[i], dir_filter, strlen(dir_filter));
				strcpy(files[i] + strlen(dir_filter), fn);
				++i;
			}
		}
		while (FindNextFileW(entry, &entry_data) != 0);

		tfree(wdir);
		return {files, files_count};
	}

	bool file_exists(char const* file)
	{
		STACK_CHAR_TO_WCHAR(file, wfile);
		uint32_t attr = GetFileAttributesW(wfile);

		return (attr != INVALID_FILE_ATTRIBUTES) && !(attr & FILE_ATTRIBUTE_DIRECTORY);
	}

	bool dir_exists(char const* dir)
	{
		STACK_CHAR_TO_WCHAR(dir, wdir);
		uint32_t attr = GetFileAttributesW(wdir);

		return (attr != INVALID_FILE_ATTRIBUTES) && (attr & FILE_ATTRIBUTE_DIRECTORY);
	}

	char* get_current_executable_path()
	{
		wchar_t wcurrent_path[512] {'\0'};
		GetModuleFileNameW(nullptr, wcurrent_path, 512);
		return wchar_to_char(wcurrent_path);
	}

	char* get_cwd()
	{
		wchar_t wcwd[512];
		GetCurrentDirectoryW(512, wcwd);
		return wchar_to_char(wcwd);
	}

	void set_cwd(char const* cwd)
	{
		STACK_CHAR_TO_WCHAR(cwd, wcwd);
		SetCurrentDirectoryW(wcwd);
	}

	bool is_absolute(char const* path)
	{
		return path[1] == ':';
	}

	bool create_dir(char const* path)
	{
		STACK_CHAR_TO_WCHAR(path, wpath);
		return CreateDirectoryW(wpath, nullptr);
	}

	bool delete_dir(char const* path)
	{
		list_files_res files = list_files(path, nullptr);
		bool           success = true;
		for (uint32_t i {0}; i < files.size; ++i)
		{
			success &= delete_file(files.files[i]);
			tfree(files.files[i]);
		}
		tfree(files.files);

		if (!success)
			return false;

		auto [dirs, dirs_size] = list_dirs(path);
		for (uint32_t i {0}; i < dirs_size; ++i)
		{
			char* delete_dir = tmalloc<char>(strlen(dirs[i]) + 2);
			strcpy(delete_dir, dirs[i]);
			strcpy(delete_dir + strlen(dirs[i]), "/");

			fs::delete_dir(delete_dir);
			tfree(delete_dir);
			tfree(dirs[i]);
		}
		tfree(dirs);

		if (!success)
			return false;

		STACK_CHAR_TO_WCHAR(path, wpath);
		success &= RemoveDirectoryW(wpath) != 0;

		return success;
	}

	bool copy_file(char const* src_path, char const* dst_path, bool overwrite)
	{
		STACK_CHAR_TO_WCHAR(src_path, wsrc_path);
		STACK_CHAR_TO_WCHAR(dst_path, wdst_path);
		return CopyFileW(wsrc_path, wdst_path, !overwrite);
	}

	bool delete_file(char const* path)
	{
		STACK_CHAR_TO_WCHAR(path, wpath);
		return DeleteFileW(wpath) != 0;
	}

	bool update_last_write_time(char const* path)
	{
		STACK_CHAR_TO_WCHAR(path, wpath);

		FILETIME ft;
		GetSystemTimeAsFileTime(&ft);
		bool res = false;

		HANDLE h = CreateFileW(wpath, FILE_WRITE_ATTRIBUTES, 0, nullptr, OPEN_EXISTING,
		                       FILE_ATTRIBUTE_NORMAL, nullptr);
		if (h != INVALID_HANDLE_VALUE)
		{
			res = SetFileTime(h, nullptr, nullptr, &ft);
			CloseHandle(h);
		}

		return res;
	}

	bool move(char* const src_path, char* const dst_path)
	{
		STACK_CHAR_TO_WCHAR(src_path, wsrc_path);
		STACK_CHAR_TO_WCHAR(dst_path, wdst_path);
		return MoveFileW(wsrc_path, wdst_path) != 0;
	}
#elif defined(__linux__) || defined(__APPLE__)
	list_dirs_res list_dirs(char const* dir_filter)
	{
		uint32_t dirs_count {0};
		DIR*     dir_p = opendir(dir_filter);

		if (!dir_p)
			return {nullptr, 0};

		dirent* entry {nullptr};
		while ((entry = readdir(dir_p)))
			if (entry->d_type == DT_DIR && strcmp(entry->d_name, ".") != 0 &&
			    strcmp(entry->d_name, "..") != 0)
				++dirs_count;

		closedir(dir_p);

		if (!dirs_count)
			return {nullptr, 0};

		char**   dirs {tmalloc<char*>(dirs_count)};
		uint32_t i {0};
		dir_p = opendir(dir_filter);
		while ((entry = readdir(dir_p)))
		{
			if (entry->d_type == DT_DIR && strcmp(entry->d_name, ".") != 0 &&
			    strcmp(entry->d_name, "..") != 0)
			{
				dirs[i] = tmalloc<char>(strlen(dir_filter) + strlen(entry->d_name) + 1);
				strncpy(dirs[i], dir_filter, strlen(dir_filter));
				strcpy(dirs[i] + strlen(dir_filter), entry->d_name);
				++i;
			}
		}

		closedir(dir_p);

		return {dirs, dirs_count};
	}

	list_files_res list_files(char const* dir_filter, char const* file_filter)
	{
		uint32_t files_count {0};
		DIR*     dir_p = opendir(dir_filter);

		if (!dir_p)
			return {nullptr, 0};

		dirent* entry {nullptr};
		while ((entry = readdir(dir_p)))
			if (entry->d_type == DT_REG &&
			    (!file_filter || str::ends_with(entry->d_name, file_filter)))
				++files_count;

		closedir(dir_p);

		if (!files_count)
			return {nullptr, 0};

		char**   files {tmalloc<char*>(files_count)};
		uint32_t i {0};
		dir_p = opendir(dir_filter);
		while ((entry = readdir(dir_p)))
		{
			if (entry->d_type == DT_REG &&
			    (!file_filter || str::ends_with(entry->d_name, file_filter)))
			{
				files[i] = tmalloc<char>(strlen(dir_filter) + strlen(entry->d_name) + 1);
				strncpy(files[i], dir_filter, strlen(dir_filter));
				strcpy(files[i] + strlen(dir_filter), entry->d_name);
				++i;
			}
		}

		closedir(dir_p);

		return {files, files_count};
	}

	bool file_exists(char const* file)
	{
		return access(file, F_OK) == 0;
	}

	bool dir_exists(char const* dir)
	{
		struct stat res;
		int32_t     err = stat(dir, &res);

		return err == 0 && S_ISDIR(res.st_mode);
	}

	char* get_cwd()
	{
		return getcwd(nullptr, 0);
	}

	void set_cwd(char const* cwd)
	{
		/*int res = */ chdir(cwd);
	}

	bool is_absolute(char const* path)
	{
		return str::starts_with(path, "/");
	}

	bool create_dir(char const* path)
	{
		return mkdir(path, 0755) == 0;
	}

	bool delete_dir(char const* path)
	{
		return rmdir(path) == 0;
	}

	bool copy_file(char const* src_path, char const* dst_path, bool overwrite)
	{
		if (overwrite && file_exists(dst_path))
			return false;

		int fd_in = open(src_path, O_RDONLY);
		int fd_out = open(src_path, O_WRONLY);

		if (fd_in == -1)
			return false;

#if defined(__linux__)
		struct stat stat;
		fstat(fd_in, &stat);

		return sendfile(fd_out, fd_in, nullptr, stat.st_size) != -1;
#elif defined(__APPLE__)
		return fcopyfile(fd_in, fd_out, 0, COPYFILE_ALL) >= 0;
#endif
	}

	bool delete_file(char const* path)
	{
		return unlink(path) == 0;
	}

	bool update_last_write_time(char const* path)
	{
		struct stat    file_stat;
		struct utimbuf new_time;

		int res = stat(path, &file_stat);
		if (res != 0)
			return false;

		new_time.actime = file_stat.st_atime;
		new_time.modtime = time(nullptr);

		res = utime(path, &new_time);
		return res == 0;
	}

	bool move(char* const src_path, char* const dst_path)
	{
		bool res = copy_file(src_path, dst_path, true);
		if (res == false)
			return false;
		return delete_file(src_path);
	}
#else
#error "Unsupported platform"
#endif
} // namespace fs