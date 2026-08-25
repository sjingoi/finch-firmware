#include <stdio.h>
#include <stdarg.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/printk.h>
#include <csp/csp.h>
#include <csp/interfaces/csp_if_kiss.h>

#define UART_ADDR 5
#define UART_RX_CBUF_LEN 32

static csp_iface_t kiss_iface = {0};
static csp_kiss_interface_data_t kiss_ifdata = {0};
static uint8_t uart_rx_cbuf[UART_RX_CBUF_LEN];
static int uart_rx_cbuf_len = 0;

#define SW0_NODE DT_ALIAS(sw0)
#define UART_NODE DT_NODELABEL(usart1)
#define SERVER_PORT 10

static const int32_t sleep_time_ms = 100;

// Router thread
static struct k_thread router_thread;
K_THREAD_STACK_DEFINE(router_stack, 1024);

// Server thread
static struct k_thread server_thread;
K_THREAD_STACK_DEFINE(server_stack, 1024);

// Client thread
static struct k_thread client_thread;
K_THREAD_STACK_DEFINE(client_stack, 1024);

// UART
static const struct device *uart_dev = DEVICE_DT_GET(UART_NODE);

static csp_iface_t *iface = NULL;
// static csp_kiss_interface_data_t kiss_data = {0};

void callback(const struct device *port, struct gpio_callback *cb, gpio_port_pins_t pins) {
    printk("Button state changed.\r\n");
}

void csp_print_func(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vprintk(fmt, args);
    va_end(args);
}

/* KISS "driver" tx: writes framed bytes straight out over the UART. */
static int uart_kiss_tx(void *driver_data, const uint8_t *data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        uart_poll_out(uart_dev, data[i]);
    }
    return CSP_ERR_NONE;
}

/* Interrupt-driven UART RX: drains whatever the hardware has right now and
 * hands it straight to the KISS decoder. This replaces libcsp's stock
 * poll-in-a-thread driver, which sleeps between polls for at least one
 * kernel tick (100us here) with no RX FIFO behind it - long enough to lose
 * bytes arriving at 115200 baud (~8.7us/byte) whenever the poll loop wasn't
 * actively spinning. Draining directly from the ISR means we never leave
 * the single RX data register unread between bytes. */
static void uart_rx_isr(const struct device *dev, void *user_data) {
    while (uart_irq_update(dev) && uart_irq_rx_ready(dev)) {
        uint8_t byte;
        int n = uart_fifo_read(dev, &byte, 1);
        if (n <= 0) {
            break;
        }

        uart_rx_cbuf[uart_rx_cbuf_len++] = byte;
        if (uart_rx_cbuf_len >= UART_RX_CBUF_LEN) {
            csp_kiss_rx(&kiss_iface, uart_rx_cbuf, uart_rx_cbuf_len, (void *)1);
            uart_rx_cbuf_len = 0;
        }
    }

    /* Hand off whatever came in on this interrupt; the next byte raises a
     * new interrupt so there's no need to wait for an idle line. */
    if (uart_rx_cbuf_len > 0) {
        csp_kiss_rx(&kiss_iface, uart_rx_cbuf, uart_rx_cbuf_len, (void *)1);
        uart_rx_cbuf_len = 0;
    }
}

static int setup_uart_kiss_interface(csp_iface_t **return_iface) {
    kiss_iface.name = "uart";
    kiss_iface.addr = UART_ADDR;
    kiss_iface.interface_data = &kiss_ifdata;
    kiss_ifdata.tx_func = uart_kiss_tx;

    int res = csp_kiss_add_interface(&kiss_iface);
    if (res != CSP_ERR_NONE) {
        return res;
    }

    uart_irq_rx_disable(uart_dev);
    uart_irq_tx_disable(uart_dev);
    uart_irq_callback_user_data_set(uart_dev, uart_rx_isr, NULL);
    uart_irq_rx_enable(uart_dev);

    if (return_iface) {
        *return_iface = &kiss_iface;
    }

    return CSP_ERR_NONE;
}

void task_router(void *p1, void *p2, void *p3) {
    while (1) {
        csp_route_work();
    }
}

void task_server(void *p1, void *p2, void *p3) {
    csp_socket_t sock = {0};

    csp_bind(&sock, CSP_ANY); // This needs more explanation
    csp_listen(&sock, 5);

    printk("Server listening on port %d\r\n", SERVER_PORT);

    while (1) {
        csp_conn_t *conn;
        if ((conn = csp_accept(&sock, 10000)) == NULL) {
            // Timeout, no connection
            continue;
        }

        csp_packet_t *packet;
        while ((packet = csp_read(conn, 50)) != NULL) {
            switch (csp_conn_dport(conn)) {
            case SERVER_PORT: 
                printk("Packet received on SERVER_PORT: %.*s\n", packet->length, (char *)packet->data);
                csp_buffer_free(packet);
                break;    
            default:
                csp_service_handler(packet);
                break;
            }
        }

        csp_close(conn);
    }
}

void task_client(void *p1, void *p2, void *p3) {

    printk("Client started\r\n");

    while (1) {
        k_msleep(2000);
        int server_address = 1;
        printf("Pinging...\r\n");
        int result = csp_ping(server_address, 1000, 100, CSP_O_NONE);
        printk("Ping address: %u, result %d [ms]\r\n", server_address, result);
    }
}

int start_router() {
    k_tid_t tid = k_thread_create(
        &router_thread,
        router_stack,
        K_THREAD_STACK_SIZEOF(router_stack),
        task_router, 
        NULL, NULL, NULL, 
        5, 
        0, 
        K_NO_WAIT
    );

    printk("Router thread started with ID: %p\r\n", tid);

    return 0;
}

int start_server() {
    k_tid_t tid = k_thread_create(
        &server_thread,
        server_stack,
        K_THREAD_STACK_SIZEOF(server_stack),
        task_server, 
        NULL, NULL, NULL, 
        5, 
        0, 
        K_NO_WAIT
    );

    printk("Server thread started with ID: %p\r\n", tid);
    csp_iflist_print();

    return 0;
}

int start_client() {
    k_tid_t tid = k_thread_create(
        &client_thread,
        client_stack,
        K_THREAD_STACK_SIZEOF(client_stack),
        task_client, 
        NULL, NULL, NULL, 
        5, 
        0, 
        K_NO_WAIT
    );

    printk("Client thread started with ID: %p\r\n", tid);

    return 0;
}

int main(void) {

    csp_init();

    setup_uart_kiss_interface(&iface);
    iface->is_default = 1;

    start_router();
    start_server();
    start_client();

    // Do forever
    while (1) {
        k_msleep(sleep_time_ms);
    }
}
