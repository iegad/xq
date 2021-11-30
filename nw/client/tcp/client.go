package tcp

import (
	"net"
	"sync"
	"time"

	"github.com/iegad/xq/log"
	"github.com/iegad/xq/nw"
	"github.com/iegad/xq/nw/client"
	"github.com/iegad/xq/nw/io"
)

type Client struct {
	async   bool
	recvSeq uint32
	sendSeq uint32
	conn    *net.TCPConn
	timeout time.Duration
	encoder client.EncodeHandler
	decoder client.DecodeHandler
	wch     chan []byte
	wg      *sync.WaitGroup
}

func NewClient(option *client.Option) (client.IClient, error) {
	if option == nil {
		return nil, client.ErrOptNil
	}

	if len(option.Host) == 0 {
		return nil, client.ErrOptHost
	}

	raddr, err := net.ResolveTCPAddr(nw.PROTOCOL_TCP, option.Host)
	if err != nil {
		return nil, err
	}

	conn, err := net.DialTCP(nw.PROTOCOL_TCP, nil, raddr)
	if err != nil {
		return nil, err
	}

	this_ := &Client{
		timeout: time.Duration(option.Timeout) * time.Second,
		conn:    conn,
		encoder: option.Encoder,
		decoder: option.Decoder,
	}

	if option.Async {
		this_.wch = make(chan []byte, 100)
		this_.wg = &sync.WaitGroup{}
		this_.wg.Add(1)
		go this_._handleWrite()
	}

	return this_, nil
}

func (this_ *Client) RemoteAddr() net.Addr {
	return this_.conn.RemoteAddr()
}

func (this_ *Client) LocalAddr() net.Addr {
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

func (this_ *Client) Write(data []byte, mt ...int) error {
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

	err = io.Writen(this_.conn, data)
	if err != nil {
		return err
	}

	this_.sendSeq++
	return nil
}

func (this_ *Client) Read(mt ...int) ([]byte, error) {
	if this_.timeout > 0 {
		err := this_.conn.SetReadDeadline(time.Now().Add(this_.timeout))
		if err != nil {
			return nil, err
		}
	}

	data, err := io.Readn(this_.conn)
	if err != nil {
		return nil, err
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
		err = io.Writen(this_.conn, data)
		if err != nil {
			log.Error(err)
			continue
		}
		this_.sendSeq++
	}

	this_.wg.Done()
}
