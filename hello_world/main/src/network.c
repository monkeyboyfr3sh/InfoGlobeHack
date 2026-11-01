#include "network.h"

#include <string.h>
#include "sdkconfig.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#if CONFIG_EXAMPLE_CONNECT_ETHERNET
#include "esp_eth.h"
#endif

/*
 * Settings come from the protocol_examples_common menuconfig entries
 * ("Example Connection Configuration"): Wi-Fi SSID/password and, when
 * "connect using Ethernet interface" is enabled, the PHY model and pins.
 * We deliberately do not call example_connect(): it blocks forever waiting
 * for an Ethernet IP before starting Wi-Fi, so with no cable the globe would
 * never come online.
 */

static const char *TAG = "NETWORK";

#define ETH_GOT_IP_BIT          BIT0
#define WIFI_GOT_IP_BIT         BIT1
#define WIFI_RETRY_DELAY_US     (5 * 1000 * 1000)

/* Higher route_prio wins the default route: prefer the cable when it's up.
 * (Wi-Fi STA default is 100.) */
#define ETH_ROUTE_PRIO          128

static EventGroupHandle_t s_net_events;
static esp_netif_t *s_wifi_netif;
static esp_timer_handle_t s_wifi_retry_timer;
#if CONFIG_EXAMPLE_CONNECT_ETHERNET
static esp_netif_t *s_eth_netif;
#endif

/* ---------------------------------------------------------------- IP events */

static void on_ip_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    switch (id) {
    case IP_EVENT_ETH_GOT_IP: {
        const ip_event_got_ip_t *e = data;
        ESP_LOGI(TAG, "Ethernet got IP " IPSTR, IP2STR(&e->ip_info.ip));
        xEventGroupSetBits(s_net_events, ETH_GOT_IP_BIT);
        break;
    }
    case IP_EVENT_ETH_LOST_IP:
        ESP_LOGW(TAG, "Ethernet lost IP");
        xEventGroupClearBits(s_net_events, ETH_GOT_IP_BIT);
        break;
    case IP_EVENT_STA_GOT_IP: {
        const ip_event_got_ip_t *e = data;
        ESP_LOGI(TAG, "Wi-Fi got IP " IPSTR, IP2STR(&e->ip_info.ip));
        xEventGroupSetBits(s_net_events, WIFI_GOT_IP_BIT);
        break;
    }
    case IP_EVENT_STA_LOST_IP:
        ESP_LOGW(TAG, "Wi-Fi lost IP");
        xEventGroupClearBits(s_net_events, WIFI_GOT_IP_BIT);
        break;
    default:
        break;
    }
}

/* -------------------------------------------------------------------- Wi-Fi */

static void wifi_retry_cb(void *arg)
{
    esp_err_t err = esp_wifi_connect();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "esp_wifi_connect: %s", esp_err_to_name(err));
    }
}

static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    switch (id) {
    case WIFI_EVENT_STA_START:
        ESP_LOGI(TAG, "Wi-Fi connecting to \"%s\"...", CONFIG_EXAMPLE_WIFI_SSID);
        esp_wifi_connect();
        break;
    case WIFI_EVENT_STA_DISCONNECTED: {
        const wifi_event_sta_disconnected_t *d = data;
        xEventGroupClearBits(s_net_events, WIFI_GOT_IP_BIT);
        ESP_LOGW(TAG, "Wi-Fi disconnected (reason %d), retrying in %d s",
                 d->reason, WIFI_RETRY_DELAY_US / 1000000);
        /* Retry forever, but not in a tight loop. */
        esp_timer_stop(s_wifi_retry_timer);
        esp_timer_start_once(s_wifi_retry_timer, WIFI_RETRY_DELAY_US);
        break;
    }
    default:
        break;
    }
}

static esp_err_t wifi_start(void)
{
    s_wifi_netif = esp_netif_create_default_wifi_sta();

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_cfg));

    const esp_timer_create_args_t timer_args = {
        .callback = wifi_retry_cb,
        .name = "wifi_retry",
    };
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &s_wifi_retry_timer));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi_event, NULL));

    wifi_config_t wifi_cfg = { 0 };
    strlcpy((char *)wifi_cfg.sta.ssid, CONFIG_EXAMPLE_WIFI_SSID, sizeof(wifi_cfg.sta.ssid));
    strlcpy((char *)wifi_cfg.sta.password, CONFIG_EXAMPLE_WIFI_PASSWORD, sizeof(wifi_cfg.sta.password));
    wifi_cfg.sta.threshold.authmode = strlen(CONFIG_EXAMPLE_WIFI_PASSWORD) ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    wifi_cfg.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    wifi_cfg.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;

    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    return esp_wifi_start();
}

/* ----------------------------------------------------------------- Ethernet */

#if CONFIG_EXAMPLE_CONNECT_ETHERNET
static void on_eth_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    switch (id) {
    case ETHERNET_EVENT_CONNECTED:
        ESP_LOGI(TAG, "Ethernet link up");
        break;
    case ETHERNET_EVENT_DISCONNECTED:
        /* DHCP only reports LOST_IP after a long timeout; the address is
         * useless as soon as the cable is out, so stop preferring it now. */
        ESP_LOGW(TAG, "Ethernet link down");
        xEventGroupClearBits(s_net_events, ETH_GOT_IP_BIT);
        break;
    default:
        break;
    }
}

static esp_err_t eth_start(void)
{
#if !CONFIG_EXAMPLE_USE_INTERNAL_ETHERNET
#error "Only the ESP32 internal EMAC (ESP32-Ethernet-Kit) is supported"
#endif
    esp_netif_inherent_config_t base_cfg = ESP_NETIF_INHERENT_DEFAULT_ETH();
    base_cfg.route_prio = ETH_ROUTE_PRIO;
    esp_netif_config_t netif_cfg = {
        .base = &base_cfg,
        .stack = ESP_NETIF_NETSTACK_DEFAULT_ETH,
    };
    s_eth_netif = esp_netif_new(&netif_cfg);
    if (s_eth_netif == NULL) {
        return ESP_ERR_NO_MEM;
    }

    eth_mac_config_t mac_cfg = ETH_MAC_DEFAULT_CONFIG();
    eth_phy_config_t phy_cfg = ETH_PHY_DEFAULT_CONFIG();
    phy_cfg.phy_addr = CONFIG_EXAMPLE_ETH_PHY_ADDR;
    phy_cfg.reset_gpio_num = CONFIG_EXAMPLE_ETH_PHY_RST_GPIO;

    /* Default ESP32 RMII clock: external 50 MHz from the PHY into GPIO0,
     * which is how the Ethernet-Kit is wired. */
    eth_esp32_emac_config_t emac_cfg = ETH_ESP32_EMAC_DEFAULT_CONFIG();
    emac_cfg.smi_gpio.mdc_num = CONFIG_EXAMPLE_ETH_MDC_GPIO;
    emac_cfg.smi_gpio.mdio_num = CONFIG_EXAMPLE_ETH_MDIO_GPIO;

    esp_eth_mac_t *mac = esp_eth_mac_new_esp32(&emac_cfg, &mac_cfg);
#if CONFIG_EXAMPLE_ETH_PHY_IP101
    esp_eth_phy_t *phy = esp_eth_phy_new_ip101(&phy_cfg);
#elif CONFIG_EXAMPLE_ETH_PHY_RTL8201
    esp_eth_phy_t *phy = esp_eth_phy_new_rtl8201(&phy_cfg);
#elif CONFIG_EXAMPLE_ETH_PHY_LAN87XX
    esp_eth_phy_t *phy = esp_eth_phy_new_lan87xx(&phy_cfg);
#elif CONFIG_EXAMPLE_ETH_PHY_DP83848
    esp_eth_phy_t *phy = esp_eth_phy_new_dp83848(&phy_cfg);
#elif CONFIG_EXAMPLE_ETH_PHY_KSZ80XX
    esp_eth_phy_t *phy = esp_eth_phy_new_ksz80xx(&phy_cfg);
#else
    esp_eth_phy_t *phy = esp_eth_phy_new_generic(&phy_cfg);
#endif

    esp_err_t err = ESP_ERR_NO_MEM;
    esp_eth_handle_t eth = NULL;
    if (mac && phy) {
        esp_eth_config_t eth_cfg = ETH_DEFAULT_CONFIG(mac, phy);
        err = esp_eth_driver_install(&eth_cfg, &eth);
    }
    if (err != ESP_OK) {
        if (phy) { phy->del(phy); }
        if (mac) { mac->del(mac); }
        esp_netif_destroy(s_eth_netif);
        s_eth_netif = NULL;
        return err;
    }

    ESP_ERROR_CHECK(esp_netif_attach(s_eth_netif, esp_eth_new_netif_glue(eth)));
    ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, on_eth_event, NULL));
    return esp_eth_start(eth);
}
#endif /* CONFIG_EXAMPLE_CONNECT_ETHERNET */

/* ------------------------------------------------------------------- Public */

esp_err_t network_start(void)
{
    s_net_events = xEventGroupCreate();
    if (s_net_events == NULL) {
        return ESP_ERR_NO_MEM;
    }
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, on_ip_event, NULL));

#if CONFIG_EXAMPLE_CONNECT_ETHERNET
    esp_err_t eth_err = eth_start();
    if (eth_err != ESP_OK) {
        ESP_LOGE(TAG, "Ethernet init failed (%s); continuing with Wi-Fi only",
                 esp_err_to_name(eth_err));
    }
#endif

    return wifi_start();
}

bool network_wait_for_ip(TickType_t timeout)
{
    EventBits_t bits = xEventGroupWaitBits(s_net_events, ETH_GOT_IP_BIT | WIFI_GOT_IP_BIT,
                                           pdFALSE, pdFALSE, timeout);
    return (bits & (ETH_GOT_IP_BIT | WIFI_GOT_IP_BIT)) != 0;
}

static bool netif_ip_string(esp_netif_t *netif, char *buf, size_t len)
{
    esp_netif_ip_info_t ip;
    if (netif == NULL || esp_netif_get_ip_info(netif, &ip) != ESP_OK || ip.ip.addr == 0) {
        return false;
    }
    snprintf(buf, len, IPSTR, IP2STR(&ip.ip));
    return true;
}

const char *network_get_ip_string(char *buf, size_t len)
{
    EventBits_t bits = xEventGroupGetBits(s_net_events);
#if CONFIG_EXAMPLE_CONNECT_ETHERNET
    if ((bits & ETH_GOT_IP_BIT) && netif_ip_string(s_eth_netif, buf, len)) {
        return "Ethernet";
    }
#endif
    if ((bits & WIFI_GOT_IP_BIT) && netif_ip_string(s_wifi_netif, buf, len)) {
        return "Wi-Fi";
    }
    if (len) {
        buf[0] = '\0';
    }
    return NULL;
}
