#include "net.hpp"

#include "fs.hpp"
#include "lua_env.hpp"
#include "mem.hpp"
#include "string.hpp"

extern "C"
{
#include <lua/lauxlib.h>
#include <lua/lua.h>
#include <lua/lualib.h>
}

#ifdef _WIN32
#include <win32/crypt.h>
#include <win32/http.h>
#elif defined(__linux__) || defined(__APPLE__)
#include <curl/curl.h>
#include <openssl/md5.h>
#endif

extern "C"
{
#include <minizip/mz.h>
#include <minizip/mz_strm.h>
#include <minizip/mz_strm_buf.h>
#include <minizip/mz_strm_os.h>
#include <minizip/mz_zip.h>
#include <minizip/mz_zip_rw.h>
}

#include <stdint.h>
#include <stdio.h>
#include <wchar.h>

namespace net
{
	namespace
	{
#ifdef _WIN32
		wchar_t const user_agent[] {L"mingen/1.0 (win-wininet)"};

#endif

		struct hash
		{
#ifdef _WIN32
			BCRYPT_ALG_HANDLE  alg_h;
			BCRYPT_HASH_HANDLE hash_h;

			uint32_t hash_object_size;
			uint8_t* hash_object;
#elif defined(__linux__) || defined(__APPLE__)
			MD5_CTX ctx;
#endif
			uint32_t hash_size;
			uint8_t* hash;
		};

		bool hash_init(hash& hash)
		{
#ifdef _WIN32
			unsigned long unused = 0;
			// open an algorithm handle and load the algorithm provider
			if (BCryptOpenAlgorithmProvider(&hash.alg_h, BCRYPT_MD5_ALGORITHM, nullptr,
			                                0) < 0)
			{
				return false;
			}

			// calculate the size of the buffer to hold the hash object
			if (BCryptGetProperty(hash.alg_h, BCRYPT_OBJECT_LENGTH,
			                      reinterpret_cast<uint8_t*>(&hash.hash_object_size),
			                      sizeof(uint32_t), &unused, 0) < 0)
			{
				return false;
			}

			// allocate the hash object on the heap
			hash.hash_object = tmalloc<uint8_t>(hash.hash_object_size);

			// calculate the length of the hash
			if (BCryptGetProperty(hash.alg_h, BCRYPT_HASH_LENGTH,
			                      reinterpret_cast<uint8_t*>(&hash.hash_size),
			                      sizeof(uint32_t), &unused, 0) < 0)
			{
				return false;
			}

			// allocate the hash buffer on the heap
			hash.hash = tmalloc<uint8_t>(hash.hash_size);

			// Initialize the hash object
			if (BCryptCreateHash(hash.alg_h, &hash.hash_h, hash.hash_object,
			                     hash.hash_object_size, nullptr, 0, 0) < 0)
				return false;

			return true;
#elif defined(__linux__) || defined(__APPLE__)
			hash.hash_size = 32 + 1;

			hash.hash = tmalloc<uint8_t>(hash.hash_size);
			memset(hash.hash, 0, hash.hash_size);
			int res = MD5_Init(&hash.ctx);
			return res == 1;
#endif
		}

		void hash_add(hash& hash, uint8_t* data, uint32_t size)
		{
			if (!size)
				return;

#ifdef _WIN32
			BCryptHashData(hash.hash_h, data, size, 0);
#elif defined(__linux__) || defined(__APPLE__)
			MD5_Update(&hash.ctx, data, size);
#endif
		}

		void hash_complete(hash& hash)
		{
#ifdef _WIN32
			BCryptFinishHash(hash.hash_h, hash.hash, hash.hash_size, 0);
#elif defined(__linux__) || defined(__APPLE__)
			MD5_Final(hash.hash, &hash.ctx);
#endif
		}

		void hash_free([[maybe_unused]] hash& hash)
		{
#ifdef _WIN32
			tfree(hash.hash_object);
#endif
			tfree(hash.hash);
		}

#if defined(__linux__) || defined(__APPLE__)
		struct write_userdata
		{
			hash& h;
			FILE* file;
		};

		size_t write_data(void* ptr, size_t size, size_t nmemb, write_userdata* ud)

		{
			if (!nmemb)
				return 0;

			hash_add(ud->h, static_cast<uint8_t*>(ptr), nmemb);
			size_t written = fwrite(ptr, 1, nmemb, ud->file);

			return written;
		}
#endif

		bool get_archive(char const* url, char const* dest, hash& h)
		{
#ifdef _WIN32
			STACK_CHAR_TO_WCHAR(url, wurl);

			HINTERNET internet = InternetOpenW(user_agent, INTERNET_OPEN_TYPE_PRECONFIG,
			                                   nullptr, nullptr, 0);
			if (!internet)
				return false;

			wchar_t         scheme[16], host[256], path[1024];
			URL_COMPONENTSW comps {0};
			comps.dwStructSize = sizeof(comps);
			comps.lpszScheme = scheme;
			comps.dwSchemeLength = 16;
			comps.lpszHostName = host;
			comps.dwHostNameLength = 256;
			comps.lpszUrlPath = path;
			comps.dwUrlPathLength = 1024;
			if (!InternetCrackUrlW(wurl, static_cast<uint32_t>(wcslen(wurl)), 0, &comps))
			{
				InternetCloseHandle(internet);
				return -1;
			}

			HINTERNET connection = InternetConnectW(internet, host, comps.nPort, nullptr,
			                                        nullptr, INTERNET_SERVICE_HTTP, 0, 0);

			if (!connection)
			{
				InternetCloseHandle(internet);
				return false;
			}

			uint32_t flags = INTERNET_FLAG_NO_COOKIES;
			if (wcscmp(scheme, L"https") == 0)
				flags |= INTERNET_FLAG_SECURE;

			HINTERNET request = HttpOpenRequestW(connection, L"GET", path, nullptr,
			                                     nullptr, nullptr, flags, 0);

			if (!request)
			{
				InternetCloseHandle(connection);
				InternetCloseHandle(internet);
				return false;
			}

			if (!HttpSendRequestW(request, nullptr, 0, nullptr, 0))
			{
				InternetCloseHandle(request);
				InternetCloseHandle(connection);
				InternetCloseHandle(internet);
				return false;
			}

			wchar_t status[4];
			DWORD   status_size = sizeof(status);
			if (!HttpQueryInfoW(request, HTTP_QUERY_STATUS_CODE, &status, &status_size,
			                    0))
			{
				InternetCloseHandle(request);
				InternetCloseHandle(connection);
				InternetCloseHandle(internet);
				return false;
			}

			if (wcscmp(status, L"200") != 0)
				return false;
#elif defined(__linux__) || defined(__APPLE__)
			CURL* curl;
			curl = curl_easy_init();
			if (!curl)
				return false;

#endif
			FILE* file = fopen(dest, "wb+");

			if (file)
			{
				hash_init(h);
#ifdef _WIN32
				// TODO use content length, and read loops for pretty printing
				// wchar_t  content_length[32];
				// DWORD    content_length_size = sizeof(content_length);
				// uint32_t claimed_size = 0;
				// if (HttpQueryInfoW(request, HTTP_QUERY_CONTENT_LENGTH,
				//                    static_cast<LPVOID>(&content_length),
				//                    &content_length_size, 0))
				// {
				// 	claimed_size = wcstol(content_length, NULL, 10);
				// }

				uint8_t  response_buffer[4096 * 4];
				DWORD    bytes_available;
				uint32_t total_read = 0;
				while (
					(InternetQueryDataAvailable(request, &bytes_available, 0, 0) != 0) &&
					bytes_available > 0)
				{
					DWORD size_read = 0;

					uint32_t return_code =
						InternetReadFile(request, response_buffer, 4096 * 4, &size_read);

					if (return_code && size_read > 0)
					{
						fwrite(response_buffer, 1, size_read, file);
						total_read += size_read;
						hash_add(h, response_buffer, size_read);
					}
					else
						break;
				}
#elif defined(__linux__) || defined(__APPLE__)
				write_userdata ud {h, file};
				curl_easy_setopt(curl, CURLOPT_URL, url);
				curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ud);
				curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data);
				// curl_easy_setopt(curl, CURLOPT_VERBOSE, 1);
				curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1);

				CURLcode res = curl_easy_perform(curl);
				curl_easy_cleanup(curl);
				if (res != CURLE_OK)
					return false;
#endif

				fclose(file);

				hash_complete(h);

				return true;
			}

			return false;
		}

		// https://gist.github.com/xsleonard/7341172?permalink_comment_id=2700436#gistcomment-2700436
		char* bin_to_hex(uint8_t* data, uint32_t size)
		{
			char* hex_str = tmalloc<char>(size * 2);
			for (uint32_t i = 0; i < size; i++)
			{
				hex_str[2 * i] = (data[i] >> 4) + 48;
				hex_str[2 * i + 1] = (data[i] & 15) + 48;
				if (hex_str[2 * i] > 57)
					hex_str[2 * i] += 7;
				if (hex_str[2 * i + 1] > 57)
					hex_str[2 * i + 1] += 7;
			}

			return hex_str;
		}

		int32_t hex_to_bin(char const* hex_str, uint8_t* out_data, int32_t out_len)
		{
			size_t hex_len = strlen(hex_str);
			if (hex_len % 2 != 0)
				return -1;

			size_t num_bytes = hex_len / 2;
			if (num_bytes > out_len)
				return -1;

			for (size_t i = 0; i < num_bytes; i++)
				if (sscanf(hex_str + i * 2, "%2hhx", &out_data[i]) != 1)
					return -1;
			return (int)num_bytes;
		}
	} // namespace

	int32_t download(lua_State* L)
	{
		luaL_argcheck(L, lua_isstring(L, 1), 1, "'string' expected");
		luaL_argcheck(L, lua_isstring(L, 2), 2, "'string' expected");
		if (lua_gettop(L) == 3)
			luaL_argcheck(L, lua_isstring(L, 3), 3, "'string' expected");

		char const* url = lua_tostring(L, 1);
		char const* lua_dest = lua_tostring(L, 2);
		char const* lua_hash = lua_gettop(L) == 3 ? lua_tostring(L, 3) : nullptr;
		uint32_t    dest_len = strlen(lua_dest);
		bool        trailing_slash = str::ends_with(lua_dest, "/");
		char*       dest = nullptr;

		if (!fs::is_absolute(lua_dest))
		{
			dest = lua::resolve_path_from_script(L, lua_dest, dest_len);
			dest_len = strlen(dest);
		}
		else
		{
			dest = tmalloc<char>(dest_len + 1);
			strncpy(dest, lua_dest, dest_len);
			dest[dest_len] = '\0';
		}

		if (!fs::dir_exists(dest))
		{
			char*    frag = tmalloc<char>(strlen(dest));
			uint32_t dest_pos = 0;
			uint32_t dir_pos = 0;
			while ((dir_pos = str::find(dest + dest_pos, "/")) != UINT32_MAX)
			{
				strncpy(frag, dest, dir_pos + dest_pos);
				frag[dir_pos + dest_pos] = '\0';
				if (!fs::dir_exists(frag))
					fs::create_dir(frag);

				dest_pos += dir_pos + 1;
			}
			tfree(frag);
			fs::create_dir(dest);
		}

		uint32_t archive_pos = str::rfind(url, "/") + 1;
		char*    zip_dest = tmalloc<char>(dest_len + !trailing_slash + 10 /*.dl-cache/*/ +
		                                  strlen(url + archive_pos) + 1);
		strcpy(zip_dest, dest);
		if (!trailing_slash)
			zip_dest[dest_len] = '/';

		strcpy(zip_dest + dest_len + !trailing_slash, ".dl-cache/");
		if (!fs::dir_exists(zip_dest))
			fs::create_dir(zip_dest);

		strcpy(zip_dest + dest_len + !trailing_slash + 10, url + archive_pos);

		char* meta_dest =
			tmalloc<char>(dest_len + !trailing_slash + 14 /*.dl-cache/meta*/ + 1);
		strcpy(meta_dest, dest);
		if (!trailing_slash)
			meta_dest[dest_len] = '/';
		strcpy(meta_dest + dest_len + !trailing_slash, ".dl-cache/meta");
		if (lua_hash)
		{
			FILE* meta_file = fopen(meta_dest, "r");
			if (meta_file)
			{
				uint8_t buf[128];
				size_t  read = fread(buf, 1, 128, meta_file);
				fclose(meta_file);

				bool valid = true;
				if (read != strlen(lua_hash))
					valid = false;
				else
				{
					for (size_t i = 0; i < strlen(lua_hash); ++i)
					{
						char meta_char = buf[i];
						char lua_char = lua_hash[i];
						if (lua_char >= 'A' && lua_char <= 'Z')
							lua_char += 0x20;
						if (meta_char >= 'A' && meta_char <= 'Z')
							meta_char += 0x20;

						if (lua_char != meta_char)
						{
							valid = false;
							break;
						}
					}
				}

				if (valid)
				{
					lua_pushboolean(L, false);
					return 1;
				}
			}
		}

		hash h;
		if (!get_archive(url, zip_dest, h))
			luaL_error(L, "Failed to download '%s'", url);

		char* checksum_str = bin_to_hex(h.hash, h.hash_size);
		hash_free(h);

		FILE* meta_file = fopen(meta_dest, "r+");
		if (meta_file)
		{
			char     buf[1024] {'\0'};
			uint32_t read = fread(buf, 1, 1024, meta_file);
			if (read == h.hash_size * 2 &&
			    strncmp(buf, checksum_str, h.hash_size * 2) == 0)
			{
				fclose(meta_file);
				tfree(checksum_str);
				tfree(meta_dest);
				tfree(zip_dest);
				tfree(dest);
				lua_pushboolean(L, false);
				return 1;
			}
			fclose(meta_file);
		}
		meta_file = fopen(meta_dest, "w+");
		if (meta_file)
		{
			fwrite(checksum_str, 1, h.hash_size * 2, meta_file);
			fclose(meta_file);
		}
		tfree(checksum_str);

		fs::list_dirs_res dirs = fs::list_dirs(dest);
		for (uint32_t i {0}; i < dirs.size; ++i)
		{
			if (str::find(dirs.dirs[i], ".dl-cache") == UINT32_MAX)
			{
				char* delete_dir = tmalloc<char>(strlen(dirs.dirs[i]) + 2);
				strcpy(delete_dir, dirs.dirs[i]);
				strcpy(delete_dir + strlen(dirs.dirs[i]), "/");

				fs::delete_dir(delete_dir);
				tfree(delete_dir);
			}
			tfree(dirs.dirs[i]);
		}
		tfree(dirs.dirs);

		fs::list_files_res files = fs::list_files(dest, nullptr);
		for (uint32_t i {0}; i < files.size; ++i)
		{
			fs::delete_file(files.files[i]);
			tfree(files.files[i]);
		}
		tfree(files.files);

		void* zip_handle = mz_zip_create();
		void* zip_stream = mz_stream_os_create();
		void* buf_stream = nullptr;

		if (mz_stream_open(zip_stream, zip_dest, MZ_OPEN_MODE_READ) != MZ_OK)
		{
			tfree(zip_dest);
			tfree(dest);
			mz_stream_delete(&zip_stream);
			luaL_error(L, "Failed to uncompress archive");
			return 0;
		}

		buf_stream = mz_stream_buffered_create();
		mz_stream_buffered_open(buf_stream, NULL, MZ_OPEN_MODE_READ);
		mz_stream_set_base(buf_stream, zip_stream);

		int32_t res = mz_zip_open(zip_handle, buf_stream, MZ_OPEN_MODE_READ);
		if (res != MZ_OK)
		{
			tfree(zip_dest);
			tfree(dest);
			mz_stream_buffered_close(buf_stream);
			mz_stream_buffered_delete(&buf_stream);
			luaL_error(L, "Failed to uncompress archive");
			return 0;
		}

		mz_zip_goto_first_entry(zip_handle);
		mz_zip_file* info;
		mz_zip_entry_get_info(zip_handle, &info);
		bool main_dir = false;

		char     main_dir_name[128] {'\0'};
		uint32_t main_dir_size = 0;
		if (mz_zip_attrib_is_dir(info->external_fa, info->version_madeby) == MZ_OK)
		{
			strcpy(main_dir_name, info->filename);
			main_dir_size = info->filename_size;
		}
		mz_zip_goto_next_entry(zip_handle);
		mz_zip_entry_get_info(zip_handle, &info);
		if (str::find(info->filename, main_dir_name) != UINT32_MAX)
			main_dir = true;
		mz_zip_goto_first_entry(zip_handle);

		char     buf[4096];
		int32_t  err = MZ_OK;
		int32_t  bytes_read = 0;
		uint32_t local_filename_len = strlen(dest) + !trailing_slash;
		char*    local_filename = tmalloc<char>(local_filename_len + 1);
		strcpy(local_filename, dest);
		if (!trailing_slash)
		{
			local_filename[local_filename_len - 1] = '/';
			local_filename[local_filename_len] = '\0';
			++dest_len;
		}

		do
		{
			mz_zip_entry_get_info(zip_handle, &info);
			uint32_t new_len = dest_len;
			if (main_dir)
				new_len += info->filename_size - main_dir_size;
			else
				new_len += info->filename_size;
			if (new_len > local_filename_len)
			{
				local_filename = trealloc(local_filename, new_len + 1);
				local_filename_len = new_len;
			}

			if (main_dir)
				strcpy(local_filename + dest_len, info->filename + main_dir_size);
			else
				strcpy(local_filename + dest_len, info->filename);
			if (mz_zip_attrib_is_dir(info->external_fa, info->version_madeby) == MZ_OK)
			{
				if (!fs::dir_exists(local_filename))
					fs::create_dir(local_filename);
			}
			else
			{
				mz_zip_entry_read_open(zip_handle, 0, nullptr);
				FILE* file = fopen(local_filename, "wb+");
				if (file)
				{
					do
					{
						bytes_read = mz_zip_entry_read(zip_handle, buf, sizeof(buf));
						if (bytes_read < 0)
							err = bytes_read;

						fwrite(buf, 1, bytes_read, file);
					}
					while (err == MZ_OK && bytes_read > 0);
					fclose(file);
				}
				mz_zip_entry_close(zip_handle);
			}
		}
		while (mz_zip_goto_next_entry(zip_handle) == MZ_OK);

		tfree(local_filename);
		mz_zip_close(zip_handle);
		mz_zip_delete(&zip_handle);
		mz_stream_buffered_close(buf_stream);
		mz_stream_buffered_delete(&buf_stream);
		tfree(zip_dest);
		tfree(dest);

		lua_pushboolean(L, true);

		return 1;
	}
} // namespace net