#ifndef TFTP_SERVER_H
#define TFTP_SERVER_H

#include <stdint.h>
#include "tftp_server_handlers.h"
#include "../utils/tftp_logger.h"



#define MAX_RETRIES 5
#define TFTP_ROOT_DIR "./tftp_root"

void sigint_server(int sig);
void setup_signal_handler(void);
void start_tftp_server();

#endif