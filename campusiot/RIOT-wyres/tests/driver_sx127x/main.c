/*
 * Copyright (C) 2016 Unwired Devices <info@unwds.com>
 *               2017 Inria Chile
 *               2017 Inria
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

/**
 * @ingroup     tests
 * @{
 * @file
 * @brief       Test application for SX127X modem driver
 *
 * @author      Eugene P. <ep@unwds.com>
 * @author      José Ignacio Alamos <jose.alamos@inria.cl>
 * @author      Alexandre Abadie <alexandre.abadie@inria.fr>
 * @}
 */

#include <stdio.h>

#include "shell.h"

#include "message_store.h"
#include "radio_commands.h"
#include "relay.h"

static int test_cmd(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        if (i > 1) {
            printf(" ");
        }
        printf("%s", argv[i]);
    }
    if (argc > 1) {
        printf("\n");
    }
    return 0;
}

static const shell_command_t shell_commands[] = {
    { "init", "Initialize SX1272", init_sx1272_cmd },
    { "setup", "Initialize LoRa modulation settings", lora_setup_cmd },
    { "implicit", "Enable implicit header", implicit_cmd },
    { "crc", "Enable CRC", crc_cmd },
    { "payload", "Set payload length (implicit header)", payload_cmd },
    { "random", "Get random number from sx127x", random_cmd },
    { "syncword", "Get/Set the syncword", syncword_cmd },
    { "rx_timeout", "Set the RX timeout", rx_timeout_cmd },
    { "channel", "Get/Set channel frequency (in Hz)", channel_cmd },
    { "register", "Get/Set value(s) of registers of sx127x", register_cmd },
    { "send", "Send raw payload string", send_cmd },
    { "threshold", "Get/Set relay SNR threshold", threshold_cmd },
    { "listen", "Listen: [favoris] [@src...] [#dst...] (no args = all)", listen_cmd },
    { "reset", "Reset the sx127x device", reset_cmd },
    { "test", "Test the sx127x device", test_cmd },
    { "memory", "Memory all messages", memory_cmd },
    { "filter", "Filter messages by src/dst", filter_cmd },
    { "favoris", "Manage favorites: list | add | remove | clear", favoris_cmd },
    { NULL, NULL, NULL }
};

int main(void)
{
    puts("Initialization successful - starting the shell now");

    char line_buf[SHELL_DEFAULT_BUFSIZE];
    shell_run(shell_commands, line_buf, SHELL_DEFAULT_BUFSIZE);

    return 0;
}
