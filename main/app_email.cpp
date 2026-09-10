#include "app_email.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_heap_caps.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_tls.h"
#include "esp_crt_bundle.h"
#include "mbedtls/base64.h"

#include "app_settings.hpp"

namespace {

// Only one report may be in flight at a time.
volatile bool s_sending = false;

// Human-readable outcome of the most recent attempt, for GET /email/status.
// Written only by set_status() (from the email task); a torn read from the
// httpd task is harmless for diagnostics.
char s_status[224] = "idle - no attempt yet";
uint32_t s_status_ms = 0;

void set_status(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(s_status, sizeof(s_status), fmt, ap);
    va_end(ap);
    s_status_ms = static_cast<uint32_t>(esp_timer_get_time() / 1000);
}

constexpr int kNetTimeoutMs = 10000;

struct Report {
    uint8_t soc;
    int16_t temp_tenth; // internal temperature, tenths of a degree C
};

// A connection that is either a TLS session (em_ssl == true, port 465) or a
// bare TCP socket (em_ssl == false, port 25/587 - credentials go over the wire
// base64-encoded but unencrypted, hence SSL defaults on). Note: STARTTLS is not
// implemented, so em_ssl == true must use an implicit-TLS port (465 for Gmail).
struct Conn {
    esp_tls_t *tls = nullptr;
    int fd = -1;
    bool ssl = false;
};

std::string base64(const std::string &in)
{
    size_t cap = 4 * ((in.size() + 2) / 3) + 1;
    std::string out(cap, '\0');
    size_t olen = 0;
    if(mbedtls_base64_encode(reinterpret_cast<unsigned char *>(&out[0]), cap, &olen,
                             reinterpret_cast<const unsigned char *>(in.data()), in.size()) != 0)
    {
        return std::string();
    }
    out.resize(olen);
    return out;
}

bool conn_open(Conn &c, std::string &detail)
{
    c.ssl = g_settings.em_ssl;
    const std::string &host = g_settings.em_host;
    const int port = static_cast<int>(g_settings.em_port);

    char portstr[8];
    snprintf(portstr, sizeof(portstr), "%d", port);

    // Resolve explicitly first so a DNS failure is unambiguous and so the
    // resolved address can be reported regardless of what fails next.
    struct addrinfo hints = {};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo *res = nullptr;
    int gai = getaddrinfo(host.c_str(), portstr, &hints, &res);
    if(gai != 0 || !res)
    {
        char d[96];
        snprintf(d, sizeof(d), "DNS lookup of '%s' failed (gai=%d) - no DNS server?",
                 host.c_str(), gai);
        detail = d;
        if(res) freeaddrinfo(res);
        return false;
    }

    char ip[48] = "?";
    inet_ntop(AF_INET, &reinterpret_cast<struct sockaddr_in *>(res->ai_addr)->sin_addr, ip, sizeof(ip));

    if(c.ssl)
    {
        freeaddrinfo(res); // esp_tls resolves again internally

        esp_tls_cfg_t cfg = {};
        cfg.timeout_ms = 30000;               // slow links: allow a generous handshake window
        cfg.non_block = false;
        if(g_settings.em_tls_verify)
        {
            cfg.crt_bundle_attach = esp_crt_bundle_attach; // verify against bundled Mozilla CA roots
        }
        else
        {
            // No CA attached: esp-tls falls back to MBEDTLS_SSL_VERIFY_NONE, but
            // only because CONFIG_ESP_TLS_SKIP_SERVER_CERT_VERIFY is enabled.
            cfg.skip_common_name = true;
        }

        c.tls = esp_tls_init();
        if(!c.tls) { detail = "esp_tls_init() failed (out of memory)"; return false; }

        int rc = esp_tls_conn_new_sync(host.c_str(), static_cast<int>(host.size()), port, &cfg, c.tls);
        if(rc == 1) return true;

        // Pull the primary esp_err_t first (esp_tls_get_and_clear_last_error also
        // returns the mbedTLS code + cert flags); the per-type getters clear state.
        esp_err_t le = ESP_OK;
        int mbed_err = 0, cert_flags = 0, sys_err = 0;
        esp_tls_error_handle_t eh = nullptr;
        if(esp_tls_get_error_handle(c.tls, &eh) == ESP_OK && eh)
        {
            le = esp_tls_get_and_clear_last_error(eh, &mbed_err, &cert_flags);
            esp_tls_get_and_clear_error_type(eh, ESP_TLS_ERR_TYPE_SYSTEM, &sys_err);
        }
        char d[200];
        snprintf(d, sizeof(d),
                 "resolved %s; TLS conn rc=%d esp_err=0x%X (%s) mbedtls=-0x%04X cert_flags=0x%X sock_errno=%d",
                 ip, rc, static_cast<unsigned>(le), esp_err_to_name(le),
                 (mbed_err < 0 ? -mbed_err : mbed_err), cert_flags, sys_err);
        detail = d;
        esp_tls_conn_destroy(c.tls);
        c.tls = nullptr;
        return false;
    }

    c.fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if(c.fd < 0) { detail = "socket() failed"; freeaddrinfo(res); return false; }

    struct timeval tv = { kNetTimeoutMs / 1000, 0 };
    setsockopt(c.fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(c.fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    int rc = connect(c.fd, res->ai_addr, res->ai_addrlen);
    int cerr = errno;
    freeaddrinfo(res);
    if(rc != 0)
    {
        char d[128];
        snprintf(d, sizeof(d), "resolved %s; TCP connect failed: errno=%d", ip, cerr);
        detail = d;
        close(c.fd);
        c.fd = -1;
        return false;
    }
    return true;
}

void conn_close(Conn &c)
{
    if(c.tls) { esp_tls_conn_destroy(c.tls); c.tls = nullptr; }
    if(c.fd >= 0) { close(c.fd); c.fd = -1; }
}

bool conn_write(Conn &c, const char *data, size_t len)
{
    size_t off = 0;
    while(off < len)
    {
        int w = c.ssl ? esp_tls_conn_write(c.tls, data + off, len - off)
                      : static_cast<int>(send(c.fd, data + off, len - off, 0));
        if(w < 0)
        {
            if(c.ssl && (w == ESP_TLS_ERR_SSL_WANT_READ || w == ESP_TLS_ERR_SSL_WANT_WRITE)) continue;
            return false;
        }
        off += static_cast<size_t>(w);
    }
    return true;
}

bool conn_write(Conn &c, const std::string &s)
{
    return conn_write(c, s.data(), s.size());
}

int conn_read(Conn &c, char *buf, size_t len)
{
    if(c.ssl) return esp_tls_conn_read(c.tls, buf, len);
    return static_cast<int>(recv(c.fd, buf, len, 0));
}

// Read one SMTP reply, following multi-line "code-text" continuations. Returns
// true iff the status code starts with want_first. The final reply line (code +
// text) is copied into *reply for diagnostics, on both success and failure.
bool smtp_expect(Conn &c, char want_first, std::string *reply)
{
    std::string resp;
    char buf[256];

    for(int guard = 0; guard < 64; guard++)
    {
        int r = conn_read(c, buf, sizeof(buf));
        if(r == 0) { if(reply) *reply = "connection closed by server"; return false; }
        if(r < 0)
        {
            if(c.ssl && (r == ESP_TLS_ERR_SSL_WANT_READ || r == ESP_TLS_ERR_SSL_WANT_WRITE)) continue;
            if(reply) *reply = "socket read error / timeout";
            return false;
        }
        resp.append(buf, static_cast<size_t>(r));

        size_t end = resp.rfind("\r\n");
        if(end == std::string::npos) continue;

        size_t prev = (end == 0) ? std::string::npos : resp.rfind("\r\n", end - 1);
        size_t start = (prev == std::string::npos) ? 0 : prev + 2;

        // The final line of a reply is "NNN <text>"; continuation lines are "NNN-<text>".
        if(end - start >= 4 && resp[start + 3] == ' ')
        {
            if(reply) *reply = resp.substr(start, end - start);
            return resp[start] == want_first;
        }
    }
    if(reply) *reply = "no complete reply within limit";
    return false;
}

bool smtp_cmd(Conn &c, const std::string &line, char want_first, std::string *reply)
{
    if(!conn_write(c, line + "\r\n")) { if(reply) *reply = "socket write failed"; return false; }
    return smtp_expect(c, want_first, reply);
}

bool do_send(const Report &rep)
{
    if(g_settings.em_host.empty() || g_settings.em_to.empty())
    {
        set_status("FAIL: SMTP host or recipient not configured");
        return false;
    }

    const std::string from = g_settings.em_from.empty() ? g_settings.em_user : g_settings.em_from;
    const std::string ehlo_name = g_settings.sta_hostname.empty() ? std::string("esp32")
                                                                  : g_settings.sta_hostname;

    set_status("connecting to %s:%u (%s%s)...", g_settings.em_host.c_str(),
               static_cast<unsigned>(g_settings.em_port),
               g_settings.em_ssl ? "implicit TLS" : "plain",
               (g_settings.em_ssl && !g_settings.em_tls_verify) ? ", cert check OFF" : "");

    Conn c;
    std::string detail;
    if(!conn_open(c, detail))
    {
        set_status("FAIL to reach %s:%u - %s [free heap %u, largest block %u]",
                   g_settings.em_host.c_str(), static_cast<unsigned>(g_settings.em_port),
                   detail.c_str(),
                   static_cast<unsigned>(esp_get_free_heap_size()),
                   static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)));
        return false;
    }

    std::string reply;
    const char *step = "server greeting";
    bool ok = false;
    do {
        if(!smtp_expect(c, '2', &reply)) break;

        step = "EHLO";
        if(!smtp_cmd(c, "EHLO " + ehlo_name, '2', &reply)) break;

        if(!g_settings.em_user.empty())
        {
            step = "AUTH LOGIN";
            if(!smtp_cmd(c, "AUTH LOGIN", '3', &reply)) break;
            step = "AUTH (username)";
            if(!smtp_cmd(c, base64(g_settings.em_user), '3', &reply)) break;
            step = "AUTH (password)";
            if(!smtp_cmd(c, base64(g_settings.em_password), '2', &reply)) break;
        }

        step = "MAIL FROM";
        if(!smtp_cmd(c, "MAIL FROM:<" + from + ">", '2', &reply)) break;
        step = "RCPT TO";
        if(!smtp_cmd(c, "RCPT TO:<" + g_settings.em_to + ">", '2', &reply)) break;
        step = "DATA";
        if(!smtp_cmd(c, "DATA", '3', &reply)) break;

        char body[512];
        int n = snprintf(body, sizeof(body),
            "From: <%s>\r\n"
            "To: <%s>\r\n"
            "Subject: SBMS status message\r\n"
            "Content-Type: text/plain; charset=utf-8\r\n"
            "\r\n"
            "SOC is currently %u%%. Internal temperature is: %.1f degrees.\r\n"
            ".\r\n",
            from.c_str(), g_settings.em_to.c_str(),
            static_cast<unsigned>(rep.soc), rep.temp_tenth / 10.0);
        if(n <= 0 || n >= static_cast<int>(sizeof(body))) { step = "message formatting"; break; }

        step = "message body";
        if(!conn_write(c, body, static_cast<size_t>(n))) { reply = "socket write failed"; break; }
        if(!smtp_expect(c, '2', &reply)) break;

        smtp_cmd(c, "QUIT", '2', &reply);
        ok = true;
    } while(false);

    conn_close(c);

    if(ok) set_status("OK - accepted by %s for <%s>", g_settings.em_host.c_str(), g_settings.em_to.c_str());
    else   set_status("FAIL at [%s]: %s", step, reply.empty() ? "(no server reply)" : reply.c_str());
    return ok;
}

void email_task(void *arg)
{
    Report *rep = static_cast<Report *>(arg);
    do_send(*rep);
    delete rep;
    s_sending = false;
    vTaskDelete(nullptr);
}

} // namespace

bool email_send_status(uint8_t soc_percent, int16_t internal_temp_tenth_c)
{
    if(s_sending) return false;
    s_sending = true;
    set_status("queued...");

    Report *rep = new Report{ soc_percent, internal_temp_tenth_c };
    // 10 KB: the mbedTLS handshake + Mozilla CA bundle search is stack-hungry.
    if(xTaskCreate(email_task, "email", 10240, rep, 4, nullptr) != pdPASS)
    {
        delete rep;
        s_sending = false;
        set_status("FAIL: could not create sender task (low memory?)");
        return false;
    }
    return true;
}

void email_last_status(char *out, size_t n)
{
    if(!out || n == 0) return;
    if(s_status_ms)
    {
        uint32_t age = (static_cast<uint32_t>(esp_timer_get_time() / 1000) - s_status_ms) / 1000;
        snprintf(out, n, "[%us ago] %s", static_cast<unsigned>(age), s_status);
    }
    else
    {
        snprintf(out, n, "%s", s_status);
    }
}
