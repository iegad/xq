package nw

import "errors"

const (
	PROTOCOL_TCP = "tcp"
	PROTOCOL_KCP = "kcp"
	PROTOCOL_WS  = "ws"

	DEFAULT_CHAN_SIZE = 100 // 默认管道长度
	DEFAULT_MAX_CONN  = 100 // 默认最大连接长度
)

var (
	ErrProtoc = errors.New("protocol is not support") // 不支持的传输协议
)
