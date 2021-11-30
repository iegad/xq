package tcp

import (
	"net"
	"sync"
	"time"

	"github.com/iegad/xq/log"
	"github.com/iegad/xq/nw"
	"github.com/iegad/xq/nw/io"
	"github.com/iegad/xq/nw/server"
)

type conn struct {
	recvSeq  uint32       // 接收序列
	sendSeq  uint32       // 发送序列
	server   *Server      // 所属服务
	conn     *net.TCPConn // 连接对象
	userData interface{}  // 用户数据
	wch      chan []byte  // 异步发送管道
	done     chan bool    // 停止管道
	dmtx     sync.Mutex
}

func newConn(server *Server) *conn {
	this_ := &conn{
		server: server,
		wch:    make(chan []byte, nw.DEFAULT_CHAN_SIZE),
		done:   make(chan bool, 1),
	}

	go this_._handleWrite()
	return this_
}

func (this_ *conn) RemoteAddr() net.Addr {
	if this_.conn == nil {
		return nil
	}

	return this_.conn.RemoteAddr()
}

func (this_ *conn) LocalAddr() net.Addr {
	if this_.conn == nil {
		return nil
	}

	return this_.conn.LocalAddr()
}

func (this_ *conn) RecvSeq() uint32 {
	return this_.recvSeq
}

func (this_ *conn) SendSeq() uint32 {
	return this_.sendSeq
}

func (this_ *conn) SetUserData(userData interface{}) {
	this_.userData = userData
}

func (this_ *conn) GetUserData() interface{} {
	return this_.userData
}

func (this_ *conn) Set(conn interface{}) {
	this_.conn = conn.(*net.TCPConn)
	if this_.conn == nil {
		log.Fatal("tcp.conn Set params: conn is invalid")
	}

	err := this_.conn.SetReadBuffer(io.DEFAULT_MAX_RBUF)
	if err != nil {
		log.Fatal(err)
	}

	err = this_.conn.SetWriteBuffer(io.DEFAULT_MAX_WBUF)
	if err != nil {
		log.Fatal(err)
	}
}

func (this_ *conn) Reset() {
	this_.dmtx.Lock()
	if this_.conn != nil {
		this_.conn.Close()
	}
	this_.dmtx.Unlock()
}

func (this_ *conn) Close() {
	this_.done <- true
}

func (this_ *conn) Write(data []byte, sync ...bool) (err error) {
	defer func() {
		if ex := recover(); ex != nil {
			err = ex.(error)
		}
	}()

	if this_.server.encodeHandler != nil {
		data, err = this_.server.encodeHandler(this_, data)
		if err != nil {
			return
		}
	}

	if len(sync) > 0 && sync[0] {
		err = io.Writen(this_.conn, data)
		if err != nil {
			return
		}

		this_.sendSeq++
		return
	}

	this_.wch <- data
	return
}

func (this_ *conn) read(timeout ...time.Duration) ([]byte, error) {
	if len(timeout) > 0 && timeout[0] > 0 {
		err := this_.conn.SetReadDeadline(time.Now().Add(timeout[0]))
		if err != nil {
			return nil, err
		}
	}

	data, err := io.Readn(this_.conn)
	if err != nil {
		return nil, err
	}

	if this_.server.decodeHandler != nil {
		data, err = this_.server.decodeHandler(this_, data)
		if err != nil {
			return nil, err
		}
	}

	this_.recvSeq++
	return data, nil
}

func (this_ *conn) _handleWrite() {
	var (
		err  error
		data []byte
	)

	for {
		select {
		case data = <-this_.wch:
			err = io.Writen(this_.conn, data)
			if err != nil {
				if this_.server.errorHandler != nil {
					this_.server.errorHandler(server.ET_CONN, this_, err)
				}
				continue
			}
			this_.sendSeq++

		case <-this_.done:
			this_.dmtx.Lock()
			if this_.conn != nil {
				this_.conn.Close()
			}
			this_.dmtx.Unlock()
			return
		}
	}
}
