package action

import (
	"context"
	"errors"
	"path/filepath"
	"slices"
	"strings"
	"testing"
	"time"

	"haruhidb-go/haruhidb"
)

func TestExecuteReadActions(t *testing.T) {
	db := openExecuteTestDB(t)
	defer closeExecuteTestDB(t, db)

	createUsersTable(t, db, true)
	insertUserRows(t, db,
		[]haruhidb.Value{haruhidb.Int32Value(1), haruhidb.StringValue("alice")},
		[]haruhidb.Value{haruhidb.Int32Value(2), haruhidb.StringValue("bob")},
		[]haruhidb.Value{haruhidb.Int32Value(3), haruhidb.StringValue("cindy")},
	)
	createProfilesTable(t, db)

	t.Run("list_tables", func(t *testing.T) {
		resp := mustExecuteRequest(t, context.Background(), db, `{
			"version":"v1",
			"request_id":"req-read-list",
			"mode":"read_only",
			"action":"list_tables",
			"args":{}
		}`)

		tables, ok := mustSuccessData(t, resp)["tables"].([]string)
		if !ok {
			t.Fatalf("unexpected tables payload type %T", mustSuccessData(t, resp)["tables"])
		}
		if !slices.Contains(tables, "users") || !slices.Contains(tables, "profiles") {
			t.Fatalf("unexpected tables: %#v", tables)
		}
	})

	t.Run("table_exists", func(t *testing.T) {
		resp := mustExecuteRequest(t, context.Background(), db, `{
			"version":"v1",
			"request_id":"req-read-exists",
			"mode":"read_only",
			"action":"table_exists",
			"args":{"table":"users"}
		}`)

		data := mustSuccessData(t, resp)
		if got, ok := data["exists"].(bool); !ok || !got {
			t.Fatalf("unexpected exists result: %#v", data["exists"])
		}
	})

	t.Run("describe_table", func(t *testing.T) {
		resp := mustExecuteRequest(t, context.Background(), db, `{
			"version":"v1",
			"request_id":"req-read-desc",
			"mode":"read_only",
			"action":"describe_table",
			"args":{"table":"users"}
		}`)

		data := mustSuccessData(t, resp)
		columns, ok := data["columns"].([]map[string]any)
		if !ok {
			t.Fatalf("unexpected columns payload type %T", data["columns"])
		}
		indexes, ok := data["indexes"].([]map[string]any)
		if !ok {
			t.Fatalf("unexpected indexes payload type %T", data["indexes"])
		}
		if len(columns) != 2 || len(indexes) != 1 {
			t.Fatalf("unexpected describe payload: columns=%#v indexes=%#v", columns, indexes)
		}
		if columns[0]["name"] != "id" || columns[0]["type"] != TypeNameInteger {
			t.Fatalf("unexpected first column: %#v", columns[0])
		}
		if indexes[0]["name"] != "idx_users_id" {
			t.Fatalf("unexpected first index: %#v", indexes[0])
		}
	})

	t.Run("get_by_primary_int found", func(t *testing.T) {
		resp := mustExecuteRequest(t, context.Background(), db, `{
			"version":"v1",
			"request_id":"req-read-get",
			"mode":"read_only",
			"action":"get_by_primary_int",
			"args":{"table":"users","key":2}
		}`)

		data := mustSuccessData(t, resp)
		row, ok := data["row"].(RowMap)
		if !ok {
			t.Fatalf("unexpected row payload type %T", data["row"])
		}
		if got, ok := data["found"].(bool); !ok || !got {
			t.Fatalf("unexpected found flag: %#v", data["found"])
		}
		if row["id"] != int64(2) || row["name"] != "bob" {
			t.Fatalf("unexpected row: %#v", row)
		}
	})

	t.Run("get_by_primary_int missing", func(t *testing.T) {
		resp := mustExecuteRequest(t, context.Background(), db, `{
			"version":"v1",
			"request_id":"req-read-get-missing",
			"mode":"read_only",
			"action":"get_by_primary_int",
			"args":{"table":"users","key":999}
		}`)

		data := mustSuccessData(t, resp)
		if got, ok := data["found"].(bool); !ok || got {
			t.Fatalf("unexpected found flag: %#v", data["found"])
		}
		if data["row"] != nil {
			t.Fatalf("expected nil row, got %#v", data["row"])
		}
	})

	t.Run("scan_all limit and truncated", func(t *testing.T) {
		resp := mustExecuteRequest(t, context.Background(), db, `{
			"version":"v1",
			"request_id":"req-read-scan-all",
			"mode":"read_only",
			"action":"scan_all",
			"args":{"table":"users","limit":2}
		}`)

		data := mustSuccessData(t, resp)
		rows, ok := data["rows"].([]RowMap)
		if !ok {
			t.Fatalf("unexpected rows payload type %T", data["rows"])
		}
		if len(rows) != 2 || rows[0]["id"] != int64(1) || rows[1]["id"] != int64(2) {
			t.Fatalf("unexpected rows: %#v", rows)
		}
		if got, ok := data["row_count"].(int); !ok || got != 2 {
			t.Fatalf("unexpected row_count: %#v", data["row_count"])
		}
		if got, ok := data["truncated"].(bool); !ok || !got {
			t.Fatalf("unexpected truncated flag: %#v", data["truncated"])
		}
	})

	t.Run("scan_primary_int_range", func(t *testing.T) {
		resp := mustExecuteRequest(t, context.Background(), db, `{
			"version":"v1",
			"request_id":"req-read-range",
			"mode":"read_only",
			"action":"scan_primary_int_range",
			"args":{"table":"users","start_key":2,"end_key":3,"limit":10}
		}`)

		data := mustSuccessData(t, resp)
		rows, ok := data["rows"].([]RowMap)
		if !ok {
			t.Fatalf("unexpected rows payload type %T", data["rows"])
		}
		if len(rows) != 2 || rows[0]["id"] != int64(2) || rows[1]["id"] != int64(3) {
			t.Fatalf("unexpected range rows: %#v", rows)
		}
		if got, ok := data["row_count"].(int); !ok || got != 2 {
			t.Fatalf("unexpected row_count: %#v", data["row_count"])
		}
		if got, ok := data["truncated"].(bool); !ok || got {
			t.Fatalf("unexpected truncated flag: %#v", data["truncated"])
		}
	})

	t.Run("scan_primary_int_range missing", func(t *testing.T) {
		resp := mustExecuteRequest(t, context.Background(), db, `{
			"version":"v1",
			"request_id":"req-read-range-missing",
			"mode":"read_only",
			"action":"scan_primary_int_range",
			"args":{"table":"users","start_key":10,"end_key":20,"limit":10}
		}`)

		data := mustSuccessData(t, resp)
		rows, ok := data["rows"].([]RowMap)
		if !ok {
			t.Fatalf("unexpected rows payload type %T", data["rows"])
		}
		if len(rows) != 0 {
			t.Fatalf("unexpected missing range rows: %#v", rows)
		}
		if got, ok := data["row_count"].(int); !ok || got != 0 {
			t.Fatalf("unexpected row_count: %#v", data["row_count"])
		}
		if got, ok := data["truncated"].(bool); !ok || got {
			t.Fatalf("unexpected truncated flag: %#v", data["truncated"])
		}
	})

	t.Run("scan_primary_int_range limit and truncated", func(t *testing.T) {
		resp := mustExecuteRequest(t, context.Background(), db, `{
			"version":"v1",
			"request_id":"req-read-range-limit",
			"mode":"read_only",
			"action":"scan_primary_int_range",
			"args":{"table":"users","start_key":1,"end_key":3,"limit":2}
		}`)

		data := mustSuccessData(t, resp)
		rows, ok := data["rows"].([]RowMap)
		if !ok {
			t.Fatalf("unexpected rows payload type %T", data["rows"])
		}
		if len(rows) != 2 || rows[0]["id"] != int64(1) || rows[1]["id"] != int64(2) {
			t.Fatalf("unexpected limited range rows: %#v", rows)
		}
		if got, ok := data["row_count"].(int); !ok || got != 2 {
			t.Fatalf("unexpected row_count: %#v", data["row_count"])
		}
		if got, ok := data["truncated"].(bool); !ok || !got {
			t.Fatalf("unexpected truncated flag: %#v", data["truncated"])
		}
	})
}

func TestExecuteWriteActions(t *testing.T) {
	db := openExecuteTestDB(t)
	defer closeExecuteTestDB(t, db)

	createUsersTable(t, db, true)
	createProfilesTable(t, db)
	if err := db.InsertRow("profiles", []haruhidb.Value{
		haruhidb.Int32Value(10),
		haruhidb.StringValue("profile-a"),
	}); err != nil {
		t.Fatalf("insert profile seed failed: %v", err)
	}

	t.Run("insert_row", func(t *testing.T) {
		resp := mustExecuteRequest(t, context.Background(), db, `{
			"version":"v1",
			"request_id":"req-write-insert",
			"mode":"read_write",
			"action":"insert_row",
			"args":{"table":"users","values":{"id":1,"name":"alice"}}
		}`)

		data := mustSuccessData(t, resp)
		if got, ok := data["inserted"].(int); !ok || got != 1 {
			t.Fatalf("unexpected inserted count: %#v", data["inserted"])
		}
	})

	t.Run("typed db error maps to protocol error", func(t *testing.T) {
		directErr := db.InsertRow("users", []haruhidb.Value{
			haruhidb.Int32Value(1),
			haruhidb.StringValue("alice-dup-direct"),
		})
		if directErr == nil {
			t.Fatalf("expected direct duplicate insert to fail")
		}
		expectedCode := mapCatalogError(directErr).Code

		env := mustDecodeEnvelope(t, `{
			"version":"v1",
			"request_id":"req-write-dup",
			"mode":"read_write",
			"action":"insert_row",
			"args":{"table":"users","values":{"id":1,"name":"alice-dup"}}
		}`)

		resp, err := ExecuteEnvelope(context.Background(), db, env)
		if err != nil {
			t.Fatalf("ExecuteEnvelope returned unexpected error: %v", err)
		}
		if resp.Ok {
			t.Fatalf("expected protocol error response, got success: %#v", resp)
		}
		if resp.Error == nil || resp.Error.Code != expectedCode {
			t.Fatalf("unexpected response error: %#v", resp.Error)
		}
	})

	t.Run("delete existing and missing", func(t *testing.T) {
		if err := db.InsertRow("users", []haruhidb.Value{
			haruhidb.Int32Value(2),
			haruhidb.StringValue("bob"),
		}); err != nil {
			t.Fatalf("insert delete seed failed: %v", err)
		}

		resp := mustExecuteRequest(t, context.Background(), db, `{
			"version":"v1",
			"request_id":"req-write-delete",
			"mode":"read_write",
			"action":"delete_by_primary_int",
			"args":{"table":"users","key":2}
		}`)
		if got, ok := mustSuccessData(t, resp)["deleted"].(int); !ok || got != 1 {
			t.Fatalf("unexpected deleted count: %#v", mustSuccessData(t, resp)["deleted"])
		}

		resp = mustExecuteRequest(t, context.Background(), db, `{
			"version":"v1",
			"request_id":"req-write-delete-missing",
			"mode":"read_write",
			"action":"delete_by_primary_int",
			"args":{"table":"users","key":2}
		}`)
		if got, ok := mustSuccessData(t, resp)["deleted"].(int); !ok || got != 0 {
			t.Fatalf("unexpected missing deleted count: %#v", mustSuccessData(t, resp)["deleted"])
		}
	})

	t.Run("update with index patch", func(t *testing.T) {
		if err := db.InsertRow("users", []haruhidb.Value{
			haruhidb.Int32Value(3),
			haruhidb.StringValue("before"),
		}); err != nil {
			t.Fatalf("insert update seed failed: %v", err)
		}

		resp := mustExecuteRequest(t, context.Background(), db, `{
			"version":"v1",
			"request_id":"req-write-update-index",
			"mode":"read_write",
			"action":"update_by_primary_int",
			"args":{"table":"users","key":3,"values":{"name":"after"}}
		}`)
		if got, ok := mustSuccessData(t, resp)["updated"].(int); !ok || got != 1 {
			t.Fatalf("unexpected updated count: %#v", mustSuccessData(t, resp)["updated"])
		}

		row, found, err := db.GetRowByPrimaryInt("users", 3)
		if err != nil {
			t.Fatalf("GetRowByPrimaryInt failed: %v", err)
		}
		if !found || row.Values[1].String != "after" {
			t.Fatalf("unexpected updated row: found=%v row=%#v", found, row.Values)
		}
	})

	t.Run("update without index falls back to scan", func(t *testing.T) {
		resp := mustExecuteRequest(t, context.Background(), db, `{
			"version":"v1",
			"request_id":"req-write-update-scan",
			"mode":"read_write",
			"action":"update_by_primary_int",
			"args":{"table":"profiles","key":10,"values":{"name":"profile-b"}}
		}`)
		if got, ok := mustSuccessData(t, resp)["updated"].(int); !ok || got != 1 {
			t.Fatalf("unexpected updated count: %#v", mustSuccessData(t, resp)["updated"])
		}

		scan, err := db.ScanAll("profiles")
		if err != nil {
			t.Fatalf("ScanAll failed: %v", err)
		}
		defer func() {
			if closeErr := scan.Close(); closeErr != nil {
				t.Fatalf("scan close failed: %v", closeErr)
			}
		}()
		row, err := scan.Next()
		if err != nil {
			t.Fatalf("scan next failed: %v", err)
		}
		if row.Values[1].String != "profile-b" {
			t.Fatalf("unexpected profile row: %#v", row.Values)
		}
	})

	t.Run("update missing row returns zero", func(t *testing.T) {
		resp := mustExecuteRequest(t, context.Background(), db, `{
			"version":"v1",
			"request_id":"req-write-update-missing",
			"mode":"read_write",
			"action":"update_by_primary_int",
			"args":{"table":"profiles","key":999,"values":{"name":"missing"}}
		}`)
		if got, ok := mustSuccessData(t, resp)["updated"].(int); !ok || got != 0 {
			t.Fatalf("unexpected missing updated count: %#v", mustSuccessData(t, resp)["updated"])
		}
	})
}

func TestExecuteV3DDLActions(t *testing.T) {
	db := openExecuteTestDB(t)
	defer closeExecuteTestDB(t, db)

	t.Run("create_table create_index insert query drop", func(t *testing.T) {
		resp := mustExecuteRequest(t, context.Background(), db, `{
			"version":"v3",
			"request_id":"req-v3-create-table",
			"mode":"read_write",
			"action":"create_table",
			"args":{
				"table":"ddl_users",
				"columns":[
					{"name":"id","type":"INTEGER","nullable":false},
					{"name":"name","type":"VARCHAR","length":32,"nullable":false}
				]
			}
		}`)
		if got, ok := mustSuccessData(t, resp)["created"].(int); !ok || got != 1 {
			t.Fatalf("unexpected created count: %#v", mustSuccessData(t, resp)["created"])
		}

		resp = mustExecuteRequest(t, context.Background(), db, `{
			"version":"v3",
			"request_id":"req-v3-create-index",
			"mode":"read_write",
			"action":"create_primary_int_index",
			"args":{"table":"ddl_users","index":"idx_ddl_users_id"}
		}`)
		if got, ok := mustSuccessData(t, resp)["created"].(int); !ok || got != 1 {
			t.Fatalf("unexpected index created count: %#v", mustSuccessData(t, resp)["created"])
		}

		resp = mustExecuteRequest(t, context.Background(), db, `{
			"version":"v3",
			"request_id":"req-v3-insert",
			"mode":"read_write",
			"action":"insert_row",
			"args":{"table":"ddl_users","values":{"id":11,"name":"alice"}}
		}`)
		if got, ok := mustSuccessData(t, resp)["inserted"].(int); !ok || got != 1 {
			t.Fatalf("unexpected inserted count: %#v", mustSuccessData(t, resp)["inserted"])
		}

		resp = mustExecuteRequest(t, context.Background(), db, `{
			"version":"v3",
			"request_id":"req-v3-get",
			"mode":"read_only",
			"action":"get_by_primary_int",
			"args":{"table":"ddl_users","key":11}
		}`)
		row, ok := mustSuccessData(t, resp)["row"].(RowMap)
		if !ok {
			t.Fatalf("unexpected row type: %T", mustSuccessData(t, resp)["row"])
		}
		if row["id"] != int64(11) || row["name"] != "alice" {
			t.Fatalf("unexpected row payload: %#v", row)
		}

		resp = mustExecuteRequest(t, context.Background(), db, `{
			"version":"v3",
			"request_id":"req-v3-drop-index",
			"mode":"read_write",
			"action":"drop_index",
			"args":{"table":"ddl_users","index":"idx_ddl_users_id"}
		}`)
		if got, ok := mustSuccessData(t, resp)["dropped"].(int); !ok || got != 1 {
			t.Fatalf("unexpected index dropped count: %#v", mustSuccessData(t, resp)["dropped"])
		}

		resp = mustExecuteRequest(t, context.Background(), db, `{
			"version":"v3",
			"request_id":"req-v3-drop-table",
			"mode":"read_write",
			"action":"drop_table",
			"args":{"table":"ddl_users"}
		}`)
		if got, ok := mustSuccessData(t, resp)["dropped"].(int); !ok || got != 1 {
			t.Fatalf("unexpected table dropped count: %#v", mustSuccessData(t, resp)["dropped"])
		}

		resp = mustExecuteRequest(t, context.Background(), db, `{
			"version":"v3",
			"request_id":"req-v3-table-exists",
			"mode":"read_only",
			"action":"table_exists",
			"args":{"table":"ddl_users"}
		}`)
		if got, ok := mustSuccessData(t, resp)["exists"].(bool); !ok || got {
			t.Fatalf("unexpected exists after drop: %#v", mustSuccessData(t, resp)["exists"])
		}
	})
}
func TestExecuteEnvelopeValidationFailureReturnsProtocolResponse(t *testing.T) {
	db := openExecuteTestDB(t)
	defer closeExecuteTestDB(t, db)

	env := RequestEnvelope{
		Version:   "v9",
		RequestID: "req-invalid-version",
		Mode:      ModeReadOnly,
		Action:    ActionListTables,
		Args:      []byte(`{}`),
	}

	resp, err := ExecuteEnvelope(context.Background(), db, env)
	if err != nil {
		t.Fatalf("ExecuteEnvelope returned unexpected error: %v", err)
	}
	if resp.Ok {
		t.Fatalf("expected validation failure response, got success: %#v", resp)
	}
	if resp.Error == nil || resp.Error.Code != CodeInvalidRequest {
		t.Fatalf("unexpected response error: %#v", resp.Error)
	}
}

func TestExecuteBatchAction(t *testing.T) {
	t.Run("continue after sub action failure", func(t *testing.T) {
		db := openExecuteTestDB(t)
		defer closeExecuteTestDB(t, db)

		createUsersTable(t, db, true)

		resp := mustExecuteRequest(t, context.Background(), db, `{
			"version":"v1",
			"request_id":"req-batch-continue",
			"mode":"read_write",
			"action":"batch",
			"args":{
				"requests":[
					{
						"action":"insert_row",
						"args":{"table":"users","values":{"id":1,"name":"alice"}}
					},
					{
						"action":"insert_row",
						"args":{"table":"users","values":{"id":1,"name":"alice-dup"}}
					},
					{
						"action":"table_exists",
						"args":{"table":"users"}
					}
				]
			}
		}`)

		data := mustSuccessData(t, resp)
		if got, ok := data["total"].(int); !ok || got != 3 {
			t.Fatalf("unexpected total: %#v", data["total"])
		}
		if got, ok := data["succeeded"].(int); !ok || got != 2 {
			t.Fatalf("unexpected succeeded: %#v", data["succeeded"])
		}
		if got, ok := data["failed"].(int); !ok || got != 1 {
			t.Fatalf("unexpected failed: %#v", data["failed"])
		}
		if got, ok := data["stopped"].(bool); !ok || got {
			t.Fatalf("unexpected stopped flag: %#v", data["stopped"])
		}

		results, ok := data["results"].([]map[string]any)
		if !ok {
			t.Fatalf("unexpected results payload type %T", data["results"])
		}
		if len(results) != 3 {
			t.Fatalf("unexpected results size: %#v", results)
		}
		if got, ok := results[0]["ok"].(bool); !ok || !got {
			t.Fatalf("unexpected first item status: %#v", results[0])
		}
		if got, ok := results[1]["ok"].(bool); !ok || got {
			t.Fatalf("unexpected second item status: %#v", results[1])
		}
		errorMap, ok := results[1]["error"].(map[string]any)
		if !ok || errorMap["code"] == "" {
			t.Fatalf("unexpected second item error: %#v", results[1]["error"])
		}
		thirdData, ok := results[2]["data"].(map[string]any)
		if !ok {
			t.Fatalf("unexpected third item data type %T", results[2]["data"])
		}
		if got, ok := thirdData["exists"].(bool); !ok || !got {
			t.Fatalf("unexpected third item table_exists data: %#v", thirdData)
		}
	})

	t.Run("stop on first sub action failure", func(t *testing.T) {
		db := openExecuteTestDB(t)
		defer closeExecuteTestDB(t, db)

		createUsersTable(t, db, true)

		resp := mustExecuteRequest(t, context.Background(), db, `{
			"version":"v1",
			"request_id":"req-batch-stop",
			"mode":"read_write",
			"action":"batch",
			"args":{
				"stop_on_error": true,
				"requests":[
					{
						"action":"insert_row",
						"args":{"table":"users","values":{"id":1,"name":"alice"}}
					},
					{
						"action":"insert_row",
						"args":{"table":"users","values":{"id":1,"name":"alice-dup"}}
					},
					{
						"action":"table_exists",
						"args":{"table":"users"}
					}
				]
			}
		}`)

		data := mustSuccessData(t, resp)
		if got, ok := data["total"].(int); !ok || got != 3 {
			t.Fatalf("unexpected total: %#v", data["total"])
		}
		if got, ok := data["succeeded"].(int); !ok || got != 1 {
			t.Fatalf("unexpected succeeded: %#v", data["succeeded"])
		}
		if got, ok := data["failed"].(int); !ok || got != 1 {
			t.Fatalf("unexpected failed: %#v", data["failed"])
		}
		if got, ok := data["stopped"].(bool); !ok || !got {
			t.Fatalf("unexpected stopped flag: %#v", data["stopped"])
		}

		results, ok := data["results"].([]map[string]any)
		if !ok {
			t.Fatalf("unexpected results payload type %T", data["results"])
		}
		if len(results) != 2 {
			t.Fatalf("unexpected results size when stop_on_error=true: %#v", results)
		}
	})
}

func TestExecuteBatchActionShowcaseAllFeatures(t *testing.T) {
	db := openExecuteTestDB(t)
	defer closeExecuteTestDB(t, db)

	createUsersTable(t, db, true)
	insertUserRows(t, db,
		[]haruhidb.Value{haruhidb.Int32Value(1), haruhidb.StringValue("alice")},
		[]haruhidb.Value{haruhidb.Int32Value(2), haruhidb.StringValue("bob")},
		[]haruhidb.Value{haruhidb.Int32Value(3), haruhidb.StringValue("cindy")},
	)

	resp := mustExecuteRequest(t, context.Background(), db, `{
		"version":"v1",
		"request_id":"req-batch-showcase",
		"mode":"read_write",
		"action":"batch",
		"args":{
			"stop_on_error":false,
			"requests":[
				{"action":"list_tables","args":{}},
				{"action":"table_exists","args":{"table":"users"}},
				{"action":"describe_table","args":{"table":"users"}},
				{"action":"scan_all","args":{"table":"users","limit":2}},
				{"action":"scan_primary_int_range","args":{"table":"users","start_key":1,"end_key":3,"limit":10}},
				{"action":"insert_row","args":{"table":"users","values":{"id":100,"name":"new-user"}}},
				{"action":"get_by_primary_int","args":{"table":"users","key":100}},
				{"action":"update_by_primary_int","args":{"table":"users","key":100,"values":{"name":"new-user-updated"}}},
				{"action":"delete_by_primary_int","args":{"table":"users","key":100}},
				{"action":"get_by_primary_int","args":{"table":"users","key":100}}
			]
		}
	}`)

	data := mustSuccessData(t, resp)
	if got, ok := data["total"].(int); !ok || got != 10 {
		t.Fatalf("unexpected total: %#v", data["total"])
	}
	if got, ok := data["succeeded"].(int); !ok || got != 10 {
		t.Fatalf("unexpected succeeded: %#v", data["succeeded"])
	}
	if got, ok := data["failed"].(int); !ok || got != 0 {
		t.Fatalf("unexpected failed: %#v", data["failed"])
	}
	if got, ok := data["stopped"].(bool); !ok || got {
		t.Fatalf("unexpected stopped: %#v", data["stopped"])
	}

	results, ok := data["results"].([]map[string]any)
	if !ok {
		t.Fatalf("unexpected results payload type %T", data["results"])
	}
	if len(results) != 10 {
		t.Fatalf("unexpected results length: %d", len(results))
	}

	item1Data, ok := results[1]["data"].(map[string]any)
	if !ok {
		t.Fatalf("unexpected table_exists data type %T", results[1]["data"])
	}
	if exists, ok := item1Data["exists"].(bool); !ok || !exists {
		t.Fatalf("unexpected table_exists payload: %#v", item1Data)
	}

	item6Data, ok := results[6]["data"].(map[string]any)
	if !ok {
		t.Fatalf("unexpected get_by_primary_int data type %T", results[6]["data"])
	}
	if found, ok := item6Data["found"].(bool); !ok || !found {
		t.Fatalf("unexpected found payload for inserted row: %#v", item6Data)
	}
	row, ok := item6Data["row"].(RowMap)
	if !ok {
		t.Fatalf("unexpected row type: %T", item6Data["row"])
	}
	if row["id"] != int64(100) || row["name"] != "new-user" {
		t.Fatalf("unexpected inserted row: %#v", row)
	}

	item9Data, ok := results[9]["data"].(map[string]any)
	if !ok {
		t.Fatalf("unexpected final get data type %T", results[9]["data"])
	}
	if found, ok := item9Data["found"].(bool); !ok || found {
		t.Fatalf("expected final get to miss, got %#v", item9Data)
	}
	if item9Data["row"] != nil {
		t.Fatalf("expected final row nil, got %#v", item9Data["row"])
	}
}

func TestExecuteReturnsContextCanceledBeforeCall(t *testing.T) {
	db := openExecuteTestDB(t)
	defer closeExecuteTestDB(t, db)
	createUsersTable(t, db, true)

	req := mustValidatedRequest(t, db, `{
		"version":"v1",
		"request_id":"req-cancel-before",
		"mode":"read_only",
		"action":"list_tables",
		"args":{}
	}`)

	ctx, cancel := context.WithCancel(context.Background())
	cancel()

	_, err := Execute(ctx, db, req)
	if !errors.Is(err, context.Canceled) {
		t.Fatalf("expected context.Canceled, got %v", err)
	}
}

func TestScanToRowsClosesScannerOnContextCancel(t *testing.T) {
	db := openExecuteTestDB(t)
	defer closeExecuteTestDB(t, db)

	createUsersTable(t, db, true)
	insertUserRows(t, db,
		[]haruhidb.Value{haruhidb.Int32Value(1), haruhidb.StringValue("alice")},
		[]haruhidb.Value{haruhidb.Int32Value(2), haruhidb.StringValue("bob")},
	)

	scan, err := db.ScanAll("users")
	if err != nil {
		t.Fatalf("ScanAll failed: %v", err)
	}

	ctx := &stepCancelContext{cancelOnCall: 2}
	_, _, err = scanToRows(ctx, []haruhidb.ColumnInfo{
		{Name: "id", Type: haruhidb.TypeInteger, Nullable: false},
		{Name: "name", Type: haruhidb.TypeVarchar, Length: 32, Nullable: false},
	}, scan, 10)
	if !errors.Is(err, context.Canceled) {
		t.Fatalf("expected context.Canceled, got %v", err)
	}

	_, nextErr := scan.Next()
	if nextErr == nil || !strings.Contains(nextErr.Error(), "scanner is closed") {
		t.Fatalf("expected closed scanner error, got %v", nextErr)
	}
}

type stepCancelContext struct {
	cancelOnCall int
	calls        int
}

func (c *stepCancelContext) Deadline() (deadline time.Time, ok bool) {
	return time.Time{}, false
}

func (c *stepCancelContext) Done() <-chan struct{} {
	return nil
}

func (c *stepCancelContext) Err() error {
	c.calls++
	if c.calls >= c.cancelOnCall {
		return context.Canceled
	}
	return nil
}

func (c *stepCancelContext) Value(key any) any {
	return nil
}

func openExecuteTestDB(t *testing.T) *haruhidb.DB {
	t.Helper()

	dir := t.TempDir()
	dbPath := filepath.Join(dir, "action_execute.db")

	db, err := haruhidb.Open(dbPath, haruhidb.OpenOptions{})
	if err != nil {
		t.Fatalf("open test db failed: %v", err)
	}
	return db
}

func closeExecuteTestDB(t *testing.T, db *haruhidb.DB) {
	t.Helper()
	if db == nil {
		return
	}
	if err := db.Close(); err != nil {
		t.Fatalf("close test db failed: %v", err)
	}
}

func createUsersTable(t *testing.T, db *haruhidb.DB, withIndex bool) {
	t.Helper()
	if err := db.CreateTable("users", []haruhidb.ColumnDef{
		{Name: "id", Type: haruhidb.TypeInteger},
		{Name: "name", Type: haruhidb.TypeVarchar, Length: 32},
	}); err != nil {
		t.Fatalf("create users table failed: %v", err)
	}
	if withIndex {
		if err := db.CreatePrimaryIntIndex("users", "idx_users_id"); err != nil {
			t.Fatalf("create users index failed: %v", err)
		}
	}
}

func createProfilesTable(t *testing.T, db *haruhidb.DB) {
	t.Helper()
	if err := db.CreateTable("profiles", []haruhidb.ColumnDef{
		{Name: "id", Type: haruhidb.TypeInteger},
		{Name: "name", Type: haruhidb.TypeVarchar, Length: 32},
	}); err != nil {
		t.Fatalf("create profiles table failed: %v", err)
	}
}

func insertUserRows(t *testing.T, db *haruhidb.DB, rows ...[]haruhidb.Value) {
	t.Helper()
	for _, row := range rows {
		if err := db.InsertRow("users", row); err != nil {
			t.Fatalf("insert user row failed: %v", err)
		}
	}
}

func mustDecodeEnvelope(t *testing.T, payload string) RequestEnvelope {
	t.Helper()
	env, err := Decode([]byte(payload))
	if err != nil {
		t.Fatalf("Decode failed: %v", err)
	}
	return env
}

func mustValidatedRequest(t *testing.T, db *haruhidb.DB, payload string) *Request {
	t.Helper()
	env := mustDecodeEnvelope(t, payload)
	req, err := ValidateEnvelope(env, db)
	if err != nil {
		t.Fatalf("ValidateEnvelope failed: %v", err)
	}
	return req
}

func mustExecuteRequest(t *testing.T, ctx context.Context, db *haruhidb.DB, payload string) ResponseEnvelope[map[string]any] {
	t.Helper()
	req := mustValidatedRequest(t, db, payload)
	resp, err := Execute(ctx, db, req)
	if err != nil {
		t.Fatalf("Execute failed: %v", err)
	}
	if !resp.Ok {
		t.Fatalf("expected success response, got %#v", resp)
	}
	return resp
}

func mustSuccessData(t *testing.T, resp ResponseEnvelope[map[string]any]) map[string]any {
	t.Helper()
	if !resp.Ok {
		t.Fatalf("expected success response, got %#v", resp)
	}
	if resp.Data == nil {
		t.Fatalf("expected non-nil response data")
	}
	return *resp.Data
}
