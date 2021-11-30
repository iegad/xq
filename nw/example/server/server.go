package main

import (
	"os"
	"os/signal"
	"syscall"

	"github.com/iegad/xq/log"
	"github.com/iegad/xq/nw/server"
	"github.com/iegad/xq/nw/server/tcp"
)

type Processor struct {
}

func (this_ *Processor) OnConnected(c server.IConn) error {
	log.Debug("connected event: %s has connected", c.RemoteAddr().String())
	return nil
}

func (this_ *Processor) OnDisconnect(c server.IConn) {
	log.Debug("disconnected event: %s has disconnected", c.RemoteAddr().String())
}

func (this_ *Processor) OnProcess(c server.IConn, data []byte) error {
	return c.Write(data)
}

func (this_ *Processor) OnPrevRun(svr server.IServer) error {
	log.Debug("prevRun event: %s ready to run", svr.Host())
	return nil
}

func (this_ *Processor) OnPostRun(svr server.IServer) {
	log.Debug("postRun event: %s is running", svr.Host())
}

func (this_ *Processor) OnPrevStop(svr server.IServer) {
	log.Debug("prevStop event: %s ready to stop", svr.Host())
}

func (this_ *Processor) OnPostStop(svr server.IServer) {
	log.Exit("postStop event: %s has stopped", svr.Host())
}

func (this_ *Processor) OnError(et server.ErrorType, obj interface{}, err error) error {
	log.Debug("error event: %v: %v => %v", et, obj, err)
	return nil
}

func (this_ *Processor) Encode(c server.IConn, data []byte) ([]byte, error) {
	log.Debug("encode event: %s", c.RemoteAddr().String())
	return data, nil
}

func (this_ *Processor) Decode(c server.IConn, data []byte) ([]byte, error) {
	log.Debug("decode event: %s", c.RemoteAddr().String())
	return data, nil
}

func main() {
	log.Init()

	proc := &Processor{}

	server, err := tcp.NewServer(proc, &server.Option{
		MaxConn: 100,
		Host:    ":9090",
		Timeout: 30,
	})

	if err != nil {
		log.Fatal(err)
	}

	server.SetConnectedEvent(proc.OnConnected)
	server.SetDisconnectedEvent(proc.OnDisconnect)
	server.SetPrevRunEvent(proc.OnPrevRun)
	server.SetPostRunEvent(proc.OnPostRun)
	server.SetPrevStopEvent(proc.OnPrevStop)
	server.SetPostStopEvent(proc.OnPostStop)

	done := make(chan os.Signal, 1)
	signal.Notify(done, syscall.SIGINT)

	go func() {
		<-done
		server.Stop()
	}()

	server.Run()
	log.Release()
}
