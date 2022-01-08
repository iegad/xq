package ex

import (
	"context"
	"errors"
	"runtime"
	"time"

	"github.com/go-redis/redis/v8"
)

var (
	errRedisNil = errors.New("redis config is nil")
)

type Redis struct {
	Host string `json:"host" yaml:"host"`
	User string `json:"user" yaml:"user"`
	Pass string `json:"pass" yaml:"pass"`
}

func NewRedis(c *Redis) (*redis.Client, error) {
	if c == nil {
		return nil, errRedisNil
	}

	this_ := redis.NewClient(&redis.Options{
		Addr:     c.Host,
		Username: c.User,
		Password: c.Pass,
		PoolSize: runtime.NumCPU(),
	})

	ctx, _ := context.WithTimeout(context.TODO(), time.Second*5)
	err := this_.Ping(ctx).Err()
	if err != nil {
		this_.Close()
		return nil, err
	}

	return this_, nil
}
