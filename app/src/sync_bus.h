#ifndef SC_SYNC_BUS_H
#define SC_SYNC_BUS_H

#include "common.h"

#include <stdbool.h>
#include <stdint.h>

#include "control_msg.h"
#include "controller.h"
#include "coords.h"
#include "util/net.h"
#include "util/thread.h"

#define SC_SYNC_DEFAULT_PORT 27183

struct sc_sync {
    bool enabled; // SYNC button state
    bool applying_remote; // suppress rebroadcast while injecting remote msgs
    bool started;
    uint32_t source_id;
    uint16_t port;

    struct sc_controller *controller;
    struct sc_size frame_size; // protected by mutex

    sc_mutex mutex;

    // Hub (optional: first instance that binds)
    bool is_hub;
    sc_socket hub_listen;
    sc_thread hub_thread;
    bool hub_stopped;

    // Client connection to hub (always used when started)
    sc_socket client;
    sc_thread client_thread;
    bool client_stopped;
};

bool
sc_sync_init(struct sc_sync *sync, struct sc_controller *controller,
             uint16_t port);

bool
sc_sync_start(struct sc_sync *sync);

void
sc_sync_stop(struct sc_sync *sync);

void
sc_sync_destroy(struct sc_sync *sync);

void
sc_sync_set_enabled(struct sc_sync *sync, bool enabled);

bool
sc_sync_is_enabled(struct sc_sync *sync);

void
sc_sync_set_frame_size(struct sc_sync *sync, struct sc_size size);

// Broadcast local control message to other synced instances (no-op if disabled)
void
sc_sync_broadcast(struct sc_sync *sync, const struct sc_control_msg *msg);

#endif
