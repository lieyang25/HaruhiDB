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
		AllowWrite:     true,
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
