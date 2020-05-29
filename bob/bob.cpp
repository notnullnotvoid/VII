//this program walks various directories of the project to generate a .ninja file
//yes this is a pun on "bob the builder" don't @ me

//bob/build.sh (or bob/build.bat for windows) builds bob.
//you only need to build bob once before you can build the rest of the project

//TODO: walk folder structures recursively
//TODO: generate .app bundles for macOS
//TODO: test and make sure this works with symlinks
//TODO: optimize (profile + walk folders only once)
//TODO: compile optimized vs. unoptimized object files to different directories,
//      so we don't have to always do a full recompile of game code when switching between build targets?

#include "common.hpp"
#include "List.hpp"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <initializer_list>

static bool verbose;

template <typename VAL, typename KEY>
static inline VAL match_pair(std::initializer_list<Pair<VAL, KEY>> pairs, KEY key, VAL defaultVal) {
    for (auto it = pairs.begin(); it != pairs.end(); ++it) {
        if (it->second == key) {
            return it->first;
        }
    }
    return defaultVal;
}

template <typename TYPE>
static inline bool one_of(std::initializer_list<TYPE> list, TYPE key) {
    for (auto it = list.begin(); it != list.end(); ++it) {
        if (*it == key) {
            return true;
        }
    }
    return false;
}

////////////////////////////////////////////////////////////////////////////////
/// DIRECTORY WALKING                                                        ///
////////////////////////////////////////////////////////////////////////////////

enum FileType {
    FILE_NONE,
    FILE_CPPLIB,
    FILE_CLIB,
    FILE_SRC,
};

struct File {
    FileType type;
    char * dir;
    char * name;
    char * ext;
};

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static List<File> add_files(List<File> files, FileType type, const char * dir, const char * ext) {
    char * search = dsprintf(nullptr, "%s\\*", dir);

    WIN32_FIND_DATA findData = {};
    HANDLE handle = FindFirstFileA(search, &findData);
    if (handle == INVALID_HANDLE_VALUE) {
        int err = GetLastError();

        //TODO: fix this flow control
        if (err == ERROR_PATH_NOT_FOUND) {
            printf("WARNING: could not find directory %s with search string %s\n", dir, search);
            return files;
        } else if (err != ERROR_FILE_NOT_FOUND) {
            printf("ERROR: trying to find %s\n", dir);
            printf("ERROR: FindFirstFileA failed with code %d, aborting...\n", err);
            exit(1);
        }

        if (verbose) {
            printf("WARNING: could not find any .%s files in %s/, assuming empty\n", ext, dir);
            return files;
        }
    }

    do {
        if (!(findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            char * dot = strrchr(findData.cFileName, '.');
            if (dot && !strcmp(dot + 1, ext)) {
                files.add({ type, dup(dir), dup(findData.cFileName, dot), dup(ext) });
            }
        }
    } while (FindNextFile(handle, &findData));

    assert(GetLastError() == ERROR_NO_MORE_FILES);
    FindClose(handle);

    return files;
}

#else

#include <dirent.h>
#include <unistd.h>
#include <time.h>
#include <stdarg.h>
#include <sys/stat.h>

static List<File> add_files(List<File> files, FileType type, const char * dir, const char * ext) {
    //open directory
    DIR * dp = opendir(dir);
    if (!dp) {
        if (verbose) {
            printf("WARNING: Could not open directory %s, assuming empty\n", dir);
        }
        return files;
    }

    //scan for source files
    dirent * ep = readdir(dp);
    while (ep) {
        char * dot = strrchr(ep->d_name, '.');
        if (dot && !strcmp(dot + 1, ext)) {
            files.add({ type, dup(dir), dup(ep->d_name, dot), dup(ext) });
        }
        ep = readdir(dp);
    }

    closedir(dp);

    return files;
}

#endif

////////////////////////////////////////////////////////////////////////////////
/// MAIN                                                                     ///
////////////////////////////////////////////////////////////////////////////////

int main(int argc, char ** argv) {
    const char * root = ".";
    const char * output = "build.ninja";
    const char * config = "";

    //parse command line arguments
    enum ArgType { ARG_NONE, ARG_ROOT, ARG_OUTPUT, ARG_CONFIG };
    ArgType type = ARG_NONE;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "-v")) {
            verbose = true;
        } else if (!strcmp(argv[i], "-r")) {
            type = ARG_ROOT;
        } else if (!strcmp(argv[i], "-o")) {
            type = ARG_OUTPUT;
        } else if (!strcmp(argv[i], "-c")) {
            type = ARG_CONFIG;
        } else if (type == ARG_ROOT) {
            root = argv[i];
            type = ARG_NONE;
        } else if (type == ARG_OUTPUT) {
            output = argv[i];
            type = ARG_NONE;
        } else if (type == ARG_CONFIG) {
            config = argv[i];
            type = ARG_NONE;
        }
    }

    //match config name strings
    enum Config { CONFIG_DEBUG, CONFIG_DEV, CONFIG_PERF, CONFIG_RELEASE };
    const char * configNames[4] = { "debug", "dev", "perf", "release" };
    Config conf = CONFIG_DEV; //default if no such argument is provided
    for (int i = 0; i < ARR_SIZE(configNames); ++i) {
        if (!strcmp(config, configNames[i])) { //TODO: make case-insensitive?
            conf = (Config) i;
            break;
        }
    }

    const char * configEnumNames[] = { "CONFIG_DEBUG", "CONFIG_DEV", "CONFIG_PERF", "CONFIG_RELEASE" };
    printf("selected config: %s\n", configEnumNames[conf]);

    //HACK: for now just limit to perf builds on windows because I haven't figured out how LTO is supposed to work
    //      and that's the only difference between perf builds and release builds at the moment
    #ifdef _WIN32
        if (conf == CONFIG_RELEASE) conf = CONFIG_PERF;
    #endif

    //change directory
    #ifdef _WIN32
        assert(SetCurrentDirectory(root));
    #else
        chdir(root);
    #endif

    //write header and rules
    FILE * out = fopen(output, "w");
    fprintf(out, "ninja_required_version = 1.3\nbuilddir = build\n\n");
    fprintf(out, "rule lib\n");
    fprintf(out, "    command = clang -MMD -MF $out.d -std=c++14 -c $in -o $out -Ilib -Isoloud -ISDL2 "
        "-D_CRT_SECURE_NO_WARNINGS -Os%s\n", conf == CONFIG_RELEASE? " -flto" : "");
    fprintf(out, "    description = Build $in\n");
    fprintf(out, "    depfile = $out.d\n");
    fprintf(out, "rule clib\n");
    fprintf(out, "    command = clang -MMD -MF $out.d -c $in -o $out -Ilib -Isoloud -ISDL2 -Os%s\n",
        conf == CONFIG_RELEASE? " -flto" : "");
    fprintf(out, "    description = Build $in\n");
    fprintf(out, "    depfile = $out.d\n");
    fprintf(out, "rule src\n");
    fprintf(out, "    command = clang -MMD -MF $out.d -std=c++17 -c $in -o $out -Ilib -Isoloud -ISDL2 -Iimgui "
        "-D_CRT_SECURE_NO_WARNINGS -Wall -Wno-absolute-value %s%s%s\n",
        conf == CONFIG_DEBUG || conf == CONFIG_DEV? "-O0 -g -gcodeview -gfull " : "-Os",
        conf == CONFIG_DEBUG? " -fsanitize=address " : "", conf == CONFIG_RELEASE? " -flto" : "");
    fprintf(out, "    description = Build $in\n");
    fprintf(out, "    depfile = $out.d\n");
    fprintf(out, "rule link\n");
    #ifdef _WIN32
        fprintf(out, "    command = clang $in -o $out%s%s%s\n",
            conf == CONFIG_DEBUG || conf == CONFIG_DEV? " -g -gcodeview -gfull " : "",
            conf == CONFIG_DEBUG? " -fsanitize=address" : "", conf == CONFIG_RELEASE? " -flto" : "");
        //TODO: use lld-link to get better link times and LTO?
    #else
        fprintf(out, "    command = clang $in -o $out -lstdc++ link/libSDL2.a -framework CoreAudio "
            "-framework AudioToolbox -framework CoreVideo -framework ForceFeedback -framework IOKit -framework Carbon "
            "-framework Metal -framework AppKit -liconv%s%s\n", conf == CONFIG_DEBUG? " -fsanitize=address" : "",
            conf == CONFIG_RELEASE? " -flto" : "");
    #endif
    fprintf(out, "    description = Link $out\n");
    fprintf(out, "\n");

    //gather and classify files
    List<File> files = {};
    files = add_files(files, FILE_CPPLIB, "soloud/src", "cpp");
    files = add_files(files, FILE_CLIB, "soloud/src", "c");
    files = add_files(files, FILE_CPPLIB, "lib", "cpp");
    files = add_files(files, FILE_CLIB, "lib", "c");
    files = add_files(files, FILE_SRC, "src", "cpp");
    files = add_files(files, FILE_CPPLIB, "imgui", "cpp");

    //write compile commands
    for (File f : files) {
        auto rule = match_pair<const char *>({
            { "lib", FILE_CPPLIB },
            { "clib", FILE_CLIB },
            { "src", FILE_SRC },
        }, f.type, nullptr);

        if (rule) {
            fprintf(out, "build $builddir/%s.o: %s %s/%s.%s\n",
                f.name, rule, f.dir, f.name, f.ext);
        }
    }

    //write link command
    #ifdef _WIN32
        fprintf(out, "\nbuild game.exe: link");
    #else
        fprintf(out, "\nbuild game: link");
    #endif
    for (File f : files) {
        if (one_of({ FILE_CPPLIB, FILE_CLIB, FILE_SRC }, f.type)) {
            fprintf(out, " $builddir/%s.o", f.name);
        }
    }
    fprintf(out, "\n");

    fclose(out);
    return 0;
}
