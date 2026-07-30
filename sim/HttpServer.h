#pragma once

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "GalleryPage.h"
#include "PanelPage.h"
#include "wordclock/WebApi.h"

namespace sim {

// Winziger HTTP-Server, damit die Fallback-Seite im Browser geoeffnet werden
// kann, ohne dass ein Geraet in der Naehe ist.
//
// Beantwortet wird ausschliesslich ueber wordclock::WebApi -- also genau der
// Code, der auch auf dem ESP8266 laeuft. Hier steckt nur das Lesen und
// Schreiben auf dem Netz.
class HttpServer {
public:
    using ActionHandler = void (*)(wordclock::WebAction, void* ctx);

    // Der Frame wird fuer die Uhrenansicht mitgegeben -- eine reine
    // Simulator-Zugabe, die es auf dem Geraet nicht gibt.
    bool begin(int port, wordclock::WebApi* api, const wordclock::Frame* frame = nullptr,
               ActionHandler onAction = nullptr, void* ctx = nullptr) {
        api_ = api;
        frame_ = frame;
        onAction_ = onAction;
        ctx_ = ctx;

        listen_ = ::socket(AF_INET, SOCK_STREAM, 0);
        if (listen_ < 0) return false;

        int yes = 1;
        ::setsockopt(listen_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = htons(uint16_t(port));

        if (::bind(listen_, (sockaddr*)&addr, sizeof(addr)) < 0 || ::listen(listen_, 4) < 0) {
            ::close(listen_);
            listen_ = -1;
            return false;
        }

        // Nicht blockierend, damit die Hauptschleife weiterlaeuft -- eine
        // stehende Uhr waere ein schlechter Simulator.
        ::fcntl(listen_, F_SETFL, O_NONBLOCK);
        port_ = port;
        return true;
    }

    ~HttpServer() {
        if (listen_ >= 0) ::close(listen_);
    }

    int port() const { return port_; }

    // Alle wartenden Verbindungen abarbeiten, nicht nur eine.
    //
    // Bei einer pro Durchlauf wuerde die Seite den Server ueberholen: sie
    // fragt zweimal je 100 ms, die Schleife laeuft aber nur zehnmal je
    // Sekunde. Der Rueckstau waechst, und die Ansicht bleibt stehen.
    void poll(uint32_t nowMs) {
        if (listen_ < 0 || !api_) return;
        nowMs_ = nowMs;
        for (int i = 0; i < 16; ++i)
            if (!serveOne()) break;
    }

private:
    bool serveOne() {
        const int client = ::accept(listen_, nullptr, nullptr);
        if (client < 0) return false;

        std::string raw;
        char chunk[2048];
        ssize_t n;
        while ((n = ::recv(client, chunk, sizeof(chunk), 0)) > 0) {
            raw.append(chunk, size_t(n));
            if (raw.find("\r\n\r\n") != std::string::npos) {
                // Bei POST auf den vollstaendigen Rumpf warten.
                const size_t headerEnd = raw.find("\r\n\r\n") + 4;
                const size_t want = contentLength(raw);
                if (raw.size() - headerEnd >= want) break;
            }
        }
        if (raw.empty()) {
            ::close(client);
            return true;
        }

        std::string method, path, body;
        parse(raw, method, path, body);

        // Simulator-eigene Wege zuerst.
        if (path == "/panel") {
            send(client, 200, "text/html", panelPage(), std::strlen(panelPage()));
            ::close(client);
            return true;
        }
        if (path == "/gallery") {
            send(client, 200, "text/html", galleryPage(), std::strlen(galleryPage()));
            ::close(client);
            return true;
        }
        if (path == "/api/gallery") {
            const std::string json = gallery_.framesJson(nowMs_);
            send(client, 200, "application/json", json.c_str(), json.size());
            ::close(client);
            return true;
        }
        // Kennung, an der die Seite erkennt, dass sie im Simulator laeuft.
        // Auf dem Geraet gibt es weder /panel noch /gallery -- die Links
        // wuerden dort ins Leere zeigen.
        if (path == "/api/sim") {
            const char* body = "{\"simulator\":true}";
            send(client, 200, "application/json", body, std::strlen(body));
            ::close(client);
            return true;
        }
        if (path == "/api/frame" && frame_) {
            const std::string json = frameJson(*frame_);
            send(client, 200, "application/json", json.c_str(), json.size());
            ::close(client);
            return true;
        }

        wordclock::WebRequest req{method.c_str(), path.c_str(), body.c_str()};
        wordclock::WebResponse res;
        const wordclock::WebAction action = api_->handle(req, res, buffer_, sizeof(buffer_));

        if (res.serveIndexPage)
            send(client, 200, "text/html", wordclock::WebApi::indexPage(),
                 wordclock::WebApi::indexPageLength());
        else
            send(client, res.status, res.contentType, res.body ? res.body : "", res.length);

        ::close(client);

        if (action != wordclock::WebAction::None && onAction_) onAction_(action, ctx_);
        return true;
    }

    static size_t contentLength(const std::string& raw) {
        const size_t p = raw.find("Content-Length:");
        if (p == std::string::npos) return 0;
        return size_t(std::strtoul(raw.c_str() + p + 15, nullptr, 10));
    }

    static void parse(const std::string& raw, std::string& method, std::string& path,
                      std::string& body) {
        const size_t sp1 = raw.find(' ');
        const size_t sp2 = raw.find(' ', sp1 + 1);
        if (sp1 == std::string::npos || sp2 == std::string::npos) return;
        method = raw.substr(0, sp1);
        path = raw.substr(sp1 + 1, sp2 - sp1 - 1);

        const size_t q = path.find('?');
        if (q != std::string::npos) path.resize(q);

        const size_t headerEnd = raw.find("\r\n\r\n");
        if (headerEnd != std::string::npos) body = raw.substr(headerEnd + 4);
    }

    static void send(int client, int status, const char* type, const char* body, size_t len) {
        char head[256];
        const int n = std::snprintf(head, sizeof(head),
                                    "HTTP/1.1 %d OK\r\nContent-Type: %s\r\n"
                                    "Content-Length: %zu\r\nConnection: close\r\n"
                                    "Cache-Control: no-store\r\n\r\n",
                                    status, type, len);
        ::send(client, head, size_t(n), 0);
        if (len) ::send(client, body, len, 0);
    }

    int listen_ = -1;
    int port_ = 0;
    wordclock::WebApi* api_ = nullptr;
    const wordclock::Frame* frame_ = nullptr;
    Gallery gallery_;
    uint32_t nowMs_ = 0;
    ActionHandler onAction_ = nullptr;
    void* ctx_ = nullptr;
    char buffer_[wordclock::kWebBufferSize] = {};
};

}  // namespace sim
