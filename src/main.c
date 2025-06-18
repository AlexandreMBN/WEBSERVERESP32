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

static const char *TAG = "ESP32_AP_CONFIG";

// Variáveis de configuração
char config_ssid[32] = "";
char config_pass[32] = "";
char config_ip[16] = "";
char config_gateway[16] = "";
char config_mask[16] = "";
char config_dns[16] = "";
char config_user[32] = "";
char config_userpass[32] = "";

// WiFi scan results
wifi_ap_record_t ap_records[20];
uint16_t ap_count = 0;

// Inicializa SPIFFS
void init_spiffs() {
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

// Função para escanear redes Wi-Fi
void wifi_scan() {
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    wifi_scan_config_t scan_config = { .ssid = 0, .bssid = 0, .channel = 0, .show_hidden = true };
    ESP_ERROR_CHECK(esp_wifi_scan_start(&scan_config, true));
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_num(&ap_count));
    if (ap_count > 20) ap_count = 20;
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&ap_count, ap_records));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
}

// Salvar configuração no SPIFFS
void save_config_to_spiffs() {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "ssid", config_ssid);
    cJSON_AddStringToObject(root, "pass", config_pass);
    cJSON_AddStringToObject(root, "ip", config_ip);
    cJSON_AddStringToObject(root, "gateway", config_gateway);
    cJSON_AddStringToObject(root, "mask", config_mask);
    cJSON_AddStringToObject(root, "dns", config_dns);
    cJSON_AddStringToObject(root, "user", config_user);
    cJSON_AddStringToObject(root, "userpass", config_userpass);

    char *json_string = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    FILE *f = fopen("/spiffs/config.json", "w");
    if (f) {
        fputs(json_string, f);
        fclose(f);
        ESP_LOGI(TAG, "Configuração salva no SPIFFS.");
    } else {
        ESP_LOGE(TAG, "Erro ao salvar configuração no SPIFFS.");
    }
    free(json_string);
}

// Página de configuração (WiFi scan + form)
esp_err_t config_get_handler(httpd_req_t *req) {
    wifi_scan();

    char form[2048] = "<html><body style='text-align:center;'><h2>Configurar WiFi</h2><form action='/submit' method='get'>";

    strcat(form, "Redes WiFi Disponíveis:<br>");
    for (int i = 0; i < ap_count; i++) {
        char line[128];
        snprintf(line, sizeof(line), "<input type='radio' name='ssid' value='%s'>%s (RSSI:%d)<br>",
                 ap_records[i].ssid, ap_records[i].ssid, ap_records[i].rssi);
        strcat(form, line);
    }

    strcat(form,
        "<br>Senha WiFi: <input type='password' name='pass'><br><br>"
        "IP Estático: <input type='text' name='ip'><br><br>"
        "Gateway: <input type='text' name='gateway'><br><br>"
        "Máscara: <input type='text' name='mask'><br><br>"
        "DNS: <input type='text' name='dns'><br><br>"
        "Usuário: <input type='text' name='user'><br><br>"
        "Senha de Usuário: <input type='password' name='userpass'><br><br>"
        "<input type='submit' value='Salvar'>"
        "</form><br><a href='/'>Voltar</a></body></html>"
    );

    return httpd_resp_send(req, form, HTTPD_RESP_USE_STRLEN);
}

// Processa formulário
esp_err_t form_handler(httpd_req_t *req) {
    char buf[512];
    size_t len = httpd_req_get_url_query_len(req) + 1;

    if (len > 1) {
        httpd_req_get_url_query_str(req, buf, len);

        httpd_query_key_value(buf, "ssid", config_ssid, sizeof(config_ssid));
        httpd_query_key_value(buf, "pass", config_pass, sizeof(config_pass));
        httpd_query_key_value(buf, "ip", config_ip, sizeof(config_ip));
        httpd_query_key_value(buf, "gateway", config_gateway, sizeof(config_gateway));
        httpd_query_key_value(buf, "mask", config_mask, sizeof(config_mask));
        httpd_query_key_value(buf, "dns", config_dns, sizeof(config_dns));
        httpd_query_key_value(buf, "user", config_user, sizeof(config_user));
        httpd_query_key_value(buf, "userpass", config_userpass, sizeof(config_userpass));

        save_config_to_spiffs();
    }

    const char *resp =
        "<html><body style='text-align:center;'><h3>Configurações salvas!</h3><a href='/'>Voltar</a></body></html>";
    return httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
}

// Página inicial
esp_err_t root_get_handler(httpd_req_t *req) {
    const char *resp =
        "<html><body style='text-align:center;'>"
        "<h1>WiFi Manager</h1>"
        "<a href='/config'>Configurar WiFi</a><br><br>"
        "<a href='/exit'>Sair</a>"
        "</body></html>";
    return httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
}

// Página de saída
esp_err_t exit_get_handler(httpd_req_t *req) {
    const char *resp =
        "<html><body style='text-align:center;'>"
        "<h2>Saindo do modo configuração</h2>"
        "<p>Obrigado!</p>"
        "</body></html>";
    return httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
}

// Inicializa servidor
void start_web_server() {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server = NULL;

    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_register_uri_handler(server, &(httpd_uri_t){ .uri = "/", .method = HTTP_GET, .handler = root_get_handler });
        httpd_register_uri_handler(server, &(httpd_uri_t){ .uri = "/config", .method = HTTP_GET, .handler = config_get_handler });
        httpd_register_uri_handler(server, &(httpd_uri_t){ .uri = "/submit", .method = HTTP_GET, .handler = form_handler });
        httpd_register_uri_handler(server, &(httpd_uri_t){ .uri = "/exit", .method = HTTP_GET, .handler = exit_get_handler });
    }
}

// Modo Access Point
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

// Função principal
void app_main() {
    ESP_ERROR_CHECK(nvs_flash_init());
    init_spiffs();
    start_wifi_ap();
}
