package kcp

import (
	"io"
	"net"
	"reflect"
	"sync"
	"sync/atomic"
	"time"

	"github.com/iegad/xq/nw"
	"github.com/iegad/xq/nw/server"
	"github.com/iegad/xq/utils"
	"github.com/xtaci/kcp-go/v5"
)

// Server tcp 服务端
type Server struct {
	state     int32             // 当前状态
	maxConn   int32             // 最大连接数
	timeout   time.Duration     // 超时, 会话读超时
	listener  *kcp.Listener     // 监听对象
	processor server.IProcessor // 处理机

	connectedHandler    server.ConnectedHandler
	disconnectedHandler server.DisconnectedHandler
	prevRunHandler      server.PrevRunHandler
	postRunHandler      server.PostRunHandler
	prevStopHandler     server.PrevStopHandler
	postStopHandler     server.PostStopHandler
	errorHandler        server.ErrorHandler
	encodeHandler       server.EncodeHandler
	decodeHandler       server.DecodeHandler

	wg sync.WaitGroup // 协程控制
}

// NewServer tcp.Server构造函数
func NewServer(processor server.IProcessor, option *server.Option) (server.IServer, error) {

	// step 1: 入参检查
	if reflect.ValueOf(processor).IsNil() {
		return nil, server.ErrProcNil
	}

	if option == nil {
		return nil, server.ErrOptNil
	}

	if len(option.Host) == 0 {
		return nil, server.ErrOptHost
	}

	if option.MaxConn < 0 {
		return nil, server.ErrOptMaxConn
	}

	if option.Timeout < 0 {
		return nil, server.ErrOptTimeout
	}

	if option.MaxConn == 0 {
		option.MaxConn = nw.DEFAULT_MAX_CONN
	}

	// step 2: 构建监听对象
	listener, err := kcp.ListenWithOptions(option.Host, nil, 0, 0)
	if err != nil {
		return nil, err
	}

	// step 3: 构建服务对象
	return &Server{
		maxConn:   option.MaxConn,
		timeout:   time.Duration(option.Timeout) * time.Second,
		listener:  listener,
		processor: processor,
		state:     server.ST_INITED,
	}, nil
}

/* --------------------------------- 属性 --------------------------------- */

func (this_ *Server) Host() net.Addr {
	if this_.listener == nil {
		return nil
	}

	return this_.listener.Addr()
}

func (this_ *Server) MaxConn() int32 {
	return this_.maxConn
}

func (this_ *Server) State() server.StateType {
	return server.StateType(atomic.LoadInt32(&this_.state))
}

/* --------------------------------- 事件 --------------------------------- */

func (this_ *Server) SetConnectedEvent(handler server.ConnectedHandler) {
	this_.connectedHandler = handler
}

func (this_ *Server) SetDisconnectedEvent(handler server.DisconnectedHandler) {
	this_.disconnectedHandler = handler
}

func (this_ *Server) SetPrevRunEvent(handler server.PrevRunHandler) {
	this_.prevRunHandler = handler
}

func (this_ *Server) SetPostRunEvent(handler server.PostRunHandler) {
	this_.postRunHandler = handler
}

func (this_ *Server) SetPrevStopEvent(handler server.PrevStopHandler) {
	this_.prevStopHandler = handler
}

func (this_ *Server) SetPostStopEvent(handler server.PostStopHandler) {
	this_.postStopHandler = handler
}

func (this_ *Server) SetErrorEvent(handler server.ErrorHandler) {
	this_.errorHandler = handler
}

func (this_ *Server) SetEncodeEvent(handler server.EncodeHandler) {
	this_.encodeHandler = handler
}

func (this_ *Server) SetDecodeEvent(handler server.DecodeHandler) {
	this_.decodeHandler = handler
}

/* --------------------------------- 方法 --------------------------------- */

// Run 运行服务
func (this_ *Server) Run() error {
	this_.wg.Add(int(this_.maxConn))

	// step 1: 触发前置运行事件
	if this_.prevRunHandler != nil {
		err := this_.prevRunHandler(this_)
		if err != nil {
			return err
		}
	}

	// step 2: 开启工作协程
	for i := int32(0); i < this_.maxConn; i++ {
		go this_._run(this_.listener)
	}

	// step 3: 设置运行状态
	atomic.StoreInt32(&this_.state, int32(server.ST_RUNNING))

	// step 4: 触发后置运行事件
	if this_.postRunHandler != nil {
		this_.postRunHandler(this_)
	}

	// step 5: 挂起, 直到工作协程正常退出
	this_.wg.Wait()
	return nil
}

// Stop 停止服务
func (this_ *Server) Stop() {

	// step 1: 触发前置停止事件
	if this_.prevStopHandler != nil {
		this_.prevStopHandler(this_)
	}

	// step 2: 停止服务
	this_.wg.Add(1)
	if this_.listener != nil {
		atomic.StoreInt32(&this_.state, int32(server.ST_CLOSE))
		this_.listener.Close()
	}

	// step 3: 触发后置停止事件
	if this_.postStopHandler != nil {
		this_.postStopHandler(this_)
	}

	this_.wg.Done()
}

// handleConn 会话处理
func (this_ *Server) handleConn(c *conn) {
	var (
		err  error
		data []byte
	)

	for {
		if c.conn == nil {
			break
		}

		data, err = c.read(this_.timeout)
		if err != nil {
			if err != io.EOF && atomic.LoadInt32(&this_.state) != server.ST_CLOSE {
				if this_.errorHandler != nil {
					this_.errorHandler(server.ET_CONN, c, err)
				}
			}
			break
		}

		if data == nil {
			break
		}

		err = this_.processor.OnProcess(c, data)
		if err != nil {
			break
		}
	}
}

// _run 工作协程
func (this_ *Server) _run(l *kcp.Listener) {
	var (
		grid = utils.GetGoroutineID()
		c    = newConn(this_)
		conn *kcp.UDPSession
		err  error
	)

	for {
		// step 1: 接收连接对象
		conn, err = l.AcceptKCP()
		if err != nil {
			if nerr, ok := err.(net.Error); ok && nerr.Temporary() {
				continue
			}

			if atomic.LoadInt32(&this_.state) != int32(server.ST_CLOSE) {
				if this_.errorHandler != nil {
					this_.errorHandler(server.ET_SERVER, this_, err)
				}
			}

			// 当出现错误时, 退出工作协程
			// 只有当listener 出现异常或服务被关闭时才会退出工作协程
			break
		}

		if conn == nil {
			continue
		}

		// step 2: 设置会话
		c.Set(conn)

		if this_.connectedHandler != nil {
			err = this_.connectedHandler(c, grid)
			if err != nil {
				c.Reset()
				continue
			}
		}

		// step 3: 处理会话
		this_.handleConn(c)

		if this_.disconnectedHandler != nil {
			this_.disconnectedHandler(c, grid)
		}

		// step 4: 当会话结束时, 重置会话
		c.Reset()
	}

	this_.wg.Done()
}
