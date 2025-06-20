#include <stdio.h>
#include <string.h>
#include <esp_log.h>
#include <esp_err.h>
#include <esp_spiffs.h>
#include <cJSON.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <nvs_flash.h>
#include <esp_netif.h>
#include <esp_http_server.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define MAX_APs 20

static const char *TAG = "ESP32_AP_CONFIG";

char config_ssid[32] = "";
char config_pass[64] = "";
char config_ip[16] = "";
char config_gateway[16] = "";
char config_mask[16] = "";
char config_dns[16] = "";
char config_user[32] = "";
char config_userpass[32] = "";

wifi_ap_record_t ap_records[MAX_APs];
uint16_t ap_count = 0;

void init_spiffs(void) {
    ESP_LOGI(TAG, "Inicializando SPIFFS");
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = NULL,
        .max_files = 5,
        .format_if_mount_failed = true
    };
    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Erro ao montar SPIFFS (%s)", esp_err_to_name(ret));
    }
}

void wifi_scan() {
    ESP_LOGI(TAG, "Iniciando WiFi scan...");
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    wifi_scan_config_t scan_config = { .ssid = NULL, .bssid = NULL, .channel = 0, .show_hidden = true };
    ESP_ERROR_CHECK(esp_wifi_scan_start(&scan_config, true));

    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_num(&ap_count));
    if (ap_count > MAX_APs) ap_count = MAX_APs;
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&ap_count, ap_records));

    ESP_LOGI(TAG, "WiFi scan concluído. %d redes encontradas.", ap_count);

    ESP_ERROR_CHECK(esp_wifi_stop());
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "WiFi voltou para modo AP.");
}

esp_err_t root_get_handler(httpd_req_t *req) {
    const char *resp =
        "<html><head><meta charset='UTF-8'></head><body style='text-align:center;'>"
        "<h1>Wi-Fi Manager</h1>"
        "<a href='/config' style='display:block;background:#03A9F4;color:white;padding:12px;margin:10px auto;border-radius:6px;text-decoration:none;'>Configure WiFi</a>"
        "<a href='/exit' style='display:block;background:#03A9F4;color:white;padding:12px;margin:10px auto;border-radius:6px;text-decoration:none;'>Exit</a>"
        "</body></html>";
    return httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
}

esp_err_t config_get_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "Handler /config chamado.");
    char *form = malloc(8192);
    if (!form) {
        ESP_LOGE(TAG, "Falha ao alocar memória para o form.");
        return ESP_FAIL;
    }

    strcpy(form,
        "<html><head><meta charset='UTF-8'>"
        "<script>"
        "function fillSSID(ssid) {"
        "document.getElementById('ssid').value = ssid;"
        "}"
        "</script>"
        "</head><body style='text-align:center;'>"
        "<h2>Configurar WiFi</h2>"
        "<form action='/submit' method='get'>");

    strcat(form, "Redes WiFi Disponíveis:<br>");
    for (int i = 0; i < ap_count; i++) {
        char line[256];
        snprintf(line, sizeof(line),
            "<input type='radio' name='ssid_select' onclick=\"fillSSID('%s')\">%s (RSSI:%d)<br>",
            ap_records[i].ssid, ap_records[i].ssid, ap_records[i].rssi);
        strcat(form, line);
    }

    strcat(form,
        "<br>SSID: <input type='text' id='ssid' name='ssid'><br><br>"
        "Senha WiFi: <input type='password' name='pass'><br><br>"
        "IP Estático: <input type='text' name='ip'><br><br>"
        "Gateway: <input type='text' name='gateway'><br><br>"
        "Máscara: <input type='text' name='mask'><br><br>"
        "DNS: <input type='text' name='dns'><br><br>"
        "Usuário: <input type='text' name='user'><br><br>"
        "Senha de Usuário: <input type='password' name='userpass'><br><br>"
        "<input type='submit' value='Salvar'>"
        "</form><br><a href='/'>Voltar</a></body></html>");

    esp_err_t result = httpd_resp_send(req, form, HTTPD_RESP_USE_STRLEN);
    free(form);
    return result;
}


esp_err_t exit_get_handler(httpd_req_t *req) {
    const char *resp = "<html><body><h2>Saindo do modo de configuração</h2><p>Obrigado!</p></body></html>";
    return httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
}

esp_err_t form_handler(httpd_req_t *req) {
    char buf[512];
    size_t len = httpd_req_get_url_query_len(req) + 1;
    if (len > 1 && len < sizeof(buf)) {
        httpd_req_get_url_query_str(req, buf, len);
        httpd_query_key_value(buf, "ssid", config_ssid, sizeof(config_ssid));
        httpd_query_key_value(buf, "pass", config_pass, sizeof(config_pass));
        httpd_query_key_value(buf, "ip", config_ip, sizeof(config_ip));
        httpd_query_key_value(buf, "gateway", config_gateway, sizeof(config_gateway));
        httpd_query_key_value(buf, "mask", config_mask, sizeof(config_mask));
        httpd_query_key_value(buf, "dns", config_dns, sizeof(config_dns));
        httpd_query_key_value(buf, "user", config_user, sizeof(config_user));
        httpd_query_key_value(buf, "userpass", config_userpass, sizeof(config_userpass));
        ESP_LOGI(TAG, "Configurações recebidas: SSID=%s, IP=%s", config_ssid, config_ip);
    }

    const char *resp = "<html><body><h3>Configurações salvas!</h3><a href='/'>Voltar</a></body></html>";
    return httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
}

void start_web_server() {
    wifi_scan();
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server = NULL;
    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_register_uri_handler(server, &(httpd_uri_t){ .uri = "/", .method = HTTP_GET, .handler = root_get_handler });
        httpd_register_uri_handler(server, &(httpd_uri_t){ .uri = "/config", .method = HTTP_GET, .handler = config_get_handler });
        httpd_register_uri_handler(server, &(httpd_uri_t){ .uri = "/submit", .method = HTTP_GET, .handler = form_handler });
        httpd_register_uri_handler(server, &(httpd_uri_t){ .uri = "/exit", .method = HTTP_GET, .handler = exit_get_handler });
    }
}

void start_wifi_ap() {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_config_t ap_config = {
        .ap = {
            .ssid = "ESP32Config",
            .ssid_len = strlen("ESP32Config"),
            .password = "12345678",
            .max_connection = 4,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK
        }
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "AP iniciado.");
    start_web_server();
}

void app_main(void) {
    ESP_ERROR_CHECK(nvs_flash_init());
    init_spiffs();
    start_wifi_ap();
}
