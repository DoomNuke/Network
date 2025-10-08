#ifndef PLATFORM_EXEC_H
#define PLATFORM_EXEC_H
/*
#ifdef _WIN32
#include <process.h>
#define PATH_SEP "\\"
#else

^ windows compatibility implementation will come in the next update
*/
#include <unistd.h>
#define PATH_SEP "/"

void handle_user_action(const char *filename, const char *mode, const char *dir);

#endif