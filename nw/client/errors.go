package client

import "errors"

var (
	ErrMsgType = errors.New("message type is invalid")
	ErrOptNil  = errors.New("option is invalid")
	ErrOptHost = errors.New("option.host is invalid")
	ErrOptTimo = errors.New("option.timeout is invalid")
)
