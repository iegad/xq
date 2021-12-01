package ws

import (
	"fmt"
	"net"
	"sync"
	"time"

	"github.com/gorilla/websocket"
	"github.com/iegad/xq/log"
	"github.com/iegad/xq/nw"
	"github.com/iegad/xq/nw/client"
)

type Client struct {
	async   bool                 // 异步标识
	recvSeq uint32               // 接收序列
	sendSeq uint32               // 发送序列
	timeout time.Duration        // 超时
	conn    *websocket.Conn      // 连接对象
	wg      *sync.WaitGroup      // 异步协程控制
	encoder client.EncodeHandler // 编码
	decoder client.DecodeHandler // 解码
	wch     chan []byte          // 异步发送管道
}

// NewClient websocket client构造函数
func NewClient(option *client.Option) (client.IClient, error) {

	// step 1: 入参检查
	if option == nil {
		return nil, client.ErrOptNil
	}

	if len(option.Host) == 0 {
		return nil, client.ErrOptHost
	}

	if option.Timeout < 0 {
		return nil, client.ErrOptTimo
	}

	// step 2: 构建 websocket 连接对象
	conn, _, err := websocket.DefaultDialer.Dial(fmt.Sprintf("ws://%s", option.Host), nil)
	if err != nil {
		return nil, err
	}

	// step 3: 构建websocket client
	this_ := &Client{
		timeout: time.Duration(option.Timeout) * time.Second,
		conn:    conn,
	}

	// step 4: 设置异步客户端
	if option.Async {
		this_.wch = make(chan []byte, nw.DEFAULT_CHAN_SIZE)
		this_.wg = &sync.WaitGroup{}
		this_.wg.Add(1)
		go this_._handleWrite()
	}

	return this_, nil
}

func (this_ *Client) RemoteAddr() net.Addr {
	if this_.conn == nil {
		return nil
	}

	return this_.conn.RemoteAddr()
}

func (this_ *Client) LocalAddr() net.Addr {
	if this_.conn == nil {
		return nil
	}

	return this_.conn.LocalAddr()
}

func (this_ *Client) RecvSeq() uint32 {
	return this_.recvSeq
}

func (this_ *Client) SendSeq() uint32 {
	return this_.sendSeq
}

func (this_ *Client) Async() bool {
	return this_.async
}

func (this_ *Client) SetEncodeEvent(handler client.EncodeHandler) {
	this_.encoder = handler
}

func (this_ *Client) SetDecodeEvent(handler client.DecodeHandler) {
	this_.decoder = handler
}

func (this_ *Client) Write(data []byte) error {
	var err error

	if this_.encoder != nil {
		data, err = this_.encoder(this_, data)
		if err != nil {
			return err
		}
	}

	if this_.async {
		this_.wch <- data
		return nil
	}

	err = this_.conn.WriteMessage(websocket.BinaryMessage, data)
	if err != nil {
		return err
	}

	this_.sendSeq++
	return nil
}

func (this_ *Client) Read() ([]byte, error) {
	if this_.timeout > 0 {
		err := this_.conn.SetReadDeadline(time.Now().Add(this_.timeout))
		if err != nil {
			return nil, err
		}
	}

	mt, data, err := this_.conn.ReadMessage()
	if err != nil {
		return nil, err
	}

	if mt != websocket.BinaryMessage {
		return nil, client.ErrMsgType
	}

	if this_.decoder != nil {
		data, err = this_.decoder(this_, data)
	}

	this_.recvSeq++
	return data, err
}

func (this_ *Client) Close() {
	if this_.conn != nil {
		this_.conn.Close()
	}

	if this_.wch != nil {
		close(this_.wch)
	}

	if this_.wg != nil {
		this_.wg.Wait()
	}
}

func (this_ *Client) _handleWrite() {
	var (
		data []byte
		err  error
	)

	for data = range this_.wch {
		err = this_.conn.WriteMessage(websocket.BinaryMessage, data)
		if err != nil {
			log.Error(err)
			continue
		}
		this_.sendSeq++
	}

	this_.wg.Done()
}
