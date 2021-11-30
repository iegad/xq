package client

import "net"

type IClient interface {
	RemoteAddr() net.Addr
	LocalAddr() net.Addr
	RecvSeq() uint32
	SendSeq() uint32
	Async() bool

	Write(data []byte, mt ...int) error
	Read(mt ...int) ([]byte, error)
	Close()
}
