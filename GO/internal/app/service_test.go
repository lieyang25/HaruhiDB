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
	"haruhidb-go/internal/nl"
)

type scriptedTranslator struct {
	outputs []nl.TranslateOutput
	errs    []error
	inputs  []nl.TranslateInput
}

func (s *scriptedTranslator) Translate(_ context.Context, in nl.TranslateInput) (nl.TranslateOutput, error) {
	s.inputs = append(s.inputs, in)
	idx := len(s.inputs) - 1
	if idx < len(s.errs) && s.errs[idx] != nil {
		return nl.TranslateOutput{}, s.errs[idx]
	}
	if idx < len(s.outputs) {
		return s.outputs[idx], nil
	}
	return nl.TranslateOutput{}, errors.New("no scripted output")
}

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

func TestTranslateNLSuccessWithRepair(t *testing.T) {
	db := openServiceTestDB(t)
	defer closeServiceTestDB(t, db)
	createUsersTable(t, db)

	translator := &scriptedTranslator{
		outputs: []nl.TranslateOutput{
			{
				Candidate: []byte(`{"version":"v1","request_id":"req-nl","mode":"read_only","action":"table_exists","args":{}}`),
				Model:     "test-model",
			},
			{
				Candidate: []byte(`{"version":"v1","request_id":"req-nl","mode":"read_only","action":"list_tables","args":{}}`),
				Model:     "test-model",
			},
		},
	}

	service, err := NewActionService(Config{
		DB:             db,
		AllowWrite:     false,
		RequestTimeout: 5 * time.Second,
		Translator:     translator,
	})
	if err != nil {
		t.Fatalf("NewActionService failed: %v", err)
	}

	result, err := service.TranslateNL(context.Background(), NLRequest{
		RequestID: "req-nl",
		Input:     "列出所有表",
		Mode:      action.ModeReadOnly,
	})
	if err != nil {
		t.Fatalf("TranslateNL failed: %v", err)
	}
	if !result.Valid {
		t.Fatalf("expected valid translation, got %+v", result)
	}
	if len(result.CandidateRaw) == 0 {
		t.Fatalf("expected candidate raw JSON")
	}
	if len(translator.inputs) != 2 {
		t.Fatalf("expected repair retry, got %d calls", len(translator.inputs))
	}
	if translator.inputs[1].RepairHint == "" {
		t.Fatalf("expected second attempt to contain repair hint")
	}
}

func TestTranslateNLAutoNormalizesCommonEnvelopeFields(t *testing.T) {
	db := openServiceTestDB(t)
	defer closeServiceTestDB(t, db)

	translator := &scriptedTranslator{
		outputs: []nl.TranslateOutput{
			{
				Candidate: []byte(`{"action":"list_tables","args":[]}`),
				Model:     "qwen2.5-coder:0.5b",
			},
		},
	}

	service, err := NewActionService(Config{
		DB:             db,
		AllowWrite:     false,
		RequestTimeout: 5 * time.Second,
		Translator:     translator,
	})
	if err != nil {
		t.Fatalf("NewActionService failed: %v", err)
	}

	result, err := service.TranslateNL(context.Background(), NLRequest{
		RequestID: "req-normalize",
		Input:     "列出所有表",
		Mode:      action.ModeReadOnly,
	})
	if err != nil {
		t.Fatalf("TranslateNL failed: %v", err)
	}
	if !result.Valid {
		t.Fatalf("expected valid translation, got %+v", result)
	}
	if len(translator.inputs) != 1 {
		t.Fatalf("expected no repair retry after local normalization, got %d calls", len(translator.inputs))
	}
	if got := result.CandidateEnvelope["request_id"]; got != "req-normalize" {
		t.Fatalf("unexpected request_id in normalized candidate: %#v", got)
	}
	if got := result.CandidateEnvelope["mode"]; got != string(action.ModeReadOnly) {
		t.Fatalf("unexpected mode in normalized candidate: %#v", got)
	}
	if got := result.CandidateEnvelope["version"]; got != action.VersionV1 {
		t.Fatalf("unexpected version in normalized candidate: %#v", got)
	}
	args, ok := result.CandidateEnvelope["args"].(map[string]any)
	if !ok {
		t.Fatalf("expected normalized args object, got %#v", result.CandidateEnvelope["args"])
	}
	if len(args) != 0 {
		t.Fatalf("expected empty args object, got %#v", args)
	}
}

func TestTranslateNLDropsUnknownTopLevelFields(t *testing.T) {
	db := openServiceTestDB(t)
	defer closeServiceTestDB(t, db)

	translator := &scriptedTranslator{
		outputs: []nl.TranslateOutput{
			{
				Candidate: []byte(`{"version":"v1","request_id":"req-extra","mode":"read_only","action":"list_tables","args":{},"previous_output_error":"x"}`),
				Model:     "qwen2.5-coder:0.5b",
			},
		},
	}

	service, err := NewActionService(Config{
		DB:             db,
		AllowWrite:     false,
		RequestTimeout: 5 * time.Second,
		Translator:     translator,
	})
	if err != nil {
		t.Fatalf("NewActionService failed: %v", err)
	}

	result, err := service.TranslateNL(context.Background(), NLRequest{
		RequestID: "req-extra",
		Input:     "列出所有表",
		Mode:      action.ModeReadOnly,
	})
	if err != nil {
		t.Fatalf("TranslateNL failed: %v", err)
	}
	if !result.Valid {
		t.Fatalf("expected valid translation after dropping unknown fields, got %+v", result)
	}
	if len(translator.inputs) != 1 {
		t.Fatalf("expected no repair retry after dropping unknown fields, got %d calls", len(translator.inputs))
	}
	if _, exists := result.CandidateEnvelope["previous_output_error"]; exists {
		t.Fatalf("unexpected unknown field left in candidate envelope: %#v", result.CandidateEnvelope)
	}
}

func TestTranslateNLDropsUnknownArgsFieldsForListTables(t *testing.T) {
	db := openServiceTestDB(t)
	defer closeServiceTestDB(t, db)

	translator := &scriptedTranslator{
		outputs: []nl.TranslateOutput{
			{
				Candidate: []byte(`{"version":"v1","request_id":"req-args","mode":"read_only","action":"list_tables","args":{"catalog_snapshot_json":"...","x":1}}`),
				Model:     "qwen2.5-coder:0.5b",
			},
		},
	}

	service, err := NewActionService(Config{
		DB:             db,
		AllowWrite:     false,
		RequestTimeout: 5 * time.Second,
		Translator:     translator,
	})
	if err != nil {
		t.Fatalf("NewActionService failed: %v", err)
	}

	result, err := service.TranslateNL(context.Background(), NLRequest{
		RequestID: "req-args",
		Input:     "列出所有表",
		Mode:      action.ModeReadOnly,
	})
	if err != nil {
		t.Fatalf("TranslateNL failed: %v", err)
	}
	if !result.Valid {
		t.Fatalf("expected valid translation after args cleanup, got %+v", result)
	}
	if len(translator.inputs) != 1 {
		t.Fatalf("expected no repair retry after args cleanup, got %d calls", len(translator.inputs))
	}
	args, ok := result.CandidateEnvelope["args"].(map[string]any)
	if !ok {
		t.Fatalf("expected args object, got %#v", result.CandidateEnvelope["args"])
	}
	if len(args) != 0 {
		t.Fatalf("expected list_tables args to be empty object, got %#v", args)
	}
}

func TestTranslateNLNoTranslator(t *testing.T) {
	db := openServiceTestDB(t)
	defer closeServiceTestDB(t, db)

	service, err := NewActionService(Config{
		DB:             db,
		AllowWrite:     false,
		RequestTimeout: 5 * time.Second,
		Translator:     nil,
	})
	if err != nil {
		t.Fatalf("NewActionService failed: %v", err)
	}

	result, err := service.TranslateNL(context.Background(), NLRequest{
		RequestID: "req-nl-no-translator",
		Input:     "列出所有表",
		Mode:      action.ModeReadOnly,
	})
	if err != nil {
		t.Fatalf("TranslateNL returned unexpected error: %v", err)
	}
	if result.Valid {
		t.Fatalf("expected invalid translation without translator")
	}
	if result.Error == nil || result.Error.Code != ErrorCodeTranslation {
		t.Fatalf("unexpected translate error: %#v", result.Error)
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
