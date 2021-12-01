package ex

import (
	"github.com/iegad/xq/nw"
	"github.com/iegad/xq/nw/server"
	"github.com/iegad/xq/nw/server/tcp"
	"github.com/iegad/xq/nw/server/ws"
)

func NewServer(protocol string, processor server.IProcessor, option *server.Option) (server.IServer, error) {
	switch protocol {

	case nw.PROTOCOL_TCP:
		return tcp.NewServer(processor, option)

	case nw.PROTOCOL_WS:
		return ws.NewServer(processor, option)

	default:
		return nil, nw.ErrProtoc
	}
}
