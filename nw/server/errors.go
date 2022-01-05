package server

import "errors"

var (
	// 服务端参数错误
	ErrProcNil    = errors.New("server.processor is nil") // 消息处理机为空, 需要传递正确的IProcessor实现
	ErrOptNil     = errors.New("server.option is nil")    // 选项为空, 需要传递正确的server.Option对象
	ErrOptHost    = errors.New("server.option.host is invalid")
	ErrOptMaxConn = errors.New("server.option.maxConn is invalid")
	ErrOptTimeout = errors.New("server.option.timeout is invalid")

	// websocket 错误
	ErrMsgType = errors.New("websocket.read message type is invalid")

	// 会话错误
	ErrConnNil = errors.New("net.Conn is nil")
)
