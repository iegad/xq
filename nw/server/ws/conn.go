package ws

import (
	"net"
	"time"

	"github.com/gorilla/websocket"
	"github.com/iegad/xq/log"
	"github.com/iegad/xq/nw"
	"github.com/iegad/xq/nw/server"
)

type conn struct {
	recvSeq  uint32
	sendSeq  uint32
	server   *Server
	conn     *websocket.Conn
	userData interface{}
	wch      chan []byte
}

func newConn(server *Server) *conn {
	this_ := &conn{
		server: server,
		wch:    make(chan []byte, nw.DEFAULT_CHAN_SIZE),
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
	this_.conn = conn.(*websocket.Conn)
	if this_.conn == nil {
		log.Fatal("ws.conn.Set in parameter: conn is invalid")
	}
}

func (this_ *conn) Reset() {
	if this_.conn != nil {
		this_.conn.Close()
		this_.conn = nil
	}
}

func (this_ *conn) Close() {

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
		err = this_.conn.WriteMessage(websocket.BinaryMessage, data)
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

	mtype, data, err := this_.conn.ReadMessage()
	if err != nil {
		return nil, err
	}

	if mtype != websocket.BinaryMessage {
		return nil, server.ErrMsgType
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

	for data = range this_.wch {
		err = this_.conn.WriteMessage(websocket.BinaryMessage, data)
		if err != nil {
			if this_.server.errorHandler != nil {
				this_.server.errorHandler(server.ET_CONN, this_, err)
			}
			continue
		}

		this_.sendSeq++
	}
}
