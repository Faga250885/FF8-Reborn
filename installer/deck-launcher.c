/* The installer ZIP is appended to this executable. Python can open both formats. */
#include <limits.h>
#include <stdio.h>
#include <unistd.h>

int main(void)
{
    char path[PATH_MAX + 1];
    ssize_t length = readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (length < 0 || (size_t)length >= sizeof(path) - 1) {
        fputs("Could not locate the FF8 Reborn installer.\n", stderr);
        return 1;
    }
    path[length] = '\0';
    execl("/usr/bin/python3", "python3", path, "--gui", (char *)0);
    perror("Could not start Python 3");
    return 1;
}
