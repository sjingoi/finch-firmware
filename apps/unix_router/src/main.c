#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

#include <csp/csp.h>
#include <csp/csp_debug.h>
#include <csp/drivers/usart.h>

#define MY_ADDRESS 0
#define MY_SERVER_PORT 10

/* UART/KISS interface: connects this node to other CSP nodes over a serial link. */
#define UART_DEVICE "/dev/ttyUSB0"
#define UART_BAUDRATE 115200
#define UART_ADDRESS 1

static unsigned int server_received = 0;

static void *task_router(void *param) {
	(void)param;
	while (1) {
		csp_route_work();
	}
	return NULL;
}

static void *task_server(void *param) {
	(void)param;

	csp_socket_t sock = {0};
	csp_bind(&sock, CSP_ANY);
	csp_listen(&sock, 10);

	while (1) {
		csp_conn_t *conn = csp_accept(&sock, 10000);
		if (conn == NULL) {
			continue;
		}

		csp_packet_t *packet;
		while ((packet = csp_read(conn, 100)) != NULL) {
			csp_print("Received packet from %u, port %u, length %u, destined for port %u\n", csp_conn_src(conn), csp_conn_dport(conn), packet->length, csp_conn_dport(conn));

			if (csp_conn_dport(conn) == MY_SERVER_PORT) {
				csp_print("Packet data: %s\n", (char *)packet->data);
				++server_received;
				csp_buffer_free(packet);
			} else {
				/* Handles built-in service ports, e.g. CSP_PING. */
				csp_service_handler(packet);
			}
		}

		csp_close(conn);
	}

	return NULL;
}

static void spawn(void *(*routine)(void *)) {
	pthread_t handle;
	pthread_attr_t attributes;
	pthread_attr_init(&attributes);
	pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_DETACHED);
	pthread_create(&handle, &attributes, routine, NULL);
	pthread_attr_destroy(&attributes);
}

int main(void) {
	csp_print("Initialising CSP\n");
	csp_init();

	spawn(task_router);
	spawn(task_server);

	/* Give the server a moment to bind before the client starts hammering it. */
	usleep(100000);

	csp_print("Pinging self (address %d)...\n", MY_ADDRESS);
	int result = csp_ping(MY_ADDRESS, 1000, 100, CSP_O_NONE);
	csp_print("Ping result: %d [mS]\n", result);

	csp_conn_t *conn = csp_connect(CSP_PRIO_NORM, MY_ADDRESS, MY_SERVER_PORT, 1000, CSP_O_NONE);
	if (conn == NULL) {
		csp_print("Connect failed\n");
		return EXIT_FAILURE;
	}

	csp_packet_t *packet = csp_buffer_get(0);
	if (packet == NULL) {
		csp_print("Failed to get buffer\n");
		csp_close(conn);
		return EXIT_FAILURE;
	}

	const char *msg = "Hello, CSP!";
	strcpy((char *)packet->data, msg);
	packet->length = strlen(msg) + 1;

	csp_send(conn, packet);
	csp_close(conn);

	/* Give the router/server threads time to deliver the packet. */
	usleep(500000);

	if (result >= 0 && server_received > 0) {
		csp_print("Self-test OK: ping succeeded and server received %u packet(s)\n", server_received);
	} else {
		csp_print("Self-test FAILED: ping=%d, server_received=%u\n", result, server_received);
		return EXIT_FAILURE;
	}

	csp_usart_conf_t uart_conf = {
		.device = UART_DEVICE,
		.baudrate = UART_BAUDRATE,
		.databits = 8,
		.stopbits = 1,
		.paritysetting = 0,
	};
	csp_iface_t *uart_iface = NULL;
	int uart_ret = csp_usart_open_and_add_kiss_interface(&uart_conf, CSP_IF_KISS_DEFAULT_NAME, UART_ADDRESS, &uart_iface);
	if (uart_ret == CSP_ERR_NONE) {
		/* Route anything not otherwise local out over the UART link. */
		uart_iface->is_default = 1;
		csp_print("UART interface up on %s at %u baud, address %d\n", UART_DEVICE, UART_BAUDRATE, UART_ADDRESS);
	} else {
		csp_print("Warning: failed to open UART %s (error %d) - continuing without it\n", UART_DEVICE, uart_ret);
	}

	csp_print("Node running on address %d, port %d. Press Ctrl+C to exit.\n", MY_ADDRESS, MY_SERVER_PORT);
	while (1) {
		pause();
	}
}
