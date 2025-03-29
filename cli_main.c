#include <stdio.h>

/* Forward-declare the symlinks_main() we renamed in symlinks.c */
extern int symlinks_main(int argc, char** argv);

/* Minimal wrapper for the CLI tool */
int main(int argc, char** argv) {
    return symlinks_main(argc, argv);
}
