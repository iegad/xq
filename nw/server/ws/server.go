package ws

import (
	"io"
	"net"
	"net/http"
	"reflect"
	"sync"
	"sync/atomic"
	"time"

	"github.com/gin-contrib/cors"
	"github.com/gin-gonic/gin"
	"github.com/gorilla/websocket"
	"github.com/iegad/xq/nw"
	"github.com/iegad/xq/nw/server"
	"github.com/iegad/xq/utils"
)

type Server struct {
	state       int32               // 当前状态
	maxConn     int32               // 最大连接数
	currentConn int32               // 当前连接数
	timeout     time.Duration       // 超时, 会话读超时
	listener    *net.TCPListener    // 监听对象
	processor   server.IProcessor   // 处理机
	uper        *websocket.Upgrader // ws升级

	connectedHandler    server.ConnectedHandler
	disconnectedHandler server.DisconnectedHandler
	prevRunHandler      server.PrevRunHandler
	postRunHandler      server.PostRunHandler
	prevStopHandler     server.PrevStopHandler
	postStopHandler     server.PostStopHandler
	errorHandler        server.ErrorHandler
	encodeHandler       server.EncodeHandler
	decodeHandler       server.DecodeHandler

	cm sync.Map
	wg sync.WaitGroup
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
	gin.SetMode(gin.ReleaseMode)
	addr, err := net.ResolveTCPAddr(nw.PROTOCOL_TCP, option.Host)
	if err != nil {
		return nil, err
	}

	listener, err := net.ListenTCP(nw.PROTOCOL_TCP, addr)
	if err != nil {
		return nil, err
	}

	return &Server{
		maxConn:   option.MaxConn,
		timeout:   time.Duration(option.Timeout) * time.Second,
		listener:  listener,
		processor: processor,
		state:     server.ST_INITED,
		uper: &websocket.Upgrader{
			HandshakeTimeout: time.Duration(option.Timeout) * time.Second,
			CheckOrigin: func(r *http.Request) bool {
				return true
			},
		},
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

func (this_ *Server) CurrentConn() int32 {
	return atomic.LoadInt32(&this_.currentConn)
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
	var err error

	router := gin.Default()
	router.Use(cors.Default())
	router.GET("/", this_.handle)

	atomic.StoreInt32(&this_.state, server.ST_RUNNING)

	if this_.prevRunHandler != nil {
		err = this_.prevRunHandler(this_)
		if err != nil {
			return err
		}
	}

	this_.wg.Add(1)
	go func() {
		err = router.RunListener(this_.listener)
		if err != nil {
			if server.StateType(atomic.LoadInt32(&this_.state)) != server.ST_CLOSE {
				if this_.errorHandler != nil {
					this_.errorHandler(server.ET_SERVER, this_, err)
				}
				this_.Stop()
			}
		}
		this_.wg.Done()
	}()

	this_.wg.Wait()
	return nil
}

// Stop 停止服务
func (this_ *Server) Stop() {
	if this_.prevStopHandler != nil {
		this_.prevStopHandler(this_)
	}

	this_.wg.Add(1)
	if this_.listener != nil {
		atomic.StoreInt32(&this_.state, server.ST_CLOSE)
		this_.listener.Close()
	}

	this_.cm.Range(func(k, v interface{}) bool {
		v.(*conn).Close()
		return true
	})

	if this_.postStopHandler != nil {
		this_.postStopHandler(this_)
	}
	this_.wg.Done()
}

// handle http升级为ws协议
func (this_ *Server) handle(ctx *gin.Context) {

	if atomic.LoadInt32(&this_.currentConn) >= this_.maxConn {
		ctx.Status(http.StatusBadGateway)
		return
	}

	atomic.AddInt32(&this_.currentConn, 1)

	wc, err := this_.uper.Upgrade(ctx.Writer, ctx.Request, nil)
	if err != nil {
		ctx.Status(http.StatusBadRequest)
		return
	}

	conn := newConn(this_)
	grid := utils.GetGoroutineID()
	conn.Set(wc)
	this_.cm.Store(grid, conn)

	if this_.connectedHandler != nil {
		err = this_.connectedHandler(conn, grid)
		if err != nil {
			return
		}
	}

	this_.handleConn(conn)

	if this_.disconnectedHandler != nil {
		this_.disconnectedHandler(conn, grid)
	}

	this_.cm.Delete(grid)
	conn.Close()

	atomic.AddInt32(&this_.currentConn, -1)
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
