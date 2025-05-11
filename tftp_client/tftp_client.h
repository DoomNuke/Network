#ifndef TFTP_CLIENT_H
#define TFTP_CLIENT_H

#define TFTP_CLIENT_DIR './tftp_client'

#include "tftp_client_handlers.h"

#define MAX_PORTS 10

//for stabling multi-threading 
int ports[MAX_PORTS] = {6970, 6971, 6972, 6973, 6974, 6975, 6976, 6977, 6978,6979};


#endif