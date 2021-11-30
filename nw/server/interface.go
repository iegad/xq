package server

import (
	"net"
)

// StateType 服务状态类型
type StateType int32

// StateType 服务状态枚举值
const (
	ST_INITED  = 0 // 初始化状态
	ST_RUNNING = 1 // 运行状态
	ST_CLOSE   = 2 // 关闭状态
)

// ErrorType 错误类型
type ErrorType int

// ErrorType 错误枚举值
const (
	ET_SERVER = 1 // 服务产生的错误
	ET_CONN   = 2 // 会话产生的错误
)

// ConnectedHandler 客户端连接句柄
//  当有客户端连接成功后触发, 如果返回错误, 将主动断开客户端
type ConnectedHandler func(c IConn) error

// DisconnectedHandler 客户端断开连接句柄
//  当有客户端连接断开后触发
type DisconnectedHandler func(c IConn)

// PrevRunHandler 前置服务运行句柄
//  服务运行前触发, 如果返回错误, 服务将不会运行
type PrevRunHandler func(s IServer) error

// PostRunHandler 后置服务运行句柄
//  服务运行后触发
type PostRunHandler func(s IServer)

// PrevStopHandler 前置服务停止句柄
//  当服务停止前触发
type PrevStopHandler func(s IServer)

// PostStopHandler 后置服务停止句柄
//  当服务停止后触发
type PostStopHandler func(s IServer)

// ErrorHandler 错误句柄
//  当服务端或会话端产生错误触发
type ErrorHandler func(et ErrorType, obj interface{}, err error)

// EncodeHandler 编码句柄, 用于加密
type EncodeHandler func(c IConn, data []byte) ([]byte, error)

// DecodeHandler 解码句柄, 用于解密
type DecodeHandler func(c IConn, data []byte) ([]byte, error)

// IServer 服务接口
type IServer interface {

	/* ------------------------ 属性 ------------------------ */

	// Host 服务端地址
	Host() net.Addr

	// MaxConn 最大连接数
	MaxConn() int32

	// CurrentConn 当前连接数
	CurrentConn() int32

	// State 当前状态
	State() StateType

	/* ------------------------ 方法 ------------------------ */

	// Run 运行服务
	Run()

	// Stop 停止服务
	Stop()

	/* ------------------------ 事件 ------------------------ */
	SetConnectedEvent(handler ConnectedHandler)
	SetDisconnectedEvent(handler DisconnectedHandler)
	SetPrevRunEvent(handler PrevRunHandler)
	SetPostRunEvent(handler PostRunHandler)

	SetPrevStopEvent(handler PrevStopHandler)
	SetPostStopEvent(handler PostStopHandler)
	SetErrorEvent(handler ErrorHandler)

	SetEncodeEvent(handler EncodeHandler)
	SetDecodeEvent(handler DecodeHandler)
}

// IConn 会话接口
type IConn interface {
	Set(conn interface{})
	Reset()
	RemoteAddr() net.Addr
	LocalAddr() net.Addr
	RecvSeq() uint32
	SendSeq() uint32

	Close()
	Write(data []byte, sync ...bool) error
	SetUserData(userData interface{})
	GetUserData() interface{}
}

type IProcessor interface {
	OnProcess(c IConn, data []byte) error
}

func New(option *Option) (IServer, error) {
	// TODO
	return nil, nil
}
