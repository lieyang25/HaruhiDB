package action

import (
	"encoding/json"

	"haruhidb-go/haruhidb"
)

type Mode string

const (
	ModeReadOnly  Mode = "read_only"
	ModeReadWrite Mode = "read_write"
)

type Action string

const (
	ActionListTables          Action = "list_tables"
	ActionTableExists         Action = "table_exists"
	ActionDescribeTable       Action = "describe_table"
	ActionGetByPrimaryInt     Action = "get_by_primary_int"
	ActionScanAll             Action = "scan_all"
	ActionScanPrimaryIntRange Action = "scan_primary_int_range"
	ActionInsertRow           Action = "insert_row"
	ActionUpdateByPrimaryInt  Action = "update_by_primary_int"
	ActionDeleteByPrimaryInt  Action = "delete_by_primary_int"
	ActionBatch               Action = "batch"
)

const (
	VersionV1    = "v1"
	DefaultLimit = 100
)

type TypeName string

const (
	TypeNameInvalid  TypeName = "INVALID"
	TypeNameBoolean  TypeName = "BOOLEAN"
	TypeNameTinyInt  TypeName = "TINYINT"
	TypeNameSmallInt TypeName = "SMALLINT"
	TypeNameInteger  TypeName = "INTEGER"
	TypeNameBigInt   TypeName = "BIGINT"
	TypeNameFloat    TypeName = "FLOAT"
	TypeNameDouble   TypeName = "DOUBLE"
	TypeNameDecimal  TypeName = "DECIMAL"
	TypeNameVarchar  TypeName = "VARCHAR"
)

type RequestEnvelope struct {
	Version   string          `json:"version"`
	RequestID string          `json:"request_id"`
	Mode      Mode            `json:"mode"`
	Action    Action          `json:"action"`
	Args      json.RawMessage `json:"args"`
}

type Request struct {
	Version   string
	RequestID string
	Mode      Mode
	Action    Action
	Args      Args

	columns []haruhidb.ColumnInfo
	indexes []string
}

type Args interface {
	requestArgs()
}

type ValueMap map[string]any
type RowMap map[string]any

type ListTablesArgs struct{}

type TableExistsArgs struct {
	Table string `json:"table"`
}

type DescribeTableArgs struct {
	Table string `json:"table"`
}

type GetByPrimaryIntArgs struct {
	Table string `json:"table"`
	Key   int32  `json:"key"`
}

type ScanAllArgs struct {
	Table string `json:"table"`
	Limit int    `json:"limit"`
}

type ScanPrimaryIntRangeArgs struct {
	Table    string `json:"table"`
	StartKey int32  `json:"start_key"`
	EndKey   int32  `json:"end_key"`
	Limit    int    `json:"limit"`
}

type InsertRowArgs struct {
	Table  string   `json:"table"`
	Values ValueMap `json:"values"`

	typedValues map[string]haruhidb.Value
}

type UpdateByPrimaryIntArgs struct {
	Table  string   `json:"table"`
	Key    int32    `json:"key"`
	Values ValueMap `json:"values"`

	typedValues map[string]haruhidb.Value
}

type DeleteByPrimaryIntArgs struct {
	Table string `json:"table"`
	Key   int32  `json:"key"`
}

type BatchArgs struct {
	StopOnError bool `json:"stop_on_error,omitempty"`

	requests []*Request
}

func (ListTablesArgs) requestArgs()          {}
func (TableExistsArgs) requestArgs()         {}
func (DescribeTableArgs) requestArgs()       {}
func (GetByPrimaryIntArgs) requestArgs()     {}
func (ScanAllArgs) requestArgs()             {}
func (ScanPrimaryIntRangeArgs) requestArgs() {}
func (InsertRowArgs) requestArgs()           {}
func (UpdateByPrimaryIntArgs) requestArgs()  {}
func (DeleteByPrimaryIntArgs) requestArgs()  {}
func (BatchArgs) requestArgs()               {}

type ResponseEnvelope[T any] struct {
	Ok        bool           `json:"ok"`
	RequestID string         `json:"request_id"`
	Action    Action         `json:"action"`
	Data      *T             `json:"data"`
	Error     *ResponseError `json:"error"`
	Meta      map[string]any `json:"meta"`
}

type ResponseError struct {
	Code    Code   `json:"code"`
	Message string `json:"message"`
}

type ListTablesData struct {
	Tables []string `json:"tables"`
}

type TableExistsData struct {
	Table  string `json:"table"`
	Exists bool   `json:"exists"`
}

type DescribeTableColumn struct {
	Name     string   `json:"name"`
	Type     TypeName `json:"type"`
	Length   uint32   `json:"length,omitempty"`
	Nullable bool     `json:"nullable"`
}

type DescribeTableIndex struct {
	Name string `json:"name"`
}

type DescribeTableData struct {
	Table   string                `json:"table"`
	Columns []DescribeTableColumn `json:"columns"`
	Indexes []DescribeTableIndex  `json:"indexes"`
}

type GetByPrimaryIntData struct {
	Table string `json:"table"`
	Row   RowMap `json:"row"`
	Found bool   `json:"found"`
}

type ScanAllData struct {
	Table     string   `json:"table"`
	Rows      []RowMap `json:"rows"`
	RowCount  int      `json:"row_count"`
	Truncated bool     `json:"truncated"`
}

type ScanPrimaryIntRangeData struct {
	Table     string   `json:"table"`
	Rows      []RowMap `json:"rows"`
	RowCount  int      `json:"row_count"`
	Truncated bool     `json:"truncated"`
}

type InsertRowData struct {
	Table    string `json:"table"`
	Inserted int    `json:"inserted"`
}

type UpdateByPrimaryIntData struct {
	Table   string `json:"table"`
	Updated int    `json:"updated"`
}

type DeleteByPrimaryIntData struct {
	Table   string `json:"table"`
	Deleted int    `json:"deleted"`
}

type BatchResultItem struct {
	Index  int            `json:"index"`
	Action Action         `json:"action"`
	Ok     bool           `json:"ok"`
	Data   map[string]any `json:"data,omitempty"`
	Error  *ResponseError `json:"error,omitempty"`
}

type BatchData struct {
	Results   []BatchResultItem `json:"results"`
	Total     int               `json:"total"`
	Succeeded int               `json:"succeeded"`
	Failed    int               `json:"failed"`
	Stopped   bool              `json:"stopped"`
}

type CatalogReader interface {
	TableExists(name string) (bool, error)
	ListTableColumns(tableName string) ([]haruhidb.ColumnInfo, error)
	ListTableIndexes(tableName string) ([]string, error)
}

var _ CatalogReader = (*haruhidb.DB)(nil)

func (m Mode) Valid() bool {
	switch m {
	case ModeReadOnly, ModeReadWrite:
		return true
	default:
		return false
	}
}

func (a Action) Valid() bool {
	switch a {
	case ActionListTables,
		ActionTableExists,
		ActionDescribeTable,
		ActionGetByPrimaryInt,
		ActionScanAll,
		ActionScanPrimaryIntRange,
		ActionInsertRow,
		ActionUpdateByPrimaryInt,
		ActionDeleteByPrimaryInt,
		ActionBatch:
		return true
	default:
		return false
	}
}

func (a Action) IsWrite() bool {
	switch a {
	case ActionInsertRow, ActionUpdateByPrimaryInt, ActionDeleteByPrimaryInt:
		return true
	default:
		return false
	}
}

func ProtocolTypeName(t haruhidb.Type) (TypeName, error) {
	switch t {
	case haruhidb.TypeInvalid:
		return TypeNameInvalid, nil
	case haruhidb.TypeBoolean:
		return TypeNameBoolean, nil
	case haruhidb.TypeTinyInt:
		return TypeNameTinyInt, nil
	case haruhidb.TypeSmallInt:
		return TypeNameSmallInt, nil
	case haruhidb.TypeInteger:
		return TypeNameInteger, nil
	case haruhidb.TypeBigInt:
		return TypeNameBigInt, nil
	case haruhidb.TypeFloat:
		return TypeNameFloat, nil
	case haruhidb.TypeDouble:
		return TypeNameDouble, nil
	case haruhidb.TypeDecimal:
		return TypeNameDecimal, nil
	case haruhidb.TypeVarchar:
		return TypeNameVarchar, nil
	default:
		return "", errorf(CodeInternal, "unknown HaruhiDB type: %d", t)
	}
}
