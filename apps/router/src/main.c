#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/uart.h>
#include <csp/csp.h>
#include <csp/interfaces/csp_if_kiss.h>

#define SW0_NODE DT_ALIAS(sw0)
#define UART_NODE DT_NODELABEL(uart1)
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

static csp_iface_t iface = {0};
static csp_kiss_interface_data_t kiss_data = {0};

void callback(const struct device *port, struct gpio_callback *cb, gpio_port_pins_t pins) {
    printk("Button state changed.\r\n");
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
        // int server_address = 5;
        // printf("Pinging...\r\n");
        // int result = csp_ping(server_address, 1000, 100, CSP_O_NONE);
        // printk("Ping address: %u, result %d [ms]\r\n", server_address, result);
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

int uart_csp_tx(void *driver_data, const uint8_t * data, size_t len) {
    printk("TX %d byte(s):", len);
    for (size_t i = 0; i < len; i++) {
        uart_poll_out(uart_dev, data[i]);
    }
    return CSP_ERR_NONE;
}

void uart_csp_rx(const struct device *dev, void *user_data) {
    uint8_t buf[32];
    int woken = 0;

    if (!uart_irq_update(uart_dev) || !uart_irq_rx_ready(uart_dev))
        return;
    
    int len;
    while ((len = uart_fifo_read(dev, buf, sizeof(buf)))) {
        printk("RX %d byte(s):", len);
        for (int i = 0; i < len; i++) {
            printk(" %02x", buf[i]);
        }
        printk("\r\n");
        csp_kiss_rx(&iface, buf, len, &woken);
    }
}

int main(void) {

    csp_init();

    iface.name = "uart";
    iface.addr = 5;
    iface.is_default = 1;
    iface.interface_data = &kiss_data;
    kiss_data.tx_func = uart_csp_tx;

    csp_kiss_add_interface(&iface);

    start_router();
    start_server();
    start_client();
    uart_irq_callback_user_data_set(uart_dev, uart_csp_rx, NULL);
    uart_irq_rx_enable(uart_dev);

    // Do forever
    while (1) {
        k_msleep(sleep_time_ms);
    }
}
