#ifndef RESILIENCE_NODE_WEB_SERVER_H
#define RESILIENCE_NODE_WEB_SERVER_H

#include "esp_err.h"
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize and start the HTTP server with dashboard and REST API endpoints.
 * 
 * @return ESP_OK on success, or appropriate error code.
 */
esp_err_t web_server_start(void);

/**
 * @brief Stop the HTTP server and release resources.
 * 
 * @return ESP_OK on success.
 */
esp_err_t web_server_stop(void);

/**
 * @brief Check if the HTTP server is currently running.
 * 
 * @return true if running, false otherwise.
 */
bool web_server_is_running(void);

#ifdef __cplusplus
}
#endif

#endif /* RESILIENCE_NODE_WEB_SERVER_H */
