package main

import (
	"encoding/binary"
	"errors"
	"fmt"
	"gopher/utils"
	"io"
	"net"
	"sync"
	"syscall"
	"time"
	"unsafe"

	"github.com/vmihailenco/msgpack/v5"
)

var (
	kernel32                    = syscall.NewLazyDLL("kernel32.dll")
	procCreateFileW             = kernel32.NewProc("CreateFileW")
	procReadFile                = kernel32.NewProc("ReadFile")
	procSetNamedPipeHandleState = kernel32.NewProc("SetNamedPipeHandleState")
	procCancelIoEx              = kernel32.NewProc("CancelIoEx")
)

const (
	_PIPE_READMODE_MESSAGE_CLIENT = 0x00000002
	_ERROR_MORE_DATA              = 234
	_ERROR_BROKEN_PIPE            = 109
	_ERROR_PIPE_NOT_CONNECTED     = 233
	_ERROR_OPERATION_ABORTED      = 995
	_ERROR_NO_DATA                = 232
)

type pipeReadResult struct {
	data []byte
	err  error
}

type pipeConn struct {
	handle    syscall.Handle
	closeCh   chan struct{}
	closeOnce sync.Once

	mu       sync.Mutex
	deadline time.Time

	readCh  chan pipeReadResult
	pending []byte
}

func newPipeConn(handle syscall.Handle) *pipeConn {
	c := &pipeConn{
		handle:  handle,
		closeCh: make(chan struct{}),
		readCh:  make(chan pipeReadResult, 16),
	}
	go c.readLoop()
	return c
}

func isPipeFatal(errno syscall.Errno) bool {
	switch errno {
	case _ERROR_BROKEN_PIPE,
		_ERROR_PIPE_NOT_CONNECTED,
		_ERROR_OPERATION_ABORTED,
		_ERROR_NO_DATA:
		return true
	}
	return false
}

func (c *pipeConn) readLoop() {
	for {
		select {
		case <-c.closeCh:
			return
		default:
		}

		buf := make([]byte, 65536)
		var nRead uint32
		r, _, callErr := procReadFile.Call(
			uintptr(c.handle),
			uintptr(unsafe.Pointer(&buf[0])),
			uintptr(len(buf)),
			uintptr(unsafe.Pointer(&nRead)),
			0,
		)

		if r != 0 {
			if nRead > 0 {
				data := make([]byte, nRead)
				copy(data, buf[:nRead])
				select {
				case c.readCh <- pipeReadResult{data: data}:
				case <-c.closeCh:
					return
				}
			}
			continue
		}

		errno, _ := callErr.(syscall.Errno)

		if errno == _ERROR_MORE_DATA && nRead > 0 {
			data := make([]byte, nRead)
			copy(data, buf[:nRead])
			select {
			case c.readCh <- pipeReadResult{data: data}:
			case <-c.closeCh:
				return
			}
			continue
		}

		if isPipeFatal(errno) {
			select {
			case c.readCh <- pipeReadResult{err: io.EOF}:
			case <-c.closeCh:
			}
			return
		}

		select {
		case c.readCh <- pipeReadResult{err: callErr}:
		case <-c.closeCh:
		}
		return
	}
}

func (c *pipeConn) Read(b []byte) (int, error) {
	if len(b) == 0 {
		return 0, nil
	}

	c.mu.Lock()
	if len(c.pending) > 0 {
		n := copy(b, c.pending)
		c.pending = c.pending[n:]
		c.mu.Unlock()
		return n, nil
	}
	deadline := c.deadline
	c.mu.Unlock()

	if !deadline.IsZero() {
		remaining := time.Until(deadline)
		if remaining <= 0 {
			select {
			case res := <-c.readCh:
				if res.err != nil {
					return 0, res.err
				}
				n := copy(b, res.data)
				c.mu.Lock()
				if n < len(res.data) {
					c.pending = res.data[n:]
				}
				c.mu.Unlock()
				return n, nil
			default:
				return 0, &timeoutError{}
			}
		}

		timer := time.NewTimer(remaining)
		defer timer.Stop()

		select {
		case res := <-c.readCh:
			if res.err != nil {
				return 0, res.err
			}
			n := copy(b, res.data)
			c.mu.Lock()
			if n < len(res.data) {
				c.pending = res.data[n:]
			}
			c.mu.Unlock()
			return n, nil
		case <-timer.C:
			return 0, &timeoutError{}
		case <-c.closeCh:
			return 0, io.EOF
		}
	}

	select {
	case res := <-c.readCh:
		if res.err != nil {
			return 0, res.err
		}
		n := copy(b, res.data)
		c.mu.Lock()
		if n < len(res.data) {
			c.pending = res.data[n:]
		}
		c.mu.Unlock()
		return n, nil
	case <-c.closeCh:
		return 0, io.EOF
	}
}

func (c *pipeConn) Write(b []byte) (int, error) {
	if len(b) == 0 {
		return 0, nil
	}
	var nWritten uint32
	err := syscall.WriteFile(c.handle, b, &nWritten, nil)
	if err != nil {
		return int(nWritten), err
	}
	return int(nWritten), nil
}

func (c *pipeConn) Close() error {
	c.closeOnce.Do(func() {
		close(c.closeCh)
		procCancelIoEx.Call(uintptr(c.handle), 0)
		syscall.CloseHandle(c.handle)
	})
	return nil
}

func (c *pipeConn) LocalAddr() net.Addr  { return pipeAddr{} }
func (c *pipeConn) RemoteAddr() net.Addr { return pipeAddr{} }

func (c *pipeConn) SetDeadline(t time.Time) error      { return c.SetReadDeadline(t) }
func (c *pipeConn) SetWriteDeadline(t time.Time) error { return nil }

func (c *pipeConn) SetReadDeadline(t time.Time) error {
	c.mu.Lock()
	c.deadline = t
	c.mu.Unlock()
	return nil
}

func openNamedPipe(pipePath string) (syscall.Handle, error) {
	pipeNameUTF16, err := syscall.UTF16PtrFromString(pipePath)
	if err != nil {
		return syscall.InvalidHandle, err
	}

	deadline := time.Now().Add(5 * time.Second)
	for {
		r, _, callErr := procCreateFileW.Call(
			uintptr(unsafe.Pointer(pipeNameUTF16)),
			uintptr(syscall.GENERIC_READ|syscall.GENERIC_WRITE),
			0, 0,
			uintptr(syscall.OPEN_EXISTING),
			0, 0,
		)
		handle := syscall.Handle(r)
		if r != 0 && handle != syscall.InvalidHandle {
			mode := uint32(_PIPE_READMODE_MESSAGE_CLIENT)
			procSetNamedPipeHandleState.Call(
				uintptr(handle),
				uintptr(unsafe.Pointer(&mode)),
				0,
				0,
			)
			return handle, nil
		}

		if time.Now().After(deadline) {
			return syscall.InvalidHandle, fmt.Errorf("timeout opening pipe %s: %w", pipePath, callErr)
		}
		time.Sleep(500 * time.Millisecond)
	}
}

func (p *Pivotter) LinkSMB(taskId uint32, params utils.ParamsLink) ([]byte, error) {
	pipePath := params.Target
	handle, err := openNamedPipe(pipePath)
	if err != nil {
		return nil, err
	}

	conn := newPipeConn(handle)

	beat, err := pivotRecvFrame(conn, 15*time.Second)
	if err != nil {
		conn.Close()
		return nil, fmt.Errorf("failed to read handshake from pipe %s: %w", pipePath, err)
	}
	if len(beat) < 5 {
		conn.Close()
		return nil, errors.New("handshake too short")
	}

	pivot := &PivotData{
		Id:     taskId,
		Type:   utils.PIVOT_TYPE_SMB,
		Conn:   conn,
		Active: true,
	}

	p.mu.Lock()
	p.pivots = append(p.pivots, pivot)
	p.mu.Unlock()


	watermark := binary.LittleEndian.Uint32(beat[:4])
	beatData := beat[4:]

	ans, _ := msgpack.Marshal(utils.AnsLink{
		Type:      utils.PIVOT_TYPE_SMB,
		Watermark: watermark,
		Beat:      beatData,
	})
	return ans, nil
}
