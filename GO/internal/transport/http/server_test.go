package httptransport

import (
	"bytes"
	"context"
	"encoding/json"
	"io"
	"net/http"
	"net/http/httptest"
	"path/filepath"
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
