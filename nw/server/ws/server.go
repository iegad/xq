package ws

import (
	"net"
	"net/http"
	"reflect"
	"sync"
	"sync/atomic"
	"time"

	"github.com/gin-contrib/cors"
	"github.com/gin-gonic/gin"
	"github.com/gorilla/websocket"
	"github.com/iegad/xq/log"
	"github.com/iegad/xq/nw"
	"github.com/iegad/xq/nw/server"
)

type Server struct {
	state       int32
	currentConn int32
	maxConn     int
	timeout     time.Duration

	listener  *net.TCPListener
	processor server.IProcessor
	uper      *websocket.Upgrader

	connectedHandler    server.ConnectedHandler
	disconnectedHandler server.DisconnectedHandler
	prevRunHandler      server.PrevRunHandler
	postRunHandler      server.PostRunHandler
	prevStopHandler     server.PrevStopHandler
	postStopHandler     server.PostStopHandler
	errorHandler        server.ErrorHandler
	encodeHandler       server.EncodeHandler
	decodeHandler       server.DecodeHandler

	cch chan *websocket.Conn
	wg  sync.WaitGroup
}

func NewServer(processor server.IProcessor, option *server.Option) (server.IServer, error) {
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
		state:     server.ST_INITED,
		processor: processor,
		cch:       make(chan *websocket.Conn, nw.DEFAULT_CHAN_SIZE),
	}, nil
}

func (this_ *Server) Host() string {
	return this_.listener.Addr().String()
}

func (this_ *Server) MaxConn() int {
	return this_.maxConn
}

func (this_ *Server) State() server.StateType {
	return server.StateType(atomic.LoadInt32(&this_.state))
}

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

func (this_ *Server) Run() {
	this_.uper = &websocket.Upgrader{
		HandshakeTimeout: this_.timeout,
		CheckOrigin: func(r *http.Request) bool {
			return true
		},
	}

	this_.wg.Add(this_.maxConn)

	for i := 0; i < this_.maxConn; i++ {
		go this_._run()
	}

	router := gin.Default()
	router.Use(cors.Default())
	router.GET("/", this_.handle)

	atomic.StoreInt32(&this_.state, server.ST_RUNNING)

	if this_.prevRunHandler != nil {
		err := this_.prevRunHandler(this_)
		if err != nil {
			return
		}
	}

	err := router.RunListener(this_.listener)
	if err != nil {
		if server.StateType(atomic.LoadInt32(&this_.state)) != server.ST_CLOSE {
			if this_.errorHandler != nil {
				this_.errorHandler(server.ET_SERVER, this_, err)
			}
			this_.Stop()
		}
	}

	this_.wg.Wait()
}

func (this_ *Server) Stop() {
	if this_.prevStopHandler != nil {
		this_.prevStopHandler(this_)
	}

	this_.wg.Add(1)
	if this_.listener != nil {
		atomic.StoreInt32(&this_.state, server.ST_CLOSE)
		this_.listener.Close()
		close(this_.cch)
	}

	if this_.postStopHandler != nil {
		this_.postStopHandler(this_)
	}
	this_.wg.Done()
}

func (this_ *Server) handle(c *gin.Context) {
	if int(atomic.LoadInt32(&this_.currentConn)) >= this_.maxConn {
		c.Status(http.StatusBadGateway)
		return
	}

	wc, err := this_.uper.Upgrade(c.Writer, c.Request, nil)
	if err != nil {
		c.Status(http.StatusBadRequest)
		return
	}

	this_.cch <- wc
}

func (this_ *Server) handleConn(c *conn) {
	var (
		err  error
		data []byte
	)

	atomic.AddInt32(&this_.currentConn, 1)
	for {
		if c.conn == nil {
			break
		}

		data, err = c.read(this_.timeout)
		if err != nil && server.StateType(atomic.LoadInt32(&this_.state)) != server.ST_CLOSE {
			log.Error(err)
			break
		}

		if data == nil {
			break
		}

		err = this_.processor.OnProcess(c, data)
		if err != nil {
			log.Error(err)
			break
		}
	}

	atomic.AddInt32(&this_.currentConn, -1)
}

func (this_ *Server) _run() {
	var (
		c    = newConn(this_)
		conn *websocket.Conn
		err  error
	)

	for conn = range this_.cch {
		c.Set(conn)

		if this_.connectedHandler != nil {
			err = this_.connectedHandler(c)
			if err != nil {
				continue
			}
		}

		this_.handleConn(c)

		if this_.disconnectedHandler != nil {
			this_.disconnectedHandler(c)
		}

		c.Reset()
	}
}
