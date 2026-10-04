package chronon3d

/*
#include <chronon/abi.h>
*/
import "C"

import (
	"errors"
	"fmt"
)

// Status mirrors chronon_status.
type Status int

const (
	StatusOK                Status = 0
	StatusUnknown           Status = 1
	StatusInvalidArgument   Status = 2
	StatusParseFailed       Status = 3
	StatusRenderFailed      Status = 4
	StatusIOFailed          Status = 5
	StatusUnsupported       Status = 6
	StatusCancelled         Status = 7
	StatusABIMismatch       Status = 8
	StatusBufferTooSmall    Status = 9
	StatusBusy              Status = 10
	StatusAssetChanged      Status = 11
	StatusBudgetExceeded    Status = 12
	StatusPreflightFailed   Status = 13
	StatusInvalidPlan       Status = 14
	StatusUnsupportedSchema Status = 15
	StatusAssetNotFound     Status = 16
	StatusDecodeFailed      Status = 17
	StatusEncodeFailed      Status = 18
	StatusOutOfMemory       Status = 19
)

// String returns the canonical C name (e.g. "INVALID_PLAN").
func (s Status) String() string { return C.GoString(C.chronon_status_name(C.chronon_status(s))) }

// Sentinel errors usable with errors.Is.
var (
	ErrUnknown           = &Error{Status: StatusUnknown}
	ErrInvalidArgument   = &Error{Status: StatusInvalidArgument}
	ErrParseFailed       = &Error{Status: StatusParseFailed}
	ErrRenderFailed      = &Error{Status: StatusRenderFailed}
	ErrIOFailed          = &Error{Status: StatusIOFailed}
	ErrUnsupported       = &Error{Status: StatusUnsupported}
	ErrCancelled         = &Error{Status: StatusCancelled}
	ErrABIMismatch       = &Error{Status: StatusABIMismatch}
	ErrBufferTooSmall    = &Error{Status: StatusBufferTooSmall}
	ErrBusy              = &Error{Status: StatusBusy}
	ErrAssetChanged      = &Error{Status: StatusAssetChanged}
	ErrBudgetExceeded    = &Error{Status: StatusBudgetExceeded}
	ErrPreflightFailed   = &Error{Status: StatusPreflightFailed}
	ErrInvalidPlan       = &Error{Status: StatusInvalidPlan}
	ErrUnsupportedSchema = &Error{Status: StatusUnsupportedSchema}
	ErrAssetNotFound     = &Error{Status: StatusAssetNotFound}
	ErrDecodeFailed      = &Error{Status: StatusDecodeFailed}
	ErrEncodeFailed      = &Error{Status: StatusEncodeFailed}
	ErrOutOfMemory       = &Error{Status: StatusOutOfMemory}
)

// Error is a structured C ABI error. errors.Is(err, ErrInvalidPlan) matches by Status.
type Error struct {
	Status    Status
	Message   string
	Code      string
	Component string
	NodeID    string
	Asset     string
}

func (e *Error) Error() string {
	if e.Message == "" {
		return "chronon3d: " + e.Status.String()
	}
	return fmt.Sprintf("chronon3d: %s: %s", e.Status, e.Message)
}

// Is matches any *Error with the same Status.
func (e *Error) Is(target error) bool {
	var t *Error
	return errors.As(target, &t) && t.Status == e.Status
}

func newError(st Status, msg string) error { return &Error{Status: st, Message: msg} }

func gs(p *C.char) string {
	if p == nil {
		return ""
	}
	return C.GoString(p)
}

func errorFromInfo(st Status, info *C.chronon_error_info, fallback string) error {
	e := &Error{Status: st, Message: gs(info.message), Code: gs(info.code),
		Component: gs(info.component), NodeID: gs(info.node_id), Asset: gs(info.asset)}
	if e.Message == "" {
		e.Message = fallback
	}
	return e
}
