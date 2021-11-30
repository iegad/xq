package ex

import (
	"context"
	"sync"
	"time"

	v3 "go.etcd.io/etcd/client/v3"
	"go.etcd.io/etcd/client/v3/concurrency"
)

type Mutex struct {
	expire time.Duration
	cli    *v3.Client
	sess   *concurrency.Session
	mtx    *concurrency.Mutex
	lk     sync.Mutex
}

func NewMutex(hosts []string, key string, expire int64) (*Mutex, error) {
	cli, err := v3.New(v3.Config{
		Endpoints:   hosts,
		DialTimeout: _TIMEOUT,
	})
	if err != nil {
		return nil, err
	}

	sess, err := concurrency.NewSession(cli)
	if err != nil {
		cli.Close()
		return nil, err
	}

	mtx := concurrency.NewMutex(sess, key)
	return &Mutex{
		cli:    cli,
		sess:   sess,
		mtx:    mtx,
		expire: time.Duration(expire) * time.Second,
	}, nil
}

func (this_ *Mutex) Close() {
	this_.lk.Lock()

	if this_.sess != nil {
		this_.sess.Close()
	}

	if this_.cli != nil {
		this_.cli.Close()
	}

	this_.lk.Unlock()
}

func (this_ *Mutex) Lock() error {
	ctx, fn := context.WithTimeout(context.TODO(), this_.expire)
	this_.lk.Lock()
	err := this_.mtx.Lock(ctx)
	this_.lk.Unlock()
	fn()
	return err
}

func (this_ *Mutex) Unlock() error {
	this_.lk.Lock()
	err := this_.mtx.Unlock(context.TODO())
	this_.lk.Unlock()
	return err
}
