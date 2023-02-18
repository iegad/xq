package main

import (
	"fmt"
	"os"
	"sync"
	"sync/atomic"
	"time"

	"github.com/xtaci/kcp-go"
)

var (
	total    int32
	delay    int32
	maxDelay int64
	sumDelay int64

	HOST = "192.168.0.101:6688"
	NCONN int = 400
	NTIME int = 100000
)

func displayDelay() {
	for {
		time.Sleep(time.Minute)
		t := atomic.LoadInt32(&total)
		atomic.StoreInt32(&total, 0)

		d := atomic.LoadInt32(&delay)
		atomic.StoreInt32(&delay, 0)

		m := atomic.LoadInt64(&maxDelay)
		atomic.StoreInt64(&maxDelay, 0)

		s := atomic.LoadInt64(&sumDelay)
		atomic.StoreInt64(&sumDelay, 0)

		avg := int64(0)
		if d > 0 {
			avg = s / int64(t)
		}

		fmt.Printf("[%v] ------------- 接发包数: %d, 延迟超过240ms的包数: %d, 最大延迟: %d ms, 平均延迟: %d ms, 延迟率: %.4f %% -------------\n",
			time.Now().Format("2006-01-02 15:04:06"), t, d, m, avg, float64(d)/float64(t)*100)
	}
}

func client(wg *sync.WaitGroup, i uint32) {
	defer wg.Done()

	conv := i + 1
	c, err := kcp.DialWithOptions(HOST, nil, 0, 0, conv)
	if err != nil {
		fmt.Printf("DialWithOptions failed: %v\n", err)
		os.Exit(1)
	}
	defer c.Close()

	c.SetNoDelay(1, 5, 3, 0)
	c.SetWindowSize(1024, 1024)
	c.SetMtu(1418)

	for i := 0; i < NTIME; i++ {
		time.Sleep(time.Millisecond * 20)
		beg := time.Now()
		data := fmt.Sprintf("Hello world: %v", i)

		_, err = c.Write([]byte(data))
		if err != nil {
			fmt.Printf("Write failed: %v\n", err)
			break
		}

		buf := make([]byte, 8*1500)
		n, err := c.Read(buf)
		if err != nil {
			fmt.Printf("%d ntime: %d, error: %v\n", conv, i, err)
			os.Exit(1)
		}

		if string(buf[:n]) != data {
			fmt.Printf("%d error: RAW[%s] <> RECV[%s]\n", conv, data, string(buf[:n]))
			os.Exit(1)
		}

		end := time.Since(beg)
		dl := end.Milliseconds()
		if dl > 240 {
			atomic.AddInt32(&delay, 1)
			if atomic.LoadInt64(&maxDelay) < dl {
				atomic.StoreInt64(&maxDelay, dl)
			}
		}
		atomic.AddInt64(&sumDelay, dl)
		atomic.AddInt32(&total, 1)
	}
}

func main() {
	wg := sync.WaitGroup{}
	wg.Add(NCONN)
	beg := time.Now()
	go displayDelay()
	for i := uint32(0); i < uint32(NCONN); i++ {
		go client(&wg, i)
	}
	wg.Wait()
	fmt.Printf("Done[%d]... => %v\n", total, time.Since(beg))
}
