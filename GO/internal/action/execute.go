package action

import (
	"context"
	"errors"
	"fmt"
	"io"

	"haruhidb-go/haruhidb"
)

func ExecuteEnvelope(
	ctx context.Context,
	db *haruhidb.DB,
	env RequestEnvelope,
) (ResponseEnvelope[map[string]any], error) {
	if err := checkContext(ctx); err != nil {
		return ResponseEnvelope[map[string]any]{}, err
	}
	if db == nil {
		return failureResponse(env.RequestID, env.Action, errorf(CodeInvalidHandle, "db must not be nil")), nil
	}

	req, err := ValidateEnvelope(env, db)
	if err != nil {
		return failureResponse(env.RequestID, env.Action, err), nil
	}
	return Execute(ctx, db, req)
}

func Execute(
	ctx context.Context,
	db *haruhidb.DB,
	req *Request,
) (ResponseEnvelope[map[string]any], error) {
	if err := checkContext(ctx); err != nil {
		return ResponseEnvelope[map[string]any]{}, err
	}
	if req == nil {
		return failureResponse("", "", errorf(CodeInvalidRequest, "request must not be nil")), nil
	}
	if db == nil {
		return failureResponse(req.RequestID, req.Action, errorf(CodeInvalidHandle, "db must not be nil")), nil
	}

	data, err := executeAction(ctx, db, req)
	if err != nil {
		if errors.Is(err, context.Canceled) || errors.Is(err, context.DeadlineExceeded) {
			return ResponseEnvelope[map[string]any]{}, err
		}
		return failureResponse(req.RequestID, req.Action, err), nil
	}
	return successResponse(req.RequestID, req.Action, data), nil
}

func executeAction(ctx context.Context, db *haruhidb.DB, req *Request) (map[string]any, error) {
	switch args := req.Args.(type) {
	case ListTablesArgs:
		return executeListTables(db)
	case TableExistsArgs:
		return executeTableExists(db, args)
	case DescribeTableArgs:
		return executeDescribeTable(db, req, args)
	case GetByPrimaryIntArgs:
		return executeGetByPrimaryInt(db, req, args)
	case ScanAllArgs:
		return executeScanAll(ctx, db, req, args)
	case ScanPrimaryIntRangeArgs:
		return executeScanPrimaryIntRange(ctx, db, req, args)
	case InsertRowArgs:
		return executeInsertRow(db, req, args)
	case DeleteByPrimaryIntArgs:
		return executeDeleteByPrimaryInt(db, args)
	case UpdateByPrimaryIntArgs:
		return executeUpdateByPrimaryInt(ctx, db, req, args)
	case CreateTableArgs:
		return executeCreateTable(db, args)
	case DropTableArgs:
		return executeDropTable(db, args)
	case CreatePrimaryIntIndexArgs:
		return executeCreatePrimaryIntIndex(db, args)
	case DropIndexArgs:
		return executeDropIndex(db, args)
	case BatchArgs:
		return executeBatch(ctx, db, args)
	default:
		return nil, errorf(CodeInvalidRequest, "unsupported request args type %T", req.Args)
	}
}

func executeListTables(db *haruhidb.DB) (map[string]any, error) {
	tables, err := db.ListTables()
	if err != nil {
		return nil, err
	}
	return map[string]any{
		"tables": tables,
	}, nil
}

func executeTableExists(db *haruhidb.DB, args TableExistsArgs) (map[string]any, error) {
	exists, err := db.TableExists(args.Table)
	if err != nil {
		return nil, err
	}
	return map[string]any{
		"table":  args.Table,
		"exists": exists,
	}, nil
}

func executeDescribeTable(db *haruhidb.DB, req *Request, args DescribeTableArgs) (map[string]any, error) {
	columns, indexes, err := ensureTableMetadata(db, req, args.Table)
	if err != nil {
		return nil, err
	}

	return map[string]any{
		"table":   args.Table,
		"columns": columnsToMaps(columns),
		"indexes": indexesToMaps(indexes),
	}, nil
}

func executeGetByPrimaryInt(db *haruhidb.DB, req *Request, args GetByPrimaryIntArgs) (map[string]any, error) {
	columns, _, err := ensureTableMetadata(db, req, args.Table)
	if err != nil {
		return nil, err
	}

	row, found, err := db.GetRowByPrimaryInt(args.Table, args.Key)
	if err != nil {
		return nil, err
	}

	var rowMap any
	if found {
		converted, convErr := rowToMap(columns, row)
		if convErr != nil {
			return nil, convErr
		}
		rowMap = converted
	}

	return map[string]any{
		"table": args.Table,
		"row":   rowMap,
		"found": found,
	}, nil
}

func executeScanAll(
	ctx context.Context,
	db *haruhidb.DB,
	req *Request,
	args ScanAllArgs,
) (map[string]any, error) {
	columns, _, err := ensureTableMetadata(db, req, args.Table)
	if err != nil {
		return nil, err
	}

	scan, err := db.ScanAll(args.Table)
	if err != nil {
		return nil, err
	}

	rows, truncated, err := scanToRows(ctx, columns, scan, args.Limit)
	if err != nil {
		return nil, err
	}

	return map[string]any{
		"table":     args.Table,
		"rows":      rows,
		"row_count": len(rows),
		"truncated": truncated,
	}, nil
}

func executeScanPrimaryIntRange(
	ctx context.Context,
	db *haruhidb.DB,
	req *Request,
	args ScanPrimaryIntRangeArgs,
) (map[string]any, error) {
	columns, _, err := ensureTableMetadata(db, req, args.Table)
	if err != nil {
		return nil, err
	}

	scan, err := db.ScanByPrimaryIntRange(args.Table, args.StartKey, args.EndKey)
	if err != nil {
		return nil, err
	}

	rows, truncated, err := scanToRows(ctx, columns, scan, args.Limit)
	if err != nil {
		return nil, err
	}

	return map[string]any{
		"table":     args.Table,
		"rows":      rows,
		"row_count": len(rows),
		"truncated": truncated,
	}, nil
}

func executeInsertRow(db *haruhidb.DB, req *Request, args InsertRowArgs) (map[string]any, error) {
	columns, _, err := ensureTableMetadata(db, req, args.Table)
	if err != nil {
		return nil, err
	}

	typedValues, err := ensureInsertTypedValues(args, columns)
	if err != nil {
		return nil, err
	}
	ordered, err := typedValuesToOrdered(columns, typedValues)
	if err != nil {
		return nil, err
	}

	if err := db.InsertRow(args.Table, ordered); err != nil {
		return nil, err
	}

	return map[string]any{
		"table":    args.Table,
		"inserted": 1,
	}, nil
}

func executeDeleteByPrimaryInt(db *haruhidb.DB, args DeleteByPrimaryIntArgs) (map[string]any, error) {
	deleted, err := db.DeleteRowByPrimaryInt(args.Table, args.Key)
	if err != nil {
		return nil, err
	}

	return map[string]any{
		"table":   args.Table,
		"deleted": deleted,
	}, nil
}

func executeCreateTable(db *haruhidb.DB, args CreateTableArgs) (map[string]any, error) {
	columnDefs, err := ensureCreateTableColumnDefs(args)
	if err != nil {
		return nil, err
	}
	if err := db.CreateTable(args.Table, columnDefs); err != nil {
		return nil, err
	}
	return map[string]any{
		"table":   args.Table,
		"created": 1,
	}, nil
}

func executeDropTable(db *haruhidb.DB, args DropTableArgs) (map[string]any, error) {
	if err := db.DropTable(args.Table); err != nil {
		return nil, err
	}
	return map[string]any{
		"table":   args.Table,
		"dropped": 1,
	}, nil
}

func executeCreatePrimaryIntIndex(db *haruhidb.DB, args CreatePrimaryIntIndexArgs) (map[string]any, error) {
	if err := db.CreatePrimaryIntIndex(args.Table, args.Index); err != nil {
		return nil, err
	}
	return map[string]any{
		"table":   args.Table,
		"index":   args.Index,
		"created": 1,
	}, nil
}

func executeDropIndex(db *haruhidb.DB, args DropIndexArgs) (map[string]any, error) {
	if err := db.DropIndex(args.Table, args.Index); err != nil {
		return nil, err
	}
	return map[string]any{
		"table":   args.Table,
		"index":   args.Index,
		"dropped": 1,
	}, nil
}
func executeUpdateByPrimaryInt(
	ctx context.Context,
	db *haruhidb.DB,
	req *Request,
	args UpdateByPrimaryIntArgs,
) (map[string]any, error) {
	columns, indexes, err := ensureTableMetadata(db, req, args.Table)
	if err != nil {
		return nil, err
	}

	patchValues, err := ensureUpdatePatchTypedValues(args, columns)
	if err != nil {
		return nil, err
	}

	currentRow, found, err := loadCurrentRowForUpdate(ctx, db, args.Table, args.Key, indexes)
	if err != nil {
		return nil, err
	}
	if !found {
		return map[string]any{
			"table":   args.Table,
			"updated": 0,
		}, nil
	}

	if len(currentRow.Values) != len(columns) {
		return nil, errorf(
			CodeInternal,
			"row width %d does not match schema width %d",
			len(currentRow.Values),
			len(columns),
		)
	}

	mergedTypedValues := make(map[string]haruhidb.Value, len(columns))
	for i, column := range columns {
		mergedTypedValues[column.Name] = currentRow.Values[i]
	}
	for name, value := range patchValues {
		mergedTypedValues[name] = value
	}

	ordered, err := typedValuesToOrdered(columns, mergedTypedValues)
	if err != nil {
		return nil, err
	}

	updated, err := db.UpdateRowByPrimaryInt(args.Table, args.Key, ordered)
	if err != nil {
		return nil, err
	}

	return map[string]any{
		"table":   args.Table,
		"updated": updated,
	}, nil
}

func executeBatch(
	ctx context.Context,
	db *haruhidb.DB,
	args BatchArgs,
) (map[string]any, error) {
	if len(args.requests) == 0 {
		return nil, errorf(CodeInvalidRequest, "requests must contain at least one action")
	}

	results := make([]map[string]any, 0, len(args.requests))
	succeeded := 0
	failed := 0
	stopped := false

	for i, request := range args.requests {
		if err := checkContext(ctx); err != nil {
			return nil, err
		}
		if request == nil {
			itemErr := errorf(CodeInternal, "batch request item is nil")
			results = append(results, batchResultFailure(i, "", itemErr))
			failed++
			if args.StopOnError {
				stopped = true
				break
			}
			continue
		}

		data, err := executeAction(ctx, db, request)
		if err != nil {
			if errors.Is(err, context.Canceled) || errors.Is(err, context.DeadlineExceeded) {
				return nil, err
			}
			results = append(results, batchResultFailure(i, request.Action, err))
			failed++
			if args.StopOnError {
				stopped = true
				break
			}
			continue
		}

		results = append(results, batchResultSuccess(i, request.Action, data))
		succeeded++
	}

	return map[string]any{
		"results":   results,
		"total":     len(args.requests),
		"succeeded": succeeded,
		"failed":    failed,
		"stopped":   stopped,
	}, nil
}

func batchResultSuccess(index int, action Action, data map[string]any) map[string]any {
	if data == nil {
		data = map[string]any{}
	}
	return map[string]any{
		"index":  index,
		"action": action,
		"ok":     true,
		"data":   data,
		"error":  nil,
	}
}

func batchResultFailure(index int, action Action, err error) map[string]any {
	protocolErr := toProtocolError(err)
	return map[string]any{
		"index":  index,
		"action": action,
		"ok":     false,
		"data":   nil,
		"error": map[string]any{
			"code":    protocolErr.Code,
			"message": protocolErr.Message,
		},
	}
}

func ensureTableMetadata(
	db *haruhidb.DB,
	req *Request,
	table string,
) ([]haruhidb.ColumnInfo, []string, error) {
	if req != nil && len(req.columns) > 0 {
		return append([]haruhidb.ColumnInfo(nil), req.columns...), append([]string(nil), req.indexes...), nil
	}

	columns, err := db.ListTableColumns(table)
	if err != nil {
		return nil, nil, err
	}
	indexes, err := db.ListTableIndexes(table)
	if err != nil {
		return nil, nil, err
	}
	return columns, indexes, nil
}

func ensureCreateTableColumnDefs(args CreateTableArgs) ([]haruhidb.ColumnDef, error) {
	if len(args.columnDefs) > 0 {
		return append([]haruhidb.ColumnDef(nil), args.columnDefs...), nil
	}
	if len(args.Columns) == 0 {
		return nil, errorf(CodeInvalidRequest, "columns must contain at least one column")
	}

	defs := make([]haruhidb.ColumnDef, 0, len(args.Columns))
	for i, column := range args.Columns {
		columnType, err := HaruhiTypeFromProtocol(column.Type)
		if err != nil {
			var typed *Error
			if asError(err, &typed) {
				return nil, errorf(typed.Code, "columns[%d].type: %s", i, typed.Message)
			}
			return nil, errorf(CodeInvalidRequest, "columns[%d].type: %s", i, err.Error())
		}
		defs = append(defs, haruhidb.ColumnDef{
			Name:     column.Name,
			Type:     columnType,
			Length:   column.Length,
			Nullable: column.Nullable,
		})
	}
	return defs, nil
}
func ensureInsertTypedValues(
	args InsertRowArgs,
	columns []haruhidb.ColumnInfo,
) (map[string]haruhidb.Value, error) {
	if len(args.typedValues) > 0 {
		return args.typedValues, nil
	}
	_, typedValues, err := validateInsertValues(columns, args.Values)
	if err != nil {
		return nil, err
	}
	return typedValues, nil
}

func ensureUpdatePatchTypedValues(
	args UpdateByPrimaryIntArgs,
	columns []haruhidb.ColumnInfo,
) (map[string]haruhidb.Value, error) {
	if len(args.typedValues) > 0 {
		return args.typedValues, nil
	}
	_, typedValues, err := validateUpdateValues(columns, args.Key, args.Values)
	if err != nil {
		return nil, err
	}
	return typedValues, nil
}

func loadCurrentRowForUpdate(
	ctx context.Context,
	db *haruhidb.DB,
	table string,
	key int32,
	indexes []string,
) (haruhidb.Row, bool, error) {
	if len(indexes) > 0 {
		return db.GetRowByPrimaryInt(table, key)
	}

	scan, err := db.ScanAll(table)
	if err != nil {
		return haruhidb.Row{}, false, err
	}
	return findRowByPrimaryIntViaScan(ctx, scan, key)
}

func rowToMap(columns []haruhidb.ColumnInfo, row haruhidb.Row) (RowMap, error) {
	if len(columns) != len(row.Values) {
		return nil, errorf(
			CodeInternal,
			"row width %d does not match schema width %d",
			len(row.Values),
			len(columns),
		)
	}

	converted := make(RowMap, len(columns))
	for i, column := range columns {
		value, err := protocolScalarFromValue(row.Values[i])
		if err != nil {
			return nil, wrapError(CodeInternal, fmt.Sprintf("column %q: %s", column.Name, err.Error()), err)
		}
		converted[column.Name] = value
	}
	return converted, nil
}

func protocolScalarFromValue(value haruhidb.Value) (any, error) {
	if value.Null {
		return nil, nil
	}

	switch value.Type {
	case haruhidb.TypeBoolean:
		return value.Bool, nil
	case haruhidb.TypeTinyInt:
		return int64(value.Int8), nil
	case haruhidb.TypeSmallInt:
		return int64(value.Int16), nil
	case haruhidb.TypeInteger:
		return int64(value.Int32), nil
	case haruhidb.TypeBigInt:
		return value.Int64, nil
	case haruhidb.TypeFloat:
		return float64(value.Float32), nil
	case haruhidb.TypeDouble:
		return value.Float64, nil
	case haruhidb.TypeVarchar:
		return value.String, nil
	case haruhidb.TypeDecimal:
		return nil, errorf(CodeUnsupported, "DECIMAL values are not supported in v1")
	default:
		return nil, errorf(CodeUnsupported, "unsupported scanned value type %d", value.Type)
	}
}

func typedValuesToOrdered(
	columns []haruhidb.ColumnInfo,
	typedValues map[string]haruhidb.Value,
) ([]haruhidb.Value, error) {
	if len(columns) == 0 {
		return nil, errorf(CodeInternal, "schema columns must not be empty")
	}
	if len(typedValues) == 0 {
		return nil, errorf(CodeConstraint, "typed values must not be empty")
	}

	known := make(map[string]struct{}, len(columns))
	ordered := make([]haruhidb.Value, len(columns))

	for i, column := range columns {
		known[column.Name] = struct{}{}
		value, ok := typedValues[column.Name]
		if !ok {
			return nil, errorf(CodeConstraint, "missing value for column %q", column.Name)
		}
		ordered[i] = value
	}

	for name := range typedValues {
		if _, ok := known[name]; !ok {
			return nil, errorf(CodeConstraint, "unknown column %q", name)
		}
	}

	return ordered, nil
}

func scanToRows(
	ctx context.Context,
	columns []haruhidb.ColumnInfo,
	scanner *haruhidb.Scanner,
	limit int,
) (_ []RowMap, truncated bool, err error) {
	if scanner == nil {
		return nil, false, errorf(CodeInternal, "scanner must not be nil")
	}
	if limit <= 0 {
		return nil, false, errorf(CodeInvalidRequest, "limit must be greater than 0")
	}

	defer func() {
		closeErr := scanner.Close()
		if err == nil && closeErr != nil {
			err = closeErr
		}
	}()

	rows := make([]RowMap, 0, limit)
	for len(rows) < limit {
		if err := checkContext(ctx); err != nil {
			return nil, false, err
		}

		row, nextErr := scanner.Next()
		if errors.Is(nextErr, io.EOF) {
			return rows, false, nil
		}
		if nextErr != nil {
			return nil, false, nextErr
		}

		rowMap, convErr := rowToMap(columns, row)
		if convErr != nil {
			return nil, false, convErr
		}
		rows = append(rows, rowMap)
	}

	if err := checkContext(ctx); err != nil {
		return nil, false, err
	}

	_, nextErr := scanner.Next()
	if errors.Is(nextErr, io.EOF) {
		return rows, false, nil
	}
	if nextErr != nil {
		return nil, false, nextErr
	}
	return rows, true, nil
}

func findRowByPrimaryIntViaScan(
	ctx context.Context,
	scanner *haruhidb.Scanner,
	key int32,
) (_ haruhidb.Row, found bool, err error) {
	if scanner == nil {
		return haruhidb.Row{}, false, errorf(CodeInternal, "scanner must not be nil")
	}

	defer func() {
		closeErr := scanner.Close()
		if err == nil && closeErr != nil {
			err = closeErr
		}
	}()

	for {
		if err := checkContext(ctx); err != nil {
			return haruhidb.Row{}, false, err
		}

		row, nextErr := scanner.Next()
		if errors.Is(nextErr, io.EOF) {
			return haruhidb.Row{}, false, nil
		}
		if nextErr != nil {
			return haruhidb.Row{}, false, nextErr
		}
		if len(row.Values) == 0 || row.Values[0].Null {
			continue
		}
		if row.Values[0].Type == haruhidb.TypeInteger && row.Values[0].Int32 == key {
			return row, true, nil
		}
	}
}

func columnsToMaps(columns []haruhidb.ColumnInfo) []map[string]any {
	out := make([]map[string]any, 0, len(columns))
	for _, column := range columns {
		typeName, err := ProtocolTypeName(column.Type)
		if err != nil {
			typeName = TypeNameInvalid
		}
		item := map[string]any{
			"name":     column.Name,
			"type":     typeName,
			"nullable": column.Nullable,
		}
		if column.Length > 0 {
			item["length"] = column.Length
		}
		out = append(out, item)
	}
	return out
}

func indexesToMaps(indexes []string) []map[string]any {
	out := make([]map[string]any, 0, len(indexes))
	for _, indexName := range indexes {
		out = append(out, map[string]any{"name": indexName})
	}
	return out
}

func successResponse(
	requestID string,
	action Action,
	data map[string]any,
) ResponseEnvelope[map[string]any] {
	if data == nil {
		data = map[string]any{}
	}
	return ResponseEnvelope[map[string]any]{
		Ok:        true,
		RequestID: requestID,
		Action:    action,
		Data:      &data,
		Error:     nil,
		Meta:      map[string]any{},
	}
}

func failureResponse(
	requestID string,
	action Action,
	err error,
) ResponseEnvelope[map[string]any] {
	protocolErr := toProtocolError(err)
	return ResponseEnvelope[map[string]any]{
		Ok:        false,
		RequestID: requestID,
		Action:    action,
		Data:      nil,
		Error: &ResponseError{
			Code:    protocolErr.Code,
			Message: protocolErr.Message,
		},
		Meta: map[string]any{},
	}
}

func toProtocolError(err error) *Error {
	if err == nil {
		return errorf(CodeInternal, "unknown protocol error")
	}
	var protocolErr *Error
	if asError(err, &protocolErr) {
		return protocolErr
	}
	return mapCatalogError(err)
}

func checkContext(ctx context.Context) error {
	if ctx == nil {
		return nil
	}
	return ctx.Err()
}
