package utils

import (
	"sync"
)

type opool[T any] struct {
	pool sync.Pool
}

func NewObjectPool[T any]() *opool[T] {
	return &opool[T]{
		pool: sync.Pool{
			New: func() any {
				return new(T)
			},
		},
	}
}

func (this_ *opool[T]) Get() *T {
	return this_.pool.Get().(*T)
}

func (this_ *opool[T]) Put(obj *T) {
	this_.pool.Put(obj)
}
