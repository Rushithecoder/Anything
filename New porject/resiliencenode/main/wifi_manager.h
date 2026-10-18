#ifndef RESILIENCE_NODE_WIFI_MANAGER_H
#define RESILIENCE_NODE_WIFI_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
#include "esp_netif.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RN_AP_PASSWORD            "resilience2024"
#define RN_AP_MAX_CONNECTIONS     4
#define RN_AP_CHANNEL             1

#define RN_MAX_SSID_LEN           32
#define RN_MAX_PASSWORD_LEN       64
#define RN_MAX_IP_STR_LEN         16

#define RN_BACKOFF_MIN_SEC        1
#define RN_BACKOFF_MAX_SEC        30

/**
 * @brief Initialize AP+STA concurrent WiFi subsystems.
 *        Constructs AP SSID from MAC, loads STA credentials from NVS,
 *        and initializes backoff reconnection machinery.
 * 
 * @return ESP_OK on success.
 */
esp_err_t wifi_manager_init(void);

/**
 * @brief Save target WiFi station credentials to NVS and initiate connection.
 * 
 * @param ssid Target WiFi SSID (null-terminated).
 * @param password Target WiFi password (null-terminated).
 * @return ESP_OK on success.
 */
esp_err_t wifi_manager_set_credentials(const char *ssid, const char *password);

/**
 * @brief Retrieve currently configured STA credentials from NVS.
 * 
 * @param ssid_out Destination buffer for SSID.
 * @param ssid_len Size of SSID buffer.
 * @param pass_out Destination buffer for password.
 * @param pass_len Size of password buffer.
 * @return ESP_OK on success, or ESP_ERR_NVS_NOT_FOUND if unconfigured.
 */
esp_err_t wifi_manager_get_credentials(char *ssid_out, size_t ssid_len, char *pass_out, size_t pass_len);

/**
 * @brief Manually trigger STA connection using current configuration.
 * 
 * @return ESP_OK on success.
 */
esp_err_t wifi_manager_connect(void);

/**
 * @brief Manually disconnect STA from target AP.
 * 
 * @return ESP_OK on success.
 */
esp_err_t wifi_manager_disconnect(void);

/**
 * @brief Check whether STA is currently connected and has an assigned IP.
 * 
 * @return true if associated and IP assigned, false otherwise.
 */
bool wifi_manager_is_sta_connected(void);

/**
 * @brief Get current WiFi STA RSSI signal strength.
 * 
 * @return RSSI in dBm (e.g. -65), or 0 if not connected.
 */
int8_t wifi_manager_get_rssi(void);

/**
 * @brief Get current STA IPv4 address as a string.
 * 
 * @param ip_buf Destination buffer.
 * @param max_len Maximum length of destination buffer.
 * @return ESP_OK on success, or ESP_ERR_INVALID_STATE if no IP.
 */
esp_err_t wifi_manager_get_sta_ip(char *ip_buf, size_t max_len);

/**
 * @brief Get SoftAP IPv4 address as a string.
 * 
 * @param ip_buf Destination buffer.
 * @param max_len Maximum length of destination buffer.
 * @return ESP_OK on success.
 */
esp_err_t wifi_manager_get_ap_ip(char *ip_buf, size_t max_len);

/**
 * @brief Get active SoftAP SSID (e.g. "ResilienceNode-B4F2").
 * 
 * @param ssid_buf Destination buffer.
 * @param max_len Maximum length of destination buffer.
 * @return ESP_OK on success.
 */
esp_err_t wifi_manager_get_ap_ssid(char *ssid_buf, size_t max_len);

/**
 * @brief Get number of consecutive reconnection attempts.
 * 
 * @return Number of attempts.
 */
uint32_t wifi_manager_get_reconnect_attempts(void);

/**
 * @brief Get current exponential backoff duration in seconds.
 * 
 * @return Backoff delay (1 to 30 seconds).
 */
uint32_t wifi_manager_get_current_backoff_sec(void);

/**
 * @brief Get pointer to STA netif instance.
 */
esp_netif_t* wifi_manager_get_sta_netif(void);

/**
 * @brief Get pointer to AP netif instance.
 */
esp_netif_t* wifi_manager_get_ap_netif(void);

#ifdef __cplusplus
}
#endif

#endif /* RESILIENCE_NODE_WIFI_MANAGER_H */
