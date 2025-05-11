#ifndef TFTP_SERVER_H
#define TFTP_SERVER_H

#include <stdint.h>
#include "tftp_server_handlers.h"
#include "../common/tftp_common.h"
#include "../utils/tftp_utils.h"
#include "../utils/tftp_logger.h"

#define MAX_RETRIES 5
#define TFTP_ROOT_DIR "./tftp_root"


void start_tftp_server();

#endif