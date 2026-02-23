#include <asm-generic/errno-base.h>
#include <virtuoso/pthread_types.h>
#include <virtuoso/pthread_utils.h>
#include <stdio.h>
#include <string.h>

#include <dirent.h>
#include <fnmatch.h>

/* TODO: this file is placed under nptl/ only for temporary convenience due to Makefile. It should be placed back to nptl/virtuoso */

int __pthread_probe_accelerators(void) {
    DIR *dir = opendir("/dev/");
    if (!dir) {
        perror("Failed to open directory");
        return ENOTDIR;
    }

    struct dirent **list;
    int n = scandir("/dev/", &list, NULL, alphasort);
    if (n < 0) {
        perror("Failed to scan directory");
        closedir(dir);
        return ENOENT;
    }

    for (int i = 0; i < n; i++) {
        if (fnmatch("*_stratus.*", list[i]->d_name, FNM_NOESCAPE) != 0) {
            continue;
        }
        printf("Found accelerator device: %s\n", list[i]->d_name);
        free(list[i]);
    }
    free(list);
    closedir(dir);

    return 0;
}
// /applications/test/04_fcnn_mt_pthread/opt.exe 1000 2 models/model_64_2.txt
