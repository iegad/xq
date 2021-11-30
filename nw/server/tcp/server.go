package tcp

import (
	"io"
	"net"
	"reflect"
	"sync"
	"sync/atomic"
	"time"

	"github.com/iegad/xq/nw"
	"github.com/iegad/xq/nw/server"
)

type Server struct {
	state       int32
	maxConn     int32
	currentConn int32
	timeout     time.Duration
	listener    *net.TCPListener
	processor   server.IProcessor

	connectedHandler    server.ConnectedHandler
	disconnectedHandler server.DisconnectedHandler
	prevRunHandler      server.PrevRunHandler
	postRunHandler      server.PostRunHandler
	prevStopHandler     server.PrevStopHandler
	postStopHandler     server.PostStopHandler
	errorHandler        server.ErrorHandler
	encodeHandler       server.EncodeHandler
	decodeHandler       server.DecodeHandler

	wg sync.WaitGroup
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
		state:     int32(server.ST_INITED),
	}, nil
}

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
	this_.wg.Add(int(this_.maxConn))
	if this_.prevRunHandler != nil {
		err := this_.prevRunHandler(this_)
		if err != nil {
			return
		}
	}

	for i := int32(0); i < this_.maxConn; i++ {
		go this_._run(this_.listener)
	}

	atomic.StoreInt32(&this_.state, int32(server.ST_RUNNING))

	if this_.postRunHandler != nil {
		this_.postRunHandler(this_)
	}

	this_.wg.Wait()
}

func (this_ *Server) Stop() {
	if this_.prevStopHandler != nil {
		this_.prevStopHandler(this_)
	}

	this_.wg.Add(1)
	if this_.listener != nil {
		atomic.StoreInt32(&this_.state, int32(server.ST_CLOSE))
		this_.listener.Close()
	}

	if this_.postStopHandler != nil {
		this_.postStopHandler(this_)
	}

	this_.wg.Done()
}

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
			if err != io.EOF && atomic.LoadInt32(&this_.state) != int32(server.ST_CLOSE) {
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

func (this_ *Server) _run(l *net.TCPListener) {
	var (
		c    = newConn(this_)
		conn *net.TCPConn
		err  error
	)

	for {
		conn, err = l.AcceptTCP()
		if err != nil {
			if nerr, ok := err.(net.Error); ok && nerr.Temporary() {
				continue
			}

			if atomic.LoadInt32(&this_.state) != int32(server.ST_CLOSE) {
				if this_.errorHandler != nil {
					this_.errorHandler(server.ET_SERVER, this_, err)
				}
			}
			break
		}

		if conn == nil {
			continue
		}

		c.Set(conn)

		if this_.connectedHandler != nil {
			err = this_.connectedHandler(c)
			if err != nil {
				c.Reset()
				continue
			}
		}

		atomic.AddInt32(&this_.currentConn, 1)
		this_.handleConn(c)
		atomic.AddInt32(&this_.currentConn, -1)

		if this_.disconnectedHandler != nil {
			this_.disconnectedHandler(c)
		}

		c.Reset()
	}

	this_.wg.Done()
}
