//=====================================================================
//
// KCP - A Better ARQ Protocol Implementation
// skywind3000 (at) gmail.com, 2010-2011
//  
// Features:
// + Average RTT reduce 30% - 40% vs traditional ARQ like tcp.
// + Maximum RTT reduce three times vs tcp.
// + Lightweight, distributed as a single source file.
//
//=====================================================================
#include "ikcp.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>



//=====================================================================
// KCP BASIC
//=====================================================================
#define IKCP_RTO_NDL		30		// no delay min rto
#define IKCP_RTO_MIN		100		// normal min rto
#define IKCP_RTO_DEF		200
#define IKCP_RTO_MAX		60000
#define IKCP_CMD_PUSH		81	// cmd: push data
#define IKCP_CMD_ACK		82	// cmd: ack
#define IKCP_CMD_WASK		83	// cmd: window probe (ask)
#define IKCP_CMD_WINS		84	// cmd: window size (tell)
#define IKCP_ASK_SEND		1	// need to send IKCP_CMD_WASK
#define IKCP_ASK_TELL		2	// need to send IKCP_CMD_WINS
#define IKCP_WND_SND		32
#define IKCP_WND_RCV		128      // must >= max fragment size
#define IKCP_MTU_DEF		1400
#define IKCP_ACK_FAST		3
#define IKCP_INTERVAL		100
#define IKCP_OVERHEAD		24
#define IKCP_DEADLINK		20
#define IKCP_THRESH_INIT	2
#define IKCP_THRESH_MIN		2
#define IKCP_PROBE_INIT		7000		// 7 secs to probe window size
#define IKCP_PROBE_LIMIT	120000	// up to 120 secs to probe window
#define IKCP_FASTACK_LIMIT	5		// max times to trigger fastack


//---------------------------------------------------------------------
// encode / decode
//---------------------------------------------------------------------

/* encode 8 bits unsigned int */
static inline uint8_t *ikcp_encode8u(uint8_t *p, uint8_t c)
{
    *p++ = c;
	return p;
}

/* decode 8 bits unsigned int */
static inline const uint8_t *ikcp_decode8u(const uint8_t *p, uint8_t *c)
{
    *c = *p++;
	return p;
}

/* encode 16 bits unsigned int (lsb) */
static inline uint8_t  *ikcp_encode16u(uint8_t *p, uint16_t w)
{
#if IWORDS_BIG_ENDIAN || IWORDS_MUST_ALIGN
    *(uint8_t*)(p + 0) = (w & 0xff);
    *(uint8_t*)(p + 1) = (w >> 8);
#else
	memcpy(p, &w, 2);
#endif
	p += 2;
	return p;
}

/* decode 16 bits unsigned int (lsb) */
static inline const uint8_t *ikcp_decode16u(const uint8_t *p, uint16_t *w)
{
#if IWORDS_BIG_ENDIAN || IWORDS_MUST_ALIGN
    *w = *(const uint8_t*)(p + 1);
    *w = *(const uint8_t*)(p + 0) + (*w << 8);
#else
	memcpy(w, p, 2);
#endif
	p += 2;
	return p;
}

/* encode 32 bits unsigned int (lsb) */
static inline uint8_t *ikcp_encode32u(uint8_t *p, uint32_t l)
{
#if IWORDS_BIG_ENDIAN || IWORDS_MUST_ALIGN
    *(uint8_t*)(p + 0) = (uint8_t)((l /*>>  0*/)/* & 0xff */);
    *(uint8_t*)(p + 1) = (uint8_t)((l >>  8)/* & 0xff */);
    *(uint8_t*)(p + 2) = (uint8_t)((l >> 16)/* & 0xff */);
    *(uint8_t*)(p + 3) = (uint8_t)((l >> 24)/* & 0xff */);
#else
	memcpy(p, &l, 4);
#endif
	p += 4;
	return p;
}

/* decode 32 bits unsigned int (lsb) */
static inline const uint8_t *ikcp_decode32u(const uint8_t *p, uint32_t *l)
{
#if IWORDS_BIG_ENDIAN || IWORDS_MUST_ALIGN
    *l = *(const uint8_t*)(p + 3);
    *l = *(const uint8_t*)(p + 2) + (*l << 8);
    *l = *(const uint8_t*)(p + 1) + (*l << 8);
    *l = *(const uint8_t*)(p + 0) + (*l << 8);
#else 
	memcpy(l, p, 4);
#endif
	p += 4;
	return p;
}

static inline uint32_t _imin_(uint32_t a, uint32_t b) {
	return a <= b ? a : b;
}

static inline uint32_t _imax_(uint32_t a, uint32_t b) {
	return a >= b ? a : b;
}

static inline uint32_t _ibound_(uint32_t lower, uint32_t middle, uint32_t upper)
{
	return _imin_(_imax_(lower, middle), upper);
}


//---------------------------------------------------------------------
// manage segment
//---------------------------------------------------------------------
typedef struct IKCPSEG IKCPSEG;

static void* (*ikcp_malloc_hook)(size_t) = NULL;
static void (*ikcp_free_hook)(void *) = NULL;

// internal malloc
static void* ikcp_malloc(size_t size) {
	if (ikcp_malloc_hook) 
		return ikcp_malloc_hook(size);
	return malloc(size);
}

// internal free
static void ikcp_free(void *ptr) {
	if (ikcp_free_hook) {
		ikcp_free_hook(ptr);
	}	else {
		free(ptr);
	}
}

// redefine allocator
void ikcp_allocator(void* (*new_malloc)(size_t), void (*new_free)(void*))
{
	ikcp_malloc_hook = new_malloc;
	ikcp_free_hook = new_free;
}

// allocate a new kcp segment
static IKCPSEG* ikcp_segment_new(int size)
{
	return (IKCPSEG*)ikcp_malloc(sizeof(IKCPSEG) + size);
}

// delete a segment
static void ikcp_segment_delete(IKCPSEG *seg)
{
	ikcp_free(seg);
}


// output segment
static int ikcp_output(ikcpcb *kcp, const uint8_t *data, size_t size)
{
	assert(kcp);
	assert(kcp->output);
	if (size == 0) return 0;
    return kcp->output(data, size, kcp, kcp->user);
}


//---------------------------------------------------------------------
// create a new kcpcb
//---------------------------------------------------------------------
ikcpcb* ikcp_create(uint32_t conv, void *user)
{
	ikcpcb *kcp = (ikcpcb*)ikcp_malloc(sizeof(struct IKCPCB));
	if (kcp == NULL) return NULL;
	kcp->conv = conv;
	kcp->user = user;
	kcp->snd_una = 0;
	kcp->snd_nxt = 0;
	kcp->rcv_nxt = 0;
	kcp->ts_probe = 0;
	kcp->probe_wait = 0;
	kcp->snd_wnd = IKCP_WND_SND;
	kcp->rcv_wnd = IKCP_WND_RCV;
	kcp->rmt_wnd = IKCP_WND_RCV;
	kcp->cwnd = 0;
	kcp->incr = 0;
	kcp->probe = 0;
	kcp->mtu = IKCP_MTU_DEF;
	kcp->mss = kcp->mtu - IKCP_OVERHEAD;

    kcp->buffer = (uint8_t*)ikcp_malloc((kcp->mtu + IKCP_OVERHEAD) * 3);
	if (kcp->buffer == NULL) {
		ikcp_free(kcp);
		return NULL;
	}

	iqueue_init(&kcp->snd_queue);
	iqueue_init(&kcp->rcv_queue);
	iqueue_init(&kcp->snd_buf);
	iqueue_init(&kcp->rcv_buf);
	kcp->nrcv_buf = 0;
	kcp->nsnd_buf = 0;
	kcp->nrcv_que = 0;
	kcp->nsnd_que = 0;
	kcp->state = 0;
	kcp->acklist = NULL;
	kcp->ackblock = 0;
	kcp->ackcount = 0;
	kcp->rx_srtt = 0;
	kcp->rx_rttval = 0;
	kcp->rx_rto = IKCP_RTO_DEF;
	kcp->rx_minrto = IKCP_RTO_MIN;
	kcp->current = 0;
	kcp->interval = IKCP_INTERVAL;
	kcp->ts_flush = IKCP_INTERVAL;
	kcp->nodelay = 0;
	kcp->updated = 0;
	kcp->ssthresh = IKCP_THRESH_INIT;
	kcp->fastresend = 0;
	kcp->fastlimit = IKCP_FASTACK_LIMIT;
	kcp->nocwnd = 0;
	kcp->xmit = 0;
	kcp->dead_link = IKCP_DEADLINK;
	kcp->output = NULL;

	return kcp;
}


//---------------------------------------------------------------------
// release a new kcpcb
//---------------------------------------------------------------------
void ikcp_release(ikcpcb *kcp)
{
	assert(kcp);
	if (kcp) {
		IKCPSEG *seg;
		while (!iqueue_is_empty(&kcp->snd_buf)) {
			seg = iqueue_entry(kcp->snd_buf.next, IKCPSEG, node);
			iqueue_del(&seg->node);
            ikcp_segment_delete(seg);
		}
		while (!iqueue_is_empty(&kcp->rcv_buf)) {
			seg = iqueue_entry(kcp->rcv_buf.next, IKCPSEG, node);
			iqueue_del(&seg->node);
            ikcp_segment_delete(seg);
		}
		while (!iqueue_is_empty(&kcp->snd_queue)) {
			seg = iqueue_entry(kcp->snd_queue.next, IKCPSEG, node);
			iqueue_del(&seg->node);
            ikcp_segment_delete(seg);
		}
		while (!iqueue_is_empty(&kcp->rcv_queue)) {
			seg = iqueue_entry(kcp->rcv_queue.next, IKCPSEG, node);
			iqueue_del(&seg->node);
            ikcp_segment_delete(seg);
		}
		if (kcp->buffer) {
			ikcp_free(kcp->buffer);
		}
		if (kcp->acklist) {
			ikcp_free(kcp->acklist);
		}

		kcp->nrcv_buf = 0;
		kcp->nsnd_buf = 0;
		kcp->nrcv_que = 0;
		kcp->nsnd_que = 0;
		kcp->ackcount = 0;
		kcp->buffer = NULL;
		kcp->acklist = NULL;
		ikcp_free(kcp);
	}
}


//---------------------------------------------------------------------
// user/upper level recv: returns size, returns below zero for EAGAIN
//---------------------------------------------------------------------
int ikcp_recv(ikcpcb *kcp, uint8_t *buffer, size_t len)
{
	struct IQUEUEHEAD *p;
	int recover = 0;
	IKCPSEG *seg;
	assert(kcp);

	if (iqueue_is_empty(&kcp->rcv_queue)) {
		return -1;
	}

    int peeksize = ikcp_peeksize(kcp);

	if (peeksize < 0) {
		return -2;
	}

	if (peeksize > (int)len) {
		return -3;
	}

	if (kcp->nrcv_que >= kcp->rcv_wnd) {
		recover = 1;
	}

	// merge fragment
	for (len = 0, p = kcp->rcv_queue.next; p != &kcp->rcv_queue; ) {
		int fragment;
		seg = iqueue_entry(p, IKCPSEG, node);
		p = p->next;

		if (buffer) {
			memcpy(buffer, seg->data, seg->len);
			buffer += seg->len;
		}

		len += seg->len;
		fragment = seg->frg;

		iqueue_del(&seg->node);
		ikcp_segment_delete(seg);
		kcp->nrcv_que--;

		if (fragment == 0) {
			break;
		}
	}

	assert((int)len == peeksize);

	// move available data from rcv_buf -> rcv_queue
	while (! iqueue_is_empty(&kcp->rcv_buf)) {
		seg = iqueue_entry(kcp->rcv_buf.next, IKCPSEG, node);
		if (seg->sn == kcp->rcv_nxt && kcp->nrcv_que < kcp->rcv_wnd) {
			iqueue_del(&seg->node);
			kcp->nrcv_buf--;
			iqueue_add_tail(&seg->node, &kcp->rcv_queue);
			kcp->nrcv_que++;
			kcp->rcv_nxt++;
		}	else {
			break;
		}
	}

	// fast recover
	if (kcp->nrcv_que < kcp->rcv_wnd && recover) {
		// ready to send back IKCP_CMD_WINS in ikcp_flush
		// tell remote my window size
		kcp->probe |= IKCP_ASK_TELL;
	}

	return len;
}


//---------------------------------------------------------------------
// peek data size
//---------------------------------------------------------------------
int ikcp_peeksize(const ikcpcb *kcp)
{
	struct IQUEUEHEAD *p;
	IKCPSEG *seg;
	int length = 0;

	assert(kcp);

	if (iqueue_is_empty(&kcp->rcv_queue)) return -1;

	seg = iqueue_entry(kcp->rcv_queue.next, IKCPSEG, node);
	if (seg->frg == 0) return seg->len;

	if (kcp->nrcv_que < seg->frg + 1) return -1;

	for (p = kcp->rcv_queue.next; p != &kcp->rcv_queue; p = p->next) {
		seg = iqueue_entry(p, IKCPSEG, node);
		length += seg->len;
		if (seg->frg == 0) break;
	}

	return length;
}


//---------------------------------------------------------------------
// user/upper level send, returns below zero for error
//---------------------------------------------------------------------
int ikcp_send(ikcpcb *kcp, const uint8_t *buffer, size_t len)
{
	IKCPSEG *seg;

	assert(buffer && len > 0);
	assert(kcp->mss > 0);

	// 1, 计算拆包数
    int count = (len <= kcp->mss) ? 1 : (len + kcp->mss - 1) / kcp->mss;

	// 2, 判断是否大于最大分包
	if (count >= (int)IKCP_WND_RCV) {
		// 超过最多分段时返回 -1
		return -1;
	}

	// fragment
	for (int i = 0; i < count; i++) {
        int size = len > kcp->mss ? kcp->mss : len;
        seg = ikcp_segment_new(size);
		assert(seg);

		memcpy(seg->data, buffer, size);
		seg->len = size;
		seg->frg = count - i - 1;
		iqueue_init(&seg->node);
		iqueue_add_tail(&seg->node, &kcp->snd_queue);
		kcp->nsnd_que++;
		buffer += size;
		len -= size;
	}

	return 0;
}


//---------------------------------------------------------------------
// parse ack
//---------------------------------------------------------------------
static void ikcp_update_ack(ikcpcb *kcp, int32_t rtt)
{
    int32_t rto = 0;
	if (kcp->rx_srtt == 0) {
		kcp->rx_srtt = rtt;
		kcp->rx_rttval = rtt / 2;
	}	else {
		long delta = rtt - kcp->rx_srtt;
		if (delta < 0) delta = -delta;
		kcp->rx_rttval = (3 * kcp->rx_rttval + delta) / 4;
		kcp->rx_srtt = (7 * kcp->rx_srtt + rtt) / 8;
		if (kcp->rx_srtt < 1) kcp->rx_srtt = 1;
	}
	rto = kcp->rx_srtt + _imax_(kcp->interval, 4 * kcp->rx_rttval);
	kcp->rx_rto = _ibound_(kcp->rx_minrto, rto, IKCP_RTO_MAX);
}

static void ikcp_shrink_buf(ikcpcb *kcp)
{// 该函数确保kcp->snd_una为snd_buf中的最小值, 并且每当snd_buf队列前端的数据有变动时, 都需要调用该函数来确认snd_una
	struct IQUEUEHEAD *p = kcp->snd_buf.next;
	if (p != &kcp->snd_buf) {
		// 如果发送缓冲区不为空 snd_una 为发送缓冲区中最小数分组的sn
		IKCPSEG *seg = iqueue_entry(p, IKCPSEG, node);
		kcp->snd_una = seg->sn;
	}
	else {
		// 如果发送缓冲区为空 snd_una 为下次要发送分组的sn
		kcp->snd_una = kcp->snd_nxt;
	}
}

static void ikcp_parse_ack(ikcpcb *kcp, uint32_t sn)
{
	struct IQUEUEHEAD *p, *next;

	if (sn < kcp->snd_una || sn >= kcp->snd_nxt)
		return;

	for (p = kcp->snd_buf.next; p != &kcp->snd_buf; p = next) {
		IKCPSEG *seg = iqueue_entry(p, IKCPSEG, node);
		next = p->next;
		if (sn == seg->sn) {
			iqueue_del(p);
            ikcp_segment_delete(seg);
			kcp->nsnd_buf--;
			break;
		}
		if (sn < seg->sn) {
			// 当前分组sn 小于缓冲区中某分组的sn时, 说明该ACK已经确认过, 并不存在于缓冲区了
			break;
		}
	}
}

static void ikcp_parse_una(ikcpcb *kcp, uint32_t una)
{
	struct IQUEUEHEAD *p, *next;
	for (p = kcp->snd_buf.next; p != &kcp->snd_buf; p = next) {
		IKCPSEG *seg = iqueue_entry(p, IKCPSEG, node);
		next = p->next;
		if (una > seg->sn) {
			iqueue_del(p);
			ikcp_segment_delete(seg);
			kcp->nsnd_buf--;
		}
		else {
			break;
		}
	}
}

static void ikcp_parse_fastack(ikcpcb *kcp, uint32_t sn/*, uint32_t ts*/)
{// 确认 缓冲区中分组 sn 被跨越了多少次
	struct IQUEUEHEAD *p, *next;

	if (sn < kcp->snd_una || sn >= kcp->snd_nxt)
		return;

	for (p = kcp->snd_buf.next; p != &kcp->snd_buf; p = next) {
		IKCPSEG *seg = iqueue_entry(p, IKCPSEG, node);
		next = p->next;
		if (sn < seg->sn) {
			break;
		}
		else if (sn != seg->sn) {
			seg->fastack++;
		}
	}
}


//---------------------------------------------------------------------
// ack append
//---------------------------------------------------------------------
static void ikcp_ack_push(ikcpcb *kcp, uint32_t sn, uint32_t ts)
{
    uint32_t newsize = kcp->ackcount + 1;
    uint32_t *ptr;

	if (newsize > kcp->ackblock) {
        uint32_t *acklist;
        uint32_t newblock;

        for (newblock = 8; newblock < newsize; newblock <<= 1) {}
        acklist = (uint32_t*)ikcp_malloc(newblock * sizeof(uint32_t) * 2);
		assert(acklist);

		if (kcp->acklist != NULL) {
            uint32_t x;
			for (x = 0; x < kcp->ackcount; x++) {
				acklist[x * 2 + 0] = kcp->acklist[x * 2 + 0];
				acklist[x * 2 + 1] = kcp->acklist[x * 2 + 1];
			}
			ikcp_free(kcp->acklist);
		}

		kcp->acklist = acklist;
		kcp->ackblock = newblock;
	}

	ptr = &kcp->acklist[kcp->ackcount * 2];
	ptr[0] = sn;
	ptr[1] = ts;
	kcp->ackcount++;
}

static void ikcp_ack_get(const ikcpcb *kcp, int p, uint32_t *sn, uint32_t *ts)
{
	if (sn) sn[0] = kcp->acklist[p * 2 + 0];
	if (ts) ts[0] = kcp->acklist[p * 2 + 1];
}


//---------------------------------------------------------------------
// parse data
//---------------------------------------------------------------------
void ikcp_parse_data(ikcpcb *kcp, IKCPSEG *newseg)
{
	struct IQUEUEHEAD *p, *prev;
    uint32_t sn = newseg->sn;
	int repeat = 0;
	
	if (sn >= kcp->rcv_nxt + kcp->rcv_wnd || sn < kcp->rcv_nxt) {
        ikcp_segment_delete(newseg);
		return;
	}

	for (p = kcp->rcv_buf.prev; p != &kcp->rcv_buf; p = prev) {
		IKCPSEG *seg = iqueue_entry(p, IKCPSEG, node);
		prev = p->prev;
		if (seg->sn == sn) {
			repeat = 1;
			break;
		}
		if (sn > seg->sn) {
			break;
		}
	}

	if (repeat == 0) {
		iqueue_init(&newseg->node);
		iqueue_add(&newseg->node, p);
		kcp->nrcv_buf++;
	}
	else {
        ikcp_segment_delete(newseg);
	}

	// move available data from rcv_buf -> rcv_queue
	while (!iqueue_is_empty(&kcp->rcv_buf)) {
		IKCPSEG *seg = iqueue_entry(kcp->rcv_buf.next, IKCPSEG, node);
		if (seg->sn == kcp->rcv_nxt && kcp->nrcv_que < kcp->rcv_wnd) {
			iqueue_del(&seg->node);
			kcp->nrcv_buf--;
			iqueue_add_tail(&seg->node, &kcp->rcv_queue);
			kcp->nrcv_que++;
			kcp->rcv_nxt++;
		}
		else {
			break;
		}
	}
}


//---------------------------------------------------------------------
// input data
//---------------------------------------------------------------------
int ikcp_input(ikcpcb *kcp, const uint8_t *data, size_t size)
{
    uint32_t prev_una = kcp->snd_una;
    uint32_t maxack = 0;
	int flag = 0;

    if (data == NULL || size < IKCP_OVERHEAD) return -1;

	while (1) {
        uint32_t ts, sn, len, una, conv;
        uint16_t wnd;
        uint8_t cmd, frg;
		IKCPSEG *seg;

		if (size < IKCP_OVERHEAD) {
			break;
		}

		// Step 1: 解析消息头
		data = ikcp_decode32u(data, &conv);
		if (conv != kcp->conv) {
			// 如果该消息并非来自于当前kcp
			return -1;
		}

		data = ikcp_decode8u(data, &cmd);
		data = ikcp_decode8u(data, &frg);
		data = ikcp_decode16u(data, &wnd);
		data = ikcp_decode32u(data, &ts);
		data = ikcp_decode32u(data, &sn);
		data = ikcp_decode32u(data, &una);
		data = ikcp_decode32u(data, &len);

		size -= IKCP_OVERHEAD;

		if (size < len || len > 89 * 16 /* TODO: 后期需要修改, 主要原因是为了进行[AES128]加密 */) {
			// 当原始数据长度 < 实际数据长度, 实际数据长度 大于段最大长度
			return -2;
		}

		if (cmd < IKCP_CMD_PUSH && cmd > IKCP_CMD_WINS) {
			// 无效的CMD
			return -3;
		}

		// Step 2: 设置对端窗口
		kcp->rmt_wnd = wnd;
		// Step 3: 将seg::sn <= una 的包移出snd_buf
		ikcp_parse_una(kcp, una);
		// Step 4: 确认kcp->snd_una, 确保该值一定是snd_buf中的最小值
		ikcp_shrink_buf(kcp);

		switch (cmd) {
		case IKCP_CMD_ACK: {
			if (kcp->current >= ts) {
				// 如果当前时间 >= 分组的发送时间, 重新计算RTO
				ikcp_update_ack(kcp, kcp->current - ts);
			}
			// 将ACK对应的分组移出snd_buf
			ikcp_parse_ack(kcp, sn);
			// 重新确认kcp->snd_una.
			ikcp_shrink_buf(kcp);
			if (flag == 0) {
				flag = 1;
				maxack = sn;
			}
			else if (sn > maxack) {
				maxack = sn;
			}
		} break;

		case IKCP_CMD_PUSH: {
			if (sn < kcp->rcv_nxt + kcp->rcv_wnd) {
				ikcp_ack_push(kcp, sn, ts);
				if (sn >= kcp->rcv_nxt) {
					seg = ikcp_segment_new(len);
					seg->conv = conv;
					seg->cmd = cmd;
					seg->frg = frg;
					seg->wnd = wnd;
					seg->ts = ts;
					seg->sn = sn;
					seg->una = una;
					seg->len = len;

					if (len > 0) {
						memcpy(seg->data, data, len);
					}

					ikcp_parse_data(kcp, seg);
				}
			}
		} break;

		case IKCP_CMD_WASK: {
			kcp->probe |= IKCP_ASK_TELL;
		} break;

		case IKCP_CMD_WINS: {
			// do nothing
		} break;

		default:
			return -3;
		}

		data += len;
		size -= len;
	}

	if (flag) {
		// 检查快速重传
        ikcp_parse_fastack(kcp, maxack);
	}

	if (kcp->snd_una > prev_una) {
		// 重新计算cwnd
		if (kcp->cwnd < kcp->rmt_wnd) {
			uint32_t mss = kcp->mss;
			if (kcp->cwnd < kcp->ssthresh) {
				kcp->cwnd++;
				kcp->incr += mss;
			}
			else {
				if (kcp->incr < mss) {
					kcp->incr = mss;
				}
				kcp->incr += (mss * mss) / kcp->incr + (mss / 16);
				if ((kcp->cwnd + 1) * mss <= kcp->incr) {
					kcp->cwnd = (kcp->incr + mss - 1) / ((mss > 0)? mss : 1);
				}
			}

			if (kcp->cwnd > kcp->rmt_wnd) {
				kcp->cwnd = kcp->rmt_wnd;
				kcp->incr = kcp->rmt_wnd * mss;
			}
		}
	}

	return 0;
}


//---------------------------------------------------------------------
// ikcp_encode_seg
//---------------------------------------------------------------------
static inline uint8_t *ikcp_encode_seg(uint8_t *ptr, const IKCPSEG *seg)
{
	ptr = ikcp_encode32u(ptr, seg->conv);
    ptr = ikcp_encode8u(ptr, (uint8_t)seg->cmd);
    ptr = ikcp_encode8u(ptr, (uint8_t)seg->frg);
    ptr = ikcp_encode16u(ptr, (uint16_t)seg->wnd);
	ptr = ikcp_encode32u(ptr, seg->ts);
	ptr = ikcp_encode32u(ptr, seg->sn);
	ptr = ikcp_encode32u(ptr, seg->una);
	ptr = ikcp_encode32u(ptr, seg->len);
	return ptr;
}

static inline int ikcp_wnd_unused(const ikcpcb *kcp)
{
	if (kcp->nrcv_que < kcp->rcv_wnd) {
		return kcp->rcv_wnd - kcp->nrcv_que;
	}
	return 0;
}


//---------------------------------------------------------------------
// ikcp_flush
//---------------------------------------------------------------------
void ikcp_flush(ikcpcb *kcp)
{
    uint32_t current = kcp->current;
    uint8_t *buffer = kcp->buffer;
    uint8_t *ptr = buffer;
	int count, size, i;
    uint32_t resent, cwnd;
    uint32_t rtomin;
	struct IQUEUEHEAD *p;
	int change = 0;
	int lost = 0;
	IKCPSEG seg = {
		.node     = {0},
		.conv     = kcp-> conv,
		.cmd      = 0,
		.frg      = 0,
		.wnd      = (uint32_t)ikcp_wnd_unused(kcp),
		.ts       = 0,
		.sn       = 0,
		.una      = kcp->rcv_nxt,
		.len      = 0,
		.resendts = 0,
		.rto      = 0,
		.fastack  = 0,
		.xmit     = 0, 
		.data     = {0}
	};

	// 'ikcp_update' haven't been called. 
	if (kcp->updated == 0) {
		return;
	}

	// flush acknowledges
	count = kcp->ackcount;
	if (count > 0) {
		seg.cmd = IKCP_CMD_ACK;

		for (i = 0; i < count; i++) {
			size = (int)(ptr - buffer);
			if (size + (int)IKCP_OVERHEAD > (int)kcp->mtu) {
				ikcp_output(kcp, buffer, size);
				ptr = buffer;
			}
			ikcp_ack_get(kcp, i, &seg.sn, &seg.ts);
			ptr = ikcp_encode_seg(ptr, &seg);
		}

		kcp->ackcount = 0;
	}

	// probe window size (if remote window size equals zero)
	if (kcp->rmt_wnd == 0) {
		if (kcp->probe_wait == 0) {
			kcp->probe_wait = IKCP_PROBE_INIT;
			kcp->ts_probe = kcp->current + kcp->probe_wait;
		}	
		else {
			if (kcp->current >= kcp->ts_probe) {
				if (kcp->probe_wait < IKCP_PROBE_INIT) {
					kcp->probe_wait = IKCP_PROBE_INIT;
				}

				kcp->probe_wait += kcp->probe_wait / 2;
				if (kcp->probe_wait > IKCP_PROBE_LIMIT) {
					kcp->probe_wait = IKCP_PROBE_LIMIT;
				}

				kcp->ts_probe = kcp->current + kcp->probe_wait;
				kcp->probe |= IKCP_ASK_SEND;
			}
		}
	}
	else {
		kcp->ts_probe = 0;
		kcp->probe_wait = 0;
	}

	// flush window probing commands
	if (kcp->probe & IKCP_ASK_SEND) {
		seg.cmd = IKCP_CMD_WASK;
		size = (int)(ptr - buffer);
		if (size + (int)IKCP_OVERHEAD > (int)kcp->mtu) {
			ikcp_output(kcp, buffer, size);
			ptr = buffer;
		}
		ptr = ikcp_encode_seg(ptr, &seg);
	}

	// flush window probing commands
	if (kcp->probe & IKCP_ASK_TELL) {
		seg.cmd = IKCP_CMD_WINS;
		size = (int)(ptr - buffer);
		if (size + (int)IKCP_OVERHEAD > (int)kcp->mtu) {
			ikcp_output(kcp, buffer, size);
			ptr = buffer;
		}
		ptr = ikcp_encode_seg(ptr, &seg);
	}

	kcp->probe = 0;

	// calculate window size
	cwnd = _imin_(kcp->snd_wnd, kcp->rmt_wnd);
	if (kcp->nocwnd == 0) cwnd = _imin_(kcp->cwnd, cwnd);

	// move data from snd_queue to snd_buf
	while (kcp->snd_nxt < kcp->snd_una + cwnd) {
		IKCPSEG *newseg;
		if (iqueue_is_empty(&kcp->snd_queue)) break;

		newseg = iqueue_entry(kcp->snd_queue.next, IKCPSEG, node);

		iqueue_del(&newseg->node);
		iqueue_add_tail(&newseg->node, &kcp->snd_buf);
		kcp->nsnd_que--;
		kcp->nsnd_buf++;

		newseg->conv = kcp->conv;
		newseg->cmd = IKCP_CMD_PUSH;
		newseg->wnd = seg.wnd;
		newseg->ts = current;
        newseg->sn = kcp->snd_nxt++;
		newseg->una = kcp->rcv_nxt;
		newseg->resendts = current;
		newseg->rto = kcp->rx_rto;
		newseg->fastack = 0;
		newseg->xmit = 0;
	}

	// calculate resent
    resent = (kcp->fastresend > 0) ? (uint32_t)kcp->fastresend : 0xffffffff;
	rtomin = (kcp->nodelay == 0) ? (kcp->rx_rto >> 3) : 0;

	// flush data segments
	for (p = kcp->snd_buf.next; p != &kcp->snd_buf; p = p->next) {
		IKCPSEG *segment = iqueue_entry(p, IKCPSEG, node);
		int needsend = 0;
		if (segment->xmit == 0) {
			needsend = 1;
			segment->xmit++;
			segment->rto = kcp->rx_rto;
			segment->resendts = current + segment->rto + rtomin;
		}
		else if (current >= segment->resendts) {
			needsend = 1;
			segment->xmit++;
			kcp->xmit++;
			if (kcp->nodelay == 0) {
                segment->rto += _imax_(segment->rto, (uint32_t)kcp->rx_rto);
			}	else {
                int32_t step = (kcp->nodelay < 2) ? ((int32_t)(segment->rto)) : kcp->rx_rto;
				segment->rto += step / 2;
			}
			segment->resendts = current + segment->rto;
			lost = 1;
		}
		else if (segment->fastack >= resent) {
			if ((int)segment->xmit <= kcp->fastlimit || 
				kcp->fastlimit <= 0) {
				needsend = 1;
				segment->xmit++;
				segment->fastack = 0;
				segment->resendts = current + segment->rto;
				change++;
			}
		}

		if (needsend) {
			int need;
			segment->ts = current;
			segment->wnd = seg.wnd;
			segment->una = kcp->rcv_nxt;

			size = (int)(ptr - buffer);
			need = IKCP_OVERHEAD + segment->len;

			if (size + need > (int)kcp->mtu) {
				ikcp_output(kcp, buffer, size);
				ptr = buffer;
			}

			ptr = ikcp_encode_seg(ptr, segment);

			if (segment->len > 0) {
				memcpy(ptr, segment->data, segment->len);
				ptr += segment->len;
			}

			if (segment->xmit >= kcp->dead_link) {
                kcp->state = ~0;
			}
		}
	}

	// flash remain segments
	size = (int)(ptr - buffer);
	if (size > 0) {
		ikcp_output(kcp, buffer, size);
	}

	// update ssthresh
	if (change) {
        uint32_t inflight = kcp->snd_nxt - kcp->snd_una;
		kcp->ssthresh = inflight / 2;
		if (kcp->ssthresh < IKCP_THRESH_MIN)
			kcp->ssthresh = IKCP_THRESH_MIN;
		kcp->cwnd = kcp->ssthresh + resent;
		kcp->incr = kcp->cwnd * kcp->mss;
	}

	if (lost) {
		kcp->ssthresh = cwnd / 2;
		if (kcp->ssthresh < IKCP_THRESH_MIN)
			kcp->ssthresh = IKCP_THRESH_MIN;
		kcp->cwnd = 1;
		kcp->incr = kcp->mss;
	}

	if (kcp->cwnd < 1) {
		kcp->cwnd = 1;
		kcp->incr = kcp->mss;
	}
}


//---------------------------------------------------------------------
// update state (call it repeatedly, every 10ms-100ms), or you can ask 
// ikcp_check when to call it again (without ikcp_input/_send calling).
// 'current' - current timestamp in millisec. 
//---------------------------------------------------------------------
void ikcp_update(ikcpcb *kcp, uint32_t current)
{// 该函数主要用来记录flush的时间
    int32_t slap;

	kcp->current = current;

	if (kcp->updated == 0) {
		kcp->updated = 1;
		kcp->ts_flush = kcp->current;
	}

	slap = kcp->current - kcp->ts_flush;

	if (slap >= 10000 || slap < -10000) {
		kcp->ts_flush = kcp->current;
		slap = 0;
	}

	if (slap >= 0) {
		if (kcp->current >= kcp->ts_flush) {
			kcp->ts_flush = kcp->current + kcp->interval;
		} 
		else {
			kcp->ts_flush += kcp->interval;
		}
		ikcp_flush(kcp);
	}
}


//---------------------------------------------------------------------
// Determine when should you invoke ikcp_update:
// returns when you should invoke ikcp_update in millisec, if there 
// is no ikcp_input/_send calling. you can call ikcp_update in that
// time, instead of call update repeatly.
// Important to reduce unnacessary ikcp_update invoking. use it to 
// schedule ikcp_update (eg. implementing an epoll-like mechanism, 
// or optimize ikcp_update when handling massive kcp connections)
//---------------------------------------------------------------------
uint32_t ikcp_check(const ikcpcb *kcp, uint32_t current)
{
    uint32_t ts_flush = kcp->ts_flush;
    int32_t tm_flush = 0x7fffffff;
    int32_t tm_packet = 0x7fffffff;
    uint32_t minimal = 0;
	struct IQUEUEHEAD *p;

	if (kcp->updated == 0) {
		return current;
	}

	int32_t slap = (int32_t)(current - ts_flush);

	if (slap >= 10000 || slap < -10000) {
		ts_flush = current;
	}

	if (current >= ts_flush) {
		return current;
	}

	tm_flush = -slap;

	for (p = kcp->snd_buf.next; p != &kcp->snd_buf; p = p->next) {
		const IKCPSEG *seg = iqueue_entry(p, const IKCPSEG, node);
        int32_t diff = (int32_t)(seg->resendts - current);
		if (diff <= 0) {
			return current;
		}
		if (diff < tm_packet) tm_packet = diff;
	}

    minimal = (uint32_t)(tm_packet < tm_flush ? tm_packet : tm_flush);
	if (minimal >= kcp->interval) minimal = kcp->interval;

	return current + minimal;
}



int ikcp_setmtu(ikcpcb *kcp, int mtu)
{
    uint8_t *buffer;
	if (mtu < 50 || mtu < (int)IKCP_OVERHEAD) 
		return -1;
    buffer = (uint8_t*)ikcp_malloc((mtu + IKCP_OVERHEAD) * 3);
	if (buffer == NULL) 
		return -2;
	kcp->mtu = mtu;
	kcp->mss = kcp->mtu - IKCP_OVERHEAD;
	ikcp_free(kcp->buffer);
	kcp->buffer = buffer;
	return 0;
}

int ikcp_interval(ikcpcb *kcp, int interval)
{
	if (interval > 5000) interval = 5000;
	else if (interval < 10) interval = 10;
	kcp->interval = interval;
	return 0;
}

int ikcp_nodelay(ikcpcb *kcp, int nodelay, int interval, int resend, int nc)
{
	if (nodelay >= 0) {
		kcp->nodelay = nodelay;
		if (nodelay) {
			kcp->rx_minrto = IKCP_RTO_NDL;	
		}	
		else {
			kcp->rx_minrto = IKCP_RTO_MIN;
		}
	}
	if (interval >= 0) {
		if (interval > 5000) interval = 5000;
		else if (interval < 10) interval = 10;
		kcp->interval = interval;
	}
	if (resend >= 0) {
		kcp->fastresend = resend;
	}
	if (nc >= 0) {
		kcp->nocwnd = nc;
	}
	return 0;
}


int ikcp_wndsize(ikcpcb *kcp, int sndwnd, int rcvwnd)
{
	if (kcp) {
		if (sndwnd > 0) {
			kcp->snd_wnd = sndwnd;
		}
		if (rcvwnd > 0) {   // must >= max fragment size
			kcp->rcv_wnd = _imax_(rcvwnd, IKCP_WND_RCV);
		}
	}
	return 0;
}

int ikcp_waitsnd(const ikcpcb *kcp)
{
	return kcp->nsnd_buf + kcp->nsnd_que;
}


// read conv
uint32_t ikcp_getconv(const void *ptr)
{
    uint32_t conv;
    ikcp_decode32u((const uint8_t*)ptr, &conv);
	return conv;
}


