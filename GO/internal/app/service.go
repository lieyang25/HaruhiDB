package app

import (
	"context"
	"encoding/json"
	"errors"
	"log"
	"time"

	"haruhidb-go/haruhidb"
	"haruhidb-go/internal/action"
)

type Config struct {
	DB             *haruhidb.DB
	AllowWrite     bool
	RequestTimeout time.Duration
	Logger         *log.Logger
}

type ActionService struct {
	db             *haruhidb.DB
	allowWrite     bool
	requestTimeout time.Duration
	logger         *log.Logger
}

func NewActionService(cfg Config) (*ActionService, error) {
	if cfg.DB == nil {
		return nil, errors.New("db must not be nil")
	}

	return &ActionService{
		db:             cfg.DB,
		allowWrite:     cfg.AllowWrite,
		requestTimeout: cfg.RequestTimeout,
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

func (s *ActionService) withRequestTimeout(ctx context.Context) (context.Context, context.CancelFunc) {
	if s.requestTimeout <= 0 {
		return ctx, func() {}
	}
	return context.WithTimeout(ctx, s.requestTimeout)
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

func (s *ActionService) logf(format string, args ...any) {
	if s.logger == nil {
		return
	}
	s.logger.Printf(format, args...)
}
