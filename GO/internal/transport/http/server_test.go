package httptransport

import (
	"bytes"
	"context"
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
	"haruhidb-go/internal/nl"
)

type translatorStub struct {
	output nl.TranslateOutput
}

func (t *translatorStub) Translate(context.Context, nl.TranslateInput) (nl.TranslateOutput, error) {
	return t.output, nil
}

type capturingTranslatorStub struct {
	output nl.TranslateOutput
	inputs []nl.TranslateInput
}

func (t *capturingTranslatorStub) Translate(_ context.Context, in nl.TranslateInput) (nl.TranslateOutput, error) {
	t.inputs = append(t.inputs, in)
	return t.output, nil
}

func TestHealthz(t *testing.T) {
	manager, _, _ := newHTTPRuntimeManager(t, nil)
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
	manager, _, _ := newHTTPRuntimeManager(t, nil)
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
	manager, _, _ := newHTTPRuntimeManager(t, nil)
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
	manager, _, _ := newHTTPRuntimeManager(t, nil)
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
	if body := rr.Body.String(); !strings.Contains(body, "postJSON(\"/v1/nl/translate\"") {
		t.Fatalf("expected UI JS content, got %q", body)
	}
}

func TestActionEndpointLegacyPayload(t *testing.T) {
	manager, _, _ := newHTTPRuntimeManager(t, nil)
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
	manager, _, extraDBPath := newHTTPRuntimeManager(t, nil)
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

func TestTranslateEndpointWithDBPath(t *testing.T) {
	translator := &translatorStub{
		output: nl.TranslateOutput{
			Candidate: []byte(`{
				"version":"v3",
				"request_id":"req-http-nl",
				"mode":"read_only",
				"action":"list_tables",
				"args":{}
			}`),
			Model: "stub-model",
		},
	}

	manager, _, extraDBPath := newHTTPRuntimeManager(t, translator)
	defer closeHTTPRuntimeManager(t, manager)

	server := httptest.NewServer(NewHandler(manager, Config{}))
	defer server.Close()

	reqBody := []byte(`{
		"db_path":"` + extraDBPath + `",
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

func TestTranslateEndpointFallsBackToDefaultDBPath(t *testing.T) {
	translator := &capturingTranslatorStub{
		output: nl.TranslateOutput{
			Candidate: []byte(`{
				"version":"v3",
				"request_id":"req-http-nl-default",
				"mode":"read_only",
				"action":"list_tables",
				"args":{}
			}`),
			Model: "stub-model",
		},
	}

	manager, _, _ := newHTTPRuntimeManager(t, translator)
	defer closeHTTPRuntimeManager(t, manager)

	server := httptest.NewServer(NewHandler(manager, Config{}))
	defer server.Close()

	reqBody := []byte(`{
		"request_id":"req-http-nl-default",
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
	if len(translator.inputs) != 1 {
		t.Fatalf("expected exactly one translator call, got %d", len(translator.inputs))
	}

	tableNames := make([]string, 0, len(translator.inputs[0].Catalog.Tables))
	for _, table := range translator.inputs[0].Catalog.Tables {
		tableNames = append(tableNames, table.Name)
	}
	if !containsString(tableNames, "users") {
		t.Fatalf("expected catalog snapshot to come from default db, got %#v", tableNames)
	}
	if containsString(tableNames, "extra_users") {
		t.Fatalf("expected catalog snapshot to exclude extra db tables, got %#v", tableNames)
	}
}

func TestCapabilitiesEndpoint(t *testing.T) {
	manager, _, extraDBPath := newHTTPRuntimeManager(t, nil)
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
		Ok            bool              `json:"ok"`
		DBPath        string            `json:"db_path"`
		DefaultDBPath string            `json:"default_db_path"`
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
	manager, _, _ := newHTTPRuntimeManager(t, nil)
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
	manager, _, _ := newHTTPRuntimeManager(t, nil)
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
	manager, _, _ := newHTTPRuntimeManager(t, nil)
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

func newHTTPRuntimeManager(t *testing.T, translator nl.Translator) (*app.RuntimeManager, string, string) {
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
		Translator:     translator,
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
