package haruhidb

import (
	"errors"
	"io"
	"os"
	"path/filepath"
	"testing"
)

func requireTypedError(t *testing.T, err error, code ErrorCode, op string) *Error {
	t.Helper()
	if err == nil {
		t.Fatalf("expected error code=%d op=%q, got nil", code, op)
	}

	var typed *Error
	if !errors.As(err, &typed) {
		t.Fatalf("expected *Error, got %T (%v)", err, err)
	}
	if typed.Code != code {
		t.Fatalf("unexpected error code: got %d, want %d (err=%v)", typed.Code, code, typed)
	}
	if typed.Op != op {
		t.Fatalf("unexpected op: got %q, want %q", typed.Op, op)
	}
	if typed.Message == "" {
		t.Fatal("typed error message should not be empty")
	}
	return typed
}

func TestAPIVersionAndCapabilities(t *testing.T) {
	if got, want := APIVersion(), "1.1.0"; got != want {
		t.Fatalf("unexpected API version: got %q, want %q", got, want)
	}

	caps := Capabilities()
	required := []Capability{
		CapabilityPrimaryIntIndex,
		CapabilityPrimaryIntPointGet,
		CapabilityPrimaryIntRangeScan,
		CapabilityMetadataRead,
		CapabilityWALRuntimeOption,
	}
	for _, cap := range required {
		if !caps.Has(cap) {
			t.Fatalf("capability bit is missing: %d (caps=%d)", cap, caps)
		}
	}
}

func TestRoundTripOpenCreateInsertScan(t *testing.T) {
	dir := t.TempDir()
	dbPath := filepath.Join(dir, "haruhidb_go_roundtrip.db")
	walPath := filepath.Join(dir, "haruhidb_go_roundtrip.wal")

	db, err := Open(dbPath, OpenOptions{
		BufferPoolSize: 64,
		LRUK:           2,
		EnableWAL:      true,
		WALPath:        walPath,
	})
	if err != nil {
		t.Fatalf("open failed: %v", err)
	}
	defer func() {
		if closeErr := db.Close(); closeErr != nil {
			t.Fatalf("close failed: %v", closeErr)
		}
	}()

	if err := db.CreateTable("student", []ColumnDef{
		{Name: "id", Type: TypeInteger},
		{Name: "name", Type: TypeVarchar, Length: 32},
	}); err != nil {
		t.Fatalf("create table failed: %v", err)
	}

	exists, err := db.TableExists("student")
	if err != nil {
		t.Fatalf("table exists failed: %v", err)
	}
	if !exists {
		t.Fatal("expected table to exist")
	}

	if err := db.CreatePrimaryIntIndex("student", "idx_student_id"); err != nil {
		t.Fatalf("create index failed: %v", err)
	}

	if err := db.InsertRow("student", []Value{
		Int32Value(1),
		StringValue("haruhi"),
	}); err != nil {
		t.Fatalf("insert row 1 failed: %v", err)
	}

	if err := db.InsertRow("student", []Value{
		Int32Value(2),
		StringValue("mio"),
	}); err != nil {
		t.Fatalf("insert row 2 failed: %v", err)
	}

	scan, err := db.ScanAll("student")
	if err != nil {
		t.Fatalf("scan open failed: %v", err)
	}
	defer func() {
		if closeErr := scan.Close(); closeErr != nil {
			t.Fatalf("scan close failed: %v", closeErr)
		}
	}()

	row, err := scan.Next()
	if err != nil {
		t.Fatalf("scan next row 1 failed: %v", err)
	}
	if len(row.Values) != 2 {
		t.Fatalf("unexpected row width: %d", len(row.Values))
	}
	if row.Values[0].Int32 != 1 || row.Values[1].String != "haruhi" {
		t.Fatalf("unexpected row 1 contents: %+v", row.Values)
	}

	row, err = scan.Next()
	if err != nil {
		t.Fatalf("scan next row 2 failed: %v", err)
	}
	if row.Values[0].Int32 != 2 || row.Values[1].String != "mio" {
		t.Fatalf("unexpected row 2 contents: %+v", row.Values)
	}

	_, err = scan.Next()
	if !errors.Is(err, io.EOF) {
		t.Fatalf("expected io.EOF, got %v", err)
	}
}

func TestInsertRejectsNullInFirstVersion(t *testing.T) {
	dir := t.TempDir()
	dbPath := filepath.Join(dir, "haruhidb_go_null.db")

	db, err := Open(dbPath, OpenOptions{})
	if err != nil {
		t.Fatalf("open failed: %v", err)
	}
	defer func() {
		if closeErr := db.Close(); closeErr != nil {
			t.Fatalf("close failed: %v", closeErr)
		}
	}()

	if err := db.CreateTable("student", []ColumnDef{
		{Name: "id", Type: TypeInteger},
	}); err != nil {
		t.Fatalf("create table failed: %v", err)
	}

	err = db.InsertRow("student", []Value{NullValue(TypeInteger)})
	if err == nil {
		t.Fatal("expected NULL insert to fail")
	}
}

func TestUpdateDeleteAndDropByPrimaryInt(t *testing.T) {
	dir := t.TempDir()
	dbPath := filepath.Join(dir, "haruhidb_go_update_delete_drop.db")

	db, err := Open(dbPath, OpenOptions{})
	if err != nil {
		t.Fatalf("open failed: %v", err)
	}
	defer func() {
		if closeErr := db.Close(); closeErr != nil {
			t.Fatalf("close failed: %v", closeErr)
		}
	}()

	if err := db.CreateTable("student", []ColumnDef{
		{Name: "id", Type: TypeInteger},
		{Name: "name", Type: TypeVarchar, Length: 32},
	}); err != nil {
		t.Fatalf("create table failed: %v", err)
	}
	if err := db.CreatePrimaryIntIndex("student", "idx_student_id"); err != nil {
		t.Fatalf("create index failed: %v", err)
	}
	if err := db.InsertRow("student", []Value{Int32Value(1), StringValue("a")}); err != nil {
		t.Fatalf("insert row 1 failed: %v", err)
	}
	if err := db.InsertRow("student", []Value{Int32Value(2), StringValue("b")}); err != nil {
		t.Fatalf("insert row 2 failed: %v", err)
	}

	updated, err := db.UpdateRowByPrimaryInt("student", 1, []Value{
		Int32Value(1),
		StringValue("updated"),
	})
	if err != nil {
		t.Fatalf("update row failed: %v", err)
	}
	if updated != 1 {
		t.Fatalf("unexpected updated count: %d", updated)
	}

	updated, err = db.UpdateRowByPrimaryInt("student", 9, []Value{
		Int32Value(9),
		StringValue("missing"),
	})
	if err != nil {
		t.Fatalf("update missing row failed: %v", err)
	}
	if updated != 0 {
		t.Fatalf("unexpected updated missing count: %d", updated)
	}

	deleted, err := db.DeleteRowByPrimaryInt("student", 2)
	if err != nil {
		t.Fatalf("delete row failed: %v", err)
	}
	if deleted != 1 {
		t.Fatalf("unexpected deleted count: %d", deleted)
	}
	deleted, err = db.DeleteRowByPrimaryInt("student", 2)
	if err != nil {
		t.Fatalf("delete missing row failed: %v", err)
	}
	if deleted != 0 {
		t.Fatalf("unexpected deleted missing count: %d", deleted)
	}

	scan, err := db.ScanAll("student")
	if err != nil {
		t.Fatalf("scan open failed: %v", err)
	}
	row, err := scan.Next()
	if err != nil {
		t.Fatalf("scan next failed: %v", err)
	}
	if row.Values[0].Int32 != 1 || row.Values[1].String != "updated" {
		t.Fatalf("unexpected updated row: %+v", row.Values)
	}
	_, err = scan.Next()
	if !errors.Is(err, io.EOF) {
		t.Fatalf("expected EOF after one row, got %v", err)
	}
	if err := scan.Close(); err != nil {
		t.Fatalf("scan close failed: %v", err)
	}

	if err := db.DropIndex("student", "idx_student_id"); err != nil {
		t.Fatalf("drop index failed: %v", err)
	}
	if err := db.DropTable("student"); err != nil {
		t.Fatalf("drop table failed: %v", err)
	}
	exists, err := db.TableExists("student")
	if err != nil {
		t.Fatalf("table exists after drop failed: %v", err)
	}
	if exists {
		t.Fatal("expected dropped table to not exist")
	}
}

func TestUpdateByPrimaryIntRejectsPayloadKeyMismatch(t *testing.T) {
	dir := t.TempDir()
	dbPath := filepath.Join(dir, "haruhidb_go_update_mismatch.db")

	db, err := Open(dbPath, OpenOptions{})
	if err != nil {
		t.Fatalf("open failed: %v", err)
	}
	defer func() {
		if closeErr := db.Close(); closeErr != nil {
			t.Fatalf("close failed: %v", closeErr)
		}
	}()

	if err := db.CreateTable("student", []ColumnDef{
		{Name: "id", Type: TypeInteger},
		{Name: "name", Type: TypeVarchar, Length: 32},
	}); err != nil {
		t.Fatalf("create table failed: %v", err)
	}
	if err := db.InsertRow("student", []Value{Int32Value(1), StringValue("a")}); err != nil {
		t.Fatalf("insert row failed: %v", err)
	}

	_, err = db.UpdateRowByPrimaryInt("student", 1, []Value{
		Int32Value(2),
		StringValue("bad"),
	})
	if err == nil {
		t.Fatal("expected payload key mismatch to fail")
	}
}

func TestMetadataWrappersTrackLifecycleAndRecovery(t *testing.T) {
	dir := t.TempDir()
	dbPath := filepath.Join(dir, "haruhidb_go_metadata.db")
	walPath := filepath.Join(dir, "haruhidb_go_metadata.wal")

	openOpts := OpenOptions{
		BufferPoolSize: 64,
		LRUK:           2,
		EnableWAL:      true,
		WALPath:        walPath,
	}

	{
		db, err := Open(dbPath, openOpts)
		if err != nil {
			t.Fatalf("open failed: %v", err)
		}

		if err := db.CreateTable("student", []ColumnDef{
			{Name: "id", Type: TypeInteger},
			{Name: "name", Type: TypeVarchar, Length: 32},
		}); err != nil {
			t.Fatalf("create student failed: %v", err)
		}
		if err := db.CreateTable("course", []ColumnDef{
			{Name: "id", Type: TypeInteger},
		}); err != nil {
			t.Fatalf("create course failed: %v", err)
		}
		if err := db.CreatePrimaryIntIndex("student", "idx_student_id"); err != nil {
			t.Fatalf("create index failed: %v", err)
		}

		tables, err := db.ListTables()
		if err != nil {
			t.Fatalf("list tables failed: %v", err)
		}
		if len(tables) != 2 || tables[0] != "student" || tables[1] != "course" {
			t.Fatalf("unexpected table list: %#v", tables)
		}

		columns, err := db.ListTableColumns("student")
		if err != nil {
			t.Fatalf("list columns failed: %v", err)
		}
		if len(columns) != 2 {
			t.Fatalf("unexpected column count: %#v", columns)
		}
		if columns[0].Name != "id" || columns[0].Type != TypeInteger || columns[0].Length != 0 || columns[0].Nullable {
			t.Fatalf("unexpected first column: %#v", columns[0])
		}
		if columns[1].Name != "name" || columns[1].Type != TypeVarchar || columns[1].Length != 32 || columns[1].Nullable {
			t.Fatalf("unexpected second column: %#v", columns[1])
		}

		indexes, err := db.ListTableIndexes("student")
		if err != nil {
			t.Fatalf("list indexes failed: %v", err)
		}
		if len(indexes) != 1 || indexes[0] != "idx_student_id" {
			t.Fatalf("unexpected index list: %#v", indexes)
		}

		if err := db.DropIndex("student", "idx_student_id"); err != nil {
			t.Fatalf("drop index failed: %v", err)
		}
		if err := db.DropTable("course"); err != nil {
			t.Fatalf("drop course failed: %v", err)
		}

		tables, err = db.ListTables()
		if err != nil {
			t.Fatalf("list tables after drop failed: %v", err)
		}
		if len(tables) != 1 || tables[0] != "student" {
			t.Fatalf("unexpected table list after drop: %#v", tables)
		}

		if err := db.Close(); err != nil {
			t.Fatalf("close failed: %v", err)
		}
	}

	{
		db, err := Open(dbPath, openOpts)
		if err != nil {
			t.Fatalf("reopen failed: %v", err)
		}
		defer func() {
			if closeErr := db.Close(); closeErr != nil {
				t.Fatalf("close failed: %v", closeErr)
			}
		}()

		tables, err := db.ListTables()
		if err != nil {
			t.Fatalf("list tables after reopen failed: %v", err)
		}
		if len(tables) != 1 || tables[0] != "student" {
			t.Fatalf("unexpected table list after reopen: %#v", tables)
		}

		indexes, err := db.ListTableIndexes("student")
		if err != nil {
			t.Fatalf("list indexes after reopen failed: %v", err)
		}
		if len(indexes) != 0 {
			t.Fatalf("expected no indexes after reopen, got %#v", indexes)
		}
	}
}

func TestTypedErrorsMapToCErrorCodes(t *testing.T) {
	{
		_, err := Open("/proc/haruhidb_go_io_failure.db", OpenOptions{})
		requireTypedError(t, err, ErrorIO, "Open")
		if !errors.Is(err, ErrIO) {
			t.Fatalf("errors.Is should match ErrIO, got %v", err)
		}
	}

	dir := t.TempDir()
	dbPath := filepath.Join(dir, "haruhidb_go_typed_error.db")
	db, err := Open(dbPath, OpenOptions{})
	if err != nil {
		t.Fatalf("open failed: %v", err)
	}
	defer func() {
		if closeErr := db.Close(); closeErr != nil {
			t.Fatalf("close failed: %v", closeErr)
		}
	}()

	if err := db.CreateTable("student", []ColumnDef{
		{Name: "id", Type: TypeInteger},
		{Name: "name", Type: TypeVarchar, Length: 32},
	}); err != nil {
		t.Fatalf("create table failed: %v", err)
	}

	err = db.CreateTable("student", []ColumnDef{
		{Name: "id", Type: TypeInteger},
		{Name: "name", Type: TypeVarchar, Length: 32},
	})
	requireTypedError(t, err, ErrorAlreadyExists, "CreateTable")
	if !errors.Is(err, ErrAlreadyExists) {
		t.Fatalf("errors.Is should match ErrAlreadyExists, got %v", err)
	}
	if !errors.Is(err, &Error{Code: ErrorAlreadyExists, Op: "CreateTable"}) {
		t.Fatalf("errors.Is should match op-specific template, got %v", err)
	}

	err = db.DropTable("missing_table")
	requireTypedError(t, err, ErrorNotFound, "DropTable")
	if !errors.Is(err, ErrNotFound) {
		t.Fatalf("errors.Is should match ErrNotFound, got %v", err)
	}

	err = db.CreateTable("unsupported_t", []ColumnDef{
		{Name: "nullable_col", Type: TypeInteger, Nullable: true},
	})
	requireTypedError(t, err, ErrorUnsupported, "CreateTable")
	if !errors.Is(err, ErrUnsupported) {
		t.Fatalf("errors.Is should match ErrUnsupported, got %v", err)
	}

	err = db.InsertRow("student", []Value{
		StringValue("bad"),
		StringValue("name"),
	})
	requireTypedError(t, err, ErrorConstraint, "InsertRow")
	if !errors.Is(err, ErrConstraint) {
		t.Fatalf("errors.Is should match ErrConstraint, got %v", err)
	}
}

func TestQuickstartLikeFlowProducesDemoArtifacts(t *testing.T) {
	outputDir := filepath.Join("test_output")
	if err := os.MkdirAll(outputDir, 0o755); err != nil {
		t.Fatalf("mkdir output dir failed: %v", err)
	}

	dbPath := filepath.Join(outputDir, "quickstart_demo.db")
	walPath := filepath.Join(outputDir, "quickstart_demo.wal")
	_ = os.Remove(dbPath)
	_ = os.Remove(walPath)

	t.Logf("quickstart-like demo artifact db: %s", dbPath)
	t.Logf("quickstart-like demo artifact wal: %s", walPath)

	openOpts := OpenOptions{
		BufferPoolSize: 64,
		LRUK:           2,
		EnableWAL:      true,
		WALPath:        walPath,
	}

	{
		db, err := Open(dbPath, openOpts)
		if err != nil {
			t.Fatalf("open failed: %v", err)
		}

		if err := db.CreateTable("student", []ColumnDef{
			{Name: "id", Type: TypeInteger},
			{Name: "name", Type: TypeVarchar, Length: 32},
		}); err != nil {
			t.Fatalf("create table failed: %v", err)
		}
		if err := db.CreatePrimaryIntIndex("student", "idx_student_id"); err != nil {
			t.Fatalf("create index failed: %v", err)
		}

		for _, row := range []Row{
			{Values: []Value{Int32Value(1), StringValue("haruhi")}},
			{Values: []Value{Int32Value(2), StringValue("mio")}},
			{Values: []Value{Int32Value(3), StringValue("yuki")}},
		} {
			if err := db.InsertRow("student", row.Values); err != nil {
				t.Fatalf("insert row failed: %v", err)
			}
		}

		{
			scan, err := db.ScanAll("student")
			if err != nil {
				t.Fatalf("scan all failed: %v", err)
			}
			defer func() {
				if closeErr := scan.Close(); closeErr != nil {
					t.Fatalf("scan close failed: %v", closeErr)
				}
			}()

			gotIDs := make([]int32, 0, 3)
			gotNames := make([]string, 0, 3)
			for {
				row, nextErr := scan.Next()
				if errors.Is(nextErr, io.EOF) {
					break
				}
				if nextErr != nil {
					t.Fatalf("scan next failed: %v", nextErr)
				}
				if len(row.Values) != 2 {
					t.Fatalf("unexpected row width: %+v", row)
				}
				gotIDs = append(gotIDs, row.Values[0].Int32)
				gotNames = append(gotNames, row.Values[1].String)
			}

			if len(gotIDs) != 3 || gotIDs[0] != 1 || gotIDs[1] != 2 || gotIDs[2] != 3 {
				t.Fatalf("unexpected seq scan ids: %#v", gotIDs)
			}
			if len(gotNames) != 3 || gotNames[0] != "haruhi" || gotNames[1] != "mio" || gotNames[2] != "yuki" {
				t.Fatalf("unexpected seq scan names: %#v", gotNames)
			}
		}

		{
			scan, err := db.ScanByPrimaryIntRange("student", 2, 3)
			if err != nil {
				t.Fatalf("range scan open failed: %v", err)
			}
			defer func() {
				if closeErr := scan.Close(); closeErr != nil {
					t.Fatalf("range scan close failed: %v", closeErr)
				}
			}()

			got := make([]int32, 0, 2)
			for {
				row, nextErr := scan.Next()
				if errors.Is(nextErr, io.EOF) {
					break
				}
				if nextErr != nil {
					t.Fatalf("range scan next failed: %v", nextErr)
				}
				got = append(got, row.Values[0].Int32)
			}
			if len(got) != 2 || got[0] != 2 || got[1] != 3 {
				t.Fatalf("unexpected range scan ids: %#v", got)
			}
		}

		updated, err := db.UpdateRowByPrimaryInt("student", 2, []Value{
			Int32Value(2),
			StringValue("mio-updated"),
		})
		if err != nil {
			t.Fatalf("update failed: %v", err)
		}
		if updated != 1 {
			t.Fatalf("unexpected updated count: %d", updated)
		}

		deleted, err := db.DeleteRowByPrimaryInt("student", 1)
		if err != nil {
			t.Fatalf("delete failed: %v", err)
		}
		if deleted != 1 {
			t.Fatalf("unexpected deleted count: %d", deleted)
		}

		{
			scan, err := db.ScanAll("student")
			if err != nil {
				t.Fatalf("scan all after update/delete failed: %v", err)
			}
			defer func() {
				if closeErr := scan.Close(); closeErr != nil {
					t.Fatalf("scan close failed: %v", closeErr)
				}
			}()

			gotNames := make([]string, 0, 2)
			gotIDs := make([]int32, 0, 2)
			for {
				row, nextErr := scan.Next()
				if errors.Is(nextErr, io.EOF) {
					break
				}
				if nextErr != nil {
					t.Fatalf("scan next failed: %v", nextErr)
				}
				gotIDs = append(gotIDs, row.Values[0].Int32)
				gotNames = append(gotNames, row.Values[1].String)
			}

			if len(gotIDs) != 2 || gotIDs[0] != 2 || gotIDs[1] != 3 {
				t.Fatalf("unexpected ids after update/delete: %#v", gotIDs)
			}
			if len(gotNames) != 2 || gotNames[0] != "mio-updated" || gotNames[1] != "yuki" {
				t.Fatalf("unexpected names after update/delete: %#v", gotNames)
			}
		}

		if err := db.Close(); err != nil {
			t.Fatalf("close failed: %v", err)
		}
	}

	{
		db, err := Open(dbPath, openOpts)
		if err != nil {
			t.Fatalf("reopen failed: %v", err)
		}
		defer func() {
			if closeErr := db.Close(); closeErr != nil {
				t.Fatalf("close failed: %v", closeErr)
			}
		}()

		row, found, err := db.GetRowByPrimaryInt("student", 2)
		if err != nil {
			t.Fatalf("get row by primary int failed: %v", err)
		}
		if !found {
			t.Fatal("expected id=2 to exist after reopen")
		}
		if len(row.Values) != 2 || row.Values[0].Int32 != 2 || row.Values[1].String != "mio-updated" {
			t.Fatalf("unexpected recovered row: %#v", row.Values)
		}

		scan, err := db.ScanByPrimaryIntRange("student", 2, 3)
		if err != nil {
			t.Fatalf("recovered range scan open failed: %v", err)
		}
		defer func() {
			if closeErr := scan.Close(); closeErr != nil {
				t.Fatalf("recovered range scan close failed: %v", closeErr)
			}
		}()

		got := make([]int32, 0, 2)
		for {
			nextRow, nextErr := scan.Next()
			if errors.Is(nextErr, io.EOF) {
				break
			}
			if nextErr != nil {
				t.Fatalf("recovered range scan next failed: %v", nextErr)
			}
			got = append(got, nextRow.Values[0].Int32)
		}
		if len(got) != 2 || got[0] != 2 || got[1] != 3 {
			t.Fatalf("unexpected recovered range scan ids: %#v", got)
		}
	}

	dbInfo, err := os.Stat(dbPath)
	if err != nil {
		t.Fatalf("stat db artifact failed: %v", err)
	}
	if dbInfo.Size() <= 0 {
		t.Fatalf("db artifact is empty: %s", dbPath)
	}
	t.Logf("db artifact size: %d bytes", dbInfo.Size())

	walInfo, err := os.Stat(walPath)
	if err != nil {
		t.Fatalf("stat wal artifact failed: %v", err)
	}
	t.Logf("wal artifact size: %d bytes", walInfo.Size())
}
