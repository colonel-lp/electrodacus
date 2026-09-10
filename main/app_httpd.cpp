#include "app_httpd.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_http_server.h"

#include "app_settings.hpp"
#include "app_wifi.hpp"
#include "app_mqtt.hpp"
#include "app_ota.hpp"
#include "app_email.hpp"
#include "sbmsData.hpp"
#include "globals.hpp"
#include "util.hpp"
#include "version.hpp"

static httpd_handle_t s_server = nullptr;

// ---------------------------------------------------------------- SSE clients --

#define SSE_MAX_CLIENTS 8
static int s_sse_fds[SSE_MAX_CLIENTS];
// mutated only in the httpd task; read (racily, best-effort) from the main loop
static int s_sse_count = 0;

static void sse_add(int fd)
{
    for(int i = 0; i < SSE_MAX_CLIENTS; i++)
    {
        if(s_sse_fds[i] == fd) return;
    }
    for(int i = 0; i < SSE_MAX_CLIENTS; i++)
    {
        if(s_sse_fds[i] < 0)
        {
            s_sse_fds[i] = fd;
            s_sse_count++;
            return;
        }
    }
}

static void sse_remove(int fd)
{
    for(int i = 0; i < SSE_MAX_CLIENTS; i++)
    {
        if(s_sse_fds[i] == fd)
        {
            s_sse_fds[i] = -1;
            s_sse_count--;
            return;
        }
    }
}

static bool raw_send_all(int fd, const char *buf, size_t len)
{
    size_t sent = 0;
    while(sent < len)
    {
        int r = httpd_socket_send(s_server, fd, buf + sent, len - sent, 0);
        if(r <= 0) return false;
        sent += r;
    }
    return true;
}

int httpd_sse_count()
{
    return s_sse_count;
}

// runs in the httpd task via httpd_queue_work
struct SseJob {
    char *frame;
    size_t len;
};

static void sse_broadcast_work(void *arg)
{
    SseJob *job = static_cast<SseJob *>(arg);

    for(int i = 0; i < SSE_MAX_CLIENTS; i++)
    {
        int fd = s_sse_fds[i];
        if(fd < 0) continue;
        if(!raw_send_all(fd, job->frame, job->len))
        {
            sse_remove(fd);
            httpd_sess_trigger_close(s_server, fd);
        }
    }

    free(job->frame);
    delete job;
}

void httpd_push_sbms(const char *json, size_t len)
{
    if(!s_server || s_sse_count <= 0) return;

    // frame: "event: sbms\ndata: <json>\n\n"
    const char *pfx = "event: sbms\ndata: ";
    const char *sfx = "\n\n";
    size_t flen = strlen(pfx) + len + strlen(sfx);

    char *frame = static_cast<char *>(malloc(flen + 1));
    if(!frame) return;
    memcpy(frame, pfx, strlen(pfx));
    memcpy(frame + strlen(pfx), json, len);
    memcpy(frame + strlen(pfx) + len, sfx, strlen(sfx));
    frame[flen] = 0;

    SseJob *job = new SseJob{frame, flen};
    if(httpd_queue_work(s_server, sse_broadcast_work, job) != ESP_OK)
    {
        free(frame);
        delete job;
    }
}

static esp_err_t eData_get_handler(httpd_req_t *req)
{
    int fd = httpd_req_to_sockfd(req);
    if(fd < 0) return ESP_FAIL;

    static const char headers[] =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/event-stream\r\n"
        "Cache-Control: no-cache\r\n"
        "Connection: keep-alive\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "\r\n";
    static const char hello[] = "retry: 1000\ndata: hello there!\n\n";

    if(!raw_send_all(fd, headers, sizeof(headers) - 1)) return ESP_FAIL;
    raw_send_all(fd, hello, sizeof(hello) - 1);

    sse_add(fd);
    return ESP_OK;
}

// --------------------------------------------------------------- static files --

static const char *content_type_for(const char *path)
{
    const char *dot = strrchr(path, '.');
    if(!dot) return "text/plain";
    if(!strcmp(dot, ".html")) return "text/html";
    if(!strcmp(dot, ".js"))   return "application/javascript";
    if(!strcmp(dot, ".css"))  return "text/css";
    if(!strcmp(dot, ".json")) return "application/json";
    if(!strcmp(dot, ".ico"))  return "image/x-icon";
    if(!strcmp(dot, ".svg"))  return "image/svg+xml";
    if(!strcmp(dot, ".png"))  return "image/png";
    return "text/plain";
}

static esp_err_t send_file(httpd_req_t *req, const char *fspath, const char *ctype,
                           bool gzip, const char *cache_control)
{
    FILE *f = fopen(fspath, "rb");
    if(!f) return ESP_ERR_NOT_FOUND;

    httpd_resp_set_type(req, ctype);
    if(gzip) httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    if(cache_control) httpd_resp_set_hdr(req, "Cache-Control", cache_control);

    char buf[1024];
    size_t n;
    esp_err_t err = ESP_OK;
    while((n = fread(buf, 1, sizeof(buf), f)) > 0)
    {
        if(httpd_resp_send_chunk(req, buf, n) != ESP_OK) { err = ESP_FAIL; break; }
    }
    fclose(f);
    if(err == ESP_OK) httpd_resp_send_chunk(req, nullptr, 0);
    return err;
}

// strip a "?query" suffix and reject path traversal
static bool sanitize_uri(const char *uri, std::string &out)
{
    out = uri;
    size_t q = out.find('?');
    if(q != std::string::npos) out.resize(q);
    if(out.find("..") != std::string::npos) return false;
    return true;
}

static esp_err_t static_get_handler(httpd_req_t *req)
{
    std::string uri;
    if(!sanitize_uri(req->uri, uri))
    {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Not found");
        return ESP_OK;
    }

    if(uri == "/" ) uri = "/index.html";

    std::string base = "/spiffs/www" + uri;
    std::string gz = base + ".gz";
    const char *ctype = content_type_for(uri.c_str());

    if(send_file(req, gz.c_str(), ctype, true, "max-age=600") == ESP_OK) return ESP_OK;
    if(send_file(req, base.c_str(), ctype, false, "max-age=600") == ESP_OK) return ESP_OK;

    httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Not found");
    return ESP_OK;
}

static esp_err_t root_get_handler(httpd_req_t *req)
{
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/index.html");
    httpd_resp_send(req, nullptr, 0);
    return ESP_OK;
}

// Legacy standalone dashboard (pre-SPA). Kept for backwards compatibility;
// served verbatim from data/web/sbms.html, which pulls /rawData itself.
static esp_err_t sbms_html_get_handler(httpd_req_t *req)
{
    if(send_file(req, "/spiffs/web/sbms.html", "text/html", false, nullptr) != ESP_OK)
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Not found");
    return ESP_OK;
}

static esp_err_t cfg_get_handler(httpd_req_t *req)
{
    std::string uri;
    if(!sanitize_uri(req->uri, uri))
    {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Not found");
        return ESP_OK;
    }
    std::string fspath = "/spiffs" + uri; // /cfg/xxx -> /spiffs/cfg/xxx
    if(send_file(req, fspath.c_str(), "application/json", false, nullptr) != ESP_OK)
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Not found");
    return ESP_OK;
}

static esp_err_t rawData_get_handler(httpd_req_t *req)
{
    std::string dump = varStore.dumpVars();
    httpd_resp_set_type(req, "text/javascript");
    httpd_resp_send(req, dump.data(), dump.size());
    return ESP_OK;
}

static esp_err_t dummyData_get_handler(httpd_req_t *req)
{
    if(send_file(req, "/spiffs/testdata", "text/javascript", false, nullptr) != ESP_OK)
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Not found");
    return ESP_OK;
}

static esp_err_t version_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_sendstr(req, GIT_VERSION);
    return ESP_OK;
}

static esp_err_t debug_get_handler(httpd_req_t *req)
{
    UBaseType_t num_tasks = uxTaskGetNumberOfTasks();
    TaskStatus_t *tasks = static_cast<TaskStatus_t *>(malloc(num_tasks * sizeof(TaskStatus_t)));
    if(!tasks)
    {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "oom");
        return ESP_OK;
    }

    uint32_t run_time = 0;
    UBaseType_t num = uxTaskGetSystemState(tasks, num_tasks, &run_time);
    if(run_time == 0) run_time = 1;
    run_time /= 100;
    if(run_time == 0) run_time = 1;

    static const char *states[] = {"RUN", "RDY", "BLK", "SUS", "DEL", "INV"};

    std::string res;
    res.reserve(64 * num);
    for(UBaseType_t x = 0; x < num; x++)
    {
        unsigned st = tasks[x].eCurrentState;
        if(st > 5) st = 5;
        char line[128];
        snprintf(line, sizeof(line), "%s\t\t%s\t\t%u\t\t%lu\t\t%lu%%\r\n",
                 tasks[x].pcTaskName,
                 states[st],
                 (unsigned)tasks[x].uxBasePriority,
                 (unsigned long)tasks[x].ulRunTimeCounter,
                 (unsigned long)(tasks[x].ulRunTimeCounter / run_time));
        res += line;
    }
    free(tasks);

    httpd_resp_set_type(req, "text/plain");
    httpd_resp_send(req, res.data(), res.size());
    return ESP_OK;
}

// ---------------------------------------------------------------------- /cfg POST

static esp_err_t recv_body(httpd_req_t *req, std::string &out)
{
    out.clear();
    int remaining = req->content_len;
    int timeouts = 0;
    char buf[512];
    while(remaining > 0)
    {
        int r = httpd_req_recv(req, buf, sizeof(buf) < (size_t)remaining ? sizeof(buf) : remaining);
        if(r == HTTPD_SOCK_ERR_TIMEOUT)
        {
            if(++timeouts > 3) return ESP_FAIL;
            continue;
        }
        if(r <= 0) return ESP_FAIL;
        timeouts = 0;
        out.append(buf, r);
        remaining -= r;
    }
    return ESP_OK;
}

static esp_err_t cfg_post_handler(httpd_req_t *req)
{
    std::string uri;
    sanitize_uri(req->uri, uri);

    std::string body;
    if(recv_body(req, body) != ESP_OK)
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "recv");
        return ESP_OK;
    }

    const char *path = nullptr;
    if(uri == "/cfg/wifi")       path = "/spiffs/cfg/wifi";
    else if(uri == "/cfg/mqtt")  path = "/spiffs/cfg/mqtt";
    else if(uri == "/cfg/data")  path = "/spiffs/cfg/data";
    else if(uri == "/cfg/sys")   path = "/spiffs/cfg/sys";
    else if(uri == "/cfg/email") path = "/spiffs/cfg/email";

    if(!path)
    {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Not found");
        return ESP_OK;
    }

    file_write_all(path, body.data(), body.size());
    httpd_resp_sendstr(req, "saved");

    if(uri == "/cfg/wifi")
    {
        settings_read_wifi();
        wifi_mark_settings_changed();
    }
    else if(uri == "/cfg/mqtt")
    {
        settings_read_mqtt();
        mqtt_mark_settings_changed();
    }
    else if(uri == "/cfg/data")
    {
        settings_read_data();
    }
    else if(uri == "/cfg/sys")
    {
        settings_read_system();
    }
    else if(uri == "/cfg/email")
    {
        settings_read_email();
    }
    return ESP_OK;
}

// ------------------------------------------------------------------- /update OTA

static esp_err_t update_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    if(g_settings.system_ota_limit && millis() > 300000)
    {
        httpd_resp_sendstr(req, "OTA time limit is passed. Please reboot your esp32.");
    }
    else
    {
        httpd_resp_sendstr(req,
            "<form method='POST' action='/update' enctype='multipart/form-data'>"
            "<input type='file' name='update'><input type='submit' value='Update'></form>");
    }
    return ESP_OK;
}

static bool find_boundary(httpd_req_t *req, std::string &delim)
{
    size_t len = httpd_req_get_hdr_value_len(req, "Content-Type");
    if(len == 0) return false;
    std::string ct(len + 1, '\0');
    if(httpd_req_get_hdr_value_str(req, "Content-Type", &ct[0], len + 1) != ESP_OK) return false;
    ct.resize(len); // drop the trailing NUL written by the helper

    size_t p = ct.find("boundary=");
    if(p == std::string::npos) return false;
    std::string b = ct.substr(p + 9);
    // strip optional quotes / trailing params
    if(!b.empty() && b.front() == '"') b = b.substr(1, b.find('"', 1) - 1);
    size_t sc = b.find(';');
    if(sc != std::string::npos) b.resize(sc);

    delim = "\r\n--" + b; // delimiter that precedes the closing boundary
    return !b.empty();
}

static esp_err_t update_post_handler(httpd_req_t *req)
{
    std::string delim;
    if(!find_boundary(req, delim))
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "no boundary");
        return ESP_OK;
    }

    bool headers_done = false;
    bool is_spiffs = false;
    bool started = false;
    bool finished = false;
    std::string head;      // accumulates the part header block
    std::string pending;   // streaming buffer that may hold a partial trailing boundary

    int remaining = req->content_len;
    int timeouts = 0;
    char buf[1024];

    while(remaining > 0 && !finished)
    {
        int want = sizeof(buf) < (size_t)remaining ? (int)sizeof(buf) : remaining;
        int r = httpd_req_recv(req, buf, want);
        if(r == HTTPD_SOCK_ERR_TIMEOUT)
        {
            if(++timeouts > 3) break;
            continue;
        }
        if(r <= 0) break;
        timeouts = 0;
        remaining -= r;

        const char *in = buf;
        size_t in_len = r;

        if(!headers_done)
        {
            head.append(in, in_len);
            size_t hp = head.find("\r\n\r\n");
            if(hp == std::string::npos)
            {
                if(head.size() > 4096) break; // runaway
                continue;
            }

            // parse filename="..."
            std::string hs = head.substr(0, hp);
            size_t fp = hs.find("filename=\"");
            if(fp != std::string::npos)
            {
                size_t fs = fp + 10;
                size_t fe = hs.find('"', fs);
                std::string fname = hs.substr(fs, fe - fs);
                is_spiffs = str_starts_with(fname, "spiffs");
            }

            started = ota_begin(is_spiffs ? OtaType::Filesystem : OtaType::Firmware);
            headers_done = true;

            // remainder after the header block feeds the streaming path
            pending.assign(head, hp + 4, std::string::npos);
            head.clear();
        }
        else
        {
            pending.append(in, in_len);
        }

        if(!headers_done) continue;

        size_t bp = pending.find(delim);
        if(bp != std::string::npos)
        {
            if(started && bp > 0) ota_write(reinterpret_cast<const uint8_t *>(pending.data()), bp);
            finished = true;
            break;
        }

        // keep back delim.size() bytes in case the boundary straddles reads
        if(pending.size() > delim.size())
        {
            size_t flush = pending.size() - delim.size();
            if(started) ota_write(reinterpret_cast<const uint8_t *>(pending.data()), flush);
            pending.erase(0, flush);
        }
    }

    if(!finished && started && !pending.empty())
    {
        size_t bp = pending.find(delim);
        size_t n = (bp == std::string::npos) ? pending.size() : bp;
        if(n > 0) ota_write(reinterpret_cast<const uint8_t *>(pending.data()), n);
    }

    bool ok = started && ota_end();

    // reboot after any successful OTA so the new image takes effect immediately
    g_should_reboot = ok;

    httpd_resp_set_hdr(req, "Connection", "close");
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_sendstr(req, ok ? "OK" : "FAIL");
    return ESP_OK;
}

// ------------------------------------------------------------------ /email/*

static esp_err_t email_status_get_handler(httpd_req_t *req)
{
    char buf[256];
    email_last_status(buf, sizeof(buf));
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_sendstr(req, buf);
    return ESP_OK;
}

// Trigger a status e-mail right now (bypasses the once-a-day scheduling), using
// the latest SBMS values. Poll /email/status afterwards for the outcome.
static esp_err_t email_test_post_handler(httpd_req_t *req)
{
    std::string raw = varStore.getVar("sbms");
    uint8_t soc = 0;
    int16_t temp = 0;
    if(!raw.empty())
    {
        SbmsData sbms(raw);
        soc = sbms.stateOfChargePercent;
        temp = sbms.temperatureInternalTenthC;
    }
    bool dispatched = email_send_status(soc, temp);

    httpd_resp_set_type(req, "text/plain");
    httpd_resp_sendstr(req, dispatched ? "dispatched - poll /email/status"
                                       : "not dispatched - a send is already running");
    return ESP_OK;
}

// ------------------------------------------------------------------ /cells/reset

// Clear the rolling 24 h per-cell voltage min/max. The main loop performs the
// actual reset on its next SBMS frame so all history state stays single-task.
static esp_err_t cells_reset_handler(httpd_req_t *req)
{
    g_cell_minmax_reset = true;
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_sendstr(req, "ok");
    return ESP_OK;
}

// ----------------------------------------------------------------------- /reboot

static esp_err_t reboot_handler(httpd_req_t *req)
{
    httpd_resp_set_hdr(req, "Connection", "close");
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_sendstr(req, "rebooting");

    // main loop picks this up and restarts after a 1s grace period
    g_should_reboot = true;
    return ESP_OK;
}

// -------------------------------------------------------------------- registration

static void reg(const char *uri, httpd_method_t method, esp_err_t (*h)(httpd_req_t *))
{
    httpd_uri_t u = {};
    u.uri = uri;
    u.method = method;
    u.handler = h;
    httpd_register_uri_handler(s_server, &u);
}

void httpd_start()
{
    for(int i = 0; i < SSE_MAX_CLIENTS; i++) s_sse_fds[i] = -1;

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.max_uri_handlers = 20;
    config.max_open_sockets = 10;
    config.stack_size = 8192;
    config.lru_purge_enable = true;
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.recv_wait_timeout = 10;
    config.send_wait_timeout = 10;

    if(httpd_start(&s_server, &config) != ESP_OK) return;

    // specific routes first, wildcard static last
    reg("/eData",      HTTP_GET,  eData_get_handler);
    reg("/sbms.html",  HTTP_GET,  sbms_html_get_handler);
    reg("/rawData",    HTTP_GET,  rawData_get_handler);
    reg("/dummyData",  HTTP_GET,  dummyData_get_handler);
    reg("/version",    HTTP_GET,  version_get_handler);
    reg("/debug",      HTTP_GET,  debug_get_handler);
    reg("/update",       HTTP_GET,  update_get_handler);
    reg("/update",       HTTP_POST, update_post_handler);
    reg("/email/status", HTTP_GET,  email_status_get_handler);
    reg("/email/test",   HTTP_POST, email_test_post_handler);
    reg("/reboot",       HTTP_GET,  reboot_handler);
    reg("/reboot",       HTTP_POST, reboot_handler);
    reg("/cells/reset",  HTTP_POST, cells_reset_handler);
    reg("/cfg/*",      HTTP_POST, cfg_post_handler);
    reg("/cfg/*",      HTTP_GET,  cfg_get_handler);
    reg("/",           HTTP_GET,  root_get_handler);
    reg("/*",          HTTP_GET,  static_get_handler);
}
