package httptransport

import (
	"bytes"
	"context"
	"encoding/json"
	"errors"
	"io"
	"log"
	"net/http"
	"strings"
	"time"

	"haruhidb-go/internal/action"
	"haruhidb-go/internal/app"
)

const (
	defaultMaxBodyBytes int64 = 1 << 20
)

type Config struct {
	Logger *log.Logger
}

func NewHandler(runtimeManager *app.RuntimeManager, cfg Config) http.Handler {
	if runtimeManager == nil {
		panic("runtime manager must not be nil")
	}

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

		raw, err := readLimitedBody(r.Body, defaultMaxBodyBytes)
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
		dbPath, envelopeRaw, extractErr := extractActionEnvelope(raw)
		if extractErr != nil {
			writeJSON(w, http.StatusBadRequest, map[string]any{
				"error": map[string]any{
					"code":    string(action.CodeInvalidRequest),
					"message": extractErr.Error(),
				},
			})
			logRequest(cfg.Logger, r, "", http.StatusBadRequest, start)
			return
		}

		requestID := decodeRequestID(envelopeRaw)
		service, resolvedDBPath, resolveErr := runtimeManager.Resolve(dbPath)
		if resolveErr != nil {
			writeJSON(w, http.StatusBadRequest, map[string]any{
				"error": map[string]any{
					"code":    string(action.CodeInvalidRequest),
					"message": resolveErr.Error(),
				},
			})
			logRequest(cfg.Logger, r, requestID, http.StatusBadRequest, start)
			return
		}

		resp, execErr := service.ExecuteJSON(r.Context(), envelopeRaw)
		if execErr != nil {
			writeSystemError(w, execErr)
			logRequest(cfg.Logger, r, requestID, systemErrorStatus(execErr), start)
			return
		}
		if cfg.Logger != nil {
			cfg.Logger.Printf("action_request request_id=%s db_path=%q", requestID, resolvedDBPath)
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

		raw, err := readLimitedBody(r.Body, defaultMaxBodyBytes)
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

		service, resolvedDBPath, resolveErr := runtimeManager.Resolve(req.DBPath)
		if resolveErr != nil {
			writeJSON(w, http.StatusBadRequest, map[string]any{
				"error": map[string]any{
					"code":    string(action.CodeInvalidRequest),
					"message": resolveErr.Error(),
				},
			})
			logRequest(cfg.Logger, r, req.RequestID, http.StatusBadRequest, start)
			return
		}
		req.DBPath = resolvedDBPath

		result, translateErr := service.TranslateNL(r.Context(), req)
		if translateErr != nil {
			writeSystemError(w, translateErr)
			logRequest(cfg.Logger, r, req.RequestID, systemErrorStatus(translateErr), start)
			return
		}

		writeJSON(w, http.StatusOK, result)
		logRequest(cfg.Logger, r, result.RequestID, http.StatusOK, start)
	})

	mux.HandleFunc("/v1/capabilities", func(w http.ResponseWriter, r *http.Request) {
		start := time.Now()
		if r.Method != http.MethodGet {
			writeMethodNotAllowed(w, http.MethodGet)
			logRequest(cfg.Logger, r, "", http.StatusMethodNotAllowed, start)
			return
		}

		dbPath := strings.TrimSpace(r.URL.Query().Get("db_path"))
		service, resolvedDBPath, err := runtimeManager.Resolve(dbPath)
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

		tables, _ := listTablesForCapabilities(r.Context(), service)
		payload := map[string]any{
			"ok":              true,
			"db_path":         resolvedDBPath,
			"default_db_path": runtimeManager.DefaultDBPath(),
			"protocol_version": action.DefaultVersion,
			"actions":         action.PublicActionSpecs(),
			"actions_by_mode": map[string]any{
				string(action.ModeReadOnly): map[string]any{
					"names":      action.PublicActionNamesForMode(action.ModeReadOnly),
					"categories": action.PublicActionSpecsByCategoryForMode(action.ModeReadOnly),
				},
				string(action.ModeReadWrite): map[string]any{
					"names":      action.PublicActionNamesForMode(action.ModeReadWrite),
					"categories": action.PublicActionSpecsByCategoryForMode(action.ModeReadWrite),
				},
			},
			"tables": tables,
		}
		writeJSON(w, http.StatusOK, payload)
		logRequest(cfg.Logger, r, "", http.StatusOK, start)
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

func decodeRequestID(raw []byte) string {
	env, err := action.Decode(raw)
	if err != nil {
		return ""
	}
	return env.RequestID
}

func extractActionEnvelope(raw []byte) (dbPath string, envelopeRaw []byte, err error) {
	var payload map[string]json.RawMessage
	decoder := json.NewDecoder(bytes.NewReader(raw))
	decoder.UseNumber()
	if err := decoder.Decode(&payload); err != nil {
		return "", nil, err
	}
	var trailing any
	if err := decoder.Decode(&trailing); err != io.EOF {
		if err == nil {
			return "", nil, errors.New("unexpected trailing JSON input")
		}
		return "", nil, err
	}

	rawDBPath, hasDBPath := payload["db_path"]
	if !hasDBPath {
		return "", raw, nil
	}

	var parsedDBPath string
	if err := decodeStrictJSON(rawDBPath, &parsedDBPath); err != nil {
		return "", nil, errors.New("db_path must be a JSON string")
	}
	delete(payload, "db_path")
	if len(payload) == 0 {
		return "", nil, errors.New("action envelope is required")
	}

	envelopeRaw, err = json.Marshal(payload)
	if err != nil {
		return "", nil, err
	}
	return strings.TrimSpace(parsedDBPath), envelopeRaw, nil
}

func listTablesForCapabilities(ctx context.Context, service *app.ActionService) ([]string, error) {
	if service == nil {
		return nil, errors.New("service is nil")
	}

	payload, err := json.Marshal(map[string]any{
		"version":    action.DefaultVersion,
		"request_id": "caps-list-tables",
		"mode":       action.ModeReadOnly,
		"action":     action.ActionListTables,
		"args":       map[string]any{},
	})
	if err != nil {
		return nil, err
	}

	respRaw, err := service.ExecuteJSON(ctx, payload)
	if err != nil {
		return nil, err
	}

	var parsed struct {
		Ok   bool `json:"ok"`
		Data struct {
			Tables []string `json:"tables"`
		} `json:"data"`
	}
	if err := json.Unmarshal(respRaw, &parsed); err != nil {
		return nil, err
	}
	if !parsed.Ok {
		return nil, nil
	}
	return append([]string(nil), parsed.Data.Tables...), nil
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
