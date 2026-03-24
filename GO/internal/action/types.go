package action

import (
	"encoding/json"
	"strings"

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
	ActionCreateTable         Action = "create_table"
	ActionDropTable           Action = "drop_table"
	ActionCreatePrimaryIndex  Action = "create_primary_int_index"
	ActionDropIndex           Action = "drop_index"
	ActionBatch               Action = "batch"
)

const (
	VersionV1      = "v1"
	VersionV2      = "v2"
	VersionV3      = "v3"
	DefaultVersion = VersionV3
	DefaultLimit   = 100
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

type CreateTableColumnSpec struct {
	Name     string   `json:"name"`
	Type     TypeName `json:"type"`
	Length   uint32   `json:"length,omitempty"`
	Nullable bool     `json:"nullable"`
}

type CreateTableArgs struct {
	Table   string                  `json:"table"`
	Columns []CreateTableColumnSpec `json:"columns"`

	columnDefs []haruhidb.ColumnDef
}

type DropTableArgs struct {
	Table string `json:"table"`
}

type CreatePrimaryIntIndexArgs struct {
	Table string `json:"table"`
	Index string `json:"index"`
}

type DropIndexArgs struct {
	Table string `json:"table"`
	Index string `json:"index"`
}

type BatchArgs struct {
	StopOnError bool `json:"stop_on_error,omitempty"`

	requests []*Request
}

func (ListTablesArgs) requestArgs()            {}
func (TableExistsArgs) requestArgs()           {}
func (DescribeTableArgs) requestArgs()         {}
func (GetByPrimaryIntArgs) requestArgs()       {}
func (ScanAllArgs) requestArgs()               {}
func (ScanPrimaryIntRangeArgs) requestArgs()   {}
func (InsertRowArgs) requestArgs()             {}
func (UpdateByPrimaryIntArgs) requestArgs()    {}
func (DeleteByPrimaryIntArgs) requestArgs()    {}
func (CreateTableArgs) requestArgs()           {}
func (DropTableArgs) requestArgs()             {}
func (CreatePrimaryIntIndexArgs) requestArgs() {}
func (DropIndexArgs) requestArgs()             {}
func (BatchArgs) requestArgs()                 {}

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

type CreateTableData struct {
	Table   string `json:"table"`
	Created int    `json:"created"`
}

type DropTableData struct {
	Table   string `json:"table"`
	Dropped int    `json:"dropped"`
}

type CreatePrimaryIntIndexData struct {
	Table   string `json:"table"`
	Index   string `json:"index"`
	Created int    `json:"created"`
}

type DropIndexData struct {
	Table   string `json:"table"`
	Index   string `json:"index"`
	Dropped int    `json:"dropped"`
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
		ActionCreateTable,
		ActionDropTable,
		ActionCreatePrimaryIndex,
		ActionDropIndex,
		ActionBatch:
		return true
	default:
		return false
	}
}

func (a Action) IsWrite() bool {
	switch a {
	case ActionInsertRow,
		ActionUpdateByPrimaryInt,
		ActionDeleteByPrimaryInt,
		ActionCreateTable,
		ActionDropTable,
		ActionCreatePrimaryIndex,
		ActionDropIndex:
		return true
	default:
		return false
	}
}

func SupportedVersion(version string) bool {
	_, ok := CanonicalProtocolVersion(version)
	return ok
}

func CanonicalProtocolVersion(version string) (string, bool) {
	switch version {
	case VersionV1, VersionV2, VersionV3:
		return VersionV3, true
	default:
		switch strings.ToLower(strings.TrimSpace(version)) {
		case VersionV1, VersionV2, VersionV3:
			return VersionV3, true
		default:
			return "", false
		}
	}
}

func ActionSupportedInVersion(version string, action Action) bool {
	if !action.Valid() {
		return false
	}
	_, ok := CanonicalProtocolVersion(version)
	return ok
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

func HaruhiTypeFromProtocol(name TypeName) (haruhidb.Type, error) {
	switch name {
	case TypeNameBoolean:
		return haruhidb.TypeBoolean, nil
	case TypeNameTinyInt:
		return haruhidb.TypeTinyInt, nil
	case TypeNameSmallInt:
		return haruhidb.TypeSmallInt, nil
	case TypeNameInteger:
		return haruhidb.TypeInteger, nil
	case TypeNameBigInt:
		return haruhidb.TypeBigInt, nil
	case TypeNameFloat:
		return haruhidb.TypeFloat, nil
	case TypeNameDouble:
		return haruhidb.TypeDouble, nil
	case TypeNameDecimal:
		return haruhidb.TypeDecimal, nil
	case TypeNameVarchar:
		return haruhidb.TypeVarchar, nil
	case TypeNameInvalid:
		return haruhidb.TypeInvalid, errorf(CodeInvalidRequest, "column type %q is not supported", name)
	default:
		return haruhidb.TypeInvalid, errorf(CodeInvalidRequest, "unknown column type %q", name)
	}
}
