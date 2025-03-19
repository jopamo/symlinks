#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <libgen.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#ifndef S_ISLNK
#define S_ISLNK(mode) (((mode) & S_IFMT) == S_IFLNK)
#endif

#ifndef PATH_MAX
#define PATH_MAX 1024
#endif

#define PROGRAM_VERSION "1.4.3"

/* Global flags set by command-line options */
static int g_verbose = 0;   /* -v */
static int g_fix_links = 0; /* -c */
static int g_recurse = 0;   /* -r */
static int g_delete = 0;    /* -d */
static int g_shorten = 0;   /* -s */
static int g_testing = 0;   /* -t */
static int g_single_fs = 1; /* -o (off by default => single-fs=1) */
static int g_debug = 0;     /* -x (new debug switch) */

/*
 * replace_substring:
 *   Replace occurrences of 'old_sub' in 's' with 'new_sub' in-place, up to bufsize.
 *   Returns the number of replacements performed, or -1 if there is not enough space.
 */
static int replace_substring(char* s, size_t bufsize, const char* old_sub, const char* new_sub) {
    if (!s || !old_sub || !*old_sub) {
        return 0;
    }

    size_t old_len = strlen(old_sub);
    size_t new_len = (new_sub ? strlen(new_sub) : 0);
    if (old_len == 0) {
        /* Prevent infinite loop if old_sub is an empty string */
        return 0;
    }

    int total_replacements = 0;

    while (1) {
        char* match_pos = strstr(s, old_sub);
        if (!match_pos) {
            break; /* no more matches */
        }

        /* Check buffer space if the replacement is larger. */
        if (new_len > old_len) {
            size_t needed = strlen(s) + (new_len - old_len) + 1;
            if (needed > bufsize) {
                return -1; /* not enough space */
            }
        }

        /* Temporary buffer for building the replaced string. */
        char temp[PATH_MAX * 2];
        memset(temp, 0, sizeof(temp));

        size_t prefix_len = (size_t)(match_pos - s);

        /* Copy up to the match */
        strncpy(temp, s, prefix_len);

        /* Append new_sub */
        if (new_sub) {
            strncat(temp, new_sub, sizeof(temp) - strlen(temp) - 1);
        }

        /* Append whatever comes after old_sub */
        strncat(temp, match_pos + old_len, sizeof(temp) - strlen(temp) - 1);

        /* Copy back to s */
        strncpy(s, temp, bufsize - 1);
        s[bufsize - 1] = '\0';

        total_replacements++;
    }

    return total_replacements;
}

/*
 * tidy_path:
 *   Removes redundant slashes, "./" references, and collapses "/../" if possible.
 *   Modifies path in-place. Returns non-zero if modifications were made.
 */
static int tidy_path(char* path) {
    if (!path || !*path) {
        return 0;
    }

    int changed = 0;
    char working[PATH_MAX * 2];
    memset(working, 0, sizeof(working));

    /* 1) Ensure a trailing slash to simplify some patterns. */
    size_t len = strlen(path);
    if (len + 2 < sizeof(working) && path[len - 1] != '/') {
        snprintf(working, sizeof(working), "%s/", path);
    }
    else {
        strncpy(working, path, sizeof(working) - 1);
    }

    int r;
    /* Remove "/./" occurrences */
    while ((r = replace_substring(working, sizeof(working), "/./", "/")) > 0) {
        changed = 1;
    }
    /* Remove consecutive slashes "//" */
    while ((r = replace_substring(working, sizeof(working), "//", "/")) > 0) {
        changed = 1;
    }

    /*
     * Collapse "/../" - naive approach: for each occurrence of "/../",
     * remove one preceding directory component if possible.
     */
    for (;;) {
        char* p = strstr(working, "/../");
        if (!p) {
            break;
        }
        if (p == working) {
            /* "/../" at the start => remove it */
            replace_substring(working, sizeof(working), "/../", "/");
            changed = 1;
            continue;
        }
        /* Find slash before "/../" */
        char* slash = p - 1;
        while (slash > working && *slash != '/') {
            slash--;
        }
        if (slash == working && *slash == '/') {
            /* At root => remove the "/.." part */
            replace_substring(working, sizeof(working), "/../", "/");
            changed = 1;
        }
        else {
            memmove(slash, p + 3, strlen(p + 3) + 1);
            changed = 1;
        }
    }

    /* Remove trailing slash if not root "/" */
    len = strlen(working);
    while (len > 1 && working[len - 1] == '/') {
        working[len - 1] = '\0';
        --len;
        changed = 1;
    }

    /* Remove leading "./" if any */
    while (!strncmp(working, "./", 2)) {
        memmove(working, working + 2, strlen(working + 2) + 1);
        changed = 1;
    }

    strncpy(path, working, PATH_MAX - 1);
    path[PATH_MAX - 1] = '\0';
    return changed;
}

/*
 * shorten_path:
 *   Attempts to remove unnecessary "../dir" segments (a naive approach).
 *   Returns non-zero if changes were made.
 */
static int shorten_path(char* link_path, const char* base_path) {
    if (!link_path || !*link_path || !base_path || !*base_path) {
        return 0;
    }

    int shortened = 0;

    for (;;) {
        char* p = strstr(link_path, "../");
        if (!p) {
            break;
        }
        /* If base_path is "/", can't go higher. */
        if (!strcmp(base_path, "/")) {
            break;
        }

        char* slash_after_dir = strchr(p + 3, '/');
        if (!slash_after_dir) {
            break;
        }
        /* Remove the entire "../xxx/" portion from link_path */
        memmove(p, slash_after_dir + 1, strlen(slash_after_dir + 1) + 1);
        shortened = 1;
    }

    return shortened;
}

/*
 * build_relative_path:
 *   Builds a relative path from 'from_dir' to 'to_path' using realpath().
 */
static int build_relative_path(const char* from_dir, const char* to_path, char* out, size_t out_size) {
    if (!from_dir || !to_path || !out) {
        return -1;
    }

    char resolved_from[PATH_MAX];
    char resolved_to[PATH_MAX];

    if (!realpath(from_dir, resolved_from)) {
        return -1;
    }
    if (!realpath(to_path, resolved_to)) {
        return -1;
    }

    /* Tokenize each resolved path */
    char from_copy[PATH_MAX], to_copy[PATH_MAX];
    strncpy(from_copy, resolved_from, sizeof(from_copy) - 1);
    from_copy[sizeof(from_copy) - 1] = '\0';
    strncpy(to_copy, resolved_to, sizeof(to_copy) - 1);
    to_copy[sizeof(to_copy) - 1] = '\0';

    char *from_tokens[PATH_MAX], *to_tokens[PATH_MAX];
    int from_count = 0, to_count = 0;

    {
        char* p = strtok(from_copy, "/");
        while (p && from_count < (int)(sizeof(from_tokens) / sizeof(from_tokens[0]))) {
            from_tokens[from_count++] = p;
            p = strtok(NULL, "/");
        }
    }
    {
        char* q = strtok(to_copy, "/");
        while (q && to_count < (int)(sizeof(to_tokens) / sizeof(to_tokens[0]))) {
            to_tokens[to_count++] = q;
            q = strtok(NULL, "/");
        }
    }

    /* Find common prefix */
    int i = 0;
    while (i < from_count && i < to_count) {
        if (strcmp(from_tokens[i], to_tokens[i]) != 0) {
            break;
        }
        i++;
    }

    /* Build a relative path */
    out[0] = '\0';
    int needed_len = 0;

    /* Add ../ for each remaining component in 'from' */
    for (int j = i; j < from_count; j++) {
        if (needed_len + 4 >= (int)out_size) {
            return -1;
        }
        strcat(out, "../");
        needed_len += 3;
    }

    /* Add forward path for remainder of 'to' */
    for (int j = i; j < to_count; j++) {
        size_t seg_len = strlen(to_tokens[j]);
        /* +1 for '/', +1 for final '\0' */
        if (needed_len + seg_len + 2 >= out_size) {
            return -1;
        }
        strcat(out, to_tokens[j]);
        needed_len += seg_len;
        if (j < to_count - 1) {
            strcat(out, "/");
            needed_len += 1;
        }
    }

    /* If nothing was added => same directory */
    if (out[0] == '\0') {
        if (out_size > 1) {
            strcpy(out, ".");
        }
        else {
            return -1;
        }
    }

    return 0;
}

/*
 * fix_symlink:
 *   Processes a symlink at 'symlink_path'.
 */
static void fix_symlink(const char* symlink_path, dev_t base_dev) {
    char link_value[PATH_MAX + 1];
    memset(link_value, 0, sizeof(link_value));

    ssize_t n = readlink(symlink_path, link_value, PATH_MAX);
    if (n < 0) {
        fprintf(stderr, "readlink error on %s: %s\n", symlink_path, strerror(errno));
        return;
    }
    link_value[n] = '\0';

    if (g_debug) {
        fprintf(stderr, "[DEBUG] Symlink: %s -> %s\n", symlink_path, link_value);
    }

    /* Build absolute version to check if it's dangling or cross-FS. */
    char abs_resolved[PATH_MAX * 2];
    memset(abs_resolved, 0, sizeof(abs_resolved));

    if (link_value[0] == '/') {
        strncpy(abs_resolved, link_value, sizeof(abs_resolved) - 1);
    }
    else {
        strncpy(abs_resolved, symlink_path, sizeof(abs_resolved) - 1);
        char* last_slash = strrchr(abs_resolved, '/');
        if (last_slash) {
            *(last_slash + 1) = '\0';
        }
        else {
            strcpy(abs_resolved, "./");
        }
        strncat(abs_resolved, link_value, sizeof(abs_resolved) - strlen(abs_resolved) - 1);
    }

    tidy_path(abs_resolved);

    if (g_debug) {
        fprintf(stderr, "[DEBUG] Resolved path for stat(): %s\n", abs_resolved);
    }

    struct stat stbuf;
    if (stat(abs_resolved, &stbuf) == -1) {
        /* Dangling link. */
        if (g_verbose) {
            printf("dangling: %s -> %s\n", symlink_path, link_value);
        }
        if (g_debug) {
            fprintf(stderr, "[DEBUG] stat failed; link is dangling.\n");
        }
        if (g_delete) {
            if (unlink(symlink_path) == 0) {
                printf("deleted:  %s -> %s\n", symlink_path, link_value);
            }
            else {
                perror("unlink");
            }
        }
        return;
    }

    /* Check filesystem boundaries if -o is NOT set => g_single_fs=1 */
    if (g_single_fs && stbuf.st_dev != base_dev) {
        if (g_verbose) {
            printf("other_fs: %s -> %s\n", symlink_path, link_value);
        }
        if (g_debug) {
            fprintf(stderr, "[DEBUG] Different filesystem, skipping unless -o used.\n");
        }
        return;
    }

    char new_link[PATH_MAX + 1];
    snprintf(new_link, sizeof(new_link), "%s", link_value);

    int is_abs = (link_value[0] == '/');
    int changed_messy = tidy_path(new_link);
    int changed_short = 0;
    if (g_shorten) {
        changed_short = shorten_path(new_link, symlink_path);
    }

    if (g_debug) {
        fprintf(stderr, "[DEBUG] new_link after tidy/shorten: %s\n", new_link);
    }

    if (strcmp(new_link, link_value) == 0) {
        changed_messy = 0;
        changed_short = 0;
    }

    if (g_verbose) {
        if (is_abs && !g_fix_links) {
            printf("absolute: %s -> %s\n", symlink_path, link_value);
        }
        else if (!is_abs) {
            if (changed_messy || changed_short) {
                printf("relative (messy/shortened): %s -> %s\n", symlink_path, link_value);
            }
            else {
                printf("relative: %s -> %s\n", symlink_path, link_value);
            }
        }
    }

    /* If not converting links and not in test mode, do nothing unless they changed. */
    if ((!g_fix_links && !g_testing) && !(changed_messy || changed_short)) {
        if (g_debug) {
            fprintf(stderr, "[DEBUG] No conversion needed, returning.\n");
        }
        return;
    }

    /* Convert absolute link to relative if -c is set. */
    if (g_fix_links && is_abs) {
        char symlink_dir[PATH_MAX + 1];
        strncpy(symlink_dir, symlink_path, sizeof(symlink_dir) - 1);
        symlink_dir[sizeof(symlink_dir) - 1] = '\0';

        char* slash = strrchr(symlink_dir, '/');
        if (slash) {
            *(slash + 1) = '\0';
        }
        else {
            strcpy(symlink_dir, "./");
        }

        if (g_debug) {
            fprintf(stderr, "[DEBUG] symlink_dir = %s\n", symlink_dir);
            fprintf(stderr, "[DEBUG] abs_resolved = %s\n", abs_resolved);
        }

        if (build_relative_path(symlink_dir, abs_resolved, new_link, sizeof(new_link)) < 0) {
            /* Fallback */
            strncpy(new_link, link_value, sizeof(new_link) - 1);
            new_link[sizeof(new_link) - 1] = '\0';
            if (g_debug) {
                fprintf(stderr, "[DEBUG] build_relative_path failed; fallback to link_value\n");
            }
        }
        else {
            if (g_shorten) {
                shorten_path(new_link, symlink_path);
            }
            if (g_debug) {
                fprintf(stderr, "[DEBUG] new_link after build_relative_path: %s\n", new_link);
            }
        }
    }

    if (g_testing) {
        printf("(test) would change: %s -> %s\n", symlink_path, new_link);
        if (g_debug) {
            fprintf(stderr, "[DEBUG] In test mode; not changing filesystem.\n");
        }
        return;
    }

    if (strcmp(new_link, link_value) == 0) {
        if (g_debug) {
            fprintf(stderr, "[DEBUG] final link is identical to existing; skipping rewrite.\n");
        }
        return;
    }

    /* Perform the actual change */
    if (unlink(symlink_path) != 0) {
        fprintf(stderr, "Cannot unlink %s: %s\n", symlink_path, strerror(errno));
        return;
    }
    if (symlink(new_link, symlink_path) != 0) {
        fprintf(stderr, "Cannot symlink %s -> %s: %s\n", symlink_path, new_link, strerror(errno));
        return;
    }

    printf("changed:  %s -> %s\n", symlink_path, new_link);
}

/*
 * scan_directory:
 *   Recursively scans directory at 'path'.
 */
static void scan_directory(char* path, dev_t base_dev, int depth) {
    if (!path) {
        return;
    }
    if (depth > 128) {
        fprintf(stderr, "Recursion limit reached at %s; skipping.\n", path);
        return;
    }

    if (g_debug) {
        fprintf(stderr, "[DEBUG] scan_directory: %s (depth=%d)\n", path, depth);
    }

    DIR* dfd = opendir(path);
    if (!dfd) {
        fprintf(stderr, "opendir failed on %s: %s\n", path, strerror(errno));
        return;
    }

    char original_path[PATH_MAX + 1];
    strncpy(original_path, path, sizeof(original_path) - 1);
    original_path[sizeof(original_path) - 1] = '\0';

    /* Append slash if needed */
    size_t path_len = strlen(path);
    if (path_len + 2 < PATH_MAX && path[path_len - 1] != '/') {
        path[path_len++] = '/';
        path[path_len] = '\0';
    }

    struct dirent* dp;
    while ((dp = readdir(dfd)) != NULL) {
        const char* name = dp->d_name;
        if (!strcmp(name, ".") || !strcmp(name, "..")) {
            continue;
        }

        strncpy(path + path_len, name, PATH_MAX - path_len);
        path[path_len + PATH_MAX - path_len - 1] = '\0'; /* ensure termination */

        if (g_debug) {
            fprintf(stderr, "[DEBUG] Checking entry: %s\n", path);
        }

        struct stat st;
        if (lstat(path, &st) == -1) {
            fprintf(stderr, "lstat failed on %s: %s\n", path, strerror(errno));
            path[path_len] = '\0';
            continue;
        }

        if (S_ISLNK(st.st_mode)) {
            fix_symlink(path, base_dev);
        }
        else if (S_ISDIR(st.st_mode) && g_recurse) {
            if (!g_single_fs || (st.st_dev == base_dev)) {
                scan_directory(path, base_dev, depth + 1);
            }
        }

        /* Restore the original directory path */
        path[path_len] = '\0';
    }

    closedir(dfd);
    strncpy(path, original_path, PATH_MAX);
    path[PATH_MAX - 1] = '\0';
}

/*
 * print_usage:
 *   Print usage help to stderr.
 */
static void print_usage(const char* progname) {
    fprintf(stderr,
            "\n"
            "Usage: %s [OPTIONS] DIR...\n"
            "Scan and fix symbolic links in the specified directories.\n\n"
            "Version: %s\n"
            "\n"
            "Options:\n"
            "  -c  Convert absolute or messy links to relative.\n"
            "  -d  Delete dangling links (those pointing to nonexistent targets).\n"
            "  -o  Allow links across filesystems (otherwise just note 'other_fs').\n"
            "  -r  Recurse into subdirectories.\n"
            "  -s  Shorten links by removing unnecessary '../dir' sequences.\n"
            "  -t  Test mode: show what would be done with -c, but do not modify.\n"
            "  -v  Verbose: show all symlinks, including relative.\n"
            "  -x  Debug: display internal processing details.\n"
            "\n"
            "Examples:\n"
            "  %s -r /path/to/dir       Recursively scan directories for symlinks\n"
            "  %s -rc /path/to/dir      Convert absolute to relative while scanning\n"
            "  %s -rd /path/to/dir      Remove dangling links during a recursive scan\n"
            "\n",
            progname, PROGRAM_VERSION, progname, progname, progname);
}

int symlinks_main(int argc, char** argv) {
    const char* progname = argv[0];
    int opt;

    while ((opt = getopt(argc, argv, "cdorstvx")) != -1) {
        switch (opt) {
            case 'c':
                g_fix_links = 1;
                break;
            case 'd':
                g_delete = 1;
                break;
            case 'o':
                g_single_fs = 0;
                break; /* allow cross-FS */
            case 'r':
                g_recurse = 1;
                break;
            case 's':
                g_shorten = 1;
                break;
            case 't':
                g_testing = 1;
                break;
            case 'v':
                g_verbose = 1;
                break;
            case 'x':
                g_debug = 1;
                break;
            default:
                print_usage(progname);
                exit(EXIT_FAILURE);
        }
    }

    if (optind >= argc) {
        print_usage(progname);
        exit(EXIT_FAILURE);
    }

    int dircount = 0;
    while (optind < argc) {
        char path[PATH_MAX + 1];
        memset(path, 0, sizeof(path));

        const char* input = argv[optind++];
        if (input[0] == '/') {
            strncpy(path, input, sizeof(path) - 1);
        }
        else {
            char cwd[PATH_MAX];
            if (!getcwd(cwd, sizeof(cwd))) {
                fprintf(stderr, "getcwd() failed: %s\n", strerror(errno));
                exit(EXIT_FAILURE);
            }
            strncat(cwd, "/", sizeof(cwd) - strlen(cwd) - 1);
            strncat(cwd, input, sizeof(cwd) - strlen(cwd) - 1);
            strncpy(path, cwd, sizeof(path) - 1);
        }

        tidy_path(path);

        struct stat st;
        if (lstat(path, &st) == -1) {
            fprintf(stderr, "Cannot lstat %s: %s\n", path, strerror(errno));
            continue;
        }

        if (S_ISDIR(st.st_mode)) {
            scan_directory(path, st.st_dev, 0);
        }
        else if (S_ISLNK(st.st_mode)) {
            fix_symlink(path, st.st_dev);
        }
        else {
            fprintf(stderr, "%s is not a directory or symlink; skipping.\n", path);
        }
        dircount++;
    }

    if (dircount == 0) {
        print_usage(progname);
    }

    return 0;
}
