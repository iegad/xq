package server

import "errors"

var (
	ErrProcNil    = errors.New("server.processor is nil")
	ErrOptNil     = errors.New("server.option is nil")
	ErrOptHost    = errors.New("server.option.host is invalid")
	ErrOptMaxConn = errors.New("server.option.maxConn is invalid")
	ErrOptTimeout = errors.New("server.option.timeout is invalid")

	ErrMsgType = errors.New("websocket.read message type is invalid")
)
