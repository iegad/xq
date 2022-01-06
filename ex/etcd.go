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
	_TIMEOUT = time.Second * 5      // ETCD操作超时, 默认为5秒
	_TTL     = int64(5)             // 保活时间间隔, 默认为5秒; 每隔5秒会向ETCD发送探针
	EvPUT    = int32(mvccpb.PUT)    // ETCD监听事件, 新增KEY
	EvDEL    = int32(mvccpb.DELETE) // ETCD监听事件, 删除KEY
)

// NodePath 节点路径
type NodePath string

// String 获取节点完整路径
func (this_ *NodePath) String() string {
	return *(*string)(this_)
}

// NodeID 获取节点ID
func (this_ *NodePath) NodeID() string {
	tmp := *(*string)(this_)
	n := strings.LastIndex(tmp, "/")
	if n < 0 {
		return ""
	}

	return tmp[n+1:]
}

// Path 获取节点前辍, 不包括ID部分
func (this_ *NodePath) Path() string {
	tmp := *(*string)(this_)
	n := strings.LastIndex(tmp, "/")
	if n < 0 {
		return ""
	}

	return tmp[:n]
}

// Service 获取服务名, 不包括前辍和ID部分
func (this_ *NodePath) Service() string {
	tmp := *(*string)(this_)
	n := strings.Index(tmp, "/")
	if n < 0 {
		return ""
	}

	return tmp[:n]
}

// Node 映射go.etcd.io/etcd/client/v3/naming/endpoints/internal/update.go 中的 Update 结构
type Node struct {
	Op       uint8       // 操作
	Metadata interface{} // 元数据
	Addr     string      // 监听地址
}

// String 获取Node的JSON格式
func (this_ *Node) String() string {
	data, _ := json.Marshal(this_)
	return utils.Bytes2String(data)
}

// ParseToNode 将JSON格式转换为Node对象
func ParseToNode(jstr []byte) (*Node, error) {
	node := &Node{}
	err := json.Unmarshal(jstr, node)
	if err != nil {
		return nil, err
	}

	return node, nil
}

// Etcd 操作封装
type Etcd struct {
	cli   *v3.Client // etcdv3连接对象
	flag  int32      // 当前已注册服务数据, 当成功注册服务时, flag加1, 当注册服务失败时, flag将会减1
	count int32      // 注册服务总数量, 当调用了N次Regist(注册服务)时, count便会加1
	hosts []string   // etcd服务地址集
}

// NewEtcd ETCD工厂函数
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

// Put 写入KV到ETCD中
func (this_ *Etcd) Put(key, val string) error {
	ctx, cancel := context.WithTimeout(context.TODO(), _TIMEOUT)
	defer cancel()

	_, err := this_.cli.Put(ctx, key, val)
	return err
}

// Get 获取KEY的键值对
func (this_ *Etcd) Get(key string) (map[string]string, error) {
	ctx, cancel := context.WithTimeout(context.TODO(), _TIMEOUT)
	defer cancel()

	resp, err := this_.cli.Get(ctx, key, v3.WithPrefix())

	if err != nil {
		return nil, err
	}

	result := map[string]string{}
	for _, ev := range resp.Kvs {
		result[utils.Bytes2String(ev.Key)] = utils.Bytes2String(ev.Value)
	}

	return result, nil
}

// GetKeys 获取key下所有的子KEY
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

// Delete 删除KEY
func (this_ *Etcd) Delete(key string) error {
	ctx, cancel := context.WithTimeout(context.TODO(), _TIMEOUT)
	_, err := this_.cli.Delete(ctx, key, v3.WithPrefix())
	cancel()

	if err == nil && atomic.LoadInt32(&this_.count) > 0 {
		atomic.AddInt32(&this_.count, -1)
	}

	return err
}

// Exists 判断nodeID是否存在
func (this_ *Etcd) Exists(nodeID string) (bool, error) {
	keys, err := this_.GetKeys(nodeID)
	if err != nil {
		return false, err
	}

	return len(keys) > 0, nil
}

// EixstsOne 判断nodeID是否只有一个
func (this_ *Etcd) ExistsOne(nodeID string) (bool, error) {
	keys, err := this_.GetKeys(nodeID)
	if err != nil {
		return false, err
	}

	return len(keys) == 1, nil
}

// Regist 注册服务
//  @有自动重连和重新注册功能
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
					select {
					case _, ok = <-rc:
					}
				}

				// 当 !ok时, 表示与ETCD的连接已经断开, 下面开始重连ETCD并重新注册服务
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
