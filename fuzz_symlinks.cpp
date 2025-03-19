// fuzz_symlinks_full.c
//
// A libFuzzer harness that tests the entire "symlinks" program end-to-end.
// It randomizes command-line flags (-c, -d, -r, -s, etc.) and creates a
// temporary directory with random symlinks/files. Then calls main() with
// those arguments, letting the symlinks code scan/fix them.
//
// Compile this with -fsanitize=fuzzer (and possibly -fsanitize=address).

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// Include the symlinks header or just declare main():
// (If your symlinks code is in a separate object, link them together.)
int main(int argc, char** argv);

// A helper to create a random file or symlink inside the temp dir.
static void create_random_entry(const uint8_t* data, size_t size, const char* dirpath, int index) {
    // We'll interpret data in some minimal way:
    //  first byte decides "file" vs "symlink".
    //  subsequent bytes for the symlink target or filename contents.

    if (size < 2) {
        // Not enough data to do anything interesting.
        return;
    }

    int is_symlink = (data[0] & 1);  // 0 => file, 1 => symlink

    // We'll generate a name like "entry_<index>" in 'dirpath'
    char pathbuf[1024];
    snprintf(pathbuf, sizeof(pathbuf), "%s/entry_%d", dirpath, index);

    if (!is_symlink) {
        // Create a file
        int fd = open(pathbuf, O_WRONLY | O_CREAT | O_EXCL, 0644);
        if (fd >= 0) {
            // Optionally write some data
            write(fd, data + 1, size - 1);
            close(fd);
        }
    }
    else {
        // Create a symlink
        // We'll treat data+1 as a "target string" (ensure it's null-terminated).
        char target[256];
        size_t tlen = (size - 1 < sizeof(target) - 1) ? size - 1 : sizeof(target) - 1;
        memcpy(target, data + 1, tlen);
        target[tlen] = '\0';

        // Create the symlink
        symlink(target, pathbuf);
    }
}

// A helper to parse random bytes into symlinks command-line flags.
static void parse_flags_from_data(const uint8_t** pData, size_t* pSize, int* out_argc, char** out_argv) {
    // We'll interpret up to the first byte as flags to set.
    // e.g. bits: 1 => -c, 2 => -d, 4 => -r, 8 => -s, 16 => -t, 32 => -v, 64 => -x, ...
    // This is purely an example. Feel free to expand to all possible bits.

    if (*pSize < 1) {
        return;  // no flags
    }

    uint8_t flags = **pData;
    (*pData)++;
    (*pSize)--;

    // Now, for each bit, if set, we push an arg like "-c", "-d", ...
    // Start out_argc at 1 (argv[0] = "symlinks")
    int argc = 1;
    if (flags & 0x01) {
        out_argv[argc++] = "-c";
    }
    if (flags & 0x02) {
        out_argv[argc++] = "-d";
    }
    if (flags & 0x04) {
        out_argv[argc++] = "-r";
    }
    if (flags & 0x08) {
        out_argv[argc++] = "-s";
    }
    if (flags & 0x10) {
        out_argv[argc++] = "-t";
    }
    if (flags & 0x20) {
        out_argv[argc++] = "-v";
    }
    if (flags & 0x40) {
        out_argv[argc++] = "-x";
    }

    // We'll skip the -o flag or add more bits if you want to test cross-filesystem, etc.

    *out_argc = argc;
}

// The main fuzz entry point for libFuzzer
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* Data, size_t Size) {
    // 1) Create a temporary directory to store random files/symlinks.
    char tmpdir[] = "/tmp/symlinks_fuzzXXXXXX";
    char* dirpath = mkdtemp(tmpdir);
    if (!dirpath) {
        // If we fail to create a temp directory, just bail.
        return 0;
    }

    // 2) Extract some bytes to decide how many entries to create.
    //    Suppose the next byte means "num_entries"
    if (Size < 1) {
        rmdir(dirpath);
        return 0;
    }
    uint8_t num_entries = Data[0] % 10;  // up to 10 entries
    Data++;
    Size--;

    // 3) For each entry, we use some slice of the data to create a file or symlink.
    //    We do a simplistic approach: each entry gets 5 random bytes or so.
    //    Expand if you want more advanced directory nesting, etc.
    for (int i = 0; i < num_entries; i++) {
        if (Size < 5) {
            break;  // no more data
        }
        create_random_entry(Data, 5, dirpath, i);
        Data += 5;
        Size -= 5;
    }

    // 4) Parse some bits as symlinks flags
    const int kMaxArgs = 16;
    char* argv[kMaxArgs];
    argv[0] = (char*)"symlinks";  // fake argv[0] name

    int argc = 1;
    parse_flags_from_data(&Data, &Size, &argc, argv);

    // 5) Now pass the temporary directory as the final argument => symlinks scans it.
    argv[argc++] = dirpath;
    argv[argc] = NULL;

    // 6) Call the real symlinks "main()" with our synthetic arguments.
    //    We don't care about the return value, we just want to see if it crashes.
    (void)main(argc, argv);

    // 7) Clean up the temporary directory
    //    We'll attempt to remove all entries.  We can do it with a quick
    //    readdir+unlink approach. For brevity, we do the simplest method:
    DIR* d = opendir(dirpath);
    if (d) {
        struct dirent* de;
        while ((de = readdir(d)) != NULL) {
            if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) {
                continue;
            }
            char pathbuf[1024];
            snprintf(pathbuf, sizeof(pathbuf), "%s/%s", dirpath, de->d_name);
            unlink(pathbuf);  // if it's a file or symlink
            // If directories can be created, you'd want rmdir or recursion.
        }
        closedir(d);
    }
    rmdir(dirpath);

    return 0;
}
