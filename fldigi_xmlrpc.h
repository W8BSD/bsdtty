#ifndef FLDIGI_XMLRPC_H
#define FLDIGI_XMLRPC_H

#include <assert.h>
#include <pthread.h>

void setup_xmlrpc(pthread_t *tid);
void fldigi_add_rx(char ch);

#endif
