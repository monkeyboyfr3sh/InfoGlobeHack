#include "network.h"

#include <string.h>
#include "sdkconfig.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "mdns.h"
#if CONFIG_INFOGLOBE_ETHERNET
#include "esp_eth.h"
#include "esp_eth_phy_ip101.h"   // managed component espressif/ip101
#endif

/*
 * Settings: menuconfig -> InfoGlobe Configuration -> Network.
 * Ethernet first: Wi-Fi is only a stand-in. It comes on when Ethernet has no
 * address (none within ETH_FIRST_US of starting, or the link went down and
 * stayed down ETH_GRACE_US) and goes off again once Ethernet has one. Without
 * Ethernet (not built in, or no PHY) Wi-Fi simply runs.
 */

static const char *TAG = "NETWORK";

#define ETH_GOT_IP_BIT          BIT0
#define WIFI_GOT_IP_BIT         BIT1
#define WIFI_RETRY_DELAY_US     (5 * 1000 * 1000)
#define ETH_FIRST_US            (20 * 1000 * 1000)   // at start: how long Ethernet gets before Wi-Fi stands in
#define ETH_GRACE_US            (10 * 1000 * 1000)   // a dropped cable: how long before Wi-Fi stands in
#define ETH_BACK_US             (2 * 1000 * 1000)    // Ethernet back: Wi-Fi goes off after this

/* Higher route_prio wins the default route: prefer the cable when it's up.
 * (Wi-Fi STA default is 100.) */
#define ETH_ROUTE_PRIO          128

static EventGroupHandle_t s_net_events;
static esp_netif_t *s_wifi_netif;
static esp_timer_handle_t s_wifi_retry_timer;
static esp_timer_handle_t s_failover_timer;     // decides, a moment after Ethernet changes, whether Wi-Fi should be on
static volatile bool s_wifi_on;                 // Wi-Fi is wanted (started) right now
#if CONFIG_INFOGLOBE_ETHERNET
static esp_netif_t *s_eth_netif;
#endif

static void failover_in(uint64_t us)
{
    if (!s_failover_timer) return;
    esp_timer_stop(s_failover_timer);
    esp_timer_start_once(s_failover_timer, us);
}

/* ---------------------------------------------------------------- IP events */

static void on_ip_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    switch (id) {
    case IP_EVENT_ETH_GOT_IP: {
        const ip_event_got_ip_t *e = data;
        ESP_LOGI(TAG, "Ethernet got IP " IPSTR, IP2STR(&e->ip_info.ip));
        xEventGroupSetBits(s_net_events, ETH_GOT_IP_BIT);
        failover_in(ETH_BACK_US);                   // Wi-Fi can go off
        break;
    }
    case IP_EVENT_ETH_LOST_IP:
        ESP_LOGW(TAG, "Ethernet lost IP");
        xEventGroupClearBits(s_net_events, ETH_GOT_IP_BIT);
        failover_in(ETH_GRACE_US);
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
        ESP_LOGI(TAG, "Wi-Fi connecting to \"%s\"...", CONFIG_INFOGLOBE_WIFI_SSID);
        esp_wifi_connect();
        break;
    case WIFI_EVENT_STA_DISCONNECTED: {
        const wifi_event_sta_disconnected_t *d = data;
        xEventGroupClearBits(s_net_events, WIFI_GOT_IP_BIT);
        if (!s_wifi_on) break;                      // switched off on purpose: Ethernet is back
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

/* Wi-Fi on or off. Runs on the esp_timer task, never inside an event handler. */
static void wifi_set(bool on)
{
    if (on == s_wifi_on) return;
    s_wifi_on = on;
    if (on) {
        ESP_LOGW(TAG, "No Ethernet: Wi-Fi standing in");
        esp_wifi_start();                           // STA_START connects
    } else {
        ESP_LOGI(TAG, "Ethernet is back: Wi-Fi off");
        esp_timer_stop(s_wifi_retry_timer);
        esp_wifi_disconnect();
        esp_wifi_stop();
        xEventGroupClearBits(s_net_events, WIFI_GOT_IP_BIT);
    }
}

static void failover_cb(void *arg)
{
    bool eth = (xEventGroupGetBits(s_net_events) & ETH_GOT_IP_BIT) != 0;
    wifi_set(!eth);
}

/* Set up Wi-Fi without starting it; wifi_set() turns it on and off. */
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
    strlcpy((char *)wifi_cfg.sta.ssid, CONFIG_INFOGLOBE_WIFI_SSID, sizeof(wifi_cfg.sta.ssid));
    strlcpy((char *)wifi_cfg.sta.password, CONFIG_INFOGLOBE_WIFI_PASSWORD, sizeof(wifi_cfg.sta.password));
    wifi_cfg.sta.threshold.authmode = strlen(CONFIG_INFOGLOBE_WIFI_PASSWORD) ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    wifi_cfg.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    wifi_cfg.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;

    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));

    const esp_timer_create_args_t fo_args = { .callback = failover_cb, .name = "failover" };
    return esp_timer_create(&fo_args, &s_failover_timer);
}

/* ----------------------------------------------------------------- Ethernet */

#if CONFIG_INFOGLOBE_ETHERNET
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
        failover_in(ETH_GRACE_US);
        break;
    default:
        break;
    }
}

static esp_err_t eth_start(void)
{
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
    phy_cfg.phy_addr = CONFIG_INFOGLOBE_ETH_PHY_ADDR;
    phy_cfg.reset_gpio_num = CONFIG_INFOGLOBE_ETH_PHY_RST_GPIO;

    /* Default ESP32 RMII clock: external 50 MHz from the PHY into GPIO0,
     * which is how the Ethernet-Kit is wired. */
    eth_esp32_emac_config_t emac_cfg = ETH_ESP32_EMAC_DEFAULT_CONFIG();
    emac_cfg.clock_config.rmii.clock_mode = EMAC_CLK_EXT_IN;
    emac_cfg.clock_config.rmii.clock_gpio = 0;
    emac_cfg.smi_gpio.mdc_num = CONFIG_INFOGLOBE_ETH_MDC_GPIO;
    emac_cfg.smi_gpio.mdio_num = CONFIG_INFOGLOBE_ETH_MDIO_GPIO;

    esp_eth_mac_t *mac = esp_eth_mac_new_esp32(&emac_cfg, &mac_cfg);
    esp_eth_phy_t *phy = esp_eth_phy_new_ip101(&phy_cfg);

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
#endif /* CONFIG_INFOGLOBE_ETHERNET */

/* ------------------------------------------------------------------- Public */

static char s_hostname[33] = CONFIG_LWIP_LOCAL_HOSTNAME;

bool network_hostname_valid(const char *n)
{
    size_t len = n ? strlen(n) : 0;
    if (len < 1 || len > 32 || n[0] == '-' || n[len - 1] == '-') return false;
    for (size_t i = 0; i < len; i++) {
        char c = n[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) return false;
    }
    return true;
}

const char *network_hostname(void) { return s_hostname; }

static void rename_netif(esp_netif_t *netif)
{
    if (!netif) return;
    esp_netif_set_hostname(netif, s_hostname);
    esp_netif_dhcp_status_t st;
    if (esp_netif_dhcpc_get_status(netif, &st) == ESP_OK && st == ESP_NETIF_DHCP_STARTED) {
        esp_netif_dhcpc_stop(netif);                 // ask again, with the new name: the lease (and address) stays the same
        esp_netif_dhcpc_start(netif);
    }
}

/* The router hears the new name a moment later, so the answer to the save
 * that renamed the globe gets out before the address is asked for again. */
static esp_timer_handle_t s_rename_timer;
static void rename_now(void *arg)
{
#if CONFIG_INFOGLOBE_ETHERNET
    rename_netif(s_eth_netif);
#endif
    rename_netif(s_wifi_netif);
}

void network_set_hostname(const char *name)
{
    const char *want = network_hostname_valid(name) ? name : CONFIG_LWIP_LOCAL_HOSTNAME;
    if (!strcmp(want, s_hostname)) return;
    strlcpy(s_hostname, want, sizeof(s_hostname));
    ESP_LOGI(TAG, "Name on the network: %s", s_hostname);
    if (!s_rename_timer) {
        const esp_timer_create_args_t args = { .callback = rename_now, .name = "rename" };
        esp_timer_create(&args, &s_rename_timer);
    }
    if (s_rename_timer) { esp_timer_stop(s_rename_timer); esp_timer_start_once(s_rename_timer, 1500 * 1000); }
    else rename_now(NULL);
    mdns_hostname_set(s_hostname);                   // fails harmlessly before mDNS has started; start_mdns() uses the name then
}

esp_err_t network_start(void)
{
    s_net_events = xEventGroupCreate();
    if (s_net_events == NULL) {
        return ESP_ERR_NO_MEM;
    }
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, on_ip_event, NULL));

    esp_err_t err = wifi_start();
    if (err != ESP_OK) return err;
#if CONFIG_INFOGLOBE_ETHERNET
    esp_err_t eth_err = eth_start();
    if (eth_err != ESP_OK) {
        ESP_LOGE(TAG, "Ethernet init failed (%s); continuing with Wi-Fi only",
                 esp_err_to_name(eth_err));
        failover_in(1000);                          // Wi-Fi straight away
    } else {
        failover_in(ETH_FIRST_US);                  // give the cable a chance first
    }
#else
    failover_in(1000);
#endif
    return ESP_OK;
}

const char *network_link(void)
{
    EventBits_t bits = s_net_events ? xEventGroupGetBits(s_net_events) : 0;
    return (bits & ETH_GOT_IP_BIT) ? "lan" : (bits & WIFI_GOT_IP_BIT) ? "wifi" : "none";
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

/* Address last shown on the globe; shared by the startup and clock tasks. */
static char s_shown_ip[16];
static portMUX_TYPE s_shown_lock = portMUX_INITIALIZER_UNLOCKED;

static const char *preferred_ip(char *buf, size_t len)
{
    EventBits_t bits = s_net_events ? xEventGroupGetBits(s_net_events) : 0;
#if CONFIG_INFOGLOBE_ETHERNET
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

const char *network_get_ip_string(char *buf, size_t len)
{
    const char *iface = preferred_ip(buf, len);
    taskENTER_CRITICAL(&s_shown_lock);
    strlcpy(s_shown_ip, buf, sizeof(s_shown_ip));
    taskEXIT_CRITICAL(&s_shown_lock);
    return iface;
}

bool network_ip_changed(char *buf, size_t len, const char **iface)
{
    char now[sizeof(s_shown_ip)];
    const char *now_iface = preferred_ip(now, sizeof(now));

    taskENTER_CRITICAL(&s_shown_lock);
    bool changed = strcmp(now, s_shown_ip) != 0;
    if (changed) {
        strlcpy(s_shown_ip, now, sizeof(s_shown_ip));
    }
    taskEXIT_CRITICAL(&s_shown_lock);

    if (changed) {
        strlcpy(buf, now, len);
        *iface = now_iface;
    }
    return changed;
}
