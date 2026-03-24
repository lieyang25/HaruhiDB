package app

import (
	"context"
	"encoding/json"
	"errors"
	"path/filepath"
	"testing"
	"time"

	"haruhidb-go/haruhidb"
	"haruhidb-go/internal/action"
)

func TestExecuteJSONSuccessAndValidationFailure(t *testing.T) {
	db := openServiceTestDB(t)
	defer closeServiceTestDB(t, db)
	createUsersTable(t, db)

	service, err := NewActionService(Config{
		DB:             db,
		AllowWrite:     false,
		RequestTimeout: 5 * time.Second,
	})
	if err != nil {
		t.Fatalf("NewActionService failed: %v", err)
	}

	successResp, err := service.ExecuteJSON(context.Background(), []byte(`{
		"version":"v1",
		"request_id":"req-service-list",
		"mode":"read_only",
		"action":"list_tables",
		"args":{}
	}`))
	if err != nil {
		t.Fatalf("ExecuteJSON list_tables failed: %v", err)
	}
	assertActionResponseOK(t, successResp, true)

	failureResp, err := service.ExecuteJSON(context.Background(), []byte(`{
		"version":"v1",
		"request_id":"",
		"mode":"read_only",
		"action":"list_tables",
		"args":{}
	}`))
	if err != nil {
		t.Fatalf("ExecuteJSON validation failure returned error: %v", err)
	}
	assertActionResponseOK(t, failureResp, false)
}

func TestExecuteJSONWriteGuard(t *testing.T) {
	db := openServiceTestDB(t)
	defer closeServiceTestDB(t, db)
	createUsersTable(t, db)

	service, err := NewActionService(Config{
		DB:             db,
		AllowWrite:     false,
		RequestTimeout: 5 * time.Second,
	})
	if err != nil {
		t.Fatalf("NewActionService failed: %v", err)
	}

	resp, err := service.ExecuteJSON(context.Background(), []byte(`{
		"version":"v1",
		"request_id":"req-service-write-guard",
		"mode":"read_write",
		"action":"insert_row",
		"args":{"table":"users","values":{"id":1,"name":"alice"}}
	}`))
	if err != nil {
		t.Fatalf("ExecuteJSON write guard failed: %v", err)
	}

	var envelope action.ResponseEnvelope[map[string]any]
	if err := json.Unmarshal(resp, &envelope); err != nil {
		t.Fatalf("decode response failed: %v", err)
	}
	if envelope.Ok {
		t.Fatalf("expected write guard failure, got success: %#v", envelope)
	}
	if envelope.Error == nil || envelope.Error.Code != action.CodeInvalidRequest {
		t.Fatalf("unexpected write guard error: %#v", envelope.Error)
	}
}

func TestExecuteJSONContextCanceled(t *testing.T) {
	db := openServiceTestDB(t)
	defer closeServiceTestDB(t, db)

	service, err := NewActionService(Config{
		DB:             db,
		AllowWrite:     false,
		RequestTimeout: 5 * time.Second,
	})
	if err != nil {
		t.Fatalf("NewActionService failed: %v", err)
	}

	ctx, cancel := context.WithCancel(context.Background())
	cancel()
	_, execErr := service.ExecuteJSON(ctx, []byte(`{
		"version":"v1",
		"request_id":"req-canceled",
		"mode":"read_only",
		"action":"list_tables",
		"args":{}
	}`))
	if !errors.Is(execErr, context.Canceled) {
		t.Fatalf("expected context.Canceled, got %v", execErr)
	}
}

func openServiceTestDB(t *testing.T) *haruhidb.DB {
	t.Helper()
	dir := t.TempDir()
	path := filepath.Join(dir, "service_test.db")
	db, err := haruhidb.Open(path, haruhidb.OpenOptions{})
	if err != nil {
		t.Fatalf("open db failed: %v", err)
	}
	return db
}

func closeServiceTestDB(t *testing.T, db *haruhidb.DB) {
	t.Helper()
	if db == nil {
		return
	}
	if err := db.Close(); err != nil {
		t.Fatalf("close db failed: %v", err)
	}
}

func createUsersTable(t *testing.T, db *haruhidb.DB) {
	t.Helper()
	if err := db.CreateTable("users", []haruhidb.ColumnDef{
		{Name: "id", Type: haruhidb.TypeInteger},
		{Name: "name", Type: haruhidb.TypeVarchar, Length: 32},
	}); err != nil {
		t.Fatalf("create users table failed: %v", err)
	}
	if err := db.CreatePrimaryIntIndex("users", "idx_users_id"); err != nil {
		t.Fatalf("create users index failed: %v", err)
	}
}

func assertActionResponseOK(t *testing.T, raw []byte, expected bool) {
	t.Helper()
	var envelope action.ResponseEnvelope[map[string]any]
	if err := json.Unmarshal(raw, &envelope); err != nil {
		t.Fatalf("decode response failed: %v", err)
	}
	if envelope.Ok != expected {
		t.Fatalf("unexpected response ok: got %v want %v, payload=%s", envelope.Ok, expected, string(raw))
	}
}
