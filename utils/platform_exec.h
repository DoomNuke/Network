#ifndef PLATFORM_EXEC_H
#define PLATFORM_EXEC_H

#ifdef _WIN32
#include <process.h>
#define PATH_SEP "\\"
#else
#include <unistd.h>
#define PATH_SEP "/"
#endif

void handle_user_action(const char *filename, const char *mode);

#endif