package main

import (
	"flag"
	"sync"
	"sync/atomic"
	"time"

	"github.com/iegad/xq/ex"
	"github.com/iegad/xq/log"
	"github.com/iegad/xq/nw/client"
	"github.com/iegad/xq/utils"
)

var (
	host     = ""
	protocol = ""
	nconn    = 0
	ntimes   = 0
	content  = ""
	wg       = sync.WaitGroup{}
	nrecv    = uint32(0)
)

func _worker(data []byte) {
	defer wg.Done()

	client, err := ex.NewClient(protocol, &client.Option{
		Async:   true,
		Timeout: 28,
		Host:    host,
	})

	if err != nil {
		log.Fatal(err)
	}

	for i := 0; i < ntimes; i++ {
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

		if utils.Bytes2String(rbuf) != content {
			log.Error("数据传输异常")
			break
		}

		atomic.AddUint32(&nrecv, 1)
	}

	for client.SendSeq() < uint32(ntimes) {
	}

	client.Close()
}

func main() {
	flag.StringVar(&host, "h", "127.0.0.1:9090", "host")
	flag.StringVar(&protocol, "p", "tcp", "protocol")
	flag.IntVar(&nconn, "n", 10, "number of connection")
	flag.IntVar(&ntimes, "t", 10000, "number of translate times")
	flag.StringVar(&content, "c", "Hello world", "content")

	flag.Parse()

	data := []byte(content)

	wg.Add(nconn)
	beg := time.Now()
	for i := 0; i < nconn; i++ {
		go _worker(data)
	}

	wg.Wait()
	log.Debug("传输 %d字节， %d 个客户端, 每个传输 %d次， 总共传输%d, 总共耗时: %v", len(data), nconn, ntimes, nrecv, time.Since(beg))
}
