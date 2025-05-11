#ifndef TFTP_LOGGER_H
#define TFTP_LOGGER_H

#define LOG_FILE "server.log"

//logger setup - logger included in tftp_server_handlers.c
void logger(const char *level, const char *format, ...);

#endif