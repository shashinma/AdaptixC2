package main

import (
	"encoding/json"
	"fmt"
	"io"
	"log"
	"net/http"
	"net/url"
	"path"
	"strings"

	"github.com/Adaptix-Framework/axc2"
)

// webProxyRewriteJSON enables path-prefixed reverse proxy for SPAs that emit root-absolute /ui/, /api/ URLs and redirects.
const webProxyRewriteJSON = `{"prefix_root_redirects":true,"body_root_prefixes":["/ui/","/api/"]}`

type Teamserver interface {
	TsServiceSendDataAll(service string, data string)
	TsServiceWebProxyRegister(serviceName string, upstreamURL string, upstreamAuthorization string, rewriteConfigJSON string) error
	TsServiceWebProxyUnregister(serviceName string)
	TsClientAPIBaseURL() string
}

type panelConfig struct {
	ServiceName string `json:"service_name"`
	Title       string `json:"title"`
	ProxyHost   string `json:"proxy_host,omitempty"`
	ProxyPort   int    `json:"proxy_port,omitempty"`
	Icon        string `json:"icon,omitempty"`

	// upstream_url: BloodHound CE reachable only from teamserver (e.g. http://bloodhound:8080).
	UpstreamURL string `json:"upstream_url,omitempty"`
	// public_base_url: optional override; otherwise Teamserver.client_api_base_url from profile.
	PublicBaseURL string `json:"public_base_url,omitempty"`
	// upstream_bearer: static BH CE API token; takes priority over bh_username/bh_secret auto-login.
	UpstreamBearer string `json:"upstream_bearer,omitempty"`
	// bh_username + bh_secret: if set and upstream_bearer is empty, plugin auto-logs into BH CE
	// and uses the obtained session_token as upstream_bearer (8h TTL; re-apply settings to refresh).
	BHUsername string `json:"bh_username,omitempty"`
	BHSecret   string `json:"bh_secret,omitempty"`
}

type clientPanelPayload struct {
	URL                    string `json:"url"`
	Title                  string `json:"title"`
	ProxyHost              string `json:"proxy_host,omitempty"`
	ProxyPort              int    `json:"proxy_port,omitempty"`
	Icon                   string `json:"icon,omitempty"`
	AttachTeamserverBearer bool   `json:"attach_teamserver_bearer,omitempty"`
}

type bloodhoundService struct {
	ts          Teamserver
	cfg         panelConfig
	serviceName string
	ready       bool
	sent        bool
}

func trimSlash(s string) string {
	return strings.TrimRight(strings.TrimSpace(s), "/")
}

// mergePublicBaseWithAPIPath: if public_base_url is only scheme+host+port (path empty or "/"),
// append the teamserver API path from TsClientAPIBaseURL() (e.g. /endpoint). Otherwise Gin serves
// /service/webproxy/... at NoRoute → Adaptix HTML 404.
func mergePublicBaseWithAPIPath(ts Teamserver, base string) string {
	base = strings.TrimSpace(base)
	if base == "" {
		return ""
	}
	u, err := url.Parse(base)
	if err != nil {
		return trimSlash(base)
	}
	p := path.Clean("/" + strings.TrimPrefix(u.Path, "/"))
	if p != "/" {
		return trimSlash(base)
	}
	ref, err2 := url.Parse(ts.TsClientAPIBaseURL())
	if err2 != nil || ref.Path == "" || ref.Path == "/" {
		return trimSlash(base)
	}
	u2 := *u
	u2.Path = ref.Path
	return strings.TrimRight(u2.String(), "/")
}

func formatUpstreamAuthorization(raw string) string {
	s := strings.TrimSpace(raw)
	if s == "" {
		return ""
	}
	low := strings.ToLower(s)
	if strings.HasPrefix(low, "bearer ") {
		return s
	}
	return "Bearer " + s
}

func parseConfig(serviceConfig string) panelConfig {
	cfg := panelConfig{
		UpstreamURL: "http://127.0.0.1:8080",
		Title:       "BloodHound CE",
	}
	s := strings.TrimSpace(serviceConfig)
	if s != "" {
		_ = json.Unmarshal([]byte(s), &cfg)
	}
	if cfg.ServiceName == "" {
		cfg.ServiceName = "bloodhound-ce"
	}
	if cfg.Title == "" {
		cfg.Title = "BloodHound CE"
	}
	if strings.TrimSpace(cfg.UpstreamURL) == "" {
		cfg.UpstreamURL = "http://127.0.0.1:8080"
	}
	return cfg
}

func (b *bloodhoundService) effectivePublicBaseURL() string {
	if s := strings.TrimSpace(b.cfg.PublicBaseURL); s != "" {
		return mergePublicBaseWithAPIPath(b.ts, s)
	}
	return trimSlash(b.ts.TsClientAPIBaseURL())
}

func (b *bloodhoundService) recomputeReady() {
	up := strings.TrimSpace(b.cfg.UpstreamURL) != ""
	pub := b.effectivePublicBaseURL() != ""
	b.ready = up && pub
}

// fetchBHSessionToken posts credentials to the BH CE login endpoint and returns the session_token.
func fetchBHSessionToken(upstreamURL, username, secret string) (string, error) {
	body := fmt.Sprintf(`{"login_method":"secret","username":%q,"secret":%q}`, username, secret)
	resp, err := http.Post(trimSlash(upstreamURL)+"/api/v2/login", "application/json", strings.NewReader(body))
	if err != nil {
		return "", fmt.Errorf("login request: %w", err)
	}
	defer resp.Body.Close()
	raw, err := io.ReadAll(resp.Body)
	if err != nil {
		return "", fmt.Errorf("read login response: %w", err)
	}
	var result struct {
		Data struct {
			SessionToken string `json:"session_token"`
		} `json:"data"`
	}
	if err := json.Unmarshal(raw, &result); err != nil {
		return "", fmt.Errorf("parse login response: %w", err)
	}
	if result.Data.SessionToken == "" {
		return "", fmt.Errorf("empty session_token (status %d)", resp.StatusCode)
	}
	return result.Data.SessionToken, nil
}

// resolveUpstreamBearer returns the Authorization value to send to the BH CE upstream.
// Priority: static upstream_bearer > auto-login via bh_username/bh_secret > empty (manual login via web form).
func (b *bloodhoundService) resolveUpstreamBearer() string {
	if s := strings.TrimSpace(b.cfg.UpstreamBearer); s != "" {
		return formatUpstreamAuthorization(s)
	}
	if b.cfg.BHUsername != "" && b.cfg.BHSecret != "" {
		token, err := fetchBHSessionToken(strings.TrimSpace(b.cfg.UpstreamURL), b.cfg.BHUsername, b.cfg.BHSecret)
		if err != nil {
			log.Printf("bloodhound_ce_service: auto-login failed: %v", err)
			return ""
		}
		log.Printf("bloodhound_ce_service: auto-login OK for user %q", b.cfg.BHUsername)
		return "Bearer " + token
	}
	return ""
}

func (b *bloodhoundService) applyWebProxyRegistration() {
	if b.ready {
		if err := b.ts.TsServiceWebProxyRegister(b.serviceName, strings.TrimSpace(b.cfg.UpstreamURL), b.resolveUpstreamBearer(), webProxyRewriteJSON); err != nil {
			log.Printf("bloodhound_ce_service: webproxy register %q: %v", b.serviceName, err)
		}
	} else {
		b.ts.TsServiceWebProxyUnregister(b.serviceName)
	}
}

func (b *bloodhoundService) clientPayload() clientPanelPayload {
	out := clientPanelPayload{
		Title:     b.cfg.Title,
		ProxyHost: b.cfg.ProxyHost,
		ProxyPort: b.cfg.ProxyPort,
		Icon:      b.cfg.Icon,
	}
	if b.ready {
		base := b.effectivePublicBaseURL()
		// Open /ui/ directly: upstream redirects / → /ui; path-prefixed assets and API need server-side rewrite in TcServiceWebProxy.
		out.URL = base + "/service/webproxy/" + b.serviceName + "/ui/"
		out.AttachTeamserverBearer = true
	}
	return out
}

func (b *bloodhoundService) pushConfig() {
	if b.ts == nil {
		return
	}
	payload, err := json.Marshal(b.clientPayload())
	if err != nil {
		return
	}
	b.ts.TsServiceSendDataAll(b.serviceName, string(payload))
	b.sent = true
}

// Call handles service/call: push/reload/config/update merge JSON into cfg.
func (b *bloodhoundService) Call(_operator string, function string, args string) {
	fn := strings.TrimSpace(strings.ToLower(function))
	switch fn {
	case "reauth":
		if strings.TrimSpace(args) != "" {
			var extra panelConfig
			if err := json.Unmarshal([]byte(args), &extra); err == nil {
				if extra.UpstreamURL != "" {
					b.cfg.UpstreamURL = extra.UpstreamURL
				}
				if extra.BHUsername != "" {
					b.cfg.BHUsername = extra.BHUsername
				}
				if extra.BHSecret != "" {
					b.cfg.BHSecret = extra.BHSecret
				}
				if extra.UpstreamBearer != "" {
					b.cfg.UpstreamBearer = extra.UpstreamBearer
				}
			}
		}
		b.recomputeReady()
		b.applyWebProxyRegistration()
		// pushConfig sends TYPE_SERVICE_DATA via WebSocket — this runs AFTER the token is obtained,
		// so the client reloads the panel only once the proxy already has the correct upstreamAuth.
		b.pushConfig()
		return
	case "push", "reload", "config", "update":
		if strings.TrimSpace(args) != "" {
			var extra panelConfig
			if err := json.Unmarshal([]byte(args), &extra); err == nil {
				if extra.Title != "" {
					b.cfg.Title = extra.Title
				}
				if extra.ProxyHost != "" {
					b.cfg.ProxyHost = extra.ProxyHost
				}
				if extra.ProxyPort != 0 {
					b.cfg.ProxyPort = extra.ProxyPort
				}
				if extra.Icon != "" {
					b.cfg.Icon = extra.Icon
				}
				if extra.UpstreamURL != "" {
					b.cfg.UpstreamURL = extra.UpstreamURL
				}
			if extra.PublicBaseURL != "" {
				b.cfg.PublicBaseURL = extra.PublicBaseURL
			}
			if extra.UpstreamBearer != "" {
				b.cfg.UpstreamBearer = extra.UpstreamBearer
			}
			if extra.BHUsername != "" {
				b.cfg.BHUsername = extra.BHUsername
			}
			if extra.BHSecret != "" {
				b.cfg.BHSecret = extra.BHSecret
			}
		}
		}
		b.recomputeReady()
		b.applyWebProxyRegistration()
		b.pushConfig()
	default:
		if !b.sent {
			b.pushConfig()
		}
	}
}

func InitPlugin(ts any, _moduleDir string, serviceConfig string) adaptix.PluginService {
	sender, ok := ts.(Teamserver)
	if !ok {
		return nil
	}
	cfg := parseConfig(serviceConfig)
	b := &bloodhoundService{ts: sender, cfg: cfg, serviceName: cfg.ServiceName}
	b.recomputeReady()
	b.applyWebProxyRegistration()
	// DATA уходит только подключённым; после синка клиента teamserver дергает TsServiceCall(..., "push") для каждого сервиса.
	// Один push при загрузке плагина — если операторы уже онлайн (динамическая загрузка сервиса).
	b.pushConfig()
	return b
}
