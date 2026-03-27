package connector

import (
	"bytes"
	"compress/gzip"
	"errors"
	"fmt"
	"io"
	"net/http"
	"net/http/httputil"
	"net/url"
	"path"
	"strconv"
	"strings"

	"github.com/gin-gonic/gin"
)

// maxServiceWebProxyRewriteBody caps buffering for HTML/CSS/JS rewrite (Location rewrite has no such cap).
const maxServiceWebProxyRewriteBody = 64 << 20

var errSkipServiceWebProxyBodyRewrite = errors.New("skip service webproxy body rewrite")

func joinUpstreamPath(base *url.URL, rel string) string {
	bp := base.Path
	if rel == "" || rel == "/" {
		if bp == "" {
			return "/"
		}
		return bp
	}
	if !strings.HasPrefix(rel, "/") {
		rel = "/" + rel
	}
	if bp == "" {
		return rel
	}
	bp = strings.TrimSuffix(bp, "/")
	return bp + rel
}

func proxyPathForRequest(endpoint string, target *url.URL, service string, fullPath string) string {
	prefix := endpoint + "/service/webproxy/" + service
	if !strings.HasPrefix(fullPath, prefix) {
		return joinUpstreamPath(target, "/")
	}
	rest := fullPath[len(prefix):]
	if rest != "" && !strings.HasPrefix(rest, "/") {
		rest = "/" + rest
	}
	return joinUpstreamPath(target, rest)
}

func normalizeAPIEndpoint(ep string) string {
	s := strings.TrimSpace(ep)
	if s == "" {
		return ""
	}
	if !strings.HasPrefix(s, "/") {
		s = "/" + s
	}
	return strings.TrimSuffix(s, "/")
}

func serviceWebProxyPublicPrefix(endpoint string, service string) string {
	return normalizeAPIEndpoint(endpoint) + "/service/webproxy/" + service
}

// rewriteServiceWebProxyLocation maps upstream root-relative or same-host redirects into the proxied path prefix.
func rewriteServiceWebProxyLocation(loc string, proxyPrefix string) string {
	loc = strings.TrimSpace(loc)
	if loc == "" || proxyPrefix == "" {
		return loc
	}
	if strings.HasPrefix(loc, "//") {
		return loc
	}
	if strings.HasPrefix(loc, proxyPrefix+"/") || loc == proxyPrefix {
		return loc
	}
	if strings.HasPrefix(loc, "/") {
		return proxyPrefix + loc
	}
	u, err := url.Parse(loc)
	if err != nil || u.Scheme == "" || u.Host == "" {
		return loc
	}
	pathOnly := u.EscapedPath()
	if pathOnly == "" {
		pathOnly = "/"
	}
	out := proxyPrefix + pathOnly
	if u.RawQuery != "" {
		out += "?" + u.RawQuery
	}
	if u.Fragment != "" {
		out += "#" + u.Fragment
	}
	return out
}

func rewriteServiceWebProxyRefresh(refresh string, proxyPrefix string) string {
	refresh = strings.TrimSpace(refresh)
	if refresh == "" {
		return refresh
	}
	lower := strings.ToLower(refresh)
	idx := strings.Index(lower, "url=")
	if idx < 0 {
		return refresh
	}
	urlPart := strings.TrimSpace(refresh[idx+4:])
	urlPart = strings.TrimPrefix(urlPart, "'")
	urlPart = strings.TrimPrefix(urlPart, `"`)
	urlPart = strings.TrimSuffix(urlPart, "'")
	urlPart = strings.TrimSuffix(urlPart, `"`)
	newLoc := rewriteServiceWebProxyLocation(urlPart, proxyPrefix)
	return refresh[:idx+4] + newLoc
}

func contentTypeNeedsRootPathRewrite(ct string) bool {
	ct = strings.ToLower(strings.TrimSpace(ct))
	if ct == "" {
		return false
	}
	if i := strings.Index(ct, ";"); i >= 0 {
		ct = strings.TrimSpace(ct[:i])
	}
	switch ct {
	case "text/html", "text/css", "text/javascript", "application/javascript", "application/x-javascript",
		"application/ecmascript":
		return true
	default:
		return false
	}
}

// bodyRewriteLeads are prefix patterns before a root path (e.g. /ui/) in HTML/CSS/JS.
var bodyRewriteLeads = [][]byte{
	[]byte(`="`), []byte(`='`), []byte(`"`), []byte(`'`), []byte("`"),
	[]byte(`url(`), []byte(`url("`), []byte(`url('`),
	[]byte(`("`), []byte(`('`),
	[]byte(`fetch("`), []byte(`fetch('`),
}

// bodyRewriteQuoteTerminators are single-char quote bytes used to detect a complete quoted path value.
var bodyRewriteQuoteTerminators = [][]byte{
	[]byte(`"`), []byte(`'`), []byte("`"),
}

// rewriteServiceWebProxyBodyPrefixes rewrites root-absolute paths (per service rootPrefixes) so subresources stay under the webproxy prefix.
func rewriteServiceWebProxyBodyPrefixes(body []byte, proxyPrefix []byte, rootPrefixes []string) []byte {
	if len(body) == 0 || len(proxyPrefix) == 0 || len(rootPrefixes) == 0 {
		return body
	}
	for _, rp := range rootPrefixes {
		pref := strings.TrimSpace(rp)
		if pref == "" || pref[0] != '/' {
			continue
		}
		if !strings.HasSuffix(pref, "/") {
			pref += "/"
		}
		pb := []byte(pref)
		for _, lead := range bodyRewriteLeads {
			from := append(append([]byte(nil), lead...), pb...)
			to := append(append(append([]byte(nil), lead...), proxyPrefix...), pb...)
			body = bytes.ReplaceAll(body, from, to)
		}
		// Also rewrite the prefix without trailing slash when it appears as a complete
		// quoted value (e.g. basename:"/ui" in minified JS bundles).
		prefNoSlash := []byte(strings.TrimSuffix(pref, "/"))
		for _, q := range bodyRewriteQuoteTerminators {
			from := append(append(append([]byte(nil), q...), prefNoSlash...), q...)
			to := append(append(append(append([]byte(nil), q...), proxyPrefix...), prefNoSlash...), q...)
			body = bytes.ReplaceAll(body, from, to)
		}
	}
	return body
}

func readServiceWebProxyBodyForRewrite(resp *http.Response) (raw []byte, wasGzip bool, err error) {
	if resp.Body == nil {
		return nil, false, nil
	}
	if resp.ContentLength > maxServiceWebProxyRewriteBody {
		return nil, false, errSkipServiceWebProxyBodyRewrite
	}
	defer resp.Body.Close()
	raw, err = io.ReadAll(io.LimitReader(resp.Body, maxServiceWebProxyRewriteBody+1))
	if err != nil {
		return nil, false, err
	}
	if len(raw) > maxServiceWebProxyRewriteBody {
		return nil, false, errSkipServiceWebProxyBodyRewrite
	}
	ce := strings.ToLower(resp.Header.Get("Content-Encoding"))
	if strings.Contains(ce, "gzip") {
		gr, errGz := gzip.NewReader(bytes.NewReader(raw))
		if errGz != nil {
			return nil, false, errGz
		}
		dec, errRead := io.ReadAll(io.LimitReader(gr, maxServiceWebProxyRewriteBody+1))
		_ = gr.Close()
		if errRead != nil {
			return nil, false, errRead
		}
		if len(dec) > maxServiceWebProxyRewriteBody {
			return nil, false, errSkipServiceWebProxyBodyRewrite
		}
		return dec, true, nil
	}
	return raw, false, nil
}

// injectSessionTokenScript injects a <script> into HTML that pre-seeds localStorage with the upstream
// session token so SPAs that gate on a stored token (like BloodHound CE) don't show the login form.
// The token is extracted from "Bearer <token>" and embedded in a one-liner IIFE.
func injectSessionTokenScript(body []byte, upstreamAuth string) []byte {
	token := strings.TrimPrefix(strings.TrimSpace(upstreamAuth), "Bearer ")
	token = strings.TrimSpace(token)
	if token == "" {
		return body
	}
	// JWT tokens only contain base64url chars (A-Z a-z 0-9 - _ .) and dots — safe to embed in a JS string.
	token = strings.ReplaceAll(token, `\`, `\\`)
	token = strings.ReplaceAll(token, `"`, `\"`)
	script := fmt.Sprintf(
		`<script>(function(){try{var p=JSON.parse(localStorage.getItem("persistedState")||"{}");`+
			`if(!p.auth)p.auth={};p.auth.sessionToken="%s";`+
			`localStorage.setItem("persistedState",JSON.stringify(p));}catch(e){}})();</script>`,
		token,
	)
	// Inject just before </head>; fall back to <body if not found.
	lower := bytes.ToLower(body)
	insert := []byte("</head>")
	idx := bytes.Index(lower, insert)
	if idx < 0 {
		insert = []byte("<body")
		idx = bytes.Index(lower, insert)
	}
	if idx < 0 {
		return body
	}
	result := make([]byte, 0, len(body)+len(script))
	result = append(result, body[:idx]...)
	result = append(result, []byte(script)...)
	result = append(result, body[idx:]...)
	return result
}

func rewriteServiceWebProxyResponse(resp *http.Response, proxyPrefix string, prefixRootRedirects bool, bodyRootPrefixes []string, upstreamAuth string) error {
	if resp == nil || proxyPrefix == "" {
		return nil
	}
	if resp.StatusCode == http.StatusSwitchingProtocols {
		return nil
	}

	if prefixRootRedirects {
		if loc := resp.Header.Get("Location"); loc != "" {
			resp.Header.Set("Location", rewriteServiceWebProxyLocation(loc, proxyPrefix))
		}
		if ref := resp.Header.Get("Refresh"); ref != "" {
			resp.Header.Set("Refresh", rewriteServiceWebProxyRefresh(ref, proxyPrefix))
		}
	}

	ct := resp.Header.Get("Content-Type")
	ctBase := strings.ToLower(strings.TrimSpace(ct))
	if i := strings.Index(ctBase, ";"); i >= 0 {
		ctBase = strings.TrimSpace(ctBase[:i])
	}
	isHTML := ctBase == "text/html"
	needInject := upstreamAuth != "" && isHTML

	if len(bodyRootPrefixes) == 0 && !needInject {
		return nil
	}

	if !contentTypeNeedsRootPathRewrite(ct) && !needInject {
		return nil
	}
	ce := strings.ToLower(resp.Header.Get("Content-Encoding"))
	if ce != "" && !strings.Contains(ce, "gzip") {
		return nil
	}

	body, wasGzip, err := readServiceWebProxyBodyForRewrite(resp)
	if errors.Is(err, errSkipServiceWebProxyBodyRewrite) {
		return nil
	}
	if err != nil {
		return err
	}
	if len(body) == 0 {
		return nil
	}

	newBody := rewriteServiceWebProxyBodyPrefixes(body, []byte(proxyPrefix), bodyRootPrefixes)
	if needInject {
		newBody = injectSessionTokenScript(newBody, upstreamAuth)
	}
	if wasGzip {
		resp.Header.Del("Content-Encoding")
	}
	resp.Header.Del("Content-Length")
	resp.Header.Del("Transfer-Encoding")
	resp.ContentLength = int64(len(newBody))
	resp.Header.Set("Content-Length", strconv.Itoa(len(newBody)))
	resp.Body = io.NopCloser(bytes.NewReader(newBody))
	return nil
}

func (tc *TsConnector) webProxyServiceName(c *gin.Context) (service string, ok bool) {
	if s := strings.TrimSpace(c.Param("service")); s != "" {
		return s, true
	}
	fp := strings.TrimPrefix(c.Param("filepath"), "/")
	if fp != "" {
		slash := strings.IndexByte(fp, '/')
		if slash < 0 {
			return fp, true
		}
		return fp[:slash], true
	}
	base := normalizeAPIEndpoint(tc.Endpoint)
	if base == "" {
		return "", false
	}
	marker := base + "/service/webproxy/"
	raw := c.Request.URL.Path
	if raw == "" {
		return "", false
	}
	pathStr := path.Clean(raw)
	if !strings.HasPrefix(pathStr, marker) {
		return "", false
	}
	rest := strings.TrimPrefix(strings.TrimPrefix(pathStr, marker), "/")
	if rest == "" {
		return "", false
	}
	slash := strings.IndexByte(rest, '/')
	if slash < 0 {
		return rest, true
	}
	return rest[:slash], true
}

// TcServiceWebProxy reverse-proxies to an upstream registered per service (JWT via api_group).
func (tc *TsConnector) TcServiceWebProxy(c *gin.Context) {
	service, ok := tc.webProxyServiceName(c)
	if !ok || service == "" {
		c.Status(http.StatusNotFound)
		return
	}

	target, upstreamAuth, prefixRootRedirects, bodyRootPrefixes, ok := tc.teamserver.TsServiceWebProxyUpstreamForName(service)
	if !ok {
		c.Status(http.StatusNotFound)
		return
	}

	proxyPrefix := serviceWebProxyPublicPrefix(tc.Endpoint, service)
	needModify := prefixRootRedirects || len(bodyRootPrefixes) > 0 || upstreamAuth != ""

	proxy := httputil.NewSingleHostReverseProxy(target)
	proxy.ErrorHandler = func(w http.ResponseWriter, r *http.Request, err error) {
		_ = err
		w.WriteHeader(http.StatusBadGateway)
	}
	if needModify {
		proxy.ModifyResponse = func(resp *http.Response) error {
			return rewriteServiceWebProxyResponse(resp, proxyPrefix, prefixRootRedirects, bodyRootPrefixes, upstreamAuth)
		}
	}
	proxy.Director = func(req *http.Request) {
		req.URL.Scheme = target.Scheme
		req.URL.Host = target.Host
		req.Host = target.Host
		req.URL.Path = proxyPathForRequest(normalizeAPIEndpoint(tc.Endpoint), target, service, req.URL.Path)
		req.URL.RawPath = ""
		// The Adaptix JWT is validated by gin middleware before this handler.
		// X-Adaptix-Auth carries the Adaptix JWT (set by Qt interceptor) — strip it so upstream never sees it.
		// Authorization is NOT stripped: the browser page may set its own auth header (e.g. BH CE session token).
		req.Header.Del("X-Adaptix-Auth")
		if upstreamAuth != "" {
			// upstream_bearer overrides whatever the browser sent.
			req.Header.Set("Authorization", upstreamAuth)
		}
		if needModify {
			req.Header.Set("Accept-Encoding", "gzip, identity")
		}
	}

	proxy.ServeHTTP(c.Writer, c.Request)
}
