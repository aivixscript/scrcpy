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

#define SC_SYNC_DEFAULT_PORT 27583
#define SC_SYNC_MAX_PEERS 16
#define SC_SYNC_NAME_LEN 40

struct sc_sync_peer {
    uint32_t id;
    char name[SC_SYNC_NAME_LEN];
    bool selected;
};

struct sc_sync {
    bool applying_remote; // suppress rebroadcast while injecting remote msgs
    bool started;
    uint32_t source_id;
    uint16_t port;
    char display_name[SC_SYNC_NAME_LEN];

    struct sc_controller *controller;
    struct sc_size frame_size; // protected by mutex

    sc_mutex mutex;

    struct sc_sync_peer peers[SC_SYNC_MAX_PEERS];
    uint32_t peer_seen_ms[SC_SYNC_MAX_PEERS];
    size_t peer_count;
    uint32_t hello_last_ms;

    void (*on_log)(void *userdata, const char *line);
    void *log_userdata;

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
             uint16_t port, const char *display_name);

bool
sc_sync_start(struct sc_sync *sync);

void
sc_sync_stop(struct sc_sync *sync);

void
sc_sync_destroy(struct sc_sync *sync);

void
sc_sync_set_log_fn(struct sc_sync *sync,
                   void (*on_log)(void *userdata, const char *line),
                   void *userdata);

void
sc_sync_tick(struct sc_sync *sync);

size_t
sc_sync_copy_peers(struct sc_sync *sync, struct sc_sync_peer *out, size_t max);

bool
sc_sync_toggle_target(struct sc_sync *sync, uint32_t peer_id);

bool
sc_sync_has_targets(struct sc_sync *sync);

void
sc_sync_set_frame_size(struct sc_sync *sync, struct sc_size size);

// Send local control message to selected targets (no-op if none selected)
void
sc_sync_broadcast(struct sc_sync *sync, const struct sc_control_msg *msg);

#endif
