#pragma once

#include <stdint.h>

namespace fs
{
	struct list_dirs_res
	{
		char**   dirs;
		uint32_t size;
	};

	struct list_files_res
	{
		char**   files;
		uint32_t size;
	};

	/// @brief Lists directories contained in `dir`.
	/// @param dir String indicating the directory filter to read. A null or not '\0'
	/// terminated string results in undefined behavior
	/// @return list_dirs_res List of directories contained in `dir`. If no directories
	/// are present, `dirs = nullptr` and `size = 0`.
	list_dirs_res list_dirs(char const* dir_filter);

	/// @brief Lists files contained in `dir_filter`, filtered by `file_filter`.
	/// @param dir_filter String indicating the directory filter to read. A null or not
	/// '\0' terminated string results in undefined behavior.
	/// @param file_filter Non null, '\0' terminated string indicating the filter to
	/// process files discovered by `dir_filter` to read. If null, the files are not
	/// filtered.
	/// @return list_files_res List of files present in `dir_filter`, matching
	/// `file_filter`. If no files match, `files = nullptr` and `size = 0`.
	list_files_res list_files(char const* dir_filter, char const* file_filter);

	/// @brief Verifies `file` presence in the filesystem.
	/// @param file String pointing to the file to verify. The file path is verified as
	/// is, meaning it will use current working directory for relative path.
	/// @return true File exists.
	/// @return false File doesn't exist.
	bool file_exists(char const* file);

	/// @brief Verifies `dir` presence in the filesystem.
	/// @param file String pointing to the directory to verify. The directory path is
	/// verified as is, meaning it will use current working directory for relative path.
	/// @return true Directory exists.
	/// @return false Directory doesn't exist.
	bool dir_exists(char const* dir);

	/// @brief Retrieves the current executable path.
	/// @return char* The current executable path.
	char* get_current_executable_path();

	/// @brief Gets the current working directory.
	/// @return char* The working directory as a full path.
	char* get_cwd();

	/// @brief Sets the current working directory.
	/// @param cwd The current working directory.
	void set_cwd(char const* cwd);

	/// @brief Checks if the path is absolute.
	/// @param path The path to check.
	/// @return true Path is absolute.
	/// @return false Path is relative.
	bool is_absolute(char const* path);

	/// @brief Converts a path to a canonical absolute path.
	/// @param path The path to normalize.
	/// @param base Base path. Leaving it empty will take the current working directory.
	/// @return char* Canonical absolute path.
	char* canonical(char const* path, char const* base = nullptr);

	/// @brief Creates a directory. Doesn't create directories recursively.
	/// @param path Path to the directory to create.
	/// @return true Directory created.
	/// @return false Directory not created.
	bool create_dir(char const* path);

	/// @brief Deletes a directory. This will also delete children.
	/// @param path Path to the directory to delete.
	/// @return true Directory deleted
	/// @return false Directory not deleted
	bool delete_dir(char const* path);

	/// @brief Copies a file.
	/// @param src_path Current, path of the file to copy.
	/// @param dst_path Path of the file to be copied.
	/// @param overwrite Allow overwrite if dst_path exists, or not.
	/// @return true File copied.
	/// @return false File not copied.
	bool copy_file(char const* src_path, char const* dst_path, bool overwrite);

	/// @brief Deletes a file.
	/// @param path Path to the file to delete.
	/// @return true File deleted.
	/// @return false File not deleted.
	bool delete_file(char const* path);

	/// @brief Modifies the last write timestamp to current time.
	/// @param path Path to the file to update.
	/// @return true Update occurred.
	/// @return false Update not occurred.
	bool update_last_write_time(char const* path);

	/// @brief Moves src_path to dst_path. if src_path is a directory, the function
	/// also moves its children.
	/// @param src_path Current, existing path of the file/directory to move.
	/// @param dst_path New, non-existing path of the moved file/directory.
	/// @return true Move succeeded.
	/// @return false Move failed.
	bool move(char* const src_path, char* const dst_path);
}; // namespace fs