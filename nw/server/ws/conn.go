package ws

import (
	"net"
	"sync"
	"time"

	"github.com/gorilla/websocket"
	"github.com/iegad/xq/nw"
	"github.com/iegad/xq/nw/server"
)

type conn struct {
	recvSeq  uint32          // 接收序列
	sendSeq  uint32          // 发送序列
	server   *Server         // 所属服务
	conn     *websocket.Conn // 连接对象
	cMtx     *sync.Mutex     // 连接对象操作锁
	userData interface{}     // 用户数据
	wch      chan []byte     // 异步发送管道
	done     chan bool       // 停止管道
}

// newConn conn构造函数
func newConn(server *Server) *conn {
	this_ := &conn{
		server: server,
		cMtx:   &sync.Mutex{},
		wch:    make(chan []byte, nw.DEFAULT_CHAN_SIZE),
		done:   make(chan bool, 1),
	}

	go this_._handleWrite()
	return this_
}

/* --------------------------------- 属性 --------------------------------- */

// RemoteAddr 远端地址
func (this_ *conn) RemoteAddr() net.Addr {
	if this_.conn == nil {
		return nil
	}

	return this_.conn.RemoteAddr()
}

// LocalAddr 本端地址
func (this_ *conn) LocalAddr() net.Addr {
	if this_.conn == nil {
		return nil
	}

	return this_.conn.LocalAddr()
}

// RecvSeq 接收序列
func (this_ *conn) RecvSeq() uint32 {
	return this_.recvSeq
}

// SendSeq 发送序列
func (this_ *conn) SendSeq() uint32 {
	return this_.sendSeq
}

// SetUserData 设置用户数据
func (this_ *conn) SetUserData(userData interface{}) {
	this_.userData = userData
}

// GetUserData 获取用户数据
func (this_ *conn) GetUserData() interface{} {
	return this_.userData
}

/* --------------------------------- 方法 --------------------------------- */

// Set 设置会话
func (this_ *conn) Set(conn interface{}) bool {
	return this_.newConn(conn.(*websocket.Conn))
}

// Reset 重置会话
func (this_ *conn) Reset() {
	this_.deleteConn()
}

// Close 关闭会话
//  关闭会话会关闭 写工作协程
func (this_ *conn) Close() {
	this_.done <- true
}

// Write 发送数据
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

func (this_ *conn) newConn(c *websocket.Conn) bool {
	if c == nil {
		if this_.server.errorHandler != nil {
			this_.server.errorHandler(server.ET_CONN, this_, server.ErrConnNil)
			return false
		}
	}

	this_.cMtx.Lock()
	this_.conn = c
	defer this_.cMtx.Unlock()
	return true
}

// delete 删除会话的连接对象
func (this_ *conn) deleteConn() {
	this_.cMtx.Lock()
	if this_.conn != nil {
		this_.conn.Close()
	}
	this_.cMtx.Unlock()
}

// read 读数据
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

// _handleWrite 写工作协程
func (this_ *conn) _handleWrite() {
	var (
		err  error
		data []byte
	)

	for {
		select {

		// 写管道数据处理
		case data = <-this_.wch:
			err = this_.conn.WriteMessage(websocket.BinaryMessage, data)
			if err != nil {
				if this_.server.errorHandler != nil {
					this_.server.errorHandler(server.ET_CONN, this_, err)
				}
				continue
			}
			this_.sendSeq++

		// 会话关闭处理
		case <-this_.done:
			this_.deleteConn()
			return
		}
	}
}
