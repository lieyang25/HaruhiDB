package action

import (
	"errors"
	"fmt"

	"haruhidb-go/haruhidb"
)

type Code string

const (
	CodeInvalidRequest  Code = "INVALID_REQUEST"
	CodeInvalidArgument Code = "INVALID_ARGUMENT"
	CodeInvalidHandle   Code = "INVALID_HANDLE"
	CodeNotFound        Code = "NOT_FOUND"
	CodeAlreadyExists   Code = "ALREADY_EXISTS"
	CodeUnsupported     Code = "UNSUPPORTED"
	CodeConstraint      Code = "CONSTRAINT"
	CodeIO              Code = "IO"
	CodeInternal        Code = "INTERNAL"
)

type Error struct {
	Code    Code
	Message string
	Err     error
}

func (e *Error) Error() string {
	if e == nil {
		return "<nil>"
	}
	if e.Message != "" {
		return fmt.Sprintf("%s: %s", e.Code, e.Message)
	}
	return string(e.Code)
}

func (e *Error) Unwrap() error {
	if e == nil {
		return nil
	}
	return e.Err
}

func errorf(code Code, format string, args ...any) *Error {
	return &Error{
		Code:    code,
		Message: fmt.Sprintf(format, args...),
	}
}

func wrapError(code Code, message string, err error) *Error {
	return &Error{
		Code:    code,
		Message: message,
		Err:     err,
	}
}

func mapCatalogError(err error) *Error {
	if err == nil {
		return nil
	}

	var typed *haruhidb.Error
	if errors.As(err, &typed) {
		switch typed.Code {
		case haruhidb.ErrorInvalidArgument:
			return wrapError(CodeInvalidArgument, typed.Message, err)
		case haruhidb.ErrorInvalidHandle:
			return wrapError(CodeInvalidHandle, typed.Message, err)
		case haruhidb.ErrorNotFound:
			return wrapError(CodeNotFound, typed.Message, err)
		case haruhidb.ErrorAlreadyExists:
			return wrapError(CodeAlreadyExists, typed.Message, err)
		case haruhidb.ErrorUnsupported:
			return wrapError(CodeUnsupported, typed.Message, err)
		case haruhidb.ErrorConstraint:
			return wrapError(CodeConstraint, typed.Message, err)
		case haruhidb.ErrorIO:
			return wrapError(CodeIO, typed.Message, err)
		case haruhidb.ErrorInternal, haruhidb.ErrorUnknown:
			return wrapError(CodeInternal, typed.Message, err)
		default:
			return wrapError(CodeInternal, typed.Message, err)
		}
	}

	return wrapError(CodeInternal, err.Error(), err)
}

func asError(err error, target **Error) bool {
	return errors.As(err, target)
}
