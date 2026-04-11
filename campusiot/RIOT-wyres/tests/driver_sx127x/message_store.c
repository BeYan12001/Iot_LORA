/*
 * Copyright (C) 2016 Unwired Devices <info@unwds.com>
 *               2017 Inria Chile
 *               2017 Inria
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_defs.h"
#include "message_store.h"
#include "radio_commands.h"

static char memory[MAX_STORED_MESSAGES][MESSAGE_BUF_SIZE];
static int mem_index = 0;

static char listen_filter_srcs[MAX_LISTEN_FILTERS][NAME_BUF_SIZE];
static int listen_filter_src_count = 0;
static char listen_filter_dsts[MAX_LISTEN_FILTERS][NAME_BUF_SIZE];
static int listen_filter_dst_count = 0;
static int listen_filter_favoris = 0;

static char favoris[MAX_FAVORIS][ADDRESS_BUF_SIZE];
static int favoris_count = 0;

static int _parse_message_header(const char *msg, char *src_out, size_t src_size,
                                 char *dst_out, size_t dst_size, char *sep_out)
{
    if (strlen(msg) < (size_t)(MAX_NAME_LEN * 2 + 2)) {
        return -1;
    }

    if (msg[MAX_NAME_LEN] != '@' && msg[MAX_NAME_LEN] != '#') {
        return -1;
    }

    if (msg[(MAX_NAME_LEN * 2) + 1] != ':') {
        return -1;
    }

    if (src_out != NULL) {
        if (src_size < NAME_BUF_SIZE) {
            return -1;
        }
        memcpy(src_out, msg, MAX_NAME_LEN);
        src_out[MAX_NAME_LEN] = '\0';
    }

    if (dst_out != NULL) {
        if (dst_size < NAME_BUF_SIZE) {
            return -1;
        }
        memcpy(dst_out, msg + MAX_NAME_LEN + 1, MAX_NAME_LEN);
        dst_out[MAX_NAME_LEN] = '\0';
    }

    if (sep_out != NULL) {
        *sep_out = msg[MAX_NAME_LEN];
    }

    return 0;
}

static int _parse_field(const char *msg, int want_src, char *out, size_t out_size)
{
    char src_name[NAME_BUF_SIZE];
    char dst_name[NAME_BUF_SIZE];
    char sep;

    if (_parse_message_header(msg, src_name, sizeof(src_name),
                              dst_name, sizeof(dst_name), &sep) != 0) {
        return -1;
    }

    if (want_src) {
        strncpy(out, src_name, out_size - 1);
        out[out_size - 1] = '\0';
    }
    else {
        int len = snprintf(out, out_size, "%c%s", sep, dst_name);
        if (len < 0 || (size_t)len >= out_size) {
            return -1;
        }
        out[out_size - 1] = '\0';
    }

    return 0;
}

static int _is_favori(const char *name)
{
    for (int i = 0; i < favoris_count; i++) {
        if (strcmp(favoris[i], name) == 0) {
            return 1;
        }
    }
    return 0;
}

static int _match_favori_src(const char *src_name)
{
    char prefixed_src[ADDRESS_BUF_SIZE];
    int len = snprintf(prefixed_src, sizeof(prefixed_src), "@%s", src_name);

    if (len < 0 || (size_t)len >= sizeof(prefixed_src)) {
        return 0;
    }

    return _is_favori(prefixed_src);
}

static int _match_favori_dst(char sep, const char *dst_name)
{
    char prefixed_dst[ADDRESS_BUF_SIZE];
    int len = snprintf(prefixed_dst, sizeof(prefixed_dst), "%c%s", sep, dst_name);

    if (len < 0 || (size_t)len >= sizeof(prefixed_dst)) {
        return 0;
    }

    return _is_favori(prefixed_dst);
}

int app_message_passes_filters(const char *msg)
{
    if (listen_filter_src_count == 0 && listen_filter_dst_count == 0 &&
        !listen_filter_favoris) {
        return 1;
    }

    char msg_src[NAME_BUF_SIZE];
    char msg_dst[NAME_BUF_SIZE];
    char msg_sep = '\0';
    int header_ok = (_parse_message_header(msg,
                                           msg_src, sizeof(msg_src),
                                           msg_dst, sizeof(msg_dst),
                                           &msg_sep) == 0);
    int match = 0;

    if (header_ok) {
        for (int fi = 0; fi < listen_filter_src_count && !match; fi++) {
            if (strcmp(msg_src, listen_filter_srcs[fi]) == 0) {
                match = 1;
            }
        }
    }

    if (!match && header_ok && msg_sep == '#') {
        for (int fi = 0; fi < listen_filter_dst_count && !match; fi++) {
            if (strcmp(msg_dst, listen_filter_dsts[fi]) == 0) {
                match = 1;
            }
        }
    }

    if (!match && listen_filter_favoris) {
        if ((header_ok && _match_favori_src(msg_src)) ||
            (header_ok && _match_favori_dst(msg_sep, msg_dst))) {
            match = 1;
        }
    }

    return match;
}

void app_store_message(const char *msg)
{
    strncpy(memory[mem_index], msg, sizeof(memory[mem_index]) - 1);
    memory[mem_index][sizeof(memory[mem_index]) - 1] = '\0';
    mem_index = (mem_index + 1) % MAX_STORED_MESSAGES;
}

int listen_cmd(int argc, char **argv)
{
    listen_filter_src_count = 0;
    listen_filter_dst_count = 0;
    listen_filter_favoris = 0;
    memset(listen_filter_srcs, 0, sizeof(listen_filter_srcs));
    memset(listen_filter_dsts, 0, sizeof(listen_filter_dsts));

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "favoris") == 0) {
            listen_filter_favoris = 1;
        }
        else if (argv[i][0] == '@' && listen_filter_src_count < MAX_LISTEN_FILTERS) {
            strncpy(listen_filter_srcs[listen_filter_src_count], argv[i] + 1,
                    sizeof(listen_filter_srcs[0]) - 1);
            listen_filter_srcs[listen_filter_src_count][sizeof(listen_filter_srcs[0]) - 1] = '\0';
            listen_filter_src_count++;
        }
        else if (argv[i][0] == '#' && listen_filter_dst_count < MAX_LISTEN_FILTERS) {
            strncpy(listen_filter_dsts[listen_filter_dst_count], argv[i] + 1,
                    sizeof(listen_filter_dsts[0]) - 1);
            listen_filter_dsts[listen_filter_dst_count][sizeof(listen_filter_dsts[0]) - 1] = '\0';
            listen_filter_dst_count++;
        }
    }

    if (listen_filter_src_count == 0 && listen_filter_dst_count == 0 &&
        !listen_filter_favoris) {
        puts("Listening to all messages");
    }
    else {
        if (listen_filter_src_count > 0) {
            printf("Sources  : ");
            for (int i = 0; i < listen_filter_src_count; i++) {
                printf("@%s%s", listen_filter_srcs[i],
                       i < listen_filter_src_count - 1 ? ", " : "");
            }
            puts("");
        }
        if (listen_filter_dst_count > 0) {
            printf("Channels : ");
            for (int i = 0; i < listen_filter_dst_count; i++) {
                printf("#%s%s", listen_filter_dsts[i],
                       i < listen_filter_dst_count - 1 ? ", " : "");
            }
            puts("");
        }
        if (listen_filter_favoris) {
            puts("Filter   : favoris");
        }
    }

    netdev_t *netdev = app_netdev();
    const netopt_enable_t single = false;
    const uint32_t timeout = 0;
    netopt_state_t state = NETOPT_STATE_RX;

    netdev->driver->set(netdev, NETOPT_SINGLE_RECEIVE, &single, sizeof(single));
    netdev->driver->set(netdev, NETOPT_RX_TIMEOUT, &timeout, sizeof(timeout));
    netdev->driver->set(netdev, NETOPT_STATE, &state, sizeof(state));

    printf("Listen mode set\n");
    return 0;
}

int memory_cmd(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    puts("Stored messages:");
    int count = 0;
    for (int i = 0; i < MAX_STORED_MESSAGES; i++) {
        if (memory[i][0] != '\0') {
            printf("[%d] %s\n", i, memory[i]);
            count++;
        }
    }
    if (count == 0) {
        puts("No messages stored.");
    }
    return 0;
}

int favoris_cmd(int argc, char **argv)
{
    if (argc < 2) {
        puts("usage: favoris <list|add|remove|clear> [name]");
        return -1;
    }

    if (strcmp(argv[1], "list") == 0) {
        if (favoris_count == 0) {
            puts("No favorites configured.");
            return 0;
        }

        puts("Favorites:");
        for (int i = 0; i < favoris_count; i++) {
            printf("[%d] %s\n", i, favoris[i]);
        }
        return 0;
    }

    if (strcmp(argv[1], "add") == 0) {
        if (argc < 3) {
            puts("usage: favoris add <name>");
            return -1;
        }
        if ((argv[2][0] != '@' && argv[2][0] != '#') || argv[2][1] == '\0') {
            puts("favoris add: use @name for a person or #name for a channel.");
            return -1;
        }
        if (strlen(argv[2] + 1) != MAX_NAME_LEN) {
            printf("favoris add: name must be exactly %d characters.\n", MAX_NAME_LEN);
            return -1;
        }
        if (_is_favori(argv[2])) {
            printf("%s is already in favorites.\n", argv[2]);
            return 0;
        }
        if (favoris_count >= MAX_FAVORIS) {
            puts("Favorites list is full.");
            return -1;
        }

        strncpy(favoris[favoris_count], argv[2], sizeof(favoris[0]) - 1);
        favoris[favoris_count][sizeof(favoris[0]) - 1] = '\0';
        favoris_count++;
        printf("%s added to favorites.\n", argv[2]);
        return 0;
    }

    if (strcmp(argv[1], "remove") == 0) {
        if (argc < 3) {
            puts("usage: favoris remove <name>");
            return -1;
        }

        for (int i = 0; i < favoris_count; i++) {
            if (strcmp(favoris[i], argv[2]) == 0) {
                for (int j = i; j < favoris_count - 1; j++) {
                    strncpy(favoris[j], favoris[j + 1], sizeof(favoris[j]) - 1);
                    favoris[j][sizeof(favoris[j]) - 1] = '\0';
                }
                memset(favoris[favoris_count - 1], 0, sizeof(favoris[favoris_count - 1]));
                favoris_count--;
                printf("%s removed from favorites.\n", argv[2]);
                return 0;
            }
        }

        printf("%s not found in favorites.\n", argv[2]);
        return -1;
    }

    if (strcmp(argv[1], "clear") == 0) {
        memset(favoris, 0, sizeof(favoris));
        favoris_count = 0;
        puts("Favorites cleared.");
        return 0;
    }

    puts("usage: favoris <list|add|remove|clear> [name]");
    return -1;
}

int filter_cmd(int argc, char **argv)
{
    if (argc < 2 ||
        (strcmp(argv[1], "src") != 0 && strcmp(argv[1], "dst") != 0)) {
        puts("usage: filter <src|dst> [value]");
        return -1;
    }

    int want_src = (strcmp(argv[1], "src") == 0);
    char field[ADDRESS_BUF_SIZE];

    if (argc == 2) {
        static char seen[MAX_CONTACTS][ADDRESS_BUF_SIZE];
        int seen_count = 0;
        memset(seen, 0, sizeof(seen));

        for (int i = 0; i < MAX_STORED_MESSAGES; i++) {
            if (memory[i][0] == '\0') {
                continue;
            }
            if (_parse_field(memory[i], want_src, field, sizeof(field)) != 0) {
                continue;
            }

            int found = 0;
            for (int j = 0; j < seen_count; j++) {
                if (strcmp(seen[j], field) == 0) {
                    found = 1;
                    break;
                }
            }
            if (!found && seen_count < MAX_CONTACTS) {
                strncpy(seen[seen_count], field, sizeof(seen[seen_count]) - 1);
                seen[seen_count][sizeof(seen[seen_count]) - 1] = '\0';
                seen_count++;
            }
        }

        if (seen_count == 0) {
            printf("No %s found.\n", argv[1]);
        }
        else {
            printf("Known %s:\n", argv[1]);
            for (int i = 0; i < seen_count; i++) {
                printf("  %s\n", seen[i]);
            }
        }
    }
    else {
        const char *filter_val = argv[2];
        int count = 0;

        printf("Messages with %s=%s:\n", argv[1], filter_val);
        for (int i = 0; i < MAX_STORED_MESSAGES; i++) {
            if (memory[i][0] == '\0') {
                continue;
            }
            if (_parse_field(memory[i], want_src, field, sizeof(field)) != 0) {
                continue;
            }
            if (strcmp(field, filter_val) == 0) {
                printf("[%d] %s\n", i, memory[i]);
                count++;
            }
        }

        if (count == 0) {
            printf("No messages found for %s=%s.\n", argv[1], filter_val);
        }
    }

    return 0;
}
