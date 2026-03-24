package action

import (
	"bytes"
	"encoding/json"
	"fmt"
	"io"
	"strings"
)

type rawTableArgs struct {
	Table string `json:"table"`
}

type rawGetByPrimaryIntArgs struct {
	Table string `json:"table"`
	Key   *int32 `json:"key"`
}

type rawScanAllArgs struct {
	Table string `json:"table"`
	Limit *int   `json:"limit"`
}

type rawScanPrimaryIntRangeArgs struct {
	Table    string `json:"table"`
	StartKey *int32 `json:"start_key"`
	EndKey   *int32 `json:"end_key"`
	Limit    *int   `json:"limit"`
}

type rawInsertRowArgs struct {
	Table  string         `json:"table"`
	Values map[string]any `json:"values"`
}

type rawUpdateByPrimaryIntArgs struct {
	Table  string         `json:"table"`
	Key    *int32         `json:"key"`
	Values map[string]any `json:"values"`
}

type rawDeleteByPrimaryIntArgs struct {
	Table string `json:"table"`
	Key   *int32 `json:"key"`
}

type rawCreateTableColumn struct {
	Name     string  `json:"name"`
	Type     string  `json:"type"`
	Length   *uint32 `json:"length"`
	Nullable bool    `json:"nullable"`
}

type rawCreateTableArgs struct {
	Table   string                 `json:"table"`
	Columns []rawCreateTableColumn `json:"columns"`
}

type rawIndexArgs struct {
	Table string `json:"table"`
	Index string `json:"index"`
}

type rawBatchItem struct {
	Action Action          `json:"action"`
	Args   json.RawMessage `json:"args"`
}

type rawBatchArgs struct {
	Requests    []rawBatchItem `json:"requests"`
	StopOnError bool           `json:"stop_on_error"`
}

func DecodeAndValidate(data []byte, catalog CatalogReader) (*Request, error) {
	envelope, err := Decode(data)
	if err != nil {
		return nil, err
	}
	return ValidateEnvelope(envelope, catalog)
}

func Decode(data []byte) (RequestEnvelope, error) {
	var envelope RequestEnvelope
	if err := decodeStrictJSON(data, &envelope); err != nil {
		return RequestEnvelope{}, errorf(CodeInvalidRequest, "decode request envelope: %v", err)
	}
	return envelope, nil
}

func ValidateEnvelope(envelope RequestEnvelope, catalog CatalogReader) (*Request, error) {
	canonicalVersion, ok := CanonicalProtocolVersion(envelope.Version)
	if !ok {
		return nil, errorf(CodeInvalidRequest, "version must be one of %q, %q, or %q", VersionV1, VersionV2, VersionV3)
	}

	envelope.Version = canonicalVersion

	if strings.TrimSpace(envelope.RequestID) == "" {
		return nil, errorf(CodeInvalidRequest, "request_id must not be empty")
	}
	if !envelope.Mode.Valid() {
		return nil, errorf(CodeInvalidRequest, "mode must be one of %q or %q", ModeReadOnly, ModeReadWrite)
	}
	if !envelope.Action.Valid() {
		return nil, errorf(CodeInvalidRequest, "unsupported action %q", envelope.Action)
	}
	if !ActionSupportedInVersion(envelope.Version, envelope.Action) {
		return nil, errorf(CodeInvalidRequest, "action %q is not supported in version %q", envelope.Action, envelope.Version)
	}
	if envelope.Mode == ModeReadOnly && envelope.Action.IsWrite() {
		return nil, errorf(CodeInvalidRequest, "action %q requires mode %q", envelope.Action, ModeReadWrite)
	}
	if err := requireJSONObject(envelope.Args, "args"); err != nil {
		return nil, err
	}

	req := &Request{
		Version:   envelope.Version,
		RequestID: envelope.RequestID,
		Mode:      envelope.Mode,
		Action:    envelope.Action,
	}

	switch envelope.Action {
	case ActionListTables:
		var args ListTablesArgs
		if err := decodeStrictJSON(envelope.Args, &args); err != nil {
			return nil, errorf(CodeInvalidRequest, "decode %q args: %v", envelope.Action, err)
		}
		req.Args = args
	case ActionTableExists:
		var args TableExistsArgs
		if err := decodeStrictJSON(envelope.Args, &args); err != nil {
			return nil, errorf(CodeInvalidRequest, "decode %q args: %v", envelope.Action, err)
		}
		if err := validateTableName(args.Table); err != nil {
			return nil, err
		}
		req.Args = args
	case ActionDescribeTable:
		var raw rawTableArgs
		if err := decodeStrictJSON(envelope.Args, &raw); err != nil {
			return nil, errorf(CodeInvalidRequest, "decode %q args: %v", envelope.Action, err)
		}
		if err := validateTableName(raw.Table); err != nil {
			return nil, err
		}
		columns, indexes, err := requireKnownTable(catalog, raw.Table)
		if err != nil {
			return nil, err
		}
		req.Args = DescribeTableArgs{Table: raw.Table}
		req.columns = columns
		req.indexes = indexes
	case ActionGetByPrimaryInt:
		var raw rawGetByPrimaryIntArgs
		if err := decodeStrictJSON(envelope.Args, &raw); err != nil {
			return nil, errorf(CodeInvalidRequest, "decode %q args: %v", envelope.Action, err)
		}
		columns, indexes, err := requireKnownTable(catalog, raw.Table)
		if err != nil {
			return nil, err
		}
		args, err := validateGetByPrimaryIntArgs(raw, columns, indexes)
		if err != nil {
			return nil, err
		}
		req.Args = args
		req.columns = columns
		req.indexes = indexes
	case ActionScanAll:
		var raw rawScanAllArgs
		if err := decodeStrictJSON(envelope.Args, &raw); err != nil {
			return nil, errorf(CodeInvalidRequest, "decode %q args: %v", envelope.Action, err)
		}
		columns, indexes, err := requireKnownTable(catalog, raw.Table)
		if err != nil {
			return nil, err
		}
		args, err := validateScanAllArgs(raw)
		if err != nil {
			return nil, err
		}
		req.Args = args
		req.columns = columns
		req.indexes = indexes
	case ActionScanPrimaryIntRange:
		var raw rawScanPrimaryIntRangeArgs
		if err := decodeStrictJSON(envelope.Args, &raw); err != nil {
			return nil, errorf(CodeInvalidRequest, "decode %q args: %v", envelope.Action, err)
		}
		columns, indexes, err := requireKnownTable(catalog, raw.Table)
		if err != nil {
			return nil, err
		}
		args, err := validateScanPrimaryIntRangeArgs(raw, columns, indexes)
		if err != nil {
			return nil, err
		}
		req.Args = args
		req.columns = columns
		req.indexes = indexes
	case ActionInsertRow:
		var raw rawInsertRowArgs
		if err := decodeStrictJSON(envelope.Args, &raw); err != nil {
			return nil, errorf(CodeInvalidRequest, "decode %q args: %v", envelope.Action, err)
		}
		columns, indexes, err := requireKnownTable(catalog, raw.Table)
		if err != nil {
			return nil, err
		}
		args, err := validateInsertRowArgs(raw, columns)
		if err != nil {
			return nil, err
		}
		req.Args = args
		req.columns = columns
		req.indexes = indexes
	case ActionUpdateByPrimaryInt:
		var raw rawUpdateByPrimaryIntArgs
		if err := decodeStrictJSON(envelope.Args, &raw); err != nil {
			return nil, errorf(CodeInvalidRequest, "decode %q args: %v", envelope.Action, err)
		}
		columns, indexes, err := requireKnownTable(catalog, raw.Table)
		if err != nil {
			return nil, err
		}
		args, err := validateUpdateByPrimaryIntArgs(raw, columns)
		if err != nil {
			return nil, err
		}
		req.Args = args
		req.columns = columns
		req.indexes = indexes
	case ActionDeleteByPrimaryInt:
		var raw rawDeleteByPrimaryIntArgs
		if err := decodeStrictJSON(envelope.Args, &raw); err != nil {
			return nil, errorf(CodeInvalidRequest, "decode %q args: %v", envelope.Action, err)
		}
		columns, indexes, err := requireKnownTable(catalog, raw.Table)
		if err != nil {
			return nil, err
		}
		args, err := validateDeleteByPrimaryIntArgs(raw, columns)
		if err != nil {
			return nil, err
		}
		req.Args = args
		req.columns = columns
		req.indexes = indexes
	case ActionCreateTable:
		var raw rawCreateTableArgs
		if err := decodeStrictJSON(envelope.Args, &raw); err != nil {
			return nil, errorf(CodeInvalidRequest, "decode %q args: %v", envelope.Action, err)
		}
		args, err := validateCreateTableArgs(raw, catalog)
		if err != nil {
			return nil, err
		}
		req.Args = args
	case ActionDropTable:
		var raw rawTableArgs
		if err := decodeStrictJSON(envelope.Args, &raw); err != nil {
			return nil, errorf(CodeInvalidRequest, "decode %q args: %v", envelope.Action, err)
		}
		args, err := validateDropTableArgs(raw, catalog)
		if err != nil {
			return nil, err
		}
		req.Args = args
	case ActionCreatePrimaryIndex:
		var raw rawIndexArgs
		if err := decodeStrictJSON(envelope.Args, &raw); err != nil {
			return nil, errorf(CodeInvalidRequest, "decode %q args: %v", envelope.Action, err)
		}
		args, err := validateCreatePrimaryIntIndexArgs(raw, catalog)
		if err != nil {
			return nil, err
		}
		req.Args = args
	case ActionDropIndex:
		var raw rawIndexArgs
		if err := decodeStrictJSON(envelope.Args, &raw); err != nil {
			return nil, errorf(CodeInvalidRequest, "decode %q args: %v", envelope.Action, err)
		}
		args, err := validateDropIndexArgs(raw, catalog)
		if err != nil {
			return nil, err
		}
		req.Args = args
	case ActionBatch:
		var raw rawBatchArgs
		if err := decodeStrictJSON(envelope.Args, &raw); err != nil {
			return nil, errorf(CodeInvalidRequest, "decode %q args: %v", envelope.Action, err)
		}
		args, err := validateBatchArgs(envelope, raw, catalog)
		if err != nil {
			return nil, err
		}
		req.Args = args
	default:
		return nil, errorf(CodeInvalidRequest, "unsupported action %q", envelope.Action)
	}

	return req, nil
}

func validateBatchArgs(
	envelope RequestEnvelope,
	raw rawBatchArgs,
	catalog CatalogReader,
) (BatchArgs, error) {
	if len(raw.Requests) == 0 {
		return BatchArgs{}, errorf(CodeInvalidRequest, "requests must contain at least one action")
	}

	validated := make([]*Request, 0, len(raw.Requests))
	for i, item := range raw.Requests {
		if !item.Action.Valid() {
			return BatchArgs{}, errorf(CodeInvalidRequest, "requests[%d].action unsupported action %q", i, item.Action)
		}
		if !ActionSupportedInVersion(envelope.Version, item.Action) {
			return BatchArgs{}, errorf(CodeInvalidRequest, "requests[%d].action %q is not supported in version %q", i, item.Action, envelope.Version)
		}
		if item.Action == ActionBatch {
			return BatchArgs{}, errorf(CodeInvalidRequest, "requests[%d].action %q is not supported", i, item.Action)
		}
		if envelope.Mode == ModeReadOnly && item.Action.IsWrite() {
			return BatchArgs{}, errorf(CodeInvalidRequest, "requests[%d].action %q requires mode %q", i, item.Action, ModeReadWrite)
		}
		if err := requireJSONObject(item.Args, fmt.Sprintf("requests[%d].args", i)); err != nil {
			return BatchArgs{}, err
		}

		subEnvelope := RequestEnvelope{
			Version:   envelope.Version,
			RequestID: fmt.Sprintf("%s/%d", envelope.RequestID, i),
			Mode:      envelope.Mode,
			Action:    item.Action,
			Args:      item.Args,
		}
		subReq, err := ValidateEnvelope(subEnvelope, catalog)
		if err != nil {
			return BatchArgs{}, withBatchItemError(i, err)
		}
		validated = append(validated, subReq)
	}

	return BatchArgs{
		StopOnError: raw.StopOnError,
		requests:    validated,
	}, nil
}

func withBatchItemError(index int, err error) error {
	var typed *Error
	if asError(err, &typed) {
		return wrapError(typed.Code, fmt.Sprintf("requests[%d]: %s", index, typed.Message), err)
	}
	return wrapError(CodeInternal, fmt.Sprintf("requests[%d]: %s", index, err.Error()), err)
}

func decodeStrictJSON(data []byte, target any) error {
	dec := json.NewDecoder(bytes.NewReader(data))
	dec.DisallowUnknownFields()
	dec.UseNumber()

	if err := dec.Decode(target); err != nil {
		return err
	}

	var trailing any
	if err := dec.Decode(&trailing); err != io.EOF {
		if err == nil {
			return fmt.Errorf("unexpected trailing JSON input")
		}
		return err
	}

	return nil
}

func requireJSONObject(raw json.RawMessage, field string) error {
	trimmed := bytes.TrimSpace(raw)
	if len(trimmed) == 0 {
		return errorf(CodeInvalidRequest, "%s must be a JSON object", field)
	}
	if trimmed[0] != '{' {
		return errorf(CodeInvalidRequest, "%s must be a JSON object", field)
	}
	return nil
}
