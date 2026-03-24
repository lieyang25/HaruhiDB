package httptransport

import (
	"bytes"
	"encoding/json"
	"io"
	"net/http"
	"net/http/httptest"
	"net/url"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"haruhidb-go/haruhidb"
	"haruhidb-go/internal/action"
	"haruhidb-go/internal/app"
)

func TestHealthz(t *testing.T) {
	manager, _, _ := newHTTPRuntimeManager(t)
	defer closeHTTPRuntimeManager(t, manager)

	handler := NewHandler(manager, Config{})
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
	manager, _, _ := newHTTPRuntimeManager(t)
	defer closeHTTPRuntimeManager(t, manager)

	handler := NewHandler(manager, Config{})
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
	manager, _, _ := newHTTPRuntimeManager(t)
	defer closeHTTPRuntimeManager(t, manager)

	handler := NewHandler(manager, Config{})
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
	manager, _, _ := newHTTPRuntimeManager(t)
	defer closeHTTPRuntimeManager(t, manager)

	handler := NewHandler(manager, Config{})
	req := httptest.NewRequest(http.MethodGet, "/ui/app.js", nil)
	rr := httptest.NewRecorder()
	handler.ServeHTTP(rr, req)

	if rr.Code != http.StatusOK {
		t.Fatalf("expected 200, got %d", rr.Code)
	}
	if got := rr.Header().Get("Content-Type"); !strings.Contains(got, "javascript") {
		t.Fatalf("expected javascript content type, got %q", got)
	}
	if body := rr.Body.String(); !strings.Contains(body, "postJSON(\"/v1/action\"") {
		t.Fatalf("expected UI JS content, got %q", body)
	}
}

func TestActionEndpointLegacyPayload(t *testing.T) {
	manager, _, _ := newHTTPRuntimeManager(t)
	defer closeHTTPRuntimeManager(t, manager)

	server := httptest.NewServer(NewHandler(manager, Config{}))
	defer server.Close()

	body := []byte(`{
		"version":"v3",
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

	if envelope.Data == nil {
		t.Fatalf("expected non-nil response data")
	}
	dataMap := *envelope.Data
	rawTables, ok := dataMap["tables"].([]any)
	if !ok {
		t.Fatalf("expected tables array, got %#v", dataMap["tables"])
	}
	joined := make([]string, 0, len(rawTables))
	for _, item := range rawTables {
		if text, ok := item.(string); ok {
			joined = append(joined, text)
		}
	}
	if !containsString(joined, "users") {
		t.Fatalf("expected fallback default db tables to include users, got %#v", joined)
	}
	if containsString(joined, "extra_users") {
		t.Fatalf("expected fallback default db tables to exclude extra_users, got %#v", joined)
	}
}

func TestActionEndpointSupportsDBPathField(t *testing.T) {
	manager, _, extraDBPath := newHTTPRuntimeManager(t)
	defer closeHTTPRuntimeManager(t, manager)

	server := httptest.NewServer(NewHandler(manager, Config{}))
	defer server.Close()

	body := []byte(`{
		"db_path":"` + extraDBPath + `",
		"version":"v3",
		"request_id":"req-http-list-extra",
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

	var envelope action.ResponseEnvelope[map[string]any]
	if err := json.NewDecoder(resp.Body).Decode(&envelope); err != nil {
		t.Fatalf("decode response failed: %v", err)
	}
	if !envelope.Ok {
		t.Fatalf("expected success response, got %#v", envelope)
	}
}

func TestActionEndpointDDLEmptyTableGuard(t *testing.T) {
	manager, _, _ := newHTTPRuntimeManager(t)
	defer closeHTTPRuntimeManager(t, manager)

	server := httptest.NewServer(NewHandler(manager, Config{}))
	defer server.Close()

	insertBody := []byte(`{
		"version":"v3",
		"request_id":"req-http-insert-before-drop",
		"mode":"read_write",
		"action":"insert_row",
		"args":{"table":"users","values":{"id":1,"name":"alice"}}
	}`)
	insertResp, err := http.Post(server.URL+"/v1/action", "application/json", bytes.NewReader(insertBody))
	if err != nil {
		t.Fatalf("POST /v1/action insert failed: %v", err)
	}
	defer insertResp.Body.Close()
	if insertResp.StatusCode != http.StatusOK {
		t.Fatalf("unexpected insert status: %d", insertResp.StatusCode)
	}

	dropBody := []byte(`{
		"version":"v3",
		"request_id":"req-http-drop-denied",
		"mode":"read_write",
		"action":"drop_table",
		"args":{"table":"users"}
	}`)
	dropResp, err := http.Post(server.URL+"/v1/action", "application/json", bytes.NewReader(dropBody))
	if err != nil {
		t.Fatalf("POST /v1/action drop failed: %v", err)
	}
	defer dropResp.Body.Close()
	if dropResp.StatusCode != http.StatusOK {
		t.Fatalf("unexpected drop status: %d", dropResp.StatusCode)
	}

	var envelope action.ResponseEnvelope[map[string]any]
	if err := json.NewDecoder(dropResp.Body).Decode(&envelope); err != nil {
		t.Fatalf("decode drop response failed: %v", err)
	}
	if envelope.Ok {
		t.Fatalf("expected drop_table failure for non-empty table, got success: %#v", envelope)
	}
	if envelope.Error == nil || envelope.Error.Code != action.CodeConstraint {
		t.Fatalf("unexpected drop_table error payload: %#v", envelope.Error)
	}
}

func TestTranslateEndpointRemoved(t *testing.T) {
	manager, _, _ := newHTTPRuntimeManager(t)
	defer closeHTTPRuntimeManager(t, manager)

	server := httptest.NewServer(NewHandler(manager, Config{}))
	defer server.Close()

	resp, err := http.Post(server.URL+"/v1/nl/translate", "application/json", bytes.NewReader([]byte(`{}`)))
	if err != nil {
		t.Fatalf("POST /v1/nl/translate failed: %v", err)
	}
	defer resp.Body.Close()

	if resp.StatusCode != http.StatusNotFound {
		t.Fatalf("expected 404 for removed endpoint, got %d", resp.StatusCode)
	}
}

func TestCapabilitiesEndpoint(t *testing.T) {
	manager, _, extraDBPath := newHTTPRuntimeManager(t)
	defer closeHTTPRuntimeManager(t, manager)

	server := httptest.NewServer(NewHandler(manager, Config{}))
	defer server.Close()

	u := server.URL + "/v1/capabilities?db_path=" + url.QueryEscape(extraDBPath)
	resp, err := http.Get(u)
	if err != nil {
		t.Fatalf("GET /v1/capabilities failed: %v", err)
	}
	defer resp.Body.Close()

	if resp.StatusCode != http.StatusOK {
		t.Fatalf("unexpected status: %d", resp.StatusCode)
	}

	var parsed struct {
		Ok            bool                `json:"ok"`
		DBPath        string              `json:"db_path"`
		DefaultDBPath string              `json:"default_db_path"`
		Actions       []action.ActionSpec `json:"actions"`
	}
	if err := json.NewDecoder(resp.Body).Decode(&parsed); err != nil {
		t.Fatalf("decode capabilities failed: %v", err)
	}
	if !parsed.Ok {
		t.Fatalf("expected ok=true, got %#v", parsed)
	}
	if strings.TrimSpace(parsed.DBPath) == "" || strings.TrimSpace(parsed.DefaultDBPath) == "" {
		t.Fatalf("expected db paths to be present, got %#v", parsed)
	}
	if len(parsed.Actions) == 0 {
		t.Fatalf("expected non-empty actions list")
	}
}

func TestCapabilitiesEndpointInvalidDBPath(t *testing.T) {
	manager, _, _ := newHTTPRuntimeManager(t)
	defer closeHTTPRuntimeManager(t, manager)

	server := httptest.NewServer(NewHandler(manager, Config{}))
	defer server.Close()

	resp, err := http.Get(server.URL + "/v1/capabilities?db_path=%00")
	if err != nil {
		t.Fatalf("GET /v1/capabilities failed: %v", err)
	}
	defer resp.Body.Close()

	if resp.StatusCode != http.StatusBadRequest {
		t.Fatalf("expected 400 for invalid db_path, got %d", resp.StatusCode)
	}
}

func TestActionEndpointMethodNotAllowed(t *testing.T) {
	manager, _, _ := newHTTPRuntimeManager(t)
	defer closeHTTPRuntimeManager(t, manager)

	server := httptest.NewServer(NewHandler(manager, Config{}))
	defer server.Close()

	resp, err := http.Get(server.URL + "/v1/action")
	if err != nil {
		t.Fatalf("GET /v1/action failed: %v", err)
	}
	defer resp.Body.Close()

	if resp.StatusCode != http.StatusMethodNotAllowed {
		t.Fatalf("expected 405, got %d", resp.StatusCode)
	}
	if allow := resp.Header.Get("Allow"); allow != http.MethodPost {
		t.Fatalf("expected Allow=%q, got %q", http.MethodPost, allow)
	}
}

func TestActionEndpointBodyTooLarge(t *testing.T) {
	manager, _, _ := newHTTPRuntimeManager(t)
	defer closeHTTPRuntimeManager(t, manager)

	server := httptest.NewServer(NewHandler(manager, Config{}))
	defer server.Close()

	oversized := bytes.Repeat([]byte("a"), (1<<20)+32)
	resp, err := http.Post(server.URL+"/v1/action", "application/json", bytes.NewReader(oversized))
	if err != nil {
		t.Fatalf("POST /v1/action failed: %v", err)
	}
	defer resp.Body.Close()

	if resp.StatusCode != http.StatusBadRequest {
		t.Fatalf("expected 400, got %d", resp.StatusCode)
	}

	raw, err := io.ReadAll(resp.Body)
	if err != nil {
		t.Fatalf("read response failed: %v", err)
	}
	if !strings.Contains(string(raw), "request body too large") {
		t.Fatalf("expected body-too-large message, got %s", string(raw))
	}
}

func newHTTPRuntimeManager(t *testing.T) (*app.RuntimeManager, string, string) {
	t.Helper()
	dir := t.TempDir()
	defaultDBPath := filepath.Join(dir, "http_default.db")
	extraDBPath := filepath.Join(dir, "http_extra.db")

	createHTTPUsersTableAtPath(t, defaultDBPath, "users")
	createHTTPUsersTableAtPath(t, extraDBPath, "extra_users")

	manager, err := app.NewRuntimeManager(app.RuntimeManagerConfig{
		DefaultDBPath:  defaultDBPath,
		AllowWrite:     true,
		RequestTimeout: 5 * time.Second,
	})
	if err != nil {
		t.Fatalf("NewRuntimeManager failed: %v", err)
	}
	return manager, defaultDBPath, extraDBPath
}

func closeHTTPRuntimeManager(t *testing.T, manager *app.RuntimeManager) {
	t.Helper()
	if manager == nil {
		return
	}
	if err := manager.Close(); err != nil {
		t.Fatalf("close runtime manager failed: %v", err)
	}
}

func createHTTPUsersTableAtPath(t *testing.T, dbPath string, tableName string) {
	t.Helper()
	db, err := haruhidb.Open(dbPath, haruhidb.OpenOptions{})
	if err != nil {
		t.Fatalf("open db failed: %v", err)
	}
	defer func() {
		if closeErr := db.Close(); closeErr != nil {
			t.Fatalf("close db failed: %v", closeErr)
		}
	}()

	if err := db.CreateTable(tableName, []haruhidb.ColumnDef{
		{Name: "id", Type: haruhidb.TypeInteger},
		{Name: "name", Type: haruhidb.TypeVarchar, Length: 32},
	}); err != nil {
		t.Fatalf("create table failed: %v", err)
	}
	if err := db.CreatePrimaryIntIndex(tableName, "idx_"+tableName+"_id"); err != nil {
		t.Fatalf("create index failed: %v", err)
	}
}

func containsString(items []string, target string) bool {
	for _, item := range items {
		if item == target {
			return true
		}
	}
	return false
}
