package app

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"log"
	"strings"
	"time"

	"haruhidb-go/haruhidb"
	"haruhidb-go/internal/action"
	"haruhidb-go/internal/nl"
)

const (
	ErrorCodeTranslation = "TRANSLATION_ERROR"
)

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
