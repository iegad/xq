package utils

import (
	"sync"
)

type ObjectPool[T any] struct {
	pool *sync.Pool
}

func NewObjectPool[T any]() *ObjectPool[T] {
	return &ObjectPool[T]{
		pool: &sync.Pool{
			New: func() any {
				return new(T)
			},
		},
	}
}

func (this_ *ObjectPool[T]) New() *T {
	return this_.pool.Get().(*T)
}

func (this_ *ObjectPool[T]) Delete(obj *T) {
	this_.pool.Put(obj)
}
