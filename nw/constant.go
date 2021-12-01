package nw

import "errors"

const (
	PROTOCOL_TCP = "tcp"
	PROTOCOL_KCP = "kcp"
	PROTOCOL_WS  = "ws"

	DEFAULT_CHAN_SIZE = 100
	DEFAULT_MAX_CONN  = 100
)

var (
	ErrProtoc = errors.New("protocol is not support")
)
