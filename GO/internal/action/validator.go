package action

import (
	"encoding/json"
	"fmt"
	"math"
	"strings"

	"haruhidb-go/haruhidb"
)

const (
	minInt8  = -1 << 7
	maxInt8  = 1<<7 - 1
	minInt16 = -1 << 15
	maxInt16 = 1<<15 - 1
	minInt32 = -1 << 31
	maxInt32 = 1<<31 - 1
)

func validateTableName(name string) error {
	if strings.TrimSpace(name) == "" {
		return errorf(CodeInvalidRequest, "table must not be empty")
	}
	return nil
}

func validateIndexName(name string) error {
	if strings.TrimSpace(name) == "" {
		return errorf(CodeInvalidRequest, "index must not be empty")
	}
	return nil
}

func validateLimit(limit *int) (int, error) {
	if limit == nil {
		return DefaultLimit, nil
	}
	if *limit <= 0 {
		return 0, errorf(CodeInvalidRequest, "limit must be greater than 0")
	}
	return *limit, nil
}

func requireCatalog(catalog CatalogReader) error {
	if catalog == nil {
		return errorf(CodeInternal, "catalog reader is required for this action")
	}
	return nil
}

func requireKnownTable(catalog CatalogReader, table string) ([]haruhidb.ColumnInfo, []string, error) {
	if err := requireCatalog(catalog); err != nil {
		return nil, nil, err
	}

	exists, err := catalog.TableExists(table)
	if err != nil {
		return nil, nil, mapCatalogError(err)
	}
	if !exists {
		return nil, nil, errorf(CodeNotFound, "table %q not found", table)
	}

	columns, err := catalog.ListTableColumns(table)
	if err != nil {
		return nil, nil, mapCatalogError(err)
	}
	indexes, err := catalog.ListTableIndexes(table)
	if err != nil {
		return nil, nil, mapCatalogError(err)
	}

	return columns, indexes, nil
}

func requireMissingTable(catalog CatalogReader, table string) error {
	if err := requireCatalog(catalog); err != nil {
		return err
	}
	exists, err := catalog.TableExists(table)
	if err != nil {
		return mapCatalogError(err)
	}
	if exists {
		return errorf(CodeAlreadyExists, "table %q already exists", table)
	}
	return nil
}

func requirePrimaryIntColumns(table string, columns []haruhidb.ColumnInfo) error {
	if len(columns) == 0 {
		return errorf(CodeUnsupported, "table %q has no columns", table)
	}

	first := columns[0]
	if first.Type != haruhidb.TypeInteger {
		return errorf(CodeUnsupported, "table %q first column must be INTEGER for primary-int actions", table)
	}
	if first.Nullable {
		return errorf(CodeUnsupported, "table %q first column must be NOT NULL for primary-int actions", table)
	}
	return nil
}

func requirePrimaryIntIndex(table string, indexes []string) error {
	if len(indexes) == 0 {
		return errorf(CodeUnsupported, "table %q requires a primary-int index for this action", table)
	}
	return nil
}

func validateGetByPrimaryIntArgs(
	raw rawGetByPrimaryIntArgs,
	columns []haruhidb.ColumnInfo,
	indexes []string,
) (GetByPrimaryIntArgs, error) {
	if err := validateTableName(raw.Table); err != nil {
		return GetByPrimaryIntArgs{}, err
	}
	if raw.Key == nil {
		return GetByPrimaryIntArgs{}, errorf(CodeInvalidRequest, "key is required")
	}

	if err := requirePrimaryIntColumns(raw.Table, columns); err != nil {
		return GetByPrimaryIntArgs{}, err
	}
	if err := requirePrimaryIntIndex(raw.Table, indexes); err != nil {
		return GetByPrimaryIntArgs{}, err
	}

	return GetByPrimaryIntArgs{
		Table: raw.Table,
		Key:   *raw.Key,
	}, nil
}

func validateScanAllArgs(raw rawScanAllArgs) (ScanAllArgs, error) {
	if err := validateTableName(raw.Table); err != nil {
		return ScanAllArgs{}, err
	}

	limit, err := validateLimit(raw.Limit)
	if err != nil {
		return ScanAllArgs{}, err
	}

	return ScanAllArgs{
		Table: raw.Table,
		Limit: limit,
	}, nil
}

func validateScanPrimaryIntRangeArgs(
	raw rawScanPrimaryIntRangeArgs,
	columns []haruhidb.ColumnInfo,
	indexes []string,
) (ScanPrimaryIntRangeArgs, error) {
	if err := validateTableName(raw.Table); err != nil {
		return ScanPrimaryIntRangeArgs{}, err
	}
	if raw.StartKey == nil {
		return ScanPrimaryIntRangeArgs{}, errorf(CodeInvalidRequest, "start_key is required")
	}
	if raw.EndKey == nil {
		return ScanPrimaryIntRangeArgs{}, errorf(CodeInvalidRequest, "end_key is required")
	}
	if *raw.StartKey > *raw.EndKey {
		return ScanPrimaryIntRangeArgs{}, errorf(CodeInvalidRequest, "start_key must be less than or equal to end_key")
	}

	limit, err := validateLimit(raw.Limit)
	if err != nil {
		return ScanPrimaryIntRangeArgs{}, err
	}

	if err := requirePrimaryIntColumns(raw.Table, columns); err != nil {
		return ScanPrimaryIntRangeArgs{}, err
	}
	if err := requirePrimaryIntIndex(raw.Table, indexes); err != nil {
		return ScanPrimaryIntRangeArgs{}, err
	}

	return ScanPrimaryIntRangeArgs{
		Table:    raw.Table,
		StartKey: *raw.StartKey,
		EndKey:   *raw.EndKey,
		Limit:    limit,
	}, nil
}

func validateInsertRowArgs(raw rawInsertRowArgs, columns []haruhidb.ColumnInfo) (InsertRowArgs, error) {
	if err := validateTableName(raw.Table); err != nil {
		return InsertRowArgs{}, err
	}

	values, typedValues, err := validateInsertValues(columns, raw.Values)
	if err != nil {
		return InsertRowArgs{}, err
	}

	return InsertRowArgs{
		Table:       raw.Table,
		Values:      values,
		typedValues: typedValues,
	}, nil
}

func validateUpdateByPrimaryIntArgs(raw rawUpdateByPrimaryIntArgs, columns []haruhidb.ColumnInfo) (UpdateByPrimaryIntArgs, error) {
	if err := validateTableName(raw.Table); err != nil {
		return UpdateByPrimaryIntArgs{}, err
	}
	if raw.Key == nil {
		return UpdateByPrimaryIntArgs{}, errorf(CodeInvalidRequest, "key is required")
	}

	if err := requirePrimaryIntColumns(raw.Table, columns); err != nil {
		return UpdateByPrimaryIntArgs{}, err
	}

	values, typedValues, err := validateUpdateValues(columns, *raw.Key, raw.Values)
	if err != nil {
		return UpdateByPrimaryIntArgs{}, err
	}

	return UpdateByPrimaryIntArgs{
		Table:       raw.Table,
		Key:         *raw.Key,
		Values:      values,
		typedValues: typedValues,
	}, nil
}

func validateDeleteByPrimaryIntArgs(raw rawDeleteByPrimaryIntArgs, columns []haruhidb.ColumnInfo) (DeleteByPrimaryIntArgs, error) {
	if err := validateTableName(raw.Table); err != nil {
		return DeleteByPrimaryIntArgs{}, err
	}
	if raw.Key == nil {
		return DeleteByPrimaryIntArgs{}, errorf(CodeInvalidRequest, "key is required")
	}

	if err := requirePrimaryIntColumns(raw.Table, columns); err != nil {
		return DeleteByPrimaryIntArgs{}, err
	}

	return DeleteByPrimaryIntArgs{
		Table: raw.Table,
		Key:   *raw.Key,
	}, nil
}

func validateCreateTableArgs(raw rawCreateTableArgs, catalog CatalogReader) (CreateTableArgs, error) {
	if err := validateTableName(raw.Table); err != nil {
		return CreateTableArgs{}, err
	}
	if len(raw.Columns) == 0 {
		return CreateTableArgs{}, errorf(CodeInvalidRequest, "columns must contain at least one column")
	}
	if err := requireMissingTable(catalog, raw.Table); err != nil {
		return CreateTableArgs{}, err
	}

	seenColumns := make(map[string]struct{}, len(raw.Columns))
	columnSpecs := make([]CreateTableColumnSpec, 0, len(raw.Columns))
	columnDefs := make([]haruhidb.ColumnDef, 0, len(raw.Columns))

	for i, rawColumn := range raw.Columns {
		name := strings.TrimSpace(rawColumn.Name)
		if name == "" {
			return CreateTableArgs{}, errorf(CodeInvalidRequest, "columns[%d].name must not be empty", i)
		}
		if _, exists := seenColumns[name]; exists {
			return CreateTableArgs{}, errorf(CodeInvalidRequest, "columns[%d].name %q is duplicated", i, name)
		}
		seenColumns[name] = struct{}{}

		typeName := TypeName(strings.ToUpper(strings.TrimSpace(rawColumn.Type)))
		if strings.TrimSpace(string(typeName)) == "" {
			return CreateTableArgs{}, errorf(CodeInvalidRequest, "columns[%d].type must not be empty", i)
		}

		columnType, err := HaruhiTypeFromProtocol(typeName)
		if err != nil {
			var typed *Error
			if asError(err, &typed) {
				return CreateTableArgs{}, errorf(typed.Code, "columns[%d].type: %s", i, typed.Message)
			}
			return CreateTableArgs{}, errorf(CodeInvalidRequest, "columns[%d].type: %s", i, err.Error())
		}
		if columnType == haruhidb.TypeInvalid {
			return CreateTableArgs{}, errorf(CodeInvalidRequest, "columns[%d].type %q is not supported", i, typeName)
		}
		if columnType == haruhidb.TypeDecimal {
			return CreateTableArgs{}, errorf(CodeUnsupported, "columns[%d].type DECIMAL is not supported in public action protocol", i)
		}

		length := uint32(0)
		if columnType == haruhidb.TypeVarchar {
			if rawColumn.Length == nil || *rawColumn.Length == 0 {
				return CreateTableArgs{}, errorf(CodeInvalidRequest, "columns[%d].length must be greater than 0 for VARCHAR", i)
			}
			length = *rawColumn.Length
		} else if rawColumn.Length != nil {
			return CreateTableArgs{}, errorf(CodeInvalidRequest, "columns[%d].length is only allowed for VARCHAR columns", i)
		}

		columnSpecs = append(columnSpecs, CreateTableColumnSpec{
			Name:     name,
			Type:     typeName,
			Length:   length,
			Nullable: rawColumn.Nullable,
		})
		columnDefs = append(columnDefs, haruhidb.ColumnDef{
			Name:     name,
			Type:     columnType,
			Length:   length,
			Nullable: rawColumn.Nullable,
		})
	}

	return CreateTableArgs{
		Table:      raw.Table,
		Columns:    columnSpecs,
		columnDefs: columnDefs,
	}, nil
}

func validateDropTableArgs(raw rawTableArgs, catalog CatalogReader) (DropTableArgs, error) {
	if err := validateTableName(raw.Table); err != nil {
		return DropTableArgs{}, err
	}
	if _, _, err := requireKnownTable(catalog, raw.Table); err != nil {
		return DropTableArgs{}, err
	}
	return DropTableArgs{Table: raw.Table}, nil
}

func validateCreatePrimaryIntIndexArgs(raw rawIndexArgs, catalog CatalogReader) (CreatePrimaryIntIndexArgs, error) {
	if err := validateTableName(raw.Table); err != nil {
		return CreatePrimaryIntIndexArgs{}, err
	}
	if err := validateIndexName(raw.Index); err != nil {
		return CreatePrimaryIntIndexArgs{}, err
	}
	columns, indexes, err := requireKnownTable(catalog, raw.Table)
	if err != nil {
		return CreatePrimaryIntIndexArgs{}, err
	}
	if err := requirePrimaryIntColumns(raw.Table, columns); err != nil {
		return CreatePrimaryIntIndexArgs{}, err
	}
	if containsString(indexes, raw.Index) {
		return CreatePrimaryIntIndexArgs{}, errorf(CodeAlreadyExists, "index %q already exists on table %q", raw.Index, raw.Table)
	}
	return CreatePrimaryIntIndexArgs{Table: raw.Table, Index: raw.Index}, nil
}

func validateDropIndexArgs(raw rawIndexArgs, catalog CatalogReader) (DropIndexArgs, error) {
	if err := validateTableName(raw.Table); err != nil {
		return DropIndexArgs{}, err
	}
	if err := validateIndexName(raw.Index); err != nil {
		return DropIndexArgs{}, err
	}
	_, indexes, err := requireKnownTable(catalog, raw.Table)
	if err != nil {
		return DropIndexArgs{}, err
	}
	if !containsString(indexes, raw.Index) {
		return DropIndexArgs{}, errorf(CodeNotFound, "index %q not found on table %q", raw.Index, raw.Table)
	}
	return DropIndexArgs{Table: raw.Table, Index: raw.Index}, nil
}

func validateInsertValues(columns []haruhidb.ColumnInfo, values map[string]any) (ValueMap, map[string]haruhidb.Value, error) {
	if len(values) == 0 {
		return nil, nil, errorf(CodeConstraint, "values must not be empty")
	}

	expected := make(map[string]haruhidb.ColumnInfo, len(columns))
	for _, column := range columns {
		expected[column.Name] = column
	}

	if len(values) != len(expected) {
		return nil, nil, errorf(CodeConstraint, "values must include exactly %d columns", len(expected))
	}

	normalized := make(ValueMap, len(values))
	typed := make(map[string]haruhidb.Value, len(values))

	for name, value := range values {
		column, ok := expected[name]
		if !ok {
			return nil, nil, errorf(CodeConstraint, "unknown column %q", name)
		}

		raw, typedValue, err := normalizeValueForColumn(column, value)
		if err != nil {
			return nil, nil, withColumnError(name, err)
		}
		normalized[name] = raw
		typed[name] = typedValue
	}

	for _, column := range columns {
		if _, ok := values[column.Name]; !ok {
			return nil, nil, errorf(CodeConstraint, "missing required column %q", column.Name)
		}
	}

	return normalized, typed, nil
}

func validateUpdateValues(columns []haruhidb.ColumnInfo, key int32, values map[string]any) (ValueMap, map[string]haruhidb.Value, error) {
	if len(values) == 0 {
		return nil, nil, errorf(CodeConstraint, "values must not be empty")
	}

	known := make(map[string]haruhidb.ColumnInfo, len(columns))
	for _, column := range columns {
		known[column.Name] = column
	}

	normalized := make(ValueMap, len(values))
	typed := make(map[string]haruhidb.Value, len(values))

	for name, value := range values {
		column, ok := known[name]
		if !ok {
			return nil, nil, errorf(CodeConstraint, "unknown column %q", name)
		}

		raw, typedValue, err := normalizeValueForColumn(column, value)
		if err != nil {
			return nil, nil, withColumnError(name, err)
		}
		if name == columns[0].Name {
			rawInt, ok := raw.(int64)
			if !ok || rawInt != int64(key) {
				return nil, nil, errorf(CodeConstraint, "column %q must equal key %d", name, key)
			}
		}

		normalized[name] = raw
		typed[name] = typedValue
	}

	return normalized, typed, nil
}

func normalizeValueForColumn(column haruhidb.ColumnInfo, value any) (any, haruhidb.Value, error) {
	if value == nil {
		return nil, haruhidb.Value{}, errorf(CodeUnsupported, "NULL values are not supported in public action protocol")
	}

	switch column.Type {
	case haruhidb.TypeBoolean:
		boolean, ok := value.(bool)
		if !ok {
			return nil, haruhidb.Value{}, errorf(CodeConstraint, "expected BOOLEAN")
		}
		return boolean, haruhidb.BoolValue(boolean), nil
	case haruhidb.TypeTinyInt:
		n, err := requireJSONInteger(value)
		if err != nil {
			return nil, haruhidb.Value{}, err
		}
		if n < minInt8 || n > maxInt8 {
			return nil, haruhidb.Value{}, errorf(CodeConstraint, "out of range for TINYINT")
		}
		return n, haruhidb.Int8Value(int8(n)), nil
	case haruhidb.TypeSmallInt:
		n, err := requireJSONInteger(value)
		if err != nil {
			return nil, haruhidb.Value{}, err
		}
		if n < minInt16 || n > maxInt16 {
			return nil, haruhidb.Value{}, errorf(CodeConstraint, "out of range for SMALLINT")
		}
		return n, haruhidb.Int16Value(int16(n)), nil
	case haruhidb.TypeInteger:
		n, err := requireJSONInteger(value)
		if err != nil {
			return nil, haruhidb.Value{}, err
		}
		if n < minInt32 || n > maxInt32 {
			return nil, haruhidb.Value{}, errorf(CodeConstraint, "out of range for INTEGER")
		}
		return n, haruhidb.Int32Value(int32(n)), nil
	case haruhidb.TypeBigInt:
		n, err := requireJSONInteger(value)
		if err != nil {
			return nil, haruhidb.Value{}, err
		}
		return n, haruhidb.Int64Value(n), nil
	case haruhidb.TypeFloat:
		f, err := requireJSONNumber(value)
		if err != nil {
			return nil, haruhidb.Value{}, err
		}
		if math.IsInf(f, 0) || math.Abs(f) > math.MaxFloat32 {
			return nil, haruhidb.Value{}, errorf(CodeConstraint, "out of range for FLOAT")
		}
		return f, haruhidb.Float32Value(float32(f)), nil
	case haruhidb.TypeDouble:
		f, err := requireJSONNumber(value)
		if err != nil {
			return nil, haruhidb.Value{}, err
		}
		if math.IsInf(f, 0) {
			return nil, haruhidb.Value{}, errorf(CodeConstraint, "out of range for DOUBLE")
		}
		return f, haruhidb.Float64Value(f), nil
	case haruhidb.TypeVarchar:
		s, ok := value.(string)
		if !ok {
			return nil, haruhidb.Value{}, errorf(CodeConstraint, "expected VARCHAR")
		}
		if column.Length > 0 && uint32(len(s)) > column.Length {
			return nil, haruhidb.Value{}, errorf(CodeConstraint, "VARCHAR length exceeds %d", column.Length)
		}
		return s, haruhidb.StringValue(s), nil
	case haruhidb.TypeDecimal:
		return nil, haruhidb.Value{}, errorf(CodeUnsupported, "DECIMAL is not supported in protocol values")
	default:
		return nil, haruhidb.Value{}, errorf(CodeUnsupported, "unsupported column type %d", column.Type)
	}
}

func requireJSONInteger(value any) (int64, error) {
	number, ok := value.(json.Number)
	if !ok {
		return 0, errorf(CodeConstraint, "expected integer number")
	}
	n, err := number.Int64()
	if err != nil {
		return 0, errorf(CodeConstraint, "expected integer number")
	}
	return n, nil
}

func requireJSONNumber(value any) (float64, error) {
	number, ok := value.(json.Number)
	if !ok {
		return 0, errorf(CodeConstraint, "expected numeric value")
	}
	f, err := number.Float64()
	if err != nil {
		return 0, errorf(CodeConstraint, "expected numeric value")
	}
	return f, nil
}

func withColumnError(name string, err error) error {
	var typed *Error
	if strings.TrimSpace(name) == "" {
		return err
	}
	if ok := asError(err, &typed); ok {
		return wrapError(typed.Code, fmt.Sprintf("column %q: %s", name, typed.Message), err)
	}
	return wrapError(CodeConstraint, fmt.Sprintf("column %q: %s", name, err.Error()), err)
}

func containsString(items []string, target string) bool {
	for _, item := range items {
		if item == target {
			return true
		}
	}
	return false
}
