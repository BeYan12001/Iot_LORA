#ifndef RECEIVER_H
#define RECEIVER_H

#include "net/netdev.h"
#include "thread.h"

void app_event_cb(netdev_t *dev, netdev_event_t event);
kernel_pid_t app_start_recv_thread(void);

#endif /* RECEIVER_H */
