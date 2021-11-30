package ex

import (
	"context"
	"encoding/json"
	"fmt"
	"strings"
	"sync/atomic"
	"time"

	"github.com/emirpasic/gods/trees/btree"
	"github.com/iegad/xq/log"
	"github.com/iegad/xq/utils"
	"go.etcd.io/etcd/api/v3/mvccpb"
	v3 "go.etcd.io/etcd/client/v3"
	"go.etcd.io/etcd/client/v3/naming/endpoints"
)

const (
	_TIMEOUT = time.Second * 5
	_TTL     = int64(5)

	EvPUT = int32(mvccpb.PUT)
	EvDEL = int32(mvccpb.DELETE)
)

type NodePath string

func (this_ *NodePath) String() string {
	return *(*string)(this_)
}

func (this_ *NodePath) NodeID() string {
	tmp := *(*string)(this_)
	n := strings.LastIndex(tmp, "/")
	if n < 0 {
		return ""
	}

	return tmp[n+1:]
}

func (this_ *NodePath) Path() string {
	tmp := *(*string)(this_)
	n := strings.LastIndex(tmp, "/")
	if n < 0 {
		return ""
	}

	return tmp[:n]
}

func (this_ *NodePath) Service() string {
	tmp := *(*string)(this_)
	n := strings.Index(tmp, "/")
	if n < 0 {
		return ""
	}

	return tmp[:n]
}

type Node struct {
	Op       uint8
	Metadata interface{}
	Addr     string
}

func (this_ *Node) String() string {
	data, _ := json.Marshal(this_)
	return utils.Bytes2String(data)
}

func ParseToNode(jstr []byte) (*Node, error) {
	node := &Node{}
	err := json.Unmarshal(jstr, node)
	if err != nil {
		return nil, err
	}

	return node, nil
}

type Option struct {
	Hosts []string
}

type Etcd struct {
	cli   *v3.Client
	flag  int32
	count int32
	hosts []string
}

func NewEtcd(hosts []string) (*Etcd, error) {
	this_ := &Etcd{
		hosts: hosts,
	}

	err := this_.init()
	if err != nil {
		return nil, err
	}

	return this_, nil
}

func (this_ *Etcd) init() error {
	cli, err := v3.New(v3.Config{
		Endpoints:   this_.hosts,
		DialTimeout: _TIMEOUT,
	})

	if err != nil {
		return err
	}

	this_.cli = cli
	return nil
}

func (this_ *Etcd) Close() {
	if this_.cli != nil {
		this_.cli.Close()
	}
}

func (this_ *Etcd) Put(key, val string) error {
	ctx, cancel := context.WithTimeout(context.TODO(), _TIMEOUT)
	_, err := this_.cli.Put(ctx, key, val)
	cancel()

	return err
}

func (this_ *Etcd) Get(key string) (map[string]string, error) {
	ctx, cancel := context.WithTimeout(context.TODO(), _TIMEOUT)
	resp, err := this_.cli.Get(ctx, key, v3.WithPrefix())
	cancel()

	if err != nil {
		return nil, err
	}

	result := map[string]string{}
	for _, ev := range resp.Kvs {
		result[utils.Bytes2String(ev.Key)] = utils.Bytes2String(ev.Value)
	}

	return result, nil
}

func (this_ *Etcd) GetKeys(key string) ([]string, error) {
	ctx, cancel := context.WithTimeout(context.TODO(), _TIMEOUT)
	resp, err := this_.cli.Get(ctx, key, v3.WithPrefix())
	cancel()

	if err != nil {
		return nil, err
	}

	n := len(resp.Kvs)
	if n < 3 {
		n = 3
	}

	tree := btree.NewWithStringComparator(n)
	for _, ev := range resp.Kvs {
		tree.Put(utils.Bytes2String(ev.Key), nil)
	}

	tmpKeys := tree.Keys()
	result := []string{}

	for _, key := range tmpKeys {
		result = append(result, key.(string))
	}

	return result, nil
}

func (this_ *Etcd) GetNode(key string) (map[string]*Node, error) {
	ctx, cancel := context.WithTimeout(context.TODO(), _TIMEOUT)
	resp, err := this_.cli.Get(ctx, key, v3.WithPrefix())
	cancel()

	if err != nil {
		return nil, err
	}

	result := map[string]*Node{}
	for _, ev := range resp.Kvs {
		node, err := ParseToNode(ev.Value)
		if err != nil {
			return nil, err
		}

		result[utils.Bytes2String(ev.Key)] = node
	}

	return result, nil
}

func (this_ *Etcd) Delete(key string) error {
	ctx, cancel := context.WithTimeout(context.TODO(), _TIMEOUT)
	_, err := this_.cli.Delete(ctx, key, v3.WithPrefix())
	cancel()

	if err == nil && atomic.LoadInt32(&this_.count) > 0 {
		atomic.AddInt32(&this_.count, -1)
	}

	return err
}

func (this_ *Etcd) Regist(serv, id, addr string, metaArgs ...interface{}) (string, error) {
	var (
		meta   interface{}
		e      error
		nodeID = ""
	)

	if len(metaArgs) > 0 {
		meta = metaArgs[0]
	}

	for dwf := true; dwf; dwf = false {
		resp, err := this_.cli.Grant(context.TODO(), _TTL)
		if err != nil {
			e = err
			break
		}

		ch, err := this_.cli.KeepAlive(context.TODO(), resp.ID)
		if err != nil {
			e = err
			break
		}

		atomic.AddInt32(&this_.count, 1)
		if atomic.LoadInt32(&this_.flag) < atomic.LoadInt32(&this_.count) {
			go func(rc <-chan *v3.LeaseKeepAliveResponse) {
				atomic.AddInt32(&this_.flag, 1)
				ok := true
				for ok {
					_, ok = <-rc
				}

				atomic.AddInt32(&this_.flag, -1)

				for {
					this_.Close()
					err := this_.init()
					if err != nil {
						log.Error(err)
						continue
					}

					_, err = this_.Regist(serv, id, addr)
					if err != nil {
						log.Error(err)
						continue
					}

					break
				}
			}(ch)
		}

		em, err := endpoints.NewManager(this_.cli, serv)
		if err != nil {
			e = err
			break
		}

		nodeID = fmt.Sprintf("%s/%s", serv, id)
		err = em.AddEndpoint(context.TODO(), nodeID, endpoints.Endpoint{Addr: addr, Metadata: meta}, v3.WithLease(resp.ID))
		if err != nil {
			e = err
			break
		}
	}

	return nodeID, e
}

func (this_ *Etcd) Exists(nodeID string) (bool, error) {
	keys, err := this_.GetKeys(nodeID)
	if err != nil {
		return false, err
	}

	return len(keys) > 0, nil
}

func (this_ *Etcd) ExistsOne(nodeID string) (bool, error) {
	keys, err := this_.GetKeys(nodeID)
	if err != nil {
		return false, err
	}

	return len(keys) == 1, nil
}

// Watch 服务发现
//  @serv: 监听的路径
//  @cb:   回调
// PS: 当节点是删除事件时, 回调的第三个参数 node为nil
func (this_ *Etcd) Watch(serv string, cb func(ev int32, nodePath *NodePath, node *Node)) {
	ch := this_.cli.Watch(context.TODO(), serv, v3.WithPrefix())
	go func() {
		var (
			err      error
			nodePath NodePath
		)

		for rsp := range ch {
			err = rsp.Err()
			if err != nil {
				log.Error(err)
			}

			var node *Node

			for _, ev := range rsp.Events {

				if ev.Type != mvccpb.DELETE {
					node, err = ParseToNode(ev.Kv.Value)
					if err != nil {
						log.Error(err)
						continue
					}
				}

				nodePath = NodePath(ev.Kv.Key)
				// 当ev.Type == DELETE 时 node为nil
				cb(int32(ev.Type), &nodePath, node)
			}
		}
	}()
}
