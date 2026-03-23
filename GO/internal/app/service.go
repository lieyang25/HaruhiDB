package app

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"log"
	"regexp"
	"strconv"
	"strings"
	"time"

	"haruhidb-go/haruhidb"
	"haruhidb-go/internal/action"
	"haruhidb-go/internal/nl"
)

const (
	ErrorCodeTranslation = "TRANSLATION_ERROR"
)

var (
	strictActionCallPattern = regexp.MustCompile(`(?i)(list_tables|table_exists|describe_table|get_by_primary_int|scan_all|scan_primary_int_range|insert_row|update_by_primary_int|delete_by_primary_int|create_table|drop_table|create_primary_int_index|drop_index)\s*\(([^)]*)\)`)
	strictTableArgPattern   = regexp.MustCompile(`(?i)table\s*=\s*["']?([a-zA-Z0-9_]+)["']?`)
	strictKeyArgPattern     = regexp.MustCompile(`(?i)key\s*=\s*(-?\d+)`)
)

type semanticStepExpectation struct {
	Action action.Action
	Table  string
	Key    *int64
}

type semanticBatchExpectation struct {
	Steps []semanticStepExpectation
}

type Config struct {
	DB             *haruhidb.DB
	AllowWrite     bool
	RequestTimeout time.Duration
	Translator     nl.Translator
	Logger         *log.Logger
}

type ActionService struct {
	db             *haruhidb.DB
	allowWrite     bool
	requestTimeout time.Duration
	translator     nl.Translator
	logger         *log.Logger
}

type NLRequest struct {
	RequestID string      `json:"request_id"`
	Input     string      `json:"input"`
	Mode      action.Mode `json:"mode,omitempty"`
}

type ServiceError struct {
	Code    string `json:"code"`
	Message string `json:"message"`
}

type NLResult struct {
	RequestID         string         `json:"request_id"`
	NLInput           string         `json:"nl_input"`
	CandidateEnvelope map[string]any `json:"candidate_envelope,omitempty"`
	Valid             bool           `json:"valid"`
	Error             *ServiceError  `json:"error,omitempty"`
	Meta              map[string]any `json:"meta"`

	CandidateRaw json.RawMessage `json:"-"`
}

func NewActionService(cfg Config) (*ActionService, error) {
	if cfg.DB == nil {
		return nil, errors.New("db must not be nil")
	}

	return &ActionService{
		db:             cfg.DB,
		allowWrite:     cfg.AllowWrite,
		requestTimeout: cfg.RequestTimeout,
		translator:     cfg.Translator,
		logger:         cfg.Logger,
	}, nil
}

func (s *ActionService) HasTranslator() bool {
	return s != nil && s.translator != nil
}

func (s *ActionService) ExecuteJSON(ctx context.Context, raw []byte) ([]byte, error) {
	if s == nil {
		return nil, errors.New("service is nil")
	}

	ctx, cancel := s.withRequestTimeout(ctx)
	defer cancel()

	if err := ctx.Err(); err != nil {
		return nil, err
	}

	env, err := action.Decode(raw)
	if err != nil {
		s.logf("execute_json request_id= action=decode ok=false error=%q", err.Error())
		return marshalFailureResponse("", "", err)
	}

	if !s.allowWrite && envelopeContainsWriteAction(env) {
		s.logf("execute_json request_id=%s action=%s ok=false error=%q", env.RequestID, env.Action, "write actions are disabled by server configuration")
		return marshalFailureResponse(
			env.RequestID,
			env.Action,
			&action.Error{
				Code:    action.CodeInvalidRequest,
				Message: "write actions are disabled by server configuration",
			},
		)
	}

	resp, err := action.ExecuteEnvelope(ctx, s.db, env)
	if err != nil {
		s.logf("execute_json request_id=%s action=%s ok=false error=%q", env.RequestID, env.Action, err.Error())
		return nil, err
	}
	s.logf("execute_json request_id=%s action=%s ok=%v", env.RequestID, env.Action, resp.Ok)
	return json.Marshal(resp)
}

func (s *ActionService) TranslateNL(ctx context.Context, req NLRequest) (NLResult, error) {
	if s == nil {
		return NLResult{}, errors.New("service is nil")
	}

	ctx, cancel := s.withRequestTimeout(ctx)
	defer cancel()

	result := NLResult{
		RequestID: strings.TrimSpace(req.RequestID),
		NLInput:   req.Input,
		Meta:      map[string]any{},
	}
	if result.RequestID == "" {
		result.RequestID = fmt.Sprintf("nl-%d", time.Now().UnixNano())
	}

	req.Input = strings.TrimSpace(req.Input)
	if req.Input == "" {
		result.Error = &ServiceError{
			Code:    string(action.CodeInvalidRequest),
			Message: "input must not be empty",
		}
		s.logf("translate_nl request_id=%s valid=false error=%q", result.RequestID, result.Error.Message)
		return result, nil
	}

	if req.Mode == "" {
		req.Mode = action.ModeReadOnly
	}
	if !req.Mode.Valid() {
		result.Error = &ServiceError{
			Code:    string(action.CodeInvalidRequest),
			Message: fmt.Sprintf("mode must be one of %q or %q", action.ModeReadOnly, action.ModeReadWrite),
		}
		s.logf("translate_nl request_id=%s valid=false error=%q", result.RequestID, result.Error.Message)
		return result, nil
	}
	if s.translator == nil {
		result.Error = &ServiceError{
			Code:    ErrorCodeTranslation,
			Message: "translator is not configured",
		}
		s.logf("translate_nl request_id=%s valid=false error=%q", result.RequestID, result.Error.Message)
		return result, nil
	}

	catalogSnapshot, err := s.loadCatalogSnapshot(ctx)
	if err != nil {
		return result, err
	}

	startedAt := time.Now()
	var (
		lastErr error
		hint    string
		model   string
		meta    map[string]any
	)

	for attempt := 0; attempt < 2; attempt++ {
		if err := ctx.Err(); err != nil {
			return result, err
		}

		output, translateErr := s.translator.Translate(ctx, nl.TranslateInput{
			RequestID:      result.RequestID,
			NaturalRequest: req.Input,
			Mode:           req.Mode,
			Catalog:        catalogSnapshot,
			RepairHint:     hint,
		})
		if translateErr != nil {
			lastErr = translateErr
			break
		}

		if strings.TrimSpace(output.Model) != "" {
			model = output.Model
		}
		if len(output.Meta) > 0 {
			meta = output.Meta
		}

		candidateMap, candidateRaw, validateErr := s.validateCandidateEnvelope(output.Candidate)
		if validateErr != nil {
			if normalized, ok := normalizeCandidateEnvelope(output.Candidate, result.RequestID, req.Mode); ok {
				candidateMap, candidateRaw, validateErr = s.validateCandidateEnvelope(normalized)
			}
		}
		if validateErr == nil {
			if semanticErr := enforceSemanticExpectation(req.Input, candidateMap); semanticErr != nil {
				validateErr = semanticErr
			}
		}
		if validateErr != nil {
			lastErr = validateErr
			hint = validateErr.Error()
			continue
		}

		result.Valid = true
		result.CandidateEnvelope = candidateMap
		result.CandidateRaw = candidateRaw
		if model != "" {
			result.Meta["model"] = model
		}
		result.Meta["latency_ms"] = time.Since(startedAt).Milliseconds()
		mergeMeta(result.Meta, meta)
		s.logf("translate_nl request_id=%s valid=true model=%q", result.RequestID, result.Meta["model"])
		return result, nil
	}

	if lastErr == nil {
		lastErr = errors.New("translation failed")
	}
	result.Error = &ServiceError{
		Code:    ErrorCodeTranslation,
		Message: lastErr.Error(),
	}
	if model != "" {
		result.Meta["model"] = model
	}
	result.Meta["latency_ms"] = time.Since(startedAt).Milliseconds()
	mergeMeta(result.Meta, meta)
	s.logf("translate_nl request_id=%s valid=false error=%q", result.RequestID, result.Error.Message)
	return result, nil
}

func normalizeCandidateEnvelope(candidate []byte, requestID string, mode action.Mode) ([]byte, bool) {
	trimmed := strings.TrimSpace(string(candidate))
	if trimmed == "" {
		return nil, false
	}

	var envelope map[string]any
	dec := json.NewDecoder(strings.NewReader(trimmed))
	dec.UseNumber()
	if err := dec.Decode(&envelope); err != nil {
		return nil, false
	}

	normalized := map[string]any{}
	for _, key := range []string{"version", "request_id", "mode", "action", "args"} {
		if value, ok := envelope[key]; ok {
			normalized[key] = value
		}
	}

	changed := len(normalized) != len(envelope)
	versionMissing := false
	if version, ok := normalized["version"].(string); !ok || strings.TrimSpace(version) == "" {
		normalized["version"] = action.VersionV1
		versionMissing = true
		changed = true
	}
	if id, ok := normalized["request_id"].(string); !ok || strings.TrimSpace(id) == "" {
		normalized["request_id"] = strings.TrimSpace(requestID)
		changed = true
	}
	if m, ok := normalized["mode"].(string); !ok || strings.TrimSpace(m) == "" {
		normalized["mode"] = string(mode)
		changed = true
	}
	switch args := normalized["args"].(type) {
	case nil:
		normalized["args"] = map[string]any{}
		changed = true
	case []any:
		// Some small models occasionally emit [] for no-arg actions.
		// Protocol requires args to be a JSON object.
		if len(args) == 0 {
			normalized["args"] = map[string]any{}
			changed = true
		}
	}
	if actionName, ok := normalized["action"].(string); ok {
		canonical := canonicalActionName(actionName)
		if canonical != strings.TrimSpace(actionName) {
			normalized["action"] = canonical
			changed = true
		}

		if versionMissing {
			candidateAction := action.Action(canonical)
			if action.ActionSupportedInVersion(action.VersionV2, candidateAction) && !action.ActionSupportedInVersion(action.VersionV1, candidateAction) {
				normalized["version"] = action.VersionV2
				changed = true
			} else if candidateAction == action.ActionBatch && batchContainsV2OnlyAction(normalized["args"]) {
				normalized["version"] = action.VersionV2
				changed = true
			}
		}

		normalizedArgs, argsChanged := normalizeArgsForAction(canonical, normalized["args"])
		if argsChanged {
			normalized["args"] = normalizedArgs
			changed = true
		}
	}

	if !changed {
		return nil, false
	}
	normalizedBytes, err := json.Marshal(normalized)
	if err != nil {
		return nil, false
	}
	return normalizedBytes, true
}

func canonicalActionName(raw string) string {
	actionName := strings.TrimSpace(raw)
	switch strings.ToLower(actionName) {
	case "query_primary_int", "query_by_primary_int", "select_by_primary_int", "point_get_primary_int":
		return string(action.ActionGetByPrimaryInt)
	case "list_table", "show_tables", "list_table_names":
		return string(action.ActionListTables)
	case "delete_primary_int", "remove_by_primary_int":
		return string(action.ActionDeleteByPrimaryInt)
	case "desc_table", "describe":
		return string(action.ActionDescribeTable)
	case "create_table_if_missing", "table_create":
		return string(action.ActionCreateTable)
	case "table_drop", "delete_table":
		return string(action.ActionDropTable)
	case "create_index", "create_primary_index", "create_primary_int_idx":
		return string(action.ActionCreatePrimaryIndex)
	case "drop_primary_index", "drop_primary_int_idx", "remove_index":
		return string(action.ActionDropIndex)
	default:
		return actionName
	}
}

func normalizeArgsForAction(actionName string, argsValue any) (any, bool) {
	switch action.Action(actionName) {
	case action.ActionListTables:
		argsMap, ok := argsValue.(map[string]any)
		if !ok || len(argsMap) > 0 {
			return map[string]any{}, true
		}
		return argsMap, false
	case action.ActionTableExists, action.ActionDescribeTable:
		return filterArgsKeys(argsValue, []string{"table"})
	case action.ActionGetByPrimaryInt:
		return filterArgsKeys(argsValue, []string{"table", "key"})
	case action.ActionScanAll:
		return filterArgsKeys(argsValue, []string{"table", "limit"})
	case action.ActionScanPrimaryIntRange:
		return filterArgsKeys(argsValue, []string{"table", "start_key", "end_key", "limit"})
	case action.ActionInsertRow:
		return filterArgsKeys(argsValue, []string{"table", "values"})
	case action.ActionUpdateByPrimaryInt:
		return filterArgsKeys(argsValue, []string{"table", "key", "values"})
	case action.ActionDeleteByPrimaryInt:
		return filterArgsKeys(argsValue, []string{"table", "key"})
	case action.ActionCreateTable:
		return filterArgsKeys(argsValue, []string{"table", "columns"})
	case action.ActionDropTable:
		return filterArgsKeys(argsValue, []string{"table"})
	case action.ActionCreatePrimaryIndex, action.ActionDropIndex:
		return filterArgsKeys(argsValue, []string{"table", "index"})
	case action.ActionBatch:
		return normalizeBatchArgs(argsValue)
	default:
		return argsValue, false
	}
}

func filterArgsKeys(argsValue any, allowed []string) (any, bool) {
	argsMap, ok := argsValue.(map[string]any)
	if !ok {
		return argsValue, false
	}
	filtered := map[string]any{}
	for _, key := range allowed {
		if value, exists := argsMap[key]; exists {
			filtered[key] = value
		}
	}
	if len(filtered) == len(argsMap) {
		return argsValue, false
	}
	return filtered, true
}

func normalizeBatchArgs(argsValue any) (any, bool) {
	changed := false

	switch typed := argsValue.(type) {
	case []any:
		argsValue = map[string]any{"requests": typed}
		changed = true
	case map[string]any:
	default:
		return argsValue, false
	}

	filteredValue, filteredChanged := filterArgsKeys(argsValue, []string{"requests", "stop_on_error"})
	changed = changed || filteredChanged
	filteredArgs, ok := filteredValue.(map[string]any)
	if !ok {
		return filteredValue, changed
	}

	rawRequests, ok := filteredArgs["requests"].([]any)
	if !ok || len(rawRequests) == 0 {
		return filteredArgs, changed
	}

	normalizedRequests := make([]any, 0, len(rawRequests))
	requestsChanged := false
	for _, item := range rawRequests {
		reqMap, ok := item.(map[string]any)
		if !ok {
			normalizedRequests = append(normalizedRequests, item)
			continue
		}

		filteredReqValue, reqChanged := filterArgsKeys(reqMap, []string{"action", "args"})
		filteredReq, ok := filteredReqValue.(map[string]any)
		if !ok {
			normalizedRequests = append(normalizedRequests, filteredReqValue)
			requestsChanged = requestsChanged || reqChanged
			continue
		}

		actionName, hasAction := filteredReq["action"].(string)
		if hasAction {
			canonical := canonicalActionName(actionName)
			if canonical != strings.TrimSpace(actionName) {
				filteredReq["action"] = canonical
				reqChanged = true
			}
			normalizedSubArgs, subChanged := normalizeArgsForAction(canonical, filteredReq["args"])
			if subChanged {
				filteredReq["args"] = normalizedSubArgs
				reqChanged = true
			}
		}

		normalizedRequests = append(normalizedRequests, filteredReq)
		requestsChanged = requestsChanged || reqChanged
	}

	if requestsChanged {
		filteredArgs["requests"] = normalizedRequests
	}

	return filteredArgs, changed || requestsChanged
}

func batchContainsV2OnlyAction(argsValue any) bool {
	argsMap, ok := argsValue.(map[string]any)
	if !ok {
		return false
	}

	requests, ok := argsMap["requests"].([]any)
	if !ok {
		return false
	}

	for _, item := range requests {
		reqMap, ok := item.(map[string]any)
		if !ok {
			continue
		}
		actionName, ok := reqMap["action"].(string)
		if !ok {
			continue
		}
		candidateAction := action.Action(canonicalActionName(actionName))
		if action.ActionSupportedInVersion(action.VersionV2, candidateAction) && !action.ActionSupportedInVersion(action.VersionV1, candidateAction) {
			return true
		}
	}
	return false
}

func enforceSemanticExpectation(naturalRequest string, candidate map[string]any) error {
	expectation := deriveSemanticBatchExpectation(naturalRequest)
	if expectation == nil {
		return nil
	}

	rawAction, _ := candidate["action"].(string)
	if action.Action(canonicalActionName(rawAction)) != action.ActionBatch {
		return fmt.Errorf("semantic guard: expected top-level action %q for ordered multi-step request", action.ActionBatch)
	}

	args, ok := candidate["args"].(map[string]any)
	if !ok {
		return errors.New("semantic guard: expected args to be object")
	}

	rawRequests, ok := args["requests"].([]any)
	if !ok {
		return errors.New("semantic guard: expected batch args.requests to be array")
	}
	if len(rawRequests) != len(expectation.Steps) {
		return fmt.Errorf("semantic guard: expected %d batch steps, got %d", len(expectation.Steps), len(rawRequests))
	}

	for i, expected := range expectation.Steps {
		requestMap, ok := rawRequests[i].(map[string]any)
		if !ok {
			return fmt.Errorf("semantic guard: requests[%d] must be object", i)
		}

		rawSubAction, _ := requestMap["action"].(string)
		subAction := action.Action(canonicalActionName(rawSubAction))
		if subAction != expected.Action {
			return fmt.Errorf("semantic guard: expected requests[%d].action=%q, got %q", i, expected.Action, strings.TrimSpace(rawSubAction))
		}

		subArgs, ok := requestMap["args"].(map[string]any)
		if !ok {
			return fmt.Errorf("semantic guard: requests[%d].args must be object", i)
		}

		if expected.Table != "" {
			table, _ := subArgs["table"].(string)
			if strings.TrimSpace(table) != expected.Table {
				return fmt.Errorf("semantic guard: expected requests[%d].args.table=%q, got %q", i, expected.Table, strings.TrimSpace(table))
			}
		}
		if expected.Key != nil {
			key, ok := parseInt64Value(subArgs["key"])
			if !ok || key != *expected.Key {
				return fmt.Errorf("semantic guard: expected requests[%d].args.key=%d", i, *expected.Key)
			}
		}
	}

	return nil
}

func deriveSemanticBatchExpectation(naturalRequest string) *semanticBatchExpectation {
	trimmed := strings.TrimSpace(naturalRequest)
	if trimmed == "" {
		return nil
	}

	lower := strings.ToLower(trimmed)
	if !containsStrictSequencingMarker(lower) {
		return nil
	}

	matches := strictActionCallPattern.FindAllStringSubmatch(trimmed, -1)
	if len(matches) < 2 {
		return nil
	}

	steps := make([]semanticStepExpectation, 0, len(matches))
	for _, match := range matches {
		if len(match) < 3 {
			continue
		}

		actionName := action.Action(canonicalActionName(match[1]))
		if !actionName.Valid() || actionName == action.ActionBatch {
			continue
		}

		step := semanticStepExpectation{Action: actionName}
		argsText := match[2]
		if table := parseStrictTableArg(argsText); table != "" {
			step.Table = table
		}
		if key, ok := parseStrictKeyArg(argsText); ok {
			step.Key = &key
		}
		steps = append(steps, step)
	}

	if len(steps) < 2 {
		return nil
	}
	return &semanticBatchExpectation{Steps: steps}
}

func containsStrictSequencingMarker(lower string) bool {
	markers := []string{
		"按顺序",
		"仅执行",
		"只执行",
		"严格",
		"禁止",
		"不允许任何额外动作",
		"strict",
		"only",
		"in order",
		"forbid",
		"do not",
	}

	for _, marker := range markers {
		if strings.Contains(lower, marker) {
			return true
		}
	}
	return false
}

func parseStrictTableArg(argsText string) string {
	match := strictTableArgPattern.FindStringSubmatch(argsText)
	if len(match) < 2 {
		return ""
	}
	return strings.TrimSpace(match[1])
}

func parseStrictKeyArg(argsText string) (int64, bool) {
	match := strictKeyArgPattern.FindStringSubmatch(argsText)
	if len(match) < 2 {
		return 0, false
	}
	key, err := strconv.ParseInt(strings.TrimSpace(match[1]), 10, 64)
	if err != nil {
		return 0, false
	}
	return key, true
}

func parseInt64Value(value any) (int64, bool) {
	switch typed := value.(type) {
	case int:
		return int64(typed), true
	case int32:
		return int64(typed), true
	case int64:
		return typed, true
	case float64:
		parsed := int64(typed)
		if float64(parsed) == typed {
			return parsed, true
		}
		return 0, false
	case json.Number:
		if parsed, err := typed.Int64(); err == nil {
			return parsed, true
		}
		floatParsed, err := typed.Float64()
		if err != nil {
			return 0, false
		}
		parsed := int64(floatParsed)
		if float64(parsed) == floatParsed {
			return parsed, true
		}
		return 0, false
	case string:
		parsed, err := strconv.ParseInt(strings.TrimSpace(typed), 10, 64)
		if err != nil {
			return 0, false
		}
		return parsed, true
	default:
		return 0, false
	}
}
func (s *ActionService) withRequestTimeout(ctx context.Context) (context.Context, context.CancelFunc) {
	if s.requestTimeout <= 0 {
		return ctx, func() {}
	}
	return context.WithTimeout(ctx, s.requestTimeout)
}

func (s *ActionService) validateCandidateEnvelope(candidate []byte) (map[string]any, json.RawMessage, error) {
	env, err := action.Decode(candidate)
	if err != nil {
		return nil, nil, err
	}

	if !s.allowWrite && envelopeContainsWriteAction(env) {
		return nil, nil, &action.Error{
			Code:    action.CodeInvalidRequest,
			Message: "write actions are disabled by server configuration",
		}
	}

	if _, err := action.ValidateEnvelope(env, s.db); err != nil {
		return nil, nil, err
	}

	var candidateMap map[string]any
	if err := json.Unmarshal(candidate, &candidateMap); err != nil {
		return nil, nil, fmt.Errorf("decode candidate envelope: %w", err)
	}

	return candidateMap, append(json.RawMessage(nil), candidate...), nil
}

func (s *ActionService) loadCatalogSnapshot(ctx context.Context) (nl.CatalogSnapshot, error) {
	tables, err := s.db.ListTables()
	if err != nil {
		return nl.CatalogSnapshot{}, err
	}

	snapshot := nl.CatalogSnapshot{
		Tables: make([]nl.TableInfo, 0, len(tables)),
	}

	for _, tableName := range tables {
		if err := ctx.Err(); err != nil {
			return nl.CatalogSnapshot{}, err
		}

		columns, err := s.db.ListTableColumns(tableName)
		if err != nil {
			return nl.CatalogSnapshot{}, err
		}
		indexes, err := s.db.ListTableIndexes(tableName)
		if err != nil {
			return nl.CatalogSnapshot{}, err
		}

		table := nl.TableInfo{
			Name:    tableName,
			Columns: make([]nl.ColumnInfo, 0, len(columns)),
			Indexes: append([]string(nil), indexes...),
		}

		for _, column := range columns {
			typeName, err := action.ProtocolTypeName(column.Type)
			if err != nil {
				typeName = action.TypeNameInvalid
			}
			table.Columns = append(table.Columns, nl.ColumnInfo{
				Name:     column.Name,
				Type:     string(typeName),
				Length:   column.Length,
				Nullable: column.Nullable,
			})
		}
		snapshot.Tables = append(snapshot.Tables, table)
	}

	return snapshot, nil
}

func envelopeContainsWriteAction(env action.RequestEnvelope) bool {
	if env.Action.IsWrite() {
		return true
	}
	if string(env.Action) != "batch" {
		return false
	}

	var batch struct {
		Requests []struct {
			Action action.Action `json:"action"`
		} `json:"requests"`
	}
	if err := json.Unmarshal(env.Args, &batch); err != nil {
		return false
	}

	for _, request := range batch.Requests {
		if request.Action.IsWrite() {
			return true
		}
	}
	return false
}

func marshalFailureResponse(requestID string, actionName action.Action, err error) ([]byte, error) {
	code := action.CodeInternal
	message := "unknown error"

	var typed *action.Error
	if errors.As(err, &typed) {
		code = typed.Code
		message = typed.Message
	} else if err != nil {
		message = err.Error()
	}

	resp := action.ResponseEnvelope[map[string]any]{
		Ok:        false,
		RequestID: requestID,
		Action:    actionName,
		Data:      nil,
		Error: &action.ResponseError{
			Code:    code,
			Message: message,
		},
		Meta: map[string]any{},
	}
	return json.Marshal(resp)
}

func mergeMeta(target map[string]any, incoming map[string]any) {
	for key, value := range incoming {
		if _, exists := target[key]; exists {
			continue
		}
		target[key] = value
	}
}

func (s *ActionService) logf(format string, args ...any) {
	if s.logger == nil {
		return
	}
	s.logger.Printf(format, args...)
}
