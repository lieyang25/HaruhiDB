package action

import (
	"os"
	"regexp"
	"strings"
	"testing"

	"haruhidb-go/haruhidb"
)

type stubTable struct {
	columns []haruhidb.ColumnInfo
	indexes []string
}

type stubCatalog struct {
	tables map[string]stubTable
}

func (s stubCatalog) TableExists(name string) (bool, error) {
	_, ok := s.tables[name]
	return ok, nil
}

func (s stubCatalog) ListTableColumns(tableName string) ([]haruhidb.ColumnInfo, error) {
	table, ok := s.tables[tableName]
	if !ok {
		return nil, nil
	}
	return append([]haruhidb.ColumnInfo(nil), table.columns...), nil
}

func (s stubCatalog) ListTableIndexes(tableName string) ([]string, error) {
	table, ok := s.tables[tableName]
	if !ok {
		return nil, nil
	}
	return append([]string(nil), table.indexes...), nil
}

func docCatalog() stubCatalog {
	return stubCatalog{
		tables: map[string]stubTable{
			"users": {
				columns: []haruhidb.ColumnInfo{
					{Name: "id", Type: haruhidb.TypeInteger, Nullable: false},
					{Name: "name", Type: haruhidb.TypeVarchar, Length: 32, Nullable: false},
				},
				indexes: []string{"idx_users_id"},
			},
			"orders": {
				columns: []haruhidb.ColumnInfo{
					{Name: "id", Type: haruhidb.TypeInteger, Nullable: false},
					{Name: "amount", Type: haruhidb.TypeDouble, Nullable: false},
				},
				indexes: []string{"idx_orders_id"},
			},
		},
	}
}

func TestDecodeAndValidateExamplesFromDocs(t *testing.T) {
	examples := loadDocExamples(t)
	catalog := docCatalog()

	requestActions := []Action{
		ActionListTables,
		ActionTableExists,
		ActionDescribeTable,
		ActionGetByPrimaryInt,
		ActionScanAll,
		ActionScanPrimaryIntRange,
		ActionInsertRow,
		ActionUpdateByPrimaryInt,
		ActionDeleteByPrimaryInt,
		ActionBatch,
	}

	for _, actionName := range requestActions {
		key := "request:" + string(actionName)
		payload, ok := examples[key]
		if !ok {
			t.Fatalf("missing doc example %q", key)
		}

		req, err := DecodeAndValidate([]byte(payload), catalog)
		if err != nil {
			t.Fatalf("%s: decode failed: %v", key, err)
		}
		if req.Action != actionName {
			t.Fatalf("%s: unexpected action %q", key, req.Action)
		}
	}
}

func TestResponseExamplesFromDocs(t *testing.T) {
	examples := loadDocExamples(t)

	for key, payload := range examples {
		parts := strings.Split(key, ":")
		if len(parts) != 2 || !strings.HasPrefix(parts[0], "response_") {
			continue
		}

		actionName := Action(parts[1])
		switch parts[0] {
		case "response_success":
			switch actionName {
			case ActionListTables:
				assertSuccessResponse[ListTablesData](t, payload)
			case ActionTableExists:
				assertSuccessResponse[TableExistsData](t, payload)
			case ActionDescribeTable:
				assertSuccessResponse[DescribeTableData](t, payload)
			case ActionGetByPrimaryInt:
				assertSuccessResponse[GetByPrimaryIntData](t, payload)
			case ActionScanAll:
				assertSuccessResponse[ScanAllData](t, payload)
			case ActionScanPrimaryIntRange:
				assertSuccessResponse[ScanPrimaryIntRangeData](t, payload)
			case ActionInsertRow:
				assertSuccessResponse[InsertRowData](t, payload)
			case ActionUpdateByPrimaryInt:
				assertSuccessResponse[UpdateByPrimaryIntData](t, payload)
			case ActionDeleteByPrimaryInt:
				assertSuccessResponse[DeleteByPrimaryIntData](t, payload)
			case ActionBatch:
				assertSuccessResponse[BatchData](t, payload)
			default:
				t.Fatalf("unexpected success example action %q", actionName)
			}
		case "response_failure":
			assertFailureResponse(t, payload)
		default:
			t.Fatalf("unexpected example kind %q", parts[0])
		}
	}
}

func TestDecodeAndValidateEnvelopeFailures(t *testing.T) {
	catalog := docCatalog()

	t.Run("invalid version", func(t *testing.T) {
		_, err := DecodeAndValidate([]byte(`{
			"version":"v9",
			"request_id":"req-1",
			"mode":"read_only",
			"action":"list_tables",
			"args":{}
		}`), catalog)
		requireProtocolErrorCode(t, err, CodeInvalidRequest)
	})

	t.Run("empty request id", func(t *testing.T) {
		_, err := DecodeAndValidate([]byte(`{
			"version":"v1",
			"request_id":"   ",
			"mode":"read_only",
			"action":"list_tables",
			"args":{}
		}`), catalog)
		requireProtocolErrorCode(t, err, CodeInvalidRequest)
	})

	t.Run("unknown action", func(t *testing.T) {
		_, err := DecodeAndValidate([]byte(`{
			"version":"v1",
			"request_id":"req-1",
			"mode":"read_only",
			"action":"query",
			"args":{}
		}`), catalog)
		requireProtocolErrorCode(t, err, CodeInvalidRequest)
	})

	t.Run("read only write action", func(t *testing.T) {
		_, err := DecodeAndValidate([]byte(`{
			"version":"v1",
			"request_id":"req-1",
			"mode":"read_only",
			"action":"insert_row",
			"args":{
				"table":"users",
				"values":{"id":1,"name":"alice"}
			}
		}`), catalog)
		requireProtocolErrorCode(t, err, CodeInvalidRequest)
	})

	t.Run("unknown top level field", func(t *testing.T) {
		_, err := DecodeAndValidate([]byte(`{
			"version":"v1",
			"request_id":"req-1",
			"mode":"read_only",
			"action":"list_tables",
			"args":{},
			"extra":true
		}`), catalog)
		requireProtocolErrorCode(t, err, CodeInvalidRequest)
	})

	t.Run("unknown args field", func(t *testing.T) {
		_, err := DecodeAndValidate([]byte(`{
			"version":"v1",
			"request_id":"req-1",
			"mode":"read_only",
			"action":"scan_all",
			"args":{"table":"users","limit":10,"extra":true}
		}`), catalog)
		requireProtocolErrorCode(t, err, CodeInvalidRequest)
	})

	t.Run("batch read only includes write action", func(t *testing.T) {
		_, err := DecodeAndValidate([]byte(`{
			"version":"v1",
			"request_id":"req-1",
			"mode":"read_only",
			"action":"batch",
			"args":{
				"requests":[
					{
						"action":"insert_row",
						"args":{"table":"users","values":{"id":1,"name":"alice"}}
					}
				]
			}
		}`), catalog)
		requireProtocolErrorCode(t, err, CodeInvalidRequest)
	})

	t.Run("batch nested batch action", func(t *testing.T) {
		_, err := DecodeAndValidate([]byte(`{
			"version":"v1",
			"request_id":"req-1",
			"mode":"read_write",
			"action":"batch",
			"args":{
				"requests":[
					{
						"action":"batch",
						"args":{"requests":[{"action":"list_tables","args":{}}]}
					}
				]
			}
		}`), catalog)
		requireProtocolErrorCode(t, err, CodeInvalidRequest)
	})
}

func TestDecodeAndValidateV3UnifiedActions(t *testing.T) {
	catalog := docCatalog()

	t.Run("v1 ddl action is accepted and canonicalized to v3", func(t *testing.T) {
		req, err := DecodeAndValidate([]byte(`{
			"version":"v1",
			"request_id":"req-v1-ddl",
			"mode":"read_write",
			"action":"create_table",
			"args":{
				"table":"books",
				"columns":[
					{"name":"id","type":"INTEGER","nullable":false},
					{"name":"name","type":"VARCHAR","length":32,"nullable":false}
				]
			}
		}`), catalog)
		if err != nil {
			t.Fatalf("DecodeAndValidate failed: %v", err)
		}
		if req.Version != VersionV3 {
			t.Fatalf("unexpected canonical version: got %q want %q", req.Version, VersionV3)
		}
	})

	t.Run("v2 input is accepted and canonicalized to v3", func(t *testing.T) {
		req, err := DecodeAndValidate([]byte(`{
			"version":"v2",
			"request_id":"req-v2-list",
			"mode":"read_only",
			"action":"list_tables",
			"args":{}
		}`), catalog)
		if err != nil {
			t.Fatalf("DecodeAndValidate failed: %v", err)
		}
		if req.Version != VersionV3 {
			t.Fatalf("unexpected canonical version: got %q want %q", req.Version, VersionV3)
		}
		if req.Action != ActionListTables {
			t.Fatalf("unexpected action: %q", req.Action)
		}
	})

	t.Run("v3 create_table validates", func(t *testing.T) {
		req, err := DecodeAndValidate([]byte(`{
			"version":"v3",
			"request_id":"req-v3-create-table",
			"mode":"read_write",
			"action":"create_table",
			"args":{
				"table":"books",
				"columns":[
					{"name":"id","type":"INTEGER","nullable":false},
					{"name":"name","type":"VARCHAR","length":32,"nullable":false}
				]
			}
		}`), catalog)
		if err != nil {
			t.Fatalf("DecodeAndValidate failed: %v", err)
		}

		args, ok := req.Args.(CreateTableArgs)
		if !ok {
			t.Fatalf("unexpected args type %T", req.Args)
		}
		if args.Table != "books" {
			t.Fatalf("unexpected table: %q", args.Table)
		}
		if len(args.Columns) != 2 || len(args.columnDefs) != 2 {
			t.Fatalf("unexpected columns: specs=%d defs=%d", len(args.Columns), len(args.columnDefs))
		}
	})

	t.Run("v3 read_only rejects ddl write action", func(t *testing.T) {
		_, err := DecodeAndValidate([]byte(`{
			"version":"v3",
			"request_id":"req-v3-readonly-ddl",
			"mode":"read_only",
			"action":"drop_table",
			"args":{"table":"users"}
		}`), catalog)
		requireProtocolErrorCode(t, err, CodeInvalidRequest)
	})

	t.Run("v3 create_primary_int_index validates", func(t *testing.T) {
		req, err := DecodeAndValidate([]byte(`{
			"version":"v3",
			"request_id":"req-v3-create-index",
			"mode":"read_write",
			"action":"create_primary_int_index",
			"args":{"table":"users","index":"idx_users_new"}
		}`), catalog)
		if err != nil {
			t.Fatalf("DecodeAndValidate failed: %v", err)
		}
		if _, ok := req.Args.(CreatePrimaryIntIndexArgs); !ok {
			t.Fatalf("unexpected args type %T", req.Args)
		}
	})

	t.Run("legacy v1 batch allows ddl sub action and canonicalizes to v3", func(t *testing.T) {
		req, err := DecodeAndValidate([]byte(`{
			"version":"v1",
			"request_id":"req-v1-batch-v2",
			"mode":"read_write",
			"action":"batch",
			"args":{
				"requests":[
					{
						"action":"create_table",
						"args":{
							"table":"books",
							"columns":[
								{"name":"id","type":"INTEGER","nullable":false}
							]
						}
					}
				]
			}
		}`), catalog)
		if err != nil {
			t.Fatalf("DecodeAndValidate failed: %v", err)
		}
		if req.Version != VersionV3 {
			t.Fatalf("unexpected canonical version: got %q want %q", req.Version, VersionV3)
		}
		batchArgs, ok := req.Args.(BatchArgs)
		if !ok {
			t.Fatalf("unexpected args type %T", req.Args)
		}
		if len(batchArgs.requests) != 1 || batchArgs.requests[0] == nil {
			t.Fatalf("unexpected batch requests: %#v", batchArgs.requests)
		}
		if batchArgs.requests[0].Action != ActionCreateTable {
			t.Fatalf("unexpected sub action: %q", batchArgs.requests[0].Action)
		}
	})
}
func TestDecodeAndValidateSchemaFailures(t *testing.T) {
	t.Run("missing table", func(t *testing.T) {
		_, err := DecodeAndValidate([]byte(`{
			"version":"v1",
			"request_id":"req-1",
			"mode":"read_only",
			"action":"describe_table",
			"args":{"table":"missing"}
		}`), docCatalog())
		requireProtocolErrorCode(t, err, CodeNotFound)
	})

	t.Run("primary int first column type mismatch", func(t *testing.T) {
		catalog := stubCatalog{
			tables: map[string]stubTable{
				"bad_users": {
					columns: []haruhidb.ColumnInfo{
						{Name: "id", Type: haruhidb.TypeVarchar, Length: 32, Nullable: false},
					},
					indexes: []string{"idx_bad_users_id"},
				},
			},
		}
		_, err := DecodeAndValidate([]byte(`{
			"version":"v1",
			"request_id":"req-1",
			"mode":"read_only",
			"action":"get_by_primary_int",
			"args":{"table":"bad_users","key":1}
		}`), catalog)
		requireProtocolErrorCode(t, err, CodeUnsupported)
	})

	t.Run("primary int nullable first column", func(t *testing.T) {
		catalog := stubCatalog{
			tables: map[string]stubTable{
				"nullable_users": {
					columns: []haruhidb.ColumnInfo{
						{Name: "id", Type: haruhidb.TypeInteger, Nullable: true},
					},
					indexes: []string{"idx_nullable_users_id"},
				},
			},
		}
		_, err := DecodeAndValidate([]byte(`{
			"version":"v1",
			"request_id":"req-1",
			"mode":"read_only",
			"action":"get_by_primary_int",
			"args":{"table":"nullable_users","key":1}
		}`), catalog)
		requireProtocolErrorCode(t, err, CodeUnsupported)
	})

	t.Run("point get requires index", func(t *testing.T) {
		catalog := stubCatalog{
			tables: map[string]stubTable{
				"users": {
					columns: []haruhidb.ColumnInfo{
						{Name: "id", Type: haruhidb.TypeInteger, Nullable: false},
						{Name: "name", Type: haruhidb.TypeVarchar, Length: 32, Nullable: false},
					},
				},
			},
		}
		_, err := DecodeAndValidate([]byte(`{
			"version":"v1",
			"request_id":"req-1",
			"mode":"read_only",
			"action":"get_by_primary_int",
			"args":{"table":"users","key":1}
		}`), catalog)
		requireProtocolErrorCode(t, err, CodeUnsupported)
	})

	t.Run("range scan requires index", func(t *testing.T) {
		catalog := stubCatalog{
			tables: map[string]stubTable{
				"users": {
					columns: []haruhidb.ColumnInfo{
						{Name: "id", Type: haruhidb.TypeInteger, Nullable: false},
						{Name: "name", Type: haruhidb.TypeVarchar, Length: 32, Nullable: false},
					},
				},
			},
		}
		_, err := DecodeAndValidate([]byte(`{
			"version":"v1",
			"request_id":"req-1",
			"mode":"read_only",
			"action":"scan_primary_int_range",
			"args":{"table":"users","start_key":1,"end_key":10}
		}`), catalog)
		requireProtocolErrorCode(t, err, CodeUnsupported)
	})
}

func TestDecodeAndValidateValueFailures(t *testing.T) {
	catalog := docCatalog()

	t.Run("insert missing column", func(t *testing.T) {
		_, err := DecodeAndValidate([]byte(`{
			"version":"v1",
			"request_id":"req-1",
			"mode":"read_write",
			"action":"insert_row",
			"args":{"table":"users","values":{"id":1}}
		}`), catalog)
		requireProtocolErrorCode(t, err, CodeConstraint)
	})

	t.Run("insert unknown column", func(t *testing.T) {
		_, err := DecodeAndValidate([]byte(`{
			"version":"v1",
			"request_id":"req-1",
			"mode":"read_write",
			"action":"insert_row",
			"args":{"table":"users","values":{"id":1,"name":"alice","nickname":"ali"}}
		}`), catalog)
		requireProtocolErrorCode(t, err, CodeConstraint)
	})

	t.Run("insert null", func(t *testing.T) {
		_, err := DecodeAndValidate([]byte(`{
			"version":"v1",
			"request_id":"req-1",
			"mode":"read_write",
			"action":"insert_row",
			"args":{"table":"users","values":{"id":null,"name":"alice"}}
		}`), catalog)
		requireProtocolErrorCode(t, err, CodeConstraint)
	})

	t.Run("insert type mismatch", func(t *testing.T) {
		_, err := DecodeAndValidate([]byte(`{
			"version":"v1",
			"request_id":"req-1",
			"mode":"read_write",
			"action":"insert_row",
			"args":{"table":"users","values":{"id":"bad","name":"alice"}}
		}`), catalog)
		requireProtocolErrorCode(t, err, CodeConstraint)
	})

	t.Run("update empty values", func(t *testing.T) {
		_, err := DecodeAndValidate([]byte(`{
			"version":"v1",
			"request_id":"req-1",
			"mode":"read_write",
			"action":"update_by_primary_int",
			"args":{"table":"users","key":1,"values":{}}
		}`), catalog)
		requireProtocolErrorCode(t, err, CodeConstraint)
	})

	t.Run("update unknown column", func(t *testing.T) {
		_, err := DecodeAndValidate([]byte(`{
			"version":"v1",
			"request_id":"req-1",
			"mode":"read_write",
			"action":"update_by_primary_int",
			"args":{"table":"users","key":1,"values":{"nickname":"ali"}}
		}`), catalog)
		requireProtocolErrorCode(t, err, CodeConstraint)
	})

	t.Run("update key mismatch", func(t *testing.T) {
		_, err := DecodeAndValidate([]byte(`{
			"version":"v1",
			"request_id":"req-1",
			"mode":"read_write",
			"action":"update_by_primary_int",
			"args":{"table":"users","key":1,"values":{"id":2}}
		}`), catalog)
		requireProtocolErrorCode(t, err, CodeConstraint)
	})
}

func TestDecodeAndValidateAppliesDefaultLimit(t *testing.T) {
	req, err := DecodeAndValidate([]byte(`{
		"version":"v1",
		"request_id":"req-limit-1",
		"mode":"read_only",
		"action":"scan_all",
		"args":{"table":"users"}
	}`), docCatalog())
	if err != nil {
		t.Fatalf("DecodeAndValidate failed: %v", err)
	}

	args, ok := req.Args.(ScanAllArgs)
	if !ok {
		t.Fatalf("unexpected args type %T", req.Args)
	}
	if args.Limit != DefaultLimit {
		t.Fatalf("unexpected default limit: got %d want %d", args.Limit, DefaultLimit)
	}
}

func TestDecodeAndValidateBuildsTypedValues(t *testing.T) {
	req, err := DecodeAndValidate([]byte(`{
		"version":"v1",
		"request_id":"req-values-1",
		"mode":"read_write",
		"action":"insert_row",
		"args":{
			"table":"users",
			"values":{"id":1,"name":"alice"}
		}
	}`), docCatalog())
	if err != nil {
		t.Fatalf("DecodeAndValidate failed: %v", err)
	}

	args, ok := req.Args.(InsertRowArgs)
	if !ok {
		t.Fatalf("unexpected args type %T", req.Args)
	}
	if len(args.typedValues) != 2 {
		t.Fatalf("unexpected typed values size: %d", len(args.typedValues))
	}
	if got := args.typedValues["id"].Int32; got != 1 {
		t.Fatalf("unexpected typed id: %d", got)
	}
	if got := args.typedValues["name"].String; got != "alice" {
		t.Fatalf("unexpected typed name: %q", got)
	}
}

func requireProtocolErrorCode(t *testing.T, err error, code Code) *Error {
	t.Helper()
	if err == nil {
		t.Fatalf("expected protocol error %q, got nil", code)
	}

	typed, ok := err.(*Error)
	if !ok {
		t.Fatalf("expected *Error, got %T (%v)", err, err)
	}
	if typed.Code != code {
		t.Fatalf("unexpected error code: got %q want %q (err=%v)", typed.Code, code, typed)
	}
	return typed
}

func assertSuccessResponse[T any](t *testing.T, payload string) {
	t.Helper()

	var resp ResponseEnvelope[T]
	if err := decodeStrictJSON([]byte(payload), &resp); err != nil {
		t.Fatalf("decode success response failed: %v", err)
	}
	if !resp.Ok {
		t.Fatalf("expected ok=true, got false")
	}
	if !resp.Action.Valid() {
		t.Fatalf("unexpected action %q", resp.Action)
	}
	if resp.Error != nil {
		t.Fatalf("expected nil error, got %+v", resp.Error)
	}
	if resp.Data == nil {
		t.Fatal("expected non-nil data")
	}
	if resp.Meta == nil {
		t.Fatal("expected non-nil meta")
	}
}

func assertFailureResponse(t *testing.T, payload string) {
	t.Helper()

	var resp ResponseEnvelope[map[string]any]
	if err := decodeStrictJSON([]byte(payload), &resp); err != nil {
		t.Fatalf("decode failure response failed: %v", err)
	}
	if resp.Ok {
		t.Fatalf("expected ok=false, got true")
	}
	if !resp.Action.Valid() {
		t.Fatalf("unexpected action %q", resp.Action)
	}
	if resp.Error == nil {
		t.Fatal("expected non-nil error")
	}
	if resp.Data != nil {
		t.Fatalf("expected nil data, got %#v", resp.Data)
	}
	if resp.Meta == nil {
		t.Fatal("expected non-nil meta")
	}
}

func loadDocExamples(t *testing.T) map[string]string {
	t.Helper()

	content, err := os.ReadFile("../../../docs/action-protocol-v1.md")
	if err != nil {
		t.Fatalf("read docs file failed: %v", err)
	}

	re := regexp.MustCompile("(?s)<!-- example:([^:]+):([a-z_]+) -->\\s*```json\\s*(.*?)\\s*```")
	matches := re.FindAllStringSubmatch(string(content), -1)
	if len(matches) == 0 {
		t.Fatal("no JSON examples found in docs")
	}

	examples := make(map[string]string, len(matches))
	for _, match := range matches {
		key := match[1] + ":" + match[2]
		examples[key] = match[3]
	}
	return examples
}
