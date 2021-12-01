package client

import "net"

type EncodeHandler func(IClient, []byte) ([]byte, error)
type DecodeHandler EncodeHandler

type IClient interface {
	RemoteAddr() net.Addr
	LocalAddr() net.Addr
	RecvSeq() uint32
	SendSeq() uint32
	Async() bool

	Write(data []byte) error
	Read() ([]byte, error)
	Close()

	SetEncodeEvent(handler EncodeHandler)
	SetDecodeEvent(handler DecodeHandler)
}
