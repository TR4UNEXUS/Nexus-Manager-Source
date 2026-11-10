#include "switch.h"
#include <string.h>

// Parentheses suppress the function-like macro for the mock definition only.
Result (fsFsCreateDirectory)(FsFileSystem* fs, const char* path) {
    (void)fs;
    if (path[FS_MAX_PATH-1] != 0) return 1;
    return 0;
}

int main(int argc, char** argv) {
    FsFileSystem fs={0};
    char small[160]={0};
    const char padded[FS_MAX_PATH]="/switch";
    if (argc != 2) return 2;
    if (!strcmp(argv[1], "literal")) return fsFsCreateDirectory(&fs, "/switch");
    if (!strcmp(argv[1], "short")) return fsFsCreateDirectory(&fs, small);
    return fsFsCreateDirectory(&fs, padded);
}
