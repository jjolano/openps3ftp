/*
 * OpenPS3FTP pinned mbedtls configuration.
 *
 * Includes the stock mbedtls 2.28 config, then disables what this
 * project never uses:
 *   - MBEDTLS_NET_C: the FTP server drives sockets itself with custom
 *     BIO callbacks (f_send/f_recv); mbedtls_net_* is dead code and
 *     the ps3 port's net_sockets.c changes break the host build.
 *
 * TLS 1.0/1.1 are disabled at runtime via
 * mbedtls_ssl_conf_min_version(.., TLS1_2).
 */
#include "mbedtls/config.h"

#undef MBEDTLS_NET_C
