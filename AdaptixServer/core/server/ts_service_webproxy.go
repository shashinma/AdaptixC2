package server

import (
	"encoding/json"
	"errors"
	"fmt"
	"net/url"
	"path"
	"strings"
)

// ServiceWebProxyUpstream is the server-side target for /service/webproxy/{name}/ (URL + optional Authorization for upstream).
type ServiceWebProxyUpstream struct {
	Base          *url.URL
	Authorization string // optional full value for Authorization header sent to upstream (e.g. "Bearer <API token>")

	// Optional response rewriting for path-mounted reverse proxy (configured per service via rewriteConfigJSON at register).
	PrefixRootRedirects bool     // rewrite Location / Refresh root paths to sit under the public webproxy prefix
	BodyRootPrefixes    []string // e.g. ["/ui/","/api/"] — rewrite those root-absolute refs in html/css/js bodies; empty = skip
}

type webProxyRewriteJSON struct {
	PrefixRootRedirects bool     `json:"prefix_root_redirects"`
	BodyRootPrefixes    []string `json:"body_root_prefixes"`
}

// TsServiceWebProxyRegister registers reverse proxy target. upstreamAuthorization is optional; empty means no Authorization to upstream.
// rewriteConfigJSON: optional JSON {"prefix_root_redirects":bool,"body_root_prefixes":["/path/",...]}; empty string = transparent proxy (no rewrites).
func (ts *Teamserver) TsServiceWebProxyRegister(serviceName string, upstreamURL string, upstreamAuthorization string, rewriteConfigJSON string) error {
	u, err := url.Parse(strings.TrimSpace(upstreamURL))
	if err != nil || u.Scheme == "" || u.Host == "" {
		return errors.New("invalid upstream_url")
	}
	if u.Scheme != "http" && u.Scheme != "https" {
		return errors.New("upstream_url must be http or https")
	}
	entry := ServiceWebProxyUpstream{
		Base:          u,
		Authorization: strings.TrimSpace(upstreamAuthorization),
	}
	if s := strings.TrimSpace(rewriteConfigJSON); s != "" {
		var rw webProxyRewriteJSON
		if err := json.Unmarshal([]byte(s), &rw); err != nil {
			return fmt.Errorf("invalid webproxy rewrite_config json: %w", err)
		}
		entry.PrefixRootRedirects = rw.PrefixRootRedirects
		if len(rw.BodyRootPrefixes) > 0 {
			entry.BodyRootPrefixes = append([]string(nil), rw.BodyRootPrefixes...)
		}
	}
	ts.serviceWebProxyMu.Lock()
	defer ts.serviceWebProxyMu.Unlock()
	if ts.serviceWebProxyUpstreams == nil {
		ts.serviceWebProxyUpstreams = make(map[string]ServiceWebProxyUpstream)
	}
	ts.serviceWebProxyUpstreams[serviceName] = entry
	return nil
}

func (ts *Teamserver) TsServiceWebProxyUnregister(serviceName string) {
	ts.serviceWebProxyMu.Lock()
	defer ts.serviceWebProxyMu.Unlock()
	delete(ts.serviceWebProxyUpstreams, serviceName)
}

// TsServiceWebProxyUpstreamForName returns upstream URL, auth, and optional rewrite policy for the connector reverse proxy.
func (ts *Teamserver) TsServiceWebProxyUpstreamForName(serviceName string) (base *url.URL, upstreamAuthorization string, prefixRootRedirects bool, bodyRootPrefixes []string, ok bool) {
	ts.serviceWebProxyMu.RLock()
	defer ts.serviceWebProxyMu.RUnlock()
	u, exists := ts.serviceWebProxyUpstreams[serviceName]
	if !exists || u.Base == nil {
		return nil, "", false, nil, false
	}
	var prefixes []string
	if len(u.BodyRootPrefixes) > 0 {
		prefixes = append([]string(nil), u.BodyRootPrefixes...)
	}
	return u.Base, u.Authorization, u.PrefixRootRedirects, prefixes, true
}

// TsClientAPIBaseURL returns Teamserver.client_api_base_url from profile when set.
// If unset, returns https://127.0.0.1:{Teamserver.port}{Teamserver.endpoint} so local defaults match typical client profiles.
// For remote/NAT deployments set client_api_base_url explicitly in profile.
//
// If client_api_base_url is only scheme+host+port (path empty or "/"), the teamserver API path prefix
// (Teamserver.endpoint) is appended so URLs stay under the same route group as the rest of the API.
func (ts *Teamserver) TsClientAPIBaseURL() string {
	if ts == nil || ts.Profile == nil || ts.Profile.Server == nil {
		return ""
	}
	srv := ts.Profile.Server
	ep := strings.TrimSpace(srv.Endpoint)
	if ep == "" {
		ep = "/"
	} else if ep[0] != '/' {
		ep = "/" + ep
	}

	if s := strings.TrimSpace(srv.ClientApiBaseURL); s != "" {
		u, err := url.Parse(s)
		if err != nil {
			return strings.TrimRight(strings.TrimSpace(s), "/")
		}
		p := path.Clean("/" + strings.TrimPrefix(u.Path, "/"))
		if p == "/" {
			if ep != "/" {
				u2 := *u
				u2.Path = ep
				return strings.TrimRight(u2.String(), "/")
			}
			return strings.TrimRight(u.String(), "/")
		}
		return strings.TrimRight(u.String(), "/")
	}

	if srv.Port <= 0 {
		return ""
	}
	return fmt.Sprintf("https://127.0.0.1:%d%s", srv.Port, ep)
}
