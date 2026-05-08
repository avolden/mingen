## Functions
This documents all the Lua functions exposed, in addition to the standard ones.

Unless stated otherwise, all relative paths are resolved from the executing script when processed, meaning path `lib/lib.cc` processed in script present in `src` will resolve the path to `src/lib/lib.cc`

#### `mg.project()`
Create a project.

**Parameters**: [input table](#input-table) consisting of multiple entries to configure it.

**Returns**: output table containing sources filtered, as well as compile and link options constructed.

##### Input table

| Key | Type | Description |
|-----|------|-------------|
|`name`|`string`|(Required) Name of the project. It will define name of the output artifacts built (if there are some).|
|`type`|`mg.project_type`|(Required) Type of the project. See [below](#project-types) for available types.|
|`sources`|`string[]`|Sources given to the compilation. Files as well as simple wildcards are supported. Current wildcards supported are `**` to match recursively files from a given directory path with a given suffix (e.g. `src/**.cpp`) and `*` to match all files present in a given directory with a given suffix (e.g. `src/*.cpp`.)|
|`includes`|`string[]`|Include paths given to the compilation. Translate roughly to `-I` compile option, with path resolved from the running script if relative.|
|`compile_options`|`string[]`|Compilation options to give to the compiler when compiling the sources.|
|`link_options`|`string[]`|Link options to give to the linker if a link is needed (executable, shared library).|
|`dependencies`|`project[]`|Needed projects to build before building the current project. Resulting artifacts of dependencies are automatically added to link of the current project.|
|`static_libraries`|`string[]`|(Prebuilt project only) Static libraries to link onto. Equivalent to `-l` link option.|
|`static_libraries_directories`|`string[]`|(Prebuilt project only) Static libraries directories to reference for static libraries resolve. Equivalent to `-L` link option.|
|*`configuration`*|`table`|Indicates a scope to declare additional settings, used only when generating for *configuration*. Everything keys above can be referenced, except for `name` and `type`. The settings defined in this scope is appended to the settings defined globally.|

##### Project types

| Name | Description |
|------|-------------|
|`sources`|Project containing only sources files. The files are no precompiled into a library, but compiled and linked directly with the project adding it as a dependency. This is useful for external projects where debugging into it is frequent, or with configuration files only available as header.
|`static_library`|Project resulting in static library (`.a`,`.lib`).|
|`shared_library`|Project resulting in shared library (`.so`,`.dll`).|
|`executable`|Project resulting in executable program.|
|`prebuilt`|Project containing already built libraries, either by other build systems (CMake, premake, etc...) or available only as built (Vulkan, etc...). Currently supports only static libraries, or dynamic libraries where the shared object can be found automatically by the system.|


#### `mg.collect_files()`
Collect files from the given path. Wildcards `*` and `**` are supported.

**Parameters**: string containing a wildcard path.

**Returns**: Array of strings with the filtered files.


#### `mg.resolve_path()`
Resolve path currently relative to script, to path relative to working directory.

**Parameters**: string containing the path to resolve.

**Returns**: string containing the path resolved.

*Note*: The resolve should return an absolute path, to prevent any misuses.


#### `mg.add_pre_build_cmd()`, `mg.add_post_build_cmd()`
Adds a build command to execute either before of after the compilation of a given project.

**Parameters**:
- Project to add the command to.
- Table containing the command to execute


##### Command table

| Key | Type | Description |
|-----|------|-------------|
|`input`|`string\|string[]`|Inputs needed for the command. This helps the build process to not execute the command if the inputs didn't change compared to the last build.|
|`output`|`string\|string[]`|(Required) Output files of the command.|
|`cmd`|`string`|(Required) Command to execute. Use `${in}` variable to reference inputs. Use `${out}` variable to reference outputs.|

#### `mg.add_pre_build_copy()`, `mg.add_post_build_copy()`
Adds a copy operation to execute either before of after the compilation of a given project.

**Parameters**:
- Project to add the command to.
- Table containing the file to be copied to execute

##### Command table

| Key | Type | Description |
|-----|------|-------------|
|`input`|`string`|(Required) Input file to copy.|
|`output`|`string`|(Required) Path and name to copy the file to.|


#### `mg.generate()`
Generate the given projects as ninja build files. The generation write the file in `build/build.ninja`.

**Parameters**: Array of project to generate.


#### `mg.configurations()`
References all the configurations supported. Configuration only reference a name, and it is up to the user to give a meaning to these.

**Parameters**: String array containing all the configurations possible

#### `mg.platform()`
Retrieve the platform name of the running instance

**Returns**: string, currently either "windows" or "linux"


#### `mg.need_generate()`
Checks if the currently running file need generation. This is helpful when importing other mingen scripts that could be used as a standalone generation. This is currently a patch, and may be renamed or deleted with progress on development.

**Returns**: bool, indicating if generation is needed.


#### `mg.get_build_dir()`
Retrieves the build directory path relative to currently running script. This is used mainly for external dependencies.

**Returns**: string containing the path of the build directory, resolved to the currently running script.


#### `net.download()`
Download an compressed archive from a given URL, and extract it. This is used to retrieve external dependencies without putting them directly in the repository.

**Parameters**:
- String containing the url to download the archive to.
- String containing the destination directory.
- Optional string containing the MD5 hash of the archive to download. This allows to prevent a download to compare the exact same archives.

**Returns**: bool indicating if the download and extracts has happened. Used to rebuild libraries if needed for example.


#### `os.execute()`
Replacement to the builtin `os.execute()` Lua function. It has an overload accepting a working directory to run the process to.

**Parameters**:
- (Optional) String containing the working directory.
- String containing the command to execute.

**Returns**: the exit code of the command to.

*Note*: The internal implementation of the function differs from the original, as it use direct process creation instead of command interpretation using `system()` C call. This allow for more in-depth customisation of the process execution, but brings some flaws, like the impossibility to use shell variables. This may be fixed later.


#### `os.copy_file()`
Copies a file to a given destination.

**Parameters**:
- String containing the source file path to copy
- String containing the destination file path to copy to.

**Returns**: bool indicating if the copy succeeded or not.

#### `os.create_directory()`
Creates a directory.

**Parameters**:
- String containing the directory path to create.
- (Optional) bool to create the directory recursively or not. Defaults to `true`.

**Returns**: bool indicating if the creation succeeded or not.