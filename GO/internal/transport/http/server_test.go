package httptransport

import (
	"bytes"
	"context"
	"encoding/json"
	"io"
	"net/http"
	"net/http/httptest"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"haruhidb-go/haruhidb"
	"haruhidb-go/internal/action"
	"haruhidb-go/internal/app"
	"haruhidb-go/internal/nl"
)

type translatorStub struct {
	output nl.TranslateOutput
}

func (t *translatorStub) Translate(context.Context, nl.TranslateInput) (nl.TranslateOutput, error) {
	return t.output, nil
}

func TestHealthz(t *testing.T) {
	db := openHTTPTestDB(t)
	defer closeHTTPTestDB(t, db)

	service, err := app.NewActionService(app.Config{
		DB:             db,
		RequestTimeout: 5 * time.Second,
	})
	if err != nil {
		t.Fatalf("NewActionService failed: %v", err)
	}

	handler := NewHandler(service, Config{})
	server := httptest.NewServer(handler)
	defer server.Close()

	resp, err := http.Get(server.URL + "/healthz")
	if err != nil {
		t.Fatalf("GET /healthz failed: %v", err)
	}
	defer resp.Body.Close()

	if resp.StatusCode != http.StatusOK {
		t.Fatalf("unexpected status: %d", resp.StatusCode)
	}
}

func TestRootRedirectToUI(t *testing.T) {
	db := openHTTPTestDB(t)
	defer closeHTTPTestDB(t, db)

	service, err := app.NewActionService(app.Config{
		DB:             db,
		RequestTimeout: 5 * time.Second,
	})
	if err != nil {
		t.Fatalf("NewActionService failed: %v", err)
	}

	handler := NewHandler(service, Config{})
	req := httptest.NewRequest(http.MethodGet, "/", nil)
	rr := httptest.NewRecorder()
	handler.ServeHTTP(rr, req)

	if rr.Code != http.StatusFound {
		t.Fatalf("expected 302, got %d", rr.Code)
	}
	if got := rr.Header().Get("Location"); got != "/ui" {
		t.Fatalf("expected Location=/ui, got %q", got)
	}
}

func TestUIRouteServesHTML(t *testing.T) {
	db := openHTTPTestDB(t)
	defer closeHTTPTestDB(t, db)

	service, err := app.NewActionService(app.Config{
		DB:             db,
		RequestTimeout: 5 * time.Second,
	})
	if err != nil {
		t.Fatalf("NewActionService failed: %v", err)
	}

	handler := NewHandler(service, Config{})
	req := httptest.NewRequest(http.MethodGet, "/ui", nil)
	rr := httptest.NewRecorder()
	handler.ServeHTTP(rr, req)

	if rr.Code != http.StatusOK {
		t.Fatalf("expected 200, got %d", rr.Code)
	}
	if got := rr.Header().Get("Content-Type"); !strings.Contains(got, "text/html") {
		t.Fatalf("expected text/html content type, got %q", got)
	}
	if body := rr.Body.String(); !strings.Contains(body, "HaruhiDB Web Console") {
		t.Fatalf("expected UI HTML content, got %q", body)
	}
}

func TestUIAssetsRouteServesJS(t *testing.T) {
	db := openHTTPTestDB(t)
	defer closeHTTPTestDB(t, db)

	service, err := app.NewActionService(app.Config{
		DB:             db,
		RequestTimeout: 5 * time.Second,
	})
	if err != nil {
		t.Fatalf("NewActionService failed: %v", err)
	}

	handler := NewHandler(service, Config{})
	req := httptest.NewRequest(http.MethodGet, "/ui/app.js", nil)
	rr := httptest.NewRecorder()
	handler.ServeHTTP(rr, req)

	if rr.Code != http.StatusOK {
		t.Fatalf("expected 200, got %d", rr.Code)
	}
	if got := rr.Header().Get("Content-Type"); !strings.Contains(got, "javascript") {
		t.Fatalf("expected javascript content type, got %q", got)
	}
	if body := rr.Body.String(); !strings.Contains(body, "postJSON(\"/v1/nl/translate\"") {
		t.Fatalf("expected UI JS content, got %q", body)
	}
}
func TestActionEndpoint(t *testing.T) {
	db := openHTTPTestDB(t)
	defer closeHTTPTestDB(t, db)
	createHTTPUsersTable(t, db)

	service, err := app.NewActionService(app.Config{
		DB:             db,
		RequestTimeout: 5 * time.Second,
		AllowWrite:     false,
	})
	if err != nil {
		t.Fatalf("NewActionService failed: %v", err)
	}

	server := httptest.NewServer(NewHandler(service, Config{}))
	defer server.Close()

	body := []byte(`{
		"version":"v1",
		"request_id":"req-http-list",
		"mode":"read_only",
		"action":"list_tables",
		"args":{}
	}`)
	resp, err := http.Post(server.URL+"/v1/action", "application/json", bytes.NewReader(body))
	if err != nil {
		t.Fatalf("POST /v1/action failed: %v", err)
	}
	defer resp.Body.Close()

	if resp.StatusCode != http.StatusOK {
		t.Fatalf("unexpected status: %d", resp.StatusCode)
	}

	payload, err := io.ReadAll(resp.Body)
	if err != nil {
		t.Fatalf("read response failed: %v", err)
	}
	var envelope action.ResponseEnvelope[map[string]any]
	if err := json.Unmarshal(payload, &envelope); err != nil {
		t.Fatalf("decode response failed: %v", err)
	}
	if !envelope.Ok {
		t.Fatalf("expected success response, got %#v", envelope)
	}
}

func TestTranslateEndpoint(t *testing.T) {
	db := openHTTPTestDB(t)
	defer closeHTTPTestDB(t, db)
	createHTTPUsersTable(t, db)

	translator := &translatorStub{
		output: nl.TranslateOutput{
			Candidate: []byte(`{
				"version":"v1",
				"request_id":"req-http-nl",
				"mode":"read_only",
				"action":"list_tables",
				"args":{}
			}`),
			Model: "stub-model",
		},
	}
	service, err := app.NewActionService(app.Config{
		DB:             db,
		RequestTimeout: 5 * time.Second,
		Translator:     translator,
	})
	if err != nil {
		t.Fatalf("NewActionService failed: %v", err)
	}

	server := httptest.NewServer(NewHandler(service, Config{}))
	defer server.Close()

	reqBody := []byte(`{
		"request_id":"req-http-nl",
		"input":"列出所有表",
		"mode":"read_only"
	}`)
	resp, err := http.Post(server.URL+"/v1/nl/translate", "application/json", bytes.NewReader(reqBody))
	if err != nil {
		t.Fatalf("POST /v1/nl/translate failed: %v", err)
	}
	defer resp.Body.Close()

	if resp.StatusCode != http.StatusOK {
		t.Fatalf("unexpected status: %d", resp.StatusCode)
	}

	raw, err := io.ReadAll(resp.Body)
	if err != nil {
		t.Fatalf("read response failed: %v", err)
	}
	var parsed app.NLResult
	if err := json.Unmarshal(raw, &parsed); err != nil {
		t.Fatalf("decode response failed: %v", err)
	}
	if !parsed.Valid {
		t.Fatalf("expected valid translate response, got %+v", parsed)
	}
	if parsed.Meta["model"] != "stub-model" {
		t.Fatalf("unexpected model meta: %#v", parsed.Meta)
	}
}

func TestActionEndpointAuthToken(t *testing.T) {
	db := openHTTPTestDB(t)
	defer closeHTTPTestDB(t, db)
	createHTTPUsersTable(t, db)

	service, err := app.NewActionService(app.Config{
		DB:             db,
		RequestTimeout: 5 * time.Second,
		AllowWrite:     false,
	})
	if err != nil {
		t.Fatalf("NewActionService failed: %v", err)
	}

	server := httptest.NewServer(NewHandler(service, Config{
		AuthToken: "secret-token",
	}))
	defer server.Close()

	body := []byte(`{
		"version":"v1",
		"request_id":"req-auth",
		"mode":"read_only",
		"action":"list_tables",
		"args":{}
	}`)
	req, err := http.NewRequest(http.MethodPost, server.URL+"/v1/action", bytes.NewReader(body))
	if err != nil {
		t.Fatalf("create request failed: %v", err)
	}
	resp, err := http.DefaultClient.Do(req)
	if err != nil {
		t.Fatalf("do request failed: %v", err)
	}
	defer resp.Body.Close()
	if resp.StatusCode != http.StatusUnauthorized {
		t.Fatalf("expected 401, got %d", resp.StatusCode)
	}
}

func TestActionEndpointRateLimit(t *testing.T) {
	db := openHTTPTestDB(t)
	defer closeHTTPTestDB(t, db)
	createHTTPUsersTable(t, db)

	service, err := app.NewActionService(app.Config{
		DB:             db,
		RequestTimeout: 5 * time.Second,
	})
	if err != nil {
		t.Fatalf("NewActionService failed: %v", err)
	}

	server := httptest.NewServer(NewHandler(service, Config{
		RateLimitPerMinute: 1,
	}))
	defer server.Close()

	body := []byte(`{
		"version":"v1",
		"request_id":"req-rate",
		"mode":"read_only",
		"action":"list_tables",
		"args":{}
	}`)
	resp1, err := http.Post(server.URL+"/v1/action", "application/json", bytes.NewReader(body))
	if err != nil {
		t.Fatalf("first request failed: %v", err)
	}
	resp1.Body.Close()
	if resp1.StatusCode != http.StatusOK {
		t.Fatalf("expected first request 200, got %d", resp1.StatusCode)
	}

	resp2, err := http.Post(server.URL+"/v1/action", "application/json", bytes.NewReader(body))
	if err != nil {
		t.Fatalf("second request failed: %v", err)
	}
	defer resp2.Body.Close()
	if resp2.StatusCode != http.StatusTooManyRequests {
		t.Fatalf("expected second request 429, got %d", resp2.StatusCode)
	}
}

func TestActionEndpointRateLimitIgnoresForwardedHeadersByDefault(t *testing.T) {
	db := openHTTPTestDB(t)
	defer closeHTTPTestDB(t, db)
	createHTTPUsersTable(t, db)

	service, err := app.NewActionService(app.Config{
		DB:             db,
		RequestTimeout: 5 * time.Second,
	})
	if err != nil {
		t.Fatalf("NewActionService failed: %v", err)
	}

	server := httptest.NewServer(NewHandler(service, Config{
		RateLimitPerMinute: 1,
	}))
	defer server.Close()

	body := []byte(`{
		"version":"v1",
		"request_id":"req-rate-default-proxy",
		"mode":"read_only",
		"action":"list_tables",
		"args":{}
	}`)

	req1, err := http.NewRequest(http.MethodPost, server.URL+"/v1/action", bytes.NewReader(body))
	if err != nil {
		t.Fatalf("create first request failed: %v", err)
	}
	req1.Header.Set("Content-Type", "application/json")
	req1.Header.Set("X-Forwarded-For", "203.0.113.10")
	resp1, err := http.DefaultClient.Do(req1)
	if err != nil {
		t.Fatalf("first request failed: %v", err)
	}
	resp1.Body.Close()
	if resp1.StatusCode != http.StatusOK {
		t.Fatalf("expected first request 200, got %d", resp1.StatusCode)
	}

	req2, err := http.NewRequest(http.MethodPost, server.URL+"/v1/action", bytes.NewReader(body))
	if err != nil {
		t.Fatalf("create second request failed: %v", err)
	}
	req2.Header.Set("Content-Type", "application/json")
	req2.Header.Set("X-Forwarded-For", "198.51.100.20")
	resp2, err := http.DefaultClient.Do(req2)
	if err != nil {
		t.Fatalf("second request failed: %v", err)
	}
	defer resp2.Body.Close()
	if resp2.StatusCode != http.StatusTooManyRequests {
		t.Fatalf("expected second request 429, got %d", resp2.StatusCode)
	}
}

func TestActionEndpointRateLimitTrustsForwardedHeadersWhenEnabled(t *testing.T) {
	db := openHTTPTestDB(t)
	defer closeHTTPTestDB(t, db)
	createHTTPUsersTable(t, db)

	service, err := app.NewActionService(app.Config{
		DB:             db,
		RequestTimeout: 5 * time.Second,
	})
	if err != nil {
		t.Fatalf("NewActionService failed: %v", err)
	}

	server := httptest.NewServer(NewHandler(service, Config{
		RateLimitPerMinute: 1,
		TrustProxyHeaders:  true,
	}))
	defer server.Close()

	body := []byte(`{
		"version":"v1",
		"request_id":"req-rate-trust-proxy",
		"mode":"read_only",
		"action":"list_tables",
		"args":{}
	}`)

	req1, err := http.NewRequest(http.MethodPost, server.URL+"/v1/action", bytes.NewReader(body))
	if err != nil {
		t.Fatalf("create first request failed: %v", err)
	}
	req1.Header.Set("Content-Type", "application/json")
	req1.Header.Set("X-Forwarded-For", "203.0.113.10")
	resp1, err := http.DefaultClient.Do(req1)
	if err != nil {
		t.Fatalf("first request failed: %v", err)
	}
	resp1.Body.Close()
	if resp1.StatusCode != http.StatusOK {
		t.Fatalf("expected first request 200, got %d", resp1.StatusCode)
	}

	req2, err := http.NewRequest(http.MethodPost, server.URL+"/v1/action", bytes.NewReader(body))
	if err != nil {
		t.Fatalf("create second request failed: %v", err)
	}
	req2.Header.Set("Content-Type", "application/json")
	req2.Header.Set("X-Forwarded-For", "198.51.100.20")
	resp2, err := http.DefaultClient.Do(req2)
	if err != nil {
		t.Fatalf("second request failed: %v", err)
	}
	resp2.Body.Close()
	if resp2.StatusCode != http.StatusOK {
		t.Fatalf("expected second request 200 for different forwarded client, got %d", resp2.StatusCode)
	}

	req3, err := http.NewRequest(http.MethodPost, server.URL+"/v1/action", bytes.NewReader(body))
	if err != nil {
		t.Fatalf("create third request failed: %v", err)
	}
	req3.Header.Set("Content-Type", "application/json")
	req3.Header.Set("X-Forwarded-For", "198.51.100.20")
	resp3, err := http.DefaultClient.Do(req3)
	if err != nil {
		t.Fatalf("third request failed: %v", err)
	}
	defer resp3.Body.Close()
	if resp3.StatusCode != http.StatusTooManyRequests {
		t.Fatalf("expected third request 429 for same forwarded client, got %d", resp3.StatusCode)
	}
}

func openHTTPTestDB(t *testing.T) *haruhidb.DB {
	t.Helper()
	dir := t.TempDir()
	path := filepath.Join(dir, "http_test.db")
	db, err := haruhidb.Open(path, haruhidb.OpenOptions{})
	if err != nil {
		t.Fatalf("open db failed: %v", err)
	}
	return db
}

func closeHTTPTestDB(t *testing.T, db *haruhidb.DB) {
	t.Helper()
	if db == nil {
		return
	}
	if err := db.Close(); err != nil {
		t.Fatalf("close db failed: %v", err)
	}
}

func createHTTPUsersTable(t *testing.T, db *haruhidb.DB) {
	t.Helper()
	if err := db.CreateTable("users", []haruhidb.ColumnDef{
		{Name: "id", Type: haruhidb.TypeInteger},
		{Name: "name", Type: haruhidb.TypeVarchar, Length: 32},
	}); err != nil {
		t.Fatalf("create table failed: %v", err)
	}
	if err := db.CreatePrimaryIntIndex("users", "idx_users_id"); err != nil {
		t.Fatalf("create index failed: %v", err)
	}
}
