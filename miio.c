#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "md5.h"
#include "aes.h"
#include "json.h"

static char* host_ip = "192.168.100.200";
static char token[16] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

static uint8_t mi_aes_key[16];
static uint8_t mi_aes_iv[16];

static uint8_t device_id[4];
static uint8_t mi_tstamp[4];
static int     sequence_id = 0;
static int     sock = -1;

static void
bump_seq ()
{
	if (++sequence_id >= 9999) {
		sequence_id = 1;
	}
}

static void
calc_keyiv ()
{
	struct MD5Context context;

	MD5Init (&context);
	MD5Update (&context, (const unsigned char*) token, 16);
	MD5Final (mi_aes_key, &context);

	uint8_t tmp[32];
	memcpy (tmp, mi_aes_key, 16);
	memcpy (&tmp[16], token, 16);

	MD5Init (&context);
	MD5Update (&context, (const unsigned char*) tmp, 32);
	MD5Final (mi_aes_iv, &context);
}

static void
mi_encrypt (uint8_t* buf, uint32_t length)
{
	struct AES_ctx ctx;
	AES_init_ctx_iv (&ctx, mi_aes_key, mi_aes_iv);
	AES_CBC_encrypt_buffer (&ctx, buf, length);
}

static void
mi_decrypt (uint8_t* buf, uint32_t length)
{
	struct AES_ctx ctx;
	AES_init_ctx_iv (&ctx, mi_aes_key, mi_aes_iv);
	AES_CBC_decrypt_buffer (&ctx, buf, length);
}

static ssize_t
rx (int fd, void* buf, size_t siz)
{
	struct sockaddr_storage src;
	socklen_t src_len = sizeof (src);
	return recvfrom (fd, buf, siz, 0, (struct sockaddr*)&src, &src_len);
}

static ssize_t
tx (int fd, const void *msg, size_t len)
{
	struct addrinfo hints;
	memset (&hints, 0, sizeof (hints));

	hints.ai_family   = AF_UNSPEC;
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_flags    = AI_NUMERICHOST;

	struct addrinfo* res = 0;
	if (getaddrinfo (host_ip, "54321", &hints, &res) != 0) {
		printf ("ERROR: getaddrinfo\n");
		return -1;
	}

	ssize_t n = sendto (fd, msg, len, 0, res->ai_addr,res->ai_addrlen);
	freeaddrinfo (res);
	return n;
}

#if 0
static int
rx_dump (int fd) {
	unsigned char buf[1024];

	ssize_t n = rx (fd, buf, sizeof (buf));

	if (n <= 0) {
		perror ("Error RecvFrom");
		return -1;
	}

	printf ("Recv %zd bytes:\n", n);
	for (int i = 0; i < n; ++i) {
		printf ("%02x ", buf[i] & 0xff);
		if ((i % 16) == 15 || i == n - 1) {
			printf ("\n");
		}
	}
	if (n > 32) {
		mi_decrypt (&buf[32], n - 32);
		printf ("MSG: %s\n", &buf[32]);
	} else {
		printf ("Reply: %zd bytes\n", n);
	}
	return n;
}
#endif

static int
tx_probe (int fd)
{
	unsigned char buf[256];
	size_t len = 32;

	buf[0] = 0x21;
	buf[1] = 0x31;
	buf[2] = (len << 8) & 0xff;
	buf[3] = len & 0xff;

	for (size_t i = 4; i < len; ++i) {
		buf[i] = 0xff;
	}

	if (tx (fd, buf, len) != len) {
		printf ("Tx probe failed");
		return -1;
	}

	memset (buf, 0, 256);
	if (rx (fd, buf, 256) != 32) {
		printf ("Rx probe failed");
		return -1;
	}
	if (buf[0] != 0x21 || buf[1] != 0x31 || buf[2] != 0 || buf[3] != 0x20
	    || buf[4] != 0 || buf[5] != 0 || buf[6] != 0 || buf[7] != 0) {
		printf ("Rx invalid probe");
		return -1;
	}

	for (int i = 0; i < 4; ++i) {
		device_id[i] = buf[ 8 + i];
		mi_tstamp[i] = buf[12 + i];
	}

	return 0;
}

static int
tx_msg (int fd, const char* msg)
{
	size_t len = strlen (msg) + 1;
	size_t pad = 16 - (len & 0xf);
	size_t siz = 32 + len + pad;

	//printf ("len = %zd ; pad = 0x%02zx = %zd; tot = 0x%04zx\n", len, pad, pad, siz);
	unsigned char* buf = malloc (siz);

	buf[0] = 0x21;
	buf[1] = 0x31;
	buf[2] = (siz << 8) & 0xff;
	buf[3] = siz & 0xff;

	for (int i = 0; i < 4; ++i) {
		buf[ 4 + i] = 0;
		buf[ 8 + i] = device_id[i];
		buf[12 + i] = mi_tstamp[i];
	}

	for (int i = 0; i < 16; ++i) {
		buf[16+i] = token[i];
	}

	memcpy (&buf[32], msg, len);
	for (size_t i = 0; i < pad; ++i) {
		buf[32+len+i] = (uint8_t)(pad & 0xf);
	}
	mi_encrypt (&buf[32], len + pad);

	struct MD5Context context;
	MD5Init (&context);
	MD5Update (&context, (const unsigned char*) buf, siz);
	MD5Final (&buf[16], &context);

#if 0
	for (int i = 0; i < siz; ++i) {
		printf ("%02x ", buf[i] & 0xff);
		if ((i % 16) == 15 || i == siz - 1) {
			printf ("\n");
		}
	}
#endif

	int rv = 0;
	if (tx (fd, buf, siz) != siz) {
		printf ("ERROR: Tx msg '%s'", msg);
		rv = -1;
	}
	free (buf);
	return rv;
}

static int
setup_server () {
	struct sockaddr_in server;
	int fd = socket (AF_INET, SOCK_DGRAM, 0);

	if (fd < 0) {
		perror ("Error opening socket");
		return -1;
	}

	size_t length = sizeof (server);
	memset (&server, 0, length);
	server.sin_family      = AF_INET;
	server.sin_addr.s_addr = INADDR_ANY;
	server.sin_port        = htons (54321);

	if (bind (fd, (struct sockaddr *)&server, length) < 0) {
		perror ("Error binding socket");
		close (fd);
		return -1;
	}

	struct timeval tv;
	tv.tv_sec = 1;
	tv.tv_usec = 0;
	if (setsockopt (fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
		perror ("Error setting timeout");
		close (fd);
		return -1;
	}

	return fd;
}


int
miio_init ()
{
	if (getenv ("MIROBO_IP")) {
		host_ip = getenv ("MIROBO_IP");
	}

	if (getenv ("MIROBO_TOKEN")) {
		char* tok = getenv ("MIROBO_TOKEN");
		if (strlen(tok) == 32) {
			for (int i = 0; i < 16; ++i) {
				char xx[3];
				xx[0] = tok[2 * i];
				xx[1] = tok[2 * i + 1];
				xx[2] = 0;
				token[i] = strtol (xx, NULL, 16);
			}
		}
	}

	calc_keyiv ();

	if (sequence_id == 0) {
		sequence_id = 1 + time (NULL) % 9999;
	}

	sock = setup_server ();
	return sock > 0 ? 0 : -1;
}

void
miio_cleanup ()
{
	if (sock > 0) {
		close (sock);
	}
	sock = -1;
}

json_value*
miio_cmd (const char* cmd, const char* opt)
{
	int fd = sock;
	int retry = 3;
	ssize_t n = 0;
	char buf[1024];

	while (retry-- > 0) {
		if (tx_probe (fd)) {
			return NULL;
		}

		snprintf (buf, sizeof(buf), "{\"method\": \"%s\", \"id\": %d, %s}", cmd, sequence_id, opt);
		bump_seq ();

		if (tx_msg (fd, buf)) {
			return NULL;
		}

		if ((n = rx (fd, buf, sizeof (buf))) > 0) {
			break;
		}
	}

	if (n > 32) {
		mi_decrypt ((uint8_t*) &buf[32], n - 32);
		char* json = &buf[32];
		//printf ("%s\n", json);
		return json_parse (json, strlen(json));

	}
	return NULL;
}

const char*
vac_status (int code) {
	switch (code) {
		case 1: return "Starting";
		case 2: return "Charger disconnected";
		case 3: return "Idle";
		case 4: return "Remote control active";
		case 5: return "Cleaning";
		case 6: return "Returning home";
		case 7: return "Manual mode";
		case 8: return "Charging";
		case 9: return "Charging problem";
		case 10: return "Paused";
		case 11: return "Spot cleaning";
		case 12: return "Error";
		case 13: return "Shutting down";
		case 14: return "Updating";
		case 15: return "Docking";
		case 16: return "Going to target";
		case 17: return "Zoned cleaning";
	}
	return "-";
}

const char*
vac_error (int code) {
	switch (code) {
		case 0: return "No error";
		case 1: return "Laser distance sensor error";
		case 2: return "Collision sensor error";
		case 3: return "Wheels on top of void, move robot";
		case 4: return "Clean hovering sensors, move robot";
		case 5: return "Clean main brush";
		case 6: return "Clean side brush";
		case 7: return "Main wheel stuck?";
		case 8: return "Device stuck, clean area";
		case 9: return "Dust collector missing";
		case 10: return "Clean filter";
		case 11: return "Stuck in magnetic barrier";
		case 12: return "Low battery";
		case 13: return "Charging fault";
		case 14: return "Battery fault";
		case 15: return "Wall sensors dirty, wipe them";
		case 16: return "Place me on flat surface";
		case 17: return "Side brushes problem, reboot me";
		case 18: return "Suction fan problem";
		case 19: return "Unpowered charging station";
	}
	return "Unkown Error";
}
