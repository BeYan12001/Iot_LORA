/*
 * Copyright (C) 2016 Unwired Devices <info@unwds.com>
 *               2017 Inria Chile
 *               2017 Inria
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_defs.h"
#include "radio_commands.h"
#include "relay.h"

typedef struct {
    char src[NAME_BUF_SIZE];
    char dst[NAME_BUF_SIZE];
    char sep;
    int counter;
    int8_t best_snr;
    uint8_t relayed;
    uint8_t used;
} relay_entry_t;

static relay_entry_t relay_entries[MAX_RELAY_TRACKED];
static int8_t snr_threshold = 0;

static int _parse_routed_message(const char *msg, routed_message_t *parsed)
{
    const char *meta;
    const char *body;
    long counter;
    long ttl = 0;
    char *endptr;

    if (strlen(msg) < (size_t)(MAX_NAME_LEN * 2 + 2)) {
        return -1;
    }

    if (msg[MAX_NAME_LEN] != '@' && msg[MAX_NAME_LEN] != '#') {
        return -1;
    }

    if (msg[(MAX_NAME_LEN * 2) + 1] != ':') {
        return -1;
    }

    memcpy(parsed->src, msg, MAX_NAME_LEN);
    parsed->src[MAX_NAME_LEN] = '\0';
    parsed->sep = msg[MAX_NAME_LEN];
    memcpy(parsed->dst, msg + MAX_NAME_LEN + 1, MAX_NAME_LEN);
    parsed->dst[MAX_NAME_LEN] = '\0';

    meta = msg + (MAX_NAME_LEN * 2) + 2;
    body = strchr(meta, ':');
    if (body == NULL || body == meta) {
        return -1;
    }

    counter = strtol(meta, &endptr, 10);
    if (endptr == meta || counter < 0) {
        return -1;
    }

    parsed->counter = (int)counter;
    parsed->ttl = 0;
    parsed->has_ttl = 0;

    if (*endptr == ',') {
        ttl = strtol(endptr + 1, &endptr, 10);
        if (ttl < 0) {
            return -1;
        }
        parsed->ttl = (int)ttl;
        parsed->has_ttl = 1;
    }

    if (endptr != body) {
        return -1;
    }

    parsed->body = body + 1;
    return 0;
}

static relay_entry_t *_find_relay_entry(const routed_message_t *parsed)
{
    for (int i = 0; i < MAX_RELAY_TRACKED; i++) {
        if (!relay_entries[i].used) {
            continue;
        }
        if (relay_entries[i].sep == parsed->sep &&
            relay_entries[i].counter == parsed->counter &&
            strcmp(relay_entries[i].src, parsed->src) == 0 &&
            strcmp(relay_entries[i].dst, parsed->dst) == 0) {
            return &relay_entries[i];
        }
    }

    return NULL;
}

static relay_entry_t *_get_or_create_relay_entry(const routed_message_t *parsed)
{
    relay_entry_t *slot = _find_relay_entry(parsed);

    if (slot != NULL) {
        return slot;
    }

    for (int i = 0; i < MAX_RELAY_TRACKED; i++) {
        if (relay_entries[i].used) {
            continue;
        }

        relay_entries[i].used = 1;
        relay_entries[i].relayed = 0;
        relay_entries[i].best_snr = -128;
        relay_entries[i].sep = parsed->sep;
        relay_entries[i].counter = parsed->counter;
        strncpy(relay_entries[i].src, parsed->src, sizeof(relay_entries[i].src) - 1);
        relay_entries[i].src[sizeof(relay_entries[i].src) - 1] = '\0';
        strncpy(relay_entries[i].dst, parsed->dst, sizeof(relay_entries[i].dst) - 1);
        relay_entries[i].dst[sizeof(relay_entries[i].dst) - 1] = '\0';
        return &relay_entries[i];
    }

    relay_entries[0].used = 1;
    relay_entries[0].relayed = 0;
    relay_entries[0].best_snr = -128;
    relay_entries[0].sep = parsed->sep;
    relay_entries[0].counter = parsed->counter;
    strncpy(relay_entries[0].src, parsed->src, sizeof(relay_entries[0].src) - 1);
    relay_entries[0].src[sizeof(relay_entries[0].src) - 1] = '\0';
    strncpy(relay_entries[0].dst, parsed->dst, sizeof(relay_entries[0].dst) - 1);
    relay_entries[0].dst[sizeof(relay_entries[0].dst) - 1] = '\0';
    return &relay_entries[0];
}

int threshold_cmd(int argc, char **argv)
{
    if (argc == 1 || strcmp(argv[1], "get") == 0) {
        printf("SNR threshold: %d\n", snr_threshold);
        return 0;
    }

    if (strcmp(argv[1], "set") != 0 || argc < 3) {
        puts("usage: threshold [get|set <snr>]");
        return -1;
    }

    char *endptr;
    long value = strtol(argv[2], &endptr, 10);
    if (*argv[2] == '\0' || *endptr != '\0' || value < -128 || value > 127) {
        puts("threshold: invalid SNR value");
        return -1;
    }

    snr_threshold = (int8_t)value;
    printf("SNR threshold set to %d\n", snr_threshold);
    return 0;
}

void app_maybe_relay_message(const char *rx_msg, int8_t snr)
{
    routed_message_t parsed;
    relay_entry_t *entry;
    char relay_payload[MESSAGE_BUF_SIZE];
    int next_ttl;
    int written;

    if (_parse_routed_message(rx_msg, &parsed) != 0 || !parsed.has_ttl) {
        return;
    }

    entry = _get_or_create_relay_entry(&parsed);
    if (entry == NULL) {
        puts("relay: no slot available");
        return;
    }

    if (snr > entry->best_snr) {
        entry->best_snr = snr;
    }

    if (entry->relayed) {
        return;
    }

    if (entry->best_snr > snr_threshold) {
        printf("relay: skip %s%c%s:%d because SNR %d > threshold %d\n",
               parsed.src, parsed.sep, parsed.dst, parsed.counter,
               entry->best_snr, snr_threshold);
        return;
    }

    if (parsed.ttl <= 0) {
        return;
    }

    next_ttl = parsed.ttl - 1;
    written = snprintf(relay_payload, sizeof(relay_payload),
                       "%s%c%s:%d,%d:%s",
                       parsed.src, parsed.sep, parsed.dst,
                       parsed.counter, next_ttl, parsed.body);
    if (written < 0 || (size_t)written >= sizeof(relay_payload)) {
        puts("relay: payload too long");
        return;
    }

    if (app_send_payload(relay_payload) == 0) {
        entry->relayed = 1;
        printf("relay: forwarded with ttl %d -> %d\n", parsed.ttl, next_ttl);
    }
}
