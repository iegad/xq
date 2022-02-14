package io

import (
	"encoding/binary"
	"errors"
	"io"
	"math"
	"net"

	"github.com/iegad/xq/utils"
)

const (
	HEAD_SIZE        = 4             // 消息头长度
	PACK_MAX_SIZE    = math.MaxInt32 // 包最大长度
	DEFAULT_MAX_RBUF = 1024 * 1024   // 默认读缓冲区, 1M(TCP专用)
	DEFAULT_MAX_WBUF = 1024 * 512    // 默认写缓冲区, 512K(TCP专用)
	_V_HEAD_KEY      = 0xFAFBFCFD    // 默认消息头密钥
)

var (
	ErrReadHeadSize  = errors.New("io.Readn read head size failed") // 读取消息头长度时发生错误
	ErrReadDataSize  = errors.New("io.Readn invalid data size")     // 读取消息体长度时发生错误
	ErrReadData      = errors.New("io.Readn read data failed")      // 读取消息体时发生错误
	ErrWriteDataSize = errors.New("io.Writen invalid data size")    // 写入消息头长度时发生错误
	ErrWriteData     = errors.New("io.Writen write data failed")    // 写入消息时发生错误

	_HeadKey = utils.Uint32ToB(_V_HEAD_KEY) // 消息密钥
)

// Readn 读取消息
//  @params:
//    c: 客户端连接对象
//  @returns:
//    成功返回从客户端连接对象读取的数据, 否则返回相应错误
func Readn(c net.Conn) ([]byte, error) {
	var (
		n   int
		err error
	)

	// step 1: 读取消息头
	hbuf := make([]byte, HEAD_SIZE)

	for {
		n, err = io.ReadAtLeast(c, hbuf, HEAD_SIZE)
		if err != nil {
			if nerr, ok := err.(net.Error); ok && nerr.Temporary() && !nerr.Timeout() {
				continue
			}

			return nil, err
		}
		break
	}

	if n != HEAD_SIZE {
		return nil, ErrReadHeadSize
	}

	// step 2: 解析消息头, 消息头只有一个值, 即消息体长度
	tmp := binary.BigEndian.Uint32(hbuf)
	hlen := int(^(tmp ^ _HeadKey))

	if hlen < 0 {
		return nil, ErrReadDataSize
	}

	if hlen == 0 {
		return []byte{}, nil
	}

	if hlen > PACK_MAX_SIZE {
		return nil, ErrReadDataSize
	}

	// step 3: 读取消息体部分
	data := make([]byte, hlen)

	for {
		n, err = io.ReadAtLeast(c, data, hlen)
		if err != nil {
			if nerr, ok := err.(net.Error); ok && nerr.Temporary() && !nerr.Timeout() {
				continue
			}

			return nil, err
		}
		break
	}

	if n != hlen {
		return nil, ErrReadData
	}

	// step 4: 只将消息体作为返回值
	return data, nil
}

// Writen 发送消息
//  @params:
//    c:    客户端连接对象(消息的接收端)
//    data: 需要发送的消息
//  @returns:
//    成功发送返回nil, 否则返回相应错误
func Writen(c net.Conn, data []byte) error {
	if data == nil {
		return nil
	}

	hlen := len(data)

	if hlen > PACK_MAX_SIZE {
		return ErrWriteDataSize
	}

	buflen := HEAD_SIZE + hlen
	buf := make([]byte, buflen)

	tmp := ^(uint32(hlen) ^ _HeadKey)
	binary.BigEndian.PutUint32(buf[:HEAD_SIZE], tmp)

	copy(buf[HEAD_SIZE:], data)

	var (
		n   int
		err error
	)

	for {
		n, err = c.Write(buf)
		if err != nil {
			if nerr, ok := err.(net.Error); ok && nerr.Temporary() && !nerr.Timeout() {
				continue
			}

			return err
		}
		break
	}

	if n != buflen {
		return ErrWriteData
	}

	return nil
}
