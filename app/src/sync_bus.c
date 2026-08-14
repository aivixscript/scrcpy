#include "sync_bus.h"

#include <assert.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL_timer.h>

#ifdef _WIN32
# include <winsock2.h>
#else
# include <sys/select.h>
#endif

#include "util/binary.h"
#include "util/log.h"

#define SC_SYNC_MAGIC 0x53435359u /* 'SCSY' */
#define SC_SYNC_VERSION 1
#define SC_SYNC_MAX_CLIENTS 16
#define SC_SYNC_MAX_PACKET 256

enum sc_sync_msg_type {
    SC_SYNC_MSG_TOUCH = 1,
    SC_SYNC_MSG_SCROLL = 2,
    SC_SYNC_MSG_KEY = 3,
    SC_SYNC_MSG_BACK = 4,
};

struct sc_sync_clients {
    sc_socket sockets[SC_SYNC_MAX_CLIENTS];
    size_t count;
};

static sc_raw_socket
sc_sync_raw(sc_socket socket) {
#ifdef SC_SOCKET_CLOSE_ON_INTERRUPT
    assert(socket);
    return socket->socket;
#else
    return socket;
#endif
}

static uint16_t
norm_u16(int32_t v, uint16_t max) {
    if (max <= 1) {
        return 0;
    }
    if (v < 0) {
        v = 0;
    }
    if (v >= max) {
        v = max - 1;
    }
    return (uint16_t) ((uint32_t) v * 65535u / (uint32_t) (max - 1));
}

static int32_t
denorm_u16(uint16_t n, uint16_t max) {
    if (max <= 1) {
        return 0;
    }
    return (int32_t) ((uint32_t) n * (uint32_t) (max - 1) / 65535u);
}

static bool
sc_sync_msg_is_supported(enum sc_control_msg_type type) {
    switch (type) {
        case SC_CONTROL_MSG_TYPE_INJECT_TOUCH_EVENT:
        case SC_CONTROL_MSG_TYPE_INJECT_SCROLL_EVENT:
        case SC_CONTROL_MSG_TYPE_INJECT_KEYCODE:
        case SC_CONTROL_MSG_TYPE_BACK_OR_SCREEN_ON:
            return true;
        default:
            return false;
    }
}

static size_t
sc_sync_serialize(const struct sc_control_msg *msg, uint32_t source_id,
                  uint8_t *buf, size_t bufsize) {
    if (bufsize < SC_SYNC_MAX_PACKET) {
        return 0;
    }

    size_t o = 0;
    sc_write32be(&buf[o], SC_SYNC_MAGIC);
    o += 4;
    buf[o++] = SC_SYNC_VERSION;
    size_t type_off = o++;
    sc_write32be(&buf[o], source_id);
    o += 4;

    switch (msg->type) {
        case SC_CONTROL_MSG_TYPE_INJECT_TOUCH_EVENT: {
            buf[type_off] = SC_SYNC_MSG_TOUCH;
            const struct sc_position *pos = &msg->inject_touch_event.position;
            buf[o++] = (uint8_t) msg->inject_touch_event.action;
            sc_write32be(&buf[o], msg->inject_touch_event.action_button);
            o += 4;
            sc_write32be(&buf[o], msg->inject_touch_event.buttons);
            o += 4;
            sc_write64be(&buf[o], msg->inject_touch_event.pointer_id);
            o += 8;
            sc_write16be(&buf[o],
                         norm_u16(pos->point.x, pos->screen_size.width));
            o += 2;
            sc_write16be(&buf[o],
                         norm_u16(pos->point.y, pos->screen_size.height));
            o += 2;
            uint16_t pressure =
                (uint16_t) (msg->inject_touch_event.pressure * 65535.f);
            sc_write16be(&buf[o], pressure);
            o += 2;
            break;
        }
        case SC_CONTROL_MSG_TYPE_INJECT_SCROLL_EVENT: {
            buf[type_off] = SC_SYNC_MSG_SCROLL;
            const struct sc_position *pos = &msg->inject_scroll_event.position;
            sc_write16be(&buf[o],
                         norm_u16(pos->point.x, pos->screen_size.width));
            o += 2;
            sc_write16be(&buf[o],
                         norm_u16(pos->point.y, pos->screen_size.height));
            o += 2;
            sc_write16be(&buf[o], (uint16_t) (int16_t)
                                     (msg->inject_scroll_event.hscroll * 1000));
            o += 2;
            sc_write16be(&buf[o], (uint16_t) (int16_t)
                                     (msg->inject_scroll_event.vscroll * 1000));
            o += 2;
            sc_write32be(&buf[o], msg->inject_scroll_event.buttons);
            o += 4;
            break;
        }
        case SC_CONTROL_MSG_TYPE_INJECT_KEYCODE: {
            buf[type_off] = SC_SYNC_MSG_KEY;
            buf[o++] = (uint8_t) msg->inject_keycode.action;
            sc_write32be(&buf[o], msg->inject_keycode.keycode);
            o += 4;
            sc_write32be(&buf[o], msg->inject_keycode.repeat);
            o += 4;
            sc_write32be(&buf[o], msg->inject_keycode.metastate);
            o += 4;
            break;
        }
        case SC_CONTROL_MSG_TYPE_BACK_OR_SCREEN_ON: {
            buf[type_off] = SC_SYNC_MSG_BACK;
            buf[o++] = (uint8_t) msg->back_or_screen_on.action;
            break;
        }
        default:
            return 0;
    }

    return o;
}

static bool
sc_sync_deserialize(const uint8_t *buf, size_t len, uint32_t *source_id,
                    struct sc_control_msg *msg, struct sc_size frame_size) {
    if (len < 10) {
        return false;
    }

    size_t o = 0;
    uint32_t magic = sc_read32be(&buf[o]);
    o += 4;
    if (magic != SC_SYNC_MAGIC) {
        return false;
    }

    uint8_t version = buf[o++];
    if (version != SC_SYNC_VERSION) {
        return false;
    }

    uint8_t type = buf[o++];
    *source_id = sc_read32be(&buf[o]);
    o += 4;

    memset(msg, 0, sizeof(*msg));

    switch (type) {
        case SC_SYNC_MSG_TOUCH: {
            if (len < o + 23) {
                return false;
            }
            msg->type = SC_CONTROL_MSG_TYPE_INJECT_TOUCH_EVENT;
            msg->inject_touch_event.action = buf[o++];
            msg->inject_touch_event.action_button = sc_read32be(&buf[o]);
            o += 4;
            msg->inject_touch_event.buttons = sc_read32be(&buf[o]);
            o += 4;
            msg->inject_touch_event.pointer_id = sc_read64be(&buf[o]);
            o += 8;
            uint16_t nx = sc_read16be(&buf[o]);
            o += 2;
            uint16_t ny = sc_read16be(&buf[o]);
            o += 2;
            uint16_t pressure = sc_read16be(&buf[o]);
            o += 2;
            msg->inject_touch_event.position.screen_size = frame_size;
            msg->inject_touch_event.position.point.x =
                denorm_u16(nx, frame_size.width);
            msg->inject_touch_event.position.point.y =
                denorm_u16(ny, frame_size.height);
            msg->inject_touch_event.pressure = pressure / 65535.f;
            return true;
        }
        case SC_SYNC_MSG_SCROLL: {
            if (len < o + 12) {
                return false;
            }
            msg->type = SC_CONTROL_MSG_TYPE_INJECT_SCROLL_EVENT;
            uint16_t nx = sc_read16be(&buf[o]);
            o += 2;
            uint16_t ny = sc_read16be(&buf[o]);
            o += 2;
            int16_t hs = (int16_t) sc_read16be(&buf[o]);
            o += 2;
            int16_t vs = (int16_t) sc_read16be(&buf[o]);
            o += 2;
            msg->inject_scroll_event.buttons = sc_read32be(&buf[o]);
            o += 4;
            msg->inject_scroll_event.position.screen_size = frame_size;
            msg->inject_scroll_event.position.point.x =
                denorm_u16(nx, frame_size.width);
            msg->inject_scroll_event.position.point.y =
                denorm_u16(ny, frame_size.height);
            msg->inject_scroll_event.hscroll = hs / 1000.f;
            msg->inject_scroll_event.vscroll = vs / 1000.f;
            return true;
        }
        case SC_SYNC_MSG_KEY: {
            if (len < o + 13) {
                return false;
            }
            msg->type = SC_CONTROL_MSG_TYPE_INJECT_KEYCODE;
            msg->inject_keycode.action = buf[o++];
            msg->inject_keycode.keycode = sc_read32be(&buf[o]);
            o += 4;
            msg->inject_keycode.repeat = sc_read32be(&buf[o]);
            o += 4;
            msg->inject_keycode.metastate = sc_read32be(&buf[o]);
            o += 4;
            return true;
        }
        case SC_SYNC_MSG_BACK: {
            if (len < o + 1) {
                return false;
            }
            msg->type = SC_CONTROL_MSG_TYPE_BACK_OR_SCREEN_ON;
            msg->back_or_screen_on.action = buf[o++];
            return true;
        }
        default:
            return false;
    }
}

static void
sc_sync_clients_init(struct sc_sync_clients *clients) {
    clients->count = 0;
    for (size_t i = 0; i < SC_SYNC_MAX_CLIENTS; ++i) {
        clients->sockets[i] = SC_SOCKET_NONE;
    }
}

static bool
sc_sync_clients_add(struct sc_sync_clients *clients, sc_socket socket) {
    if (clients->count >= SC_SYNC_MAX_CLIENTS) {
        return false;
    }
    clients->sockets[clients->count++] = socket;
    return true;
}

static void
sc_sync_clients_remove_at(struct sc_sync_clients *clients, size_t index) {
    assert(index < clients->count);
    net_close(clients->sockets[index]);
    clients->sockets[index] = clients->sockets[clients->count - 1];
    clients->sockets[clients->count - 1] = SC_SOCKET_NONE;
    --clients->count;
}

static void
sc_sync_hub_broadcast(struct sc_sync_clients *clients, size_t from_index,
                      const uint8_t *buf, size_t len) {
    for (size_t i = 0; i < clients->count;) {
        if (i == from_index) {
            ++i;
            continue;
        }
        ssize_t w = net_send_all(clients->sockets[i], buf, len);
        if (w != (ssize_t) len) {
            LOGW("sync hub: peer disconnected");
            sc_sync_clients_remove_at(clients, i);
            continue;
        }
        ++i;
    }
}

static int
sc_sync_hub_thread(void *data) {
    struct sc_sync *sync = data;
    struct sc_sync_clients clients;
    sc_sync_clients_init(&clients);

    LOGI("Input sync hub on 127.0.0.1:%" PRIu16, sync->port);

    while (!sync->hub_stopped) {
        fd_set readfds;
        FD_ZERO(&readfds);

        sc_raw_socket listen_fd = sc_sync_raw(sync->hub_listen);
        FD_SET(listen_fd, &readfds);
        sc_raw_socket maxfd = listen_fd;

        for (size_t i = 0; i < clients.count; ++i) {
            sc_raw_socket fd = sc_sync_raw(clients.sockets[i]);
            FD_SET(fd, &readfds);
            if (fd > maxfd) {
                maxfd = fd;
            }
        }

        struct timeval tv = {
            .tv_sec = 0,
            .tv_usec = 200000,
        };
        int r = select((int) maxfd + 1, &readfds, NULL, NULL, &tv);
        if (r <= 0) {
            continue;
        }

        if (FD_ISSET(listen_fd, &readfds)) {
            sc_socket client = net_accept(sync->hub_listen);
            if (client != SC_SOCKET_NONE) {
                if (!sc_sync_clients_add(&clients, client)) {
                    LOGW("sync hub: too many peers");
                    net_close(client);
                } else {
                    LOGI("sync hub: peer connected (%u)",
                         (unsigned) clients.count);
                }
            }
        }

        for (size_t i = 0; i < clients.count;) {
            sc_raw_socket fd = sc_sync_raw(clients.sockets[i]);
            if (!FD_ISSET(fd, &readfds)) {
                ++i;
                continue;
            }

            uint8_t lenbuf[2];
            ssize_t n = net_recv_all(clients.sockets[i], lenbuf, 2);
            if (n != 2) {
                sc_sync_clients_remove_at(&clients, i);
                continue;
            }

            uint16_t plen = sc_read16be(lenbuf);
            if (plen == 0 || plen > SC_SYNC_MAX_PACKET) {
                sc_sync_clients_remove_at(&clients, i);
                continue;
            }

            uint8_t packet[SC_SYNC_MAX_PACKET];
            n = net_recv_all(clients.sockets[i], packet, plen);
            if (n != (ssize_t) plen) {
                sc_sync_clients_remove_at(&clients, i);
                continue;
            }

            uint8_t out[SC_SYNC_MAX_PACKET + 2];
            sc_write16be(out, plen);
            memcpy(out + 2, packet, plen);
            sc_sync_hub_broadcast(&clients, i, out, (size_t) plen + 2);
            ++i;
        }
    }

    while (clients.count) {
        sc_sync_clients_remove_at(&clients, 0);
    }
    return 0;
}

static void
sc_sync_apply_remote(struct sc_sync *sync, const uint8_t *packet, size_t len) {
    if (!sc_sync_is_enabled(sync) || !sync->controller) {
        return;
    }

    sc_mutex_lock(&sync->mutex);
    struct sc_size frame_size = sync->frame_size;
    sc_mutex_unlock(&sync->mutex);

    if (!frame_size.width || !frame_size.height) {
        return;
    }

    uint32_t source_id = 0;
    struct sc_control_msg msg;
    if (!sc_sync_deserialize(packet, len, &source_id, &msg, frame_size)) {
        return;
    }
    if (source_id == sync->source_id) {
        return;
    }

    sync->applying_remote = true;
    bool ok = sc_controller_push_msg(sync->controller, &msg);
    sync->applying_remote = false;
    if (!ok) {
        LOGW("sync: could not inject remote message");
    }
}

static int
sc_sync_client_thread(void *data) {
    struct sc_sync *sync = data;

    while (!sync->client_stopped) {
        uint8_t lenbuf[2];
        ssize_t n = net_recv_all(sync->client, lenbuf, 2);
        if (n != 2) {
            if (!sync->client_stopped) {
                LOGW("sync: disconnected from hub");
            }
            break;
        }

        uint16_t plen = sc_read16be(lenbuf);
        if (plen == 0 || plen > SC_SYNC_MAX_PACKET) {
            break;
        }

        uint8_t packet[SC_SYNC_MAX_PACKET];
        n = net_recv_all(sync->client, packet, plen);
        if (n != (ssize_t) plen) {
            break;
        }

        sc_sync_apply_remote(sync, packet, plen);
    }

    return 0;
}

bool
sc_sync_init(struct sc_sync *sync, struct sc_controller *controller,
             uint16_t port) {
    memset(sync, 0, sizeof(*sync));
    sync->controller = controller;
    sync->port = port ? port : SC_SYNC_DEFAULT_PORT;
    sync->hub_listen = SC_SOCKET_NONE;
    sync->client = SC_SOCKET_NONE;
    sync->source_id =
        (uint32_t) SDL_GetTicks() ^ (uint32_t) (uintptr_t) sync;

    return sc_mutex_init(&sync->mutex);
}

bool
sc_sync_start(struct sc_sync *sync) {
    assert(!sync->started);

    sync->hub_listen = net_socket();
    if (sync->hub_listen != SC_SOCKET_NONE
            && net_listen(sync->hub_listen, IPV4_LOCALHOST, sync->port, 8)) {
        sync->is_hub = true;
        sync->hub_stopped = false;
        if (!sc_thread_create(&sync->hub_thread, sc_sync_hub_thread,
                              "scrcpy-sync-hub", sync)) {
            net_close(sync->hub_listen);
            sync->hub_listen = SC_SOCKET_NONE;
            sync->is_hub = false;
            LOGE("Could not start sync hub thread");
            return false;
        }
    } else {
        if (sync->hub_listen != SC_SOCKET_NONE) {
            net_close(sync->hub_listen);
            sync->hub_listen = SC_SOCKET_NONE;
        }
        sync->is_hub = false;
        LOGI("Joining existing input sync hub");
    }

    sync->client = SC_SOCKET_NONE;
    bool connected = false;
    for (int i = 0; i < 30; ++i) {
        sync->client = net_socket();
        if (sync->client == SC_SOCKET_NONE) {
            break;
        }
        if (net_connect(sync->client, IPV4_LOCALHOST, sync->port)) {
            connected = true;
            break;
        }
        net_close(sync->client);
        sync->client = SC_SOCKET_NONE;
        SDL_Delay(50);
    }

    if (!connected) {
        LOGE("Could not connect to sync hub on port %" PRIu16, sync->port);
        sc_sync_stop(sync);
        return false;
    }

    sync->client_stopped = false;
    if (!sc_thread_create(&sync->client_thread, sc_sync_client_thread,
                          "scrcpy-sync-client", sync)) {
        LOGE("Could not start sync client thread");
        sc_sync_stop(sync);
        return false;
    }

    sync->started = true;
    LOGI("Input sync ready (port %" PRIu16 ", %s)", sync->port,
         sync->is_hub ? "hub" : "peer");
    return true;
}

void
sc_sync_stop(struct sc_sync *sync) {
    sync->client_stopped = true;
    sync->hub_stopped = true;

    if (sync->client != SC_SOCKET_NONE) {
        net_interrupt(sync->client);
    }
    if (sync->hub_listen != SC_SOCKET_NONE) {
        net_interrupt(sync->hub_listen);
    }

    if (sync->started) {
        sc_thread_join(&sync->client_thread, NULL);
        if (sync->is_hub) {
            sc_thread_join(&sync->hub_thread, NULL);
        }
        sync->started = false;
    } else if (sync->is_hub) {
        // hub thread started but client failed
        sc_thread_join(&sync->hub_thread, NULL);
        sync->is_hub = false;
    }

    if (sync->client != SC_SOCKET_NONE) {
        net_close(sync->client);
        sync->client = SC_SOCKET_NONE;
    }
    if (sync->hub_listen != SC_SOCKET_NONE) {
        net_close(sync->hub_listen);
        sync->hub_listen = SC_SOCKET_NONE;
    }
}

void
sc_sync_destroy(struct sc_sync *sync) {
    sc_sync_stop(sync);
    sc_mutex_destroy(&sync->mutex);
}

void
sc_sync_set_enabled(struct sc_sync *sync, bool enabled) {
    sc_mutex_lock(&sync->mutex);
    sync->enabled = enabled;
    sc_mutex_unlock(&sync->mutex);
    LOGI("Input sync %s", enabled ? "ON" : "OFF");
}

bool
sc_sync_is_enabled(struct sc_sync *sync) {
    sc_mutex_lock(&sync->mutex);
    bool enabled = sync->enabled;
    sc_mutex_unlock(&sync->mutex);
    return enabled;
}

void
sc_sync_set_frame_size(struct sc_sync *sync, struct sc_size size) {
    sc_mutex_lock(&sync->mutex);
    sync->frame_size = size;
    sc_mutex_unlock(&sync->mutex);
}

void
sc_sync_broadcast(struct sc_sync *sync, const struct sc_control_msg *msg) {
    if (!sync || !sync->started || sync->applying_remote) {
        return;
    }
    if (!sc_sync_is_enabled(sync)) {
        return;
    }
    if (!sc_sync_msg_is_supported(msg->type)) {
        return;
    }
    if (sync->client == SC_SOCKET_NONE) {
        return;
    }

    uint8_t packet[SC_SYNC_MAX_PACKET];
    size_t plen =
        sc_sync_serialize(msg, sync->source_id, packet, sizeof(packet));
    if (!plen) {
        return;
    }

    uint8_t out[SC_SYNC_MAX_PACKET + 2];
    sc_write16be(out, (uint16_t) plen);
    memcpy(out + 2, packet, plen);

    ssize_t w = net_send_all(sync->client, out, plen + 2);
    if (w != (ssize_t) (plen + 2)) {
        LOGW("sync: could not broadcast message");
    }
}
