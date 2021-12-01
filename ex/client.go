package ex

import (
	"github.com/iegad/xq/nw"
	"github.com/iegad/xq/nw/client"
	"github.com/iegad/xq/nw/client/kcp"
	"github.com/iegad/xq/nw/client/tcp"
	"github.com/iegad/xq/nw/client/ws"
)

func NewClient(protocol string, option *client.Option) (client.IClient, error) {
	switch protocol {
	case nw.PROTOCOL_TCP:
		return tcp.NewClient(option)

	case nw.PROTOCOL_WS:
		return ws.NewClient(option)

	case nw.PROTOCOL_KCP:
		return kcp.NewClient(option)

	default:
		return nil, nw.ErrProtoc
	}
}
