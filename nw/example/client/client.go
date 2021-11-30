package main

import (
	"time"

	"github.com/iegad/xq/log"
	"github.com/iegad/xq/nw/client"
	"github.com/iegad/xq/nw/client/tcp"
	"github.com/iegad/xq/utils"
)

const (
	NTIME = 100000
)

func main() {
	log.Init()
	src := "Hello world"
	data := []byte(src)

	client, err := tcp.NewClient(&client.Option{
		Async:   true,
		Timeout: 28,
		Host:    "127.0.0.1:9090",
	})

	if err != nil {
		log.Fatal(err)
	}

	log.Debug("%s has connected", client.RemoteAddr().String())

	beg := time.Now()
	for i := 0; i < NTIME; i++ {
		if i != int(client.RecvSeq()) || i != int(client.SendSeq()) {
			log.Error("序号错误")
		}

		err = client.Write(data)
		if err != nil {
			log.Error(err)
			break
		}

		rbuf, err := client.Read()
		if err != nil {
			log.Error(err)
			break
		}

		if utils.Bytes2String(rbuf) != src {
			log.Error("数据传输异常")
			break
		}
	}

	for client.SendSeq() < NTIME {
	}

	log.Debug("传输%d字节， %d次， 总共耗时: %v", len(data), NTIME, time.Since(beg))

	client.Close()
	log.Release()
}
