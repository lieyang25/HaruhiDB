package httptransport

import (
	"bytes"
	"context"
	"encoding/json"
	"errors"
	"io"
	"log"
	"net"
	"net/http"
	"strings"
	"sync"
	"time"

	"haruhidb-go/internal/action"
	"haruhidb-go/internal/app"
)

const (
	defaultMaxBodyBytes int64 = 1 << 20
)

type Config struct {
	MaxBodyBytes       int64
	AuthToken          string
	RateLimitPerMinute int
	TrustProxyHeaders  bool
	Logger             *log.Logger
}

func NewHandler(service *app.ActionService, cfg Config) http.Handler {
	if cfg.MaxBodyBytes <= 0 {
		cfg.MaxBodyBytes = defaultMaxBodyBytes
	}
	limiter := newRateLimiter(cfg.RateLimitPerMinute, cfg.TrustProxyHeaders)

	uiFileServer, uiErr := buildUIFileServer()
	if uiErr != nil {
		if cfg.Logger != nil {
			cfg.Logger.Printf("ui assets init failed: %v", uiErr)
		}
		uiFileServer = http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
			writeSystemError(w, uiErr)
		})
	}
	uiAssetsHandler := http.StripPrefix("/ui/", uiFileServer)

	mux := http.NewServeMux()
	mux.HandleFunc("/", func(w http.ResponseWriter, r *http.Request) {
		start := time.Now()
		if r.URL.Path != "/" {
			http.NotFound(w, r)
			logRequest(cfg.Logger, r, "", http.StatusNotFound, start)
			return
		}
		if r.Method != http.MethodGet {
			writeMethodNotAllowed(w, http.MethodGet)
			logRequest(cfg.Logger, r, "", http.StatusMethodNotAllowed, start)
			return
		}
		http.Redirect(w, r, "/ui", http.StatusFound)
		logRequest(cfg.Logger, r, "", http.StatusFound, start)
	})

	mux.HandleFunc("/ui", func(w http.ResponseWriter, r *http.Request) {
		if r.Method != http.MethodGet {
			writeMethodNotAllowed(w, http.MethodGet)
			return
		}
		if r.URL.Path != "/ui" {
			http.NotFound(w, r)
			return
		}
		indexHTML, err := uiAssets.ReadFile("ui/index.html")
		if err != nil {
			writeSystemError(w, err)
			return
		}
		w.Header().Set("Content-Type", "text/html; charset=utf-8")
		w.WriteHeader(http.StatusOK)
		_, _ = w.Write(indexHTML)
	})

	mux.HandleFunc("/ui/", func(w http.ResponseWriter, r *http.Request) {
		if r.Method != http.MethodGet {
			writeMethodNotAllowed(w, http.MethodGet)
			return
		}
		uiAssetsHandler.ServeHTTP(w, r)
	})
	mux.HandleFunc("/healthz", func(w http.ResponseWriter, r *http.Request) {
		start := time.Now()
		if r.Method != http.MethodGet {
			writeMethodNotAllowed(w, http.MethodGet)
			logRequest(cfg.Logger, r, "", http.StatusMethodNotAllowed, start)
			return
		}
		if !authorizeRequest(w, r, cfg.AuthToken) {
			logRequest(cfg.Logger, r, "", http.StatusUnauthorized, start)
			return
		}
		if !limiter.allow(r) {
			writeJSON(w, http.StatusTooManyRequests, map[string]any{
				"error": map[string]any{
					"code":    "RATE_LIMITED",
					"message": "rate limit exceeded",
				},
			})
			logRequest(cfg.Logger, r, "", http.StatusTooManyRequests, start)
			return
		}
		writeJSON(w, http.StatusOK, map[string]any{"ok": true})
		logRequest(cfg.Logger, r, "", http.StatusOK, start)
	})

	mux.HandleFunc("/v1/action", func(w http.ResponseWriter, r *http.Request) {
		start := time.Now()
		if r.Method != http.MethodPost {
			writeMethodNotAllowed(w, http.MethodPost)
			logRequest(cfg.Logger, r, "", http.StatusMethodNotAllowed, start)
			return
		}
		if !authorizeRequest(w, r, cfg.AuthToken) {
			logRequest(cfg.Logger, r, "", http.StatusUnauthorized, start)
			return
		}
		if !limiter.allow(r) {
			writeJSON(w, http.StatusTooManyRequests, map[string]any{
				"error": map[string]any{
					"code":    "RATE_LIMITED",
					"message": "rate limit exceeded",
				},
			})
			logRequest(cfg.Logger, r, "", http.StatusTooManyRequests, start)
			return
		}

		raw, err := readLimitedBody(r.Body, cfg.MaxBodyBytes)
		if err != nil {
			writeJSON(w, http.StatusBadRequest, map[string]any{
				"error": map[string]any{
					"code":    string(action.CodeInvalidRequest),
					"message": err.Error(),
				},
			})
			logRequest(cfg.Logger, r, "", http.StatusBadRequest, start)
			return
		}
		requestID := decodeRequestID(raw)

		resp, execErr := service.ExecuteJSON(r.Context(), raw)
		if execErr != nil {
			writeSystemError(w, execErr)
			logRequest(cfg.Logger, r, requestID, systemErrorStatus(execErr), start)
			return
		}
		w.Header().Set("Content-Type", "application/json")
		w.WriteHeader(http.StatusOK)
		_, _ = w.Write(resp)
		logRequest(cfg.Logger, r, requestID, http.StatusOK, start)
	})

	mux.HandleFunc("/v1/nl/translate", func(w http.ResponseWriter, r *http.Request) {
		start := time.Now()
		if r.Method != http.MethodPost {
			writeMethodNotAllowed(w, http.MethodPost)
			logRequest(cfg.Logger, r, "", http.StatusMethodNotAllowed, start)
			return
		}
		if !authorizeRequest(w, r, cfg.AuthToken) {
			logRequest(cfg.Logger, r, "", http.StatusUnauthorized, start)
			return
		}
		if !limiter.allow(r) {
			writeJSON(w, http.StatusTooManyRequests, map[string]any{
				"error": map[string]any{
					"code":    "RATE_LIMITED",
					"message": "rate limit exceeded",
				},
			})
			logRequest(cfg.Logger, r, "", http.StatusTooManyRequests, start)
			return
		}

		raw, err := readLimitedBody(r.Body, cfg.MaxBodyBytes)
		if err != nil {
			writeJSON(w, http.StatusBadRequest, map[string]any{
				"error": map[string]any{
					"code":    string(action.CodeInvalidRequest),
					"message": err.Error(),
				},
			})
			logRequest(cfg.Logger, r, "", http.StatusBadRequest, start)
			return
		}

		var req app.NLRequest
		if err := decodeStrictJSON(raw, &req); err != nil {
			writeJSON(w, http.StatusBadRequest, map[string]any{
				"error": map[string]any{
					"code":    string(action.CodeInvalidRequest),
					"message": err.Error(),
				},
			})
			logRequest(cfg.Logger, r, "", http.StatusBadRequest, start)
			return
		}

		result, translateErr := service.TranslateNL(r.Context(), req)
		if translateErr != nil {
			writeSystemError(w, translateErr)
			logRequest(cfg.Logger, r, req.RequestID, systemErrorStatus(translateErr), start)
			return
		}

		writeJSON(w, http.StatusOK, result)
		logRequest(cfg.Logger, r, result.RequestID, http.StatusOK, start)
	})

	return mux
}

func writeSystemError(w http.ResponseWriter, err error) {
	status := systemErrorStatus(err)
	writeJSON(w, status, map[string]any{
		"error": map[string]any{
			"code":    "INTERNAL",
			"message": err.Error(),
		},
	})
}

func systemErrorStatus(err error) int {
	if errors.Is(err, context.Canceled) || errors.Is(err, context.DeadlineExceeded) {
		return http.StatusGatewayTimeout
	}
	return http.StatusInternalServerError
}

func writeMethodNotAllowed(w http.ResponseWriter, allowed string) {
	w.Header().Set("Allow", allowed)
	writeJSON(w, http.StatusMethodNotAllowed, map[string]any{
		"error": map[string]any{
			"code":    "METHOD_NOT_ALLOWED",
			"message": "method not allowed",
		},
	})
}

func writeJSON(w http.ResponseWriter, status int, payload any) {
	data, err := json.Marshal(payload)
	if err != nil {
		http.Error(w, err.Error(), http.StatusInternalServerError)
		return
	}
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(status)
	_, _ = w.Write(data)
}

func readLimitedBody(reader io.ReadCloser, maxBytes int64) ([]byte, error) {
	defer reader.Close()
	limited := io.LimitReader(reader, maxBytes+1)
	data, err := io.ReadAll(limited)
	if err != nil {
		return nil, err
	}
	if int64(len(data)) > maxBytes {
		return nil, errors.New("request body too large")
	}
	return data, nil
}

func decodeStrictJSON(raw []byte, target any) error {
	decoder := json.NewDecoder(bytes.NewReader(raw))
	decoder.DisallowUnknownFields()
	decoder.UseNumber()
	if err := decoder.Decode(target); err != nil {
		return err
	}
	var trailing any
	if err := decoder.Decode(&trailing); err != io.EOF {
		if err == nil {
			return errors.New("unexpected trailing JSON input")
		}
		return err
	}
	return nil
}

func authorizeRequest(w http.ResponseWriter, r *http.Request, token string) bool {
	token = strings.TrimSpace(token)
	if token == "" {
		return true
	}

	authHeader := strings.TrimSpace(r.Header.Get("Authorization"))
	expected := "Bearer " + token
	if authHeader == expected {
		return true
	}

	writeJSON(w, http.StatusUnauthorized, map[string]any{
		"error": map[string]any{
			"code":    "UNAUTHORIZED",
			"message": "missing or invalid bearer token",
		},
	})
	return false
}

func decodeRequestID(raw []byte) string {
	env, err := action.Decode(raw)
	if err != nil {
		return ""
	}
	return env.RequestID
}

func logRequest(logger *log.Logger, r *http.Request, requestID string, statusCode int, startedAt time.Time) {
	if logger == nil {
		return
	}
	duration := time.Since(startedAt).Milliseconds()
	if strings.TrimSpace(requestID) != "" {
		logger.Printf("method=%s path=%s request_id=%s status=%d latency_ms=%d", r.Method, r.URL.Path, requestID, statusCode, duration)
		return
	}
	logger.Printf("method=%s path=%s status=%d latency_ms=%d", r.Method, r.URL.Path, statusCode, duration)
}

type rateLimiter struct {
	mu                sync.Mutex
	limit             int
	window            time.Duration
	trustProxyHeaders bool
	counters          map[string]rateCounter
}

type rateCounter struct {
	windowStart time.Time
	count       int
}

func newRateLimiter(perMinute int, trustProxyHeaders bool) *rateLimiter {
	if perMinute <= 0 {
		return &rateLimiter{trustProxyHeaders: trustProxyHeaders}
	}
	return &rateLimiter{
		limit:             perMinute,
		window:            time.Minute,
		trustProxyHeaders: trustProxyHeaders,
		counters:          make(map[string]rateCounter),
	}
}

func (l *rateLimiter) allow(r *http.Request) bool {
	if l == nil || l.limit <= 0 {
		return true
	}
	key := clientIP(r, l.trustProxyHeaders)
	if key == "" {
		key = "unknown"
	}

	now := time.Now()
	l.mu.Lock()
	defer l.mu.Unlock()

	counter := l.counters[key]
	if counter.windowStart.IsZero() || now.Sub(counter.windowStart) >= l.window {
		counter.windowStart = now
		counter.count = 0
	}
	if counter.count >= l.limit {
		l.counters[key] = counter
		return false
	}
	counter.count++
	l.counters[key] = counter
	return true
}

func clientIP(r *http.Request, trustProxyHeaders bool) string {
	if trustProxyHeaders {
		forwarded := strings.TrimSpace(r.Header.Get("X-Forwarded-For"))
		if forwarded != "" {
			parts := strings.Split(forwarded, ",")
			if len(parts) > 0 {
				return strings.TrimSpace(parts[0])
			}
		}
		realIP := strings.TrimSpace(r.Header.Get("X-Real-IP"))
		if realIP != "" {
			return realIP
		}
	}
	remoteAddr := strings.TrimSpace(r.RemoteAddr)
	if remoteAddr == "" {
		return ""
	}
	host, _, err := net.SplitHostPort(remoteAddr)
	if err == nil {
		return strings.TrimSpace(host)
	}
	return remoteAddr
}
