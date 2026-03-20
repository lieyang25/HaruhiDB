package haruhidb

/*
#cgo CFLAGS: -I${SRCDIR}/../../CXX/src/include
#cgo LDFLAGS: -L${SRCDIR}/../../CXX/build/src/capi -lharuhidb_capi -Wl,-rpath,${SRCDIR}/../../CXX/build/src/capi
#include <stdlib.h>
#include "capi/haruhidb.h"
*/
import "C"

import (
	"errors"
	"fmt"
	"io"
	"unsafe"
)

type Type uint8

const (
	TypeInvalid  Type = Type(C.HARUHIDB_TYPE_INVALID)
	TypeBoolean  Type = Type(C.HARUHIDB_TYPE_BOOLEAN)
	TypeTinyInt  Type = Type(C.HARUHIDB_TYPE_TINYINT)
	TypeSmallInt Type = Type(C.HARUHIDB_TYPE_SMALLINT)
	TypeInteger  Type = Type(C.HARUHIDB_TYPE_INTEGER)
	TypeBigInt   Type = Type(C.HARUHIDB_TYPE_BIGINT)
	TypeFloat    Type = Type(C.HARUHIDB_TYPE_FLOAT)
	TypeDouble   Type = Type(C.HARUHIDB_TYPE_DOUBLE)
	TypeDecimal  Type = Type(C.HARUHIDB_TYPE_DECIMAL)
	TypeVarchar  Type = Type(C.HARUHIDB_TYPE_VARCHAR)
)

type ErrorCode uint8

const (
	ErrorOK              ErrorCode = ErrorCode(C.HARUHIDB_ERROR_OK)
	ErrorInvalidArgument ErrorCode = ErrorCode(C.HARUHIDB_ERROR_INVALID_ARGUMENT)
	ErrorInvalidHandle   ErrorCode = ErrorCode(C.HARUHIDB_ERROR_INVALID_HANDLE)
	ErrorNotFound        ErrorCode = ErrorCode(C.HARUHIDB_ERROR_NOT_FOUND)
	ErrorAlreadyExists   ErrorCode = ErrorCode(C.HARUHIDB_ERROR_ALREADY_EXISTS)
	ErrorUnsupported     ErrorCode = ErrorCode(C.HARUHIDB_ERROR_UNSUPPORTED)
	ErrorConstraint      ErrorCode = ErrorCode(C.HARUHIDB_ERROR_CONSTRAINT)
	ErrorIO              ErrorCode = ErrorCode(C.HARUHIDB_ERROR_IO)
	ErrorInternal        ErrorCode = ErrorCode(C.HARUHIDB_ERROR_INTERNAL)
	ErrorUnknown         ErrorCode = 255
)

type Capability uint64

const (
	CapabilityPrimaryIntIndex     Capability = Capability(C.HARUHIDB_CAPABILITY_PRIMARY_INT_INDEX)
	CapabilityPrimaryIntPointGet  Capability = Capability(C.HARUHIDB_CAPABILITY_PRIMARY_INT_POINT_GET)
	CapabilityPrimaryIntRangeScan Capability = Capability(C.HARUHIDB_CAPABILITY_PRIMARY_INT_RANGE_SCAN)
	CapabilityMetadataRead        Capability = Capability(C.HARUHIDB_CAPABILITY_METADATA_READ)
	CapabilityWALRuntimeOption    Capability = Capability(C.HARUHIDB_CAPABILITY_WAL_RUNTIME_OPTION)
)

func (c Capability) Has(flag Capability) bool {
	return c&flag != 0
}

type Error struct {
	Op      string
	Code    ErrorCode
	Message string
}

func (e *Error) Error() string {
	if e == nil {
		return "<nil>"
	}
	if e.Op == "" {
		return e.Message
	}
	return fmt.Sprintf("%s: %s", e.Op, e.Message)
}

func (e *Error) Is(target error) bool {
	t, ok := target.(*Error)
	if !ok {
		return false
	}
	if t.Code != ErrorUnknown && e.Code != t.Code {
		return false
	}
	if t.Op != "" && e.Op != t.Op {
		return false
	}
	return true
}

var (
	ErrInvalidArgument = &Error{Code: ErrorInvalidArgument}
	ErrInvalidHandle   = &Error{Code: ErrorInvalidHandle}
	ErrNotFound        = &Error{Code: ErrorNotFound}
	ErrAlreadyExists   = &Error{Code: ErrorAlreadyExists}
	ErrUnsupported     = &Error{Code: ErrorUnsupported}
	ErrConstraint      = &Error{Code: ErrorConstraint}
	ErrIO              = &Error{Code: ErrorIO}
	ErrInternal        = &Error{Code: ErrorInternal}
)

type OpenOptions struct {
	BufferPoolSize uint64
	LRUK           uint64
	EnableWAL      bool
	WALPath        string
}

type ColumnDef struct {
	Name     string
	Type     Type
	Length   uint32
	Nullable bool
}

type Value struct {
	Type    Type
	Null    bool
	Bool    bool
	Int8    int8
	Int16   int16
	Int32   int32
	Int64   int64
	Float32 float32
	Float64 float64
	String  string
}

type Row struct {
	Values []Value
}

type ColumnInfo struct {
	Name     string
	Type     Type
	Length   uint32
	Nullable bool
}

type DB struct {
	ptr *C.haruhidb_database_t
}

type Scanner struct {
	ptr *C.haruhidb_scan_t
}

func APIVersion() string {
	return C.GoString(C.haruhidb_api_version())
}

func Capabilities() Capability {
	return Capability(C.haruhidb_capabilities())
}

func BoolValue(v bool) Value {
	return Value{Type: TypeBoolean, Bool: v}
}

func Int8Value(v int8) Value {
	return Value{Type: TypeTinyInt, Int8: v}
}

func Int16Value(v int16) Value {
	return Value{Type: TypeSmallInt, Int16: v}
}

func Int32Value(v int32) Value {
	return Value{Type: TypeInteger, Int32: v}
}

func Int64Value(v int64) Value {
	return Value{Type: TypeBigInt, Int64: v}
}

func Float32Value(v float32) Value {
	return Value{Type: TypeFloat, Float32: v}
}

func Float64Value(v float64) Value {
	return Value{Type: TypeDouble, Float64: v}
}

func StringValue(v string) Value {
	return Value{Type: TypeVarchar, String: v}
}

func NullValue(t Type) Value {
	return Value{Type: t, Null: true}
}

func Open(path string, opts OpenOptions) (*DB, error) {
	if path == "" {
		return nil, errors.New("path must not be empty")
	}

	cPath := C.CString(path)
	defer C.free(unsafe.Pointer(cPath))

	var cOptions C.haruhidb_open_options_t
	cOptions.buffer_pool_size = C.size_t(opts.BufferPoolSize)
	cOptions.lru_k = C.size_t(opts.LRUK)
	cOptions.enable_wal = C.bool(opts.EnableWAL)

	var cWalPath *C.char
	if opts.WALPath != "" {
		cWalPath = C.CString(opts.WALPath)
		defer C.free(unsafe.Pointer(cWalPath))
		cOptions.wal_path = cWalPath
	}

	var db *C.haruhidb_database_t
	var cCode C.haruhidb_error_code_t
	var cErr *C.char
	status := C.haruhidb_open_ex(cPath, &cOptions, &db, &cCode, &cErr)
	if err := consumeStatus("Open", status, cCode, cErr); err != nil {
		return nil, err
	}
	return &DB{ptr: db}, nil
}

func (db *DB) Close() error {
	if db == nil || db.ptr == nil {
		return nil
	}
	C.haruhidb_close(db.ptr)
	db.ptr = nil
	return nil
}

func (db *DB) CreateTable(name string, columns []ColumnDef) error {
	if err := db.ensureOpen(); err != nil {
		return err
	}
	if name == "" {
		return errors.New("table name must not be empty")
	}
	if len(columns) == 0 {
		return errors.New("columns must not be empty")
	}

	cName := C.CString(name)
	defer C.free(unsafe.Pointer(cName))

	cColumns := make([]C.haruhidb_column_def_t, len(columns))
	nameAllocs := make([]unsafe.Pointer, 0, len(columns))
	defer freeAll(nameAllocs)

	for i, column := range columns {
		if column.Name == "" {
			return fmt.Errorf("column %d name must not be empty", i)
		}
		cColumnName := C.CString(column.Name)
		nameAllocs = append(nameAllocs, unsafe.Pointer(cColumnName))
		cColumns[i] = C.haruhidb_column_def_t{
			name:     cColumnName,
			_type:    C.haruhidb_type_t(column.Type),
			length:   C.uint32_t(column.Length),
			nullable: C.bool(column.Nullable),
		}
	}

	var cCode C.haruhidb_error_code_t
	var cErr *C.char
	status := C.haruhidb_table_create_ex(
		db.ptr,
		cName,
		&cColumns[0],
		C.size_t(len(cColumns)),
		&cCode,
		&cErr,
	)
	return consumeStatus("CreateTable", status, cCode, cErr)
}

func (db *DB) TableExists(name string) (bool, error) {
	if err := db.ensureOpen(); err != nil {
		return false, err
	}
	if name == "" {
		return false, errors.New("table name must not be empty")
	}

	cName := C.CString(name)
	defer C.free(unsafe.Pointer(cName))

	var exists C.bool
	var cCode C.haruhidb_error_code_t
	var cErr *C.char
	status := C.haruhidb_table_exists_ex(db.ptr, cName, &exists, &cCode, &cErr)
	if err := consumeStatus("TableExists", status, cCode, cErr); err != nil {
		return false, err
	}
	return bool(exists), nil
}

func (db *DB) CreatePrimaryIntIndex(tableName, indexName string) error {
	if err := db.ensureOpen(); err != nil {
		return err
	}
	if tableName == "" || indexName == "" {
		return errors.New("table name and index name must not be empty")
	}

	cTableName := C.CString(tableName)
	defer C.free(unsafe.Pointer(cTableName))
	cIndexName := C.CString(indexName)
	defer C.free(unsafe.Pointer(cIndexName))

	var cCode C.haruhidb_error_code_t
	var cErr *C.char
	status := C.haruhidb_index_create_primary_int_ex(db.ptr, cTableName, cIndexName, &cCode, &cErr)
	return consumeStatus("CreatePrimaryIntIndex", status, cCode, cErr)
}

func (db *DB) DropIndex(tableName, indexName string) error {
	if err := db.ensureOpen(); err != nil {
		return err
	}
	if tableName == "" || indexName == "" {
		return errors.New("table name and index name must not be empty")
	}

	cTableName := C.CString(tableName)
	defer C.free(unsafe.Pointer(cTableName))
	cIndexName := C.CString(indexName)
	defer C.free(unsafe.Pointer(cIndexName))

	var cCode C.haruhidb_error_code_t
	var cErr *C.char
	status := C.haruhidb_index_drop_ex(db.ptr, cTableName, cIndexName, &cCode, &cErr)
	return consumeStatus("DropIndex", status, cCode, cErr)
}

func (db *DB) DropTable(tableName string) error {
	if err := db.ensureOpen(); err != nil {
		return err
	}
	if tableName == "" {
		return errors.New("table name must not be empty")
	}

	cTableName := C.CString(tableName)
	defer C.free(unsafe.Pointer(cTableName))

	var cCode C.haruhidb_error_code_t
	var cErr *C.char
	status := C.haruhidb_table_drop_ex(db.ptr, cTableName, &cCode, &cErr)
	return consumeStatus("DropTable", status, cCode, cErr)
}

func (db *DB) InsertRow(tableName string, values []Value) error {
	if err := db.ensureOpen(); err != nil {
		return err
	}
	if tableName == "" {
		return errors.New("table name must not be empty")
	}
	if len(values) == 0 {
		return errors.New("values must not be empty")
	}

	cTableName := C.CString(tableName)
	defer C.free(unsafe.Pointer(cTableName))

	cValues := make([]C.haruhidb_value_t, len(values))
	valueAllocs := make([]unsafe.Pointer, 0, len(values))
	defer freeAll(valueAllocs)

	for i, value := range values {
		cValue, allocated, err := marshalValue(value)
		if err != nil {
			return fmt.Errorf("value %d: %w", i, err)
		}
		cValues[i] = cValue
		if allocated != nil {
			valueAllocs = append(valueAllocs, allocated)
		}
	}

	var cCode C.haruhidb_error_code_t
	var cErr *C.char
	status := C.haruhidb_row_insert_ex(
		db.ptr,
		cTableName,
		&cValues[0],
		C.size_t(len(cValues)),
		&cCode,
		&cErr,
	)
	return consumeStatus("InsertRow", status, cCode, cErr)
}

func (db *DB) UpdateRowByPrimaryInt(tableName string, key int32, values []Value) (int, error) {
	if err := db.ensureOpen(); err != nil {
		return 0, err
	}
	if tableName == "" {
		return 0, errors.New("table name must not be empty")
	}
	if len(values) == 0 {
		return 0, errors.New("values must not be empty")
	}

	cTableName := C.CString(tableName)
	defer C.free(unsafe.Pointer(cTableName))

	cValues := make([]C.haruhidb_value_t, len(values))
	valueAllocs := make([]unsafe.Pointer, 0, len(values))
	defer freeAll(valueAllocs)

	for i, value := range values {
		cValue, allocated, err := marshalValue(value)
		if err != nil {
			return 0, fmt.Errorf("value %d: %w", i, err)
		}
		cValues[i] = cValue
		if allocated != nil {
			valueAllocs = append(valueAllocs, allocated)
		}
	}

	var updated C.size_t
	var cCode C.haruhidb_error_code_t
	var cErr *C.char
	status := C.haruhidb_row_update_primary_int_ex(
		db.ptr,
		cTableName,
		C.int32_t(key),
		&cValues[0],
		C.size_t(len(cValues)),
		&updated,
		&cCode,
		&cErr,
	)
	if err := consumeStatus("UpdateRowByPrimaryInt", status, cCode, cErr); err != nil {
		return 0, err
	}
	return int(updated), nil
}

func (db *DB) DeleteRowByPrimaryInt(tableName string, key int32) (int, error) {
	if err := db.ensureOpen(); err != nil {
		return 0, err
	}
	if tableName == "" {
		return 0, errors.New("table name must not be empty")
	}

	cTableName := C.CString(tableName)
	defer C.free(unsafe.Pointer(cTableName))

	var deleted C.size_t
	var cCode C.haruhidb_error_code_t
	var cErr *C.char
	status := C.haruhidb_row_delete_primary_int_ex(
		db.ptr,
		cTableName,
		C.int32_t(key),
		&deleted,
		&cCode,
		&cErr,
	)
	if err := consumeStatus("DeleteRowByPrimaryInt", status, cCode, cErr); err != nil {
		return 0, err
	}
	return int(deleted), nil
}

func (db *DB) GetRowByPrimaryInt(tableName string, key int32) (Row, bool, error) {
	if err := db.ensureOpen(); err != nil {
		return Row{}, false, err
	}
	if tableName == "" {
		return Row{}, false, errors.New("table name must not be empty")
	}

	cTableName := C.CString(tableName)
	defer C.free(unsafe.Pointer(cTableName))

	var found C.bool
	var cRow C.haruhidb_row_t
	var cCode C.haruhidb_error_code_t
	var cErr *C.char
	status := C.haruhidb_row_get_primary_int_ex(
		db.ptr,
		cTableName,
		C.int32_t(key),
		&found,
		&cRow,
		&cCode,
		&cErr,
	)
	if err := consumeStatus("GetRowByPrimaryInt", status, cCode, cErr); err != nil {
		return Row{}, false, err
	}
	defer C.haruhidb_row_destroy(&cRow)

	if !bool(found) {
		return Row{}, false, nil
	}

	row, err := unmarshalRow(cRow)
	if err != nil {
		return Row{}, false, err
	}
	return row, true, nil
}

func (db *DB) ScanAll(tableName string) (*Scanner, error) {
	if err := db.ensureOpen(); err != nil {
		return nil, err
	}
	if tableName == "" {
		return nil, errors.New("table name must not be empty")
	}

	cTableName := C.CString(tableName)
	defer C.free(unsafe.Pointer(cTableName))

	var scan *C.haruhidb_scan_t
	var cCode C.haruhidb_error_code_t
	var cErr *C.char
	status := C.haruhidb_scan_open_all_ex(db.ptr, cTableName, &scan, &cCode, &cErr)
	if err := consumeStatus("ScanAll", status, cCode, cErr); err != nil {
		return nil, err
	}
	return &Scanner{ptr: scan}, nil
}

func (db *DB) ScanByPrimaryIntRange(tableName string, startKey, endKey int32) (*Scanner, error) {
	if err := db.ensureOpen(); err != nil {
		return nil, err
	}
	if tableName == "" {
		return nil, errors.New("table name must not be empty")
	}
	if startKey > endKey {
		return nil, errors.New("startKey must be less than or equal to endKey")
	}

	cTableName := C.CString(tableName)
	defer C.free(unsafe.Pointer(cTableName))

	var scan *C.haruhidb_scan_t
	var cCode C.haruhidb_error_code_t
	var cErr *C.char
	status := C.haruhidb_scan_open_primary_int_range_ex(
		db.ptr,
		cTableName,
		C.int32_t(startKey),
		C.int32_t(endKey),
		&scan,
		&cCode,
		&cErr,
	)
	if err := consumeStatus("ScanByPrimaryIntRange", status, cCode, cErr); err != nil {
		return nil, err
	}
	return &Scanner{ptr: scan}, nil
}

func (db *DB) ListTables() ([]string, error) {
	if err := db.ensureOpen(); err != nil {
		return nil, err
	}

	var count C.size_t
	var cCode C.haruhidb_error_code_t
	var cErr *C.char
	status := C.haruhidb_table_count_ex(db.ptr, &count, &cCode, &cErr)
	if err := consumeStatus("ListTables", status, cCode, cErr); err != nil {
		return nil, err
	}

	tables := make([]string, 0, int(count))
	for i := C.size_t(0); i < count; i++ {
		var cName *C.char
		cErr = nil
		cCode = C.HARUHIDB_ERROR_OK
		status = C.haruhidb_table_name_at_ex(db.ptr, i, &cName, &cCode, &cErr)
		if err := consumeStatus("ListTables", status, cCode, cErr); err != nil {
			return nil, err
		}
		tables = append(tables, C.GoString(cName))
		C.haruhidb_free_string(cName)
	}
	return tables, nil
}

func (db *DB) ListTableColumns(tableName string) ([]ColumnInfo, error) {
	if err := db.ensureOpen(); err != nil {
		return nil, err
	}
	if tableName == "" {
		return nil, errors.New("table name must not be empty")
	}

	cTableName := C.CString(tableName)
	defer C.free(unsafe.Pointer(cTableName))

	var count C.size_t
	var cCode C.haruhidb_error_code_t
	var cErr *C.char
	status := C.haruhidb_table_column_count_ex(db.ptr, cTableName, &count, &cCode, &cErr)
	if err := consumeStatus("ListTableColumns", status, cCode, cErr); err != nil {
		return nil, err
	}

	columns := make([]ColumnInfo, 0, int(count))
	for i := C.size_t(0); i < count; i++ {
		var cColumn C.haruhidb_column_def_t
		cErr = nil
		cCode = C.HARUHIDB_ERROR_OK
		status = C.haruhidb_table_column_at_ex(
			db.ptr,
			cTableName,
			i,
			&cColumn,
			&cCode,
			&cErr,
		)
		if err := consumeStatus("ListTableColumns", status, cCode, cErr); err != nil {
			return nil, err
		}

		name := ""
		if cColumn.name != nil {
			name = C.GoString(cColumn.name)
			C.haruhidb_free_string((*C.char)(unsafe.Pointer(cColumn.name)))
		}
		columns = append(columns, ColumnInfo{
			Name:     name,
			Type:     Type(cColumn._type),
			Length:   uint32(cColumn.length),
			Nullable: bool(cColumn.nullable),
		})
	}
	return columns, nil
}

func (db *DB) ListTableIndexes(tableName string) ([]string, error) {
	if err := db.ensureOpen(); err != nil {
		return nil, err
	}
	if tableName == "" {
		return nil, errors.New("table name must not be empty")
	}

	cTableName := C.CString(tableName)
	defer C.free(unsafe.Pointer(cTableName))

	var count C.size_t
	var cCode C.haruhidb_error_code_t
	var cErr *C.char
	status := C.haruhidb_table_index_count_ex(db.ptr, cTableName, &count, &cCode, &cErr)
	if err := consumeStatus("ListTableIndexes", status, cCode, cErr); err != nil {
		return nil, err
	}

	indexes := make([]string, 0, int(count))
	for i := C.size_t(0); i < count; i++ {
		var cName *C.char
		cErr = nil
		cCode = C.HARUHIDB_ERROR_OK
		status = C.haruhidb_table_index_name_at_ex(
			db.ptr,
			cTableName,
			i,
			&cName,
			&cCode,
			&cErr,
		)
		if err := consumeStatus("ListTableIndexes", status, cCode, cErr); err != nil {
			return nil, err
		}
		indexes = append(indexes, C.GoString(cName))
		C.haruhidb_free_string(cName)
	}
	return indexes, nil
}

func (s *Scanner) Close() error {
	if s == nil || s.ptr == nil {
		return nil
	}
	C.haruhidb_scan_close(s.ptr)
	s.ptr = nil
	return nil
}

func (s *Scanner) Next() (Row, error) {
	if s == nil || s.ptr == nil {
		return Row{}, errors.New("scanner is closed")
	}

	var cRow C.haruhidb_row_t
	var cCode C.haruhidb_error_code_t
	var cErr *C.char
	status := C.haruhidb_scan_next_ex(s.ptr, &cRow, &cCode, &cErr)
	if err := consumeStatus("Scanner.Next", status, cCode, cErr); err != nil {
		return Row{}, err
	}
	defer C.haruhidb_row_destroy(&cRow)

	return unmarshalRow(cRow)
}

func (db *DB) ensureOpen() error {
	if db == nil || db.ptr == nil {
		return errors.New("database is closed")
	}
	return nil
}

func marshalValue(value Value) (C.haruhidb_value_t, unsafe.Pointer, error) {
	cValue := C.haruhidb_value_t{
		_type:   C.haruhidb_type_t(value.Type),
		is_null: C.bool(value.Null),
	}

	switch value.Type {
	case TypeBoolean:
		cValue.boolean_v = C.bool(value.Bool)
	case TypeTinyInt:
		cValue.int8_v = C.int8_t(value.Int8)
	case TypeSmallInt:
		cValue.int16_v = C.int16_t(value.Int16)
	case TypeInteger:
		cValue.int32_v = C.int32_t(value.Int32)
	case TypeBigInt:
		cValue.int64_v = C.int64_t(value.Int64)
	case TypeFloat:
		cValue.float_v = C.float(value.Float32)
	case TypeDouble:
		cValue.double_v = C.double(value.Float64)
	case TypeVarchar:
		if value.String == "" {
			return cValue, nil, nil
		}
		allocated := C.CBytes([]byte(value.String))
		cValue.string_data = (*C.char)(allocated)
		cValue.string_len = C.size_t(len(value.String))
		return cValue, allocated, nil
	case TypeDecimal:
		return C.haruhidb_value_t{}, nil, errors.New("DECIMAL is not supported in the first version")
	default:
		return C.haruhidb_value_t{}, nil, fmt.Errorf("unsupported value type %d", value.Type)
	}

	return cValue, nil, nil
}

func unmarshalValue(cValue C.haruhidb_value_t) (Value, error) {
	value := Value{
		Type: Type(cValue._type),
		Null: bool(cValue.is_null),
	}
	if value.Null {
		return value, nil
	}

	switch value.Type {
	case TypeBoolean:
		value.Bool = bool(cValue.boolean_v)
	case TypeTinyInt:
		value.Int8 = int8(cValue.int8_v)
	case TypeSmallInt:
		value.Int16 = int16(cValue.int16_v)
	case TypeInteger:
		value.Int32 = int32(cValue.int32_v)
	case TypeBigInt:
		value.Int64 = int64(cValue.int64_v)
	case TypeFloat:
		value.Float32 = float32(cValue.float_v)
	case TypeDouble:
		value.Float64 = float64(cValue.double_v)
	case TypeVarchar:
		value.String = C.GoStringN(cValue.string_data, C.int(cValue.string_len))
	case TypeDecimal:
		return Value{}, errors.New("DECIMAL is not supported in the first version")
	default:
		return Value{}, fmt.Errorf("unsupported scanned value type %d", value.Type)
	}
	return value, nil
}

func unmarshalRow(cRow C.haruhidb_row_t) (Row, error) {
	values := make([]Value, int(cRow.value_count))
	if cRow.value_count == 0 || cRow.values == nil {
		return Row{Values: values}, nil
	}

	cValues := unsafe.Slice(cRow.values, int(cRow.value_count))
	for i, cValue := range cValues {
		value, err := unmarshalValue(cValue)
		if err != nil {
			return Row{}, fmt.Errorf("scan row value %d: %w", i, err)
		}
		values[i] = value
	}
	return Row{Values: values}, nil
}

func normalizeErrorCode(code C.haruhidb_error_code_t) ErrorCode {
	switch code {
	case C.HARUHIDB_ERROR_OK:
		return ErrorOK
	case C.HARUHIDB_ERROR_INVALID_ARGUMENT:
		return ErrorInvalidArgument
	case C.HARUHIDB_ERROR_INVALID_HANDLE:
		return ErrorInvalidHandle
	case C.HARUHIDB_ERROR_NOT_FOUND:
		return ErrorNotFound
	case C.HARUHIDB_ERROR_ALREADY_EXISTS:
		return ErrorAlreadyExists
	case C.HARUHIDB_ERROR_UNSUPPORTED:
		return ErrorUnsupported
	case C.HARUHIDB_ERROR_CONSTRAINT:
		return ErrorConstraint
	case C.HARUHIDB_ERROR_IO:
		return ErrorIO
	case C.HARUHIDB_ERROR_INTERNAL:
		return ErrorInternal
	default:
		return ErrorUnknown
	}
}

func fallbackMessageForCode(code ErrorCode) string {
	switch code {
	case ErrorInvalidArgument:
		return "invalid argument"
	case ErrorInvalidHandle:
		return "invalid handle"
	case ErrorNotFound:
		return "not found"
	case ErrorAlreadyExists:
		return "already exists"
	case ErrorUnsupported:
		return "unsupported operation"
	case ErrorConstraint:
		return "constraint violation"
	case ErrorIO:
		return "io error"
	case ErrorInternal:
		return "internal error"
	default:
		return "haruhidb C API returned error without message"
	}
}

func consumeStatus(op string, status C.haruhidb_status_t, cCode C.haruhidb_error_code_t, cErr *C.char) error {
	if cErr != nil {
		defer C.haruhidb_free_string(cErr)
	}

	switch status {
	case C.HARUHIDB_STATUS_OK:
		return nil
	case C.HARUHIDB_STATUS_END:
		return io.EOF
	case C.HARUHIDB_STATUS_ERROR:
		code := normalizeErrorCode(cCode)
		message := fallbackMessageForCode(code)
		if cErr != nil {
			message = C.GoString(cErr)
		}
		return &Error{
			Op:      op,
			Code:    code,
			Message: message,
		}
	default:
		code := normalizeErrorCode(cCode)
		message := fmt.Sprintf("unknown haruhidb status %d", int(status))
		if cErr != nil {
			message = fmt.Sprintf("%s (status=%d)", C.GoString(cErr), int(status))
		}
		return &Error{
			Op:      op,
			Code:    code,
			Message: message,
		}
	}
}

func freeAll(ptrs []unsafe.Pointer) {
	for _, ptr := range ptrs {
		C.free(ptr)
	}
}
