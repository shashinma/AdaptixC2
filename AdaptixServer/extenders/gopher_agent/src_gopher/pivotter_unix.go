
package main

import (
	"encoding/binary"
	"errors"
	"fmt"
	"gopher/utils"
	"io"
	"net"
	"os"
	"strings"
	"sync"
	"time"

	smb2 "github.com/hirochachacha/go-smb2"
	"github.com/vmihailenco/msgpack/v5"
)

type smbPipeConn struct {
	file      *smb2.File
	session   *smb2.Session
	share     *smb2.Share
	tcpConn   net.Conn
	closeCh   chan struct{}
	closeOnce sync.Once

	mu       sync.Mutex
	deadline time.Time

	ioMu sync.Mutex

	readCh  chan smbReadResult
	pending []byte
}

type smbReadResult struct {
	data []byte
	err  error
}

func newSmbPipeConn(file *smb2.File, session *smb2.Session, share *smb2.Share, tcpConn net.Conn) *smbPipeConn {
	c := &smbPipeConn{
		file:    file,
		session: session,
		share:   share,
		tcpConn: tcpConn,
		closeCh: make(chan struct{}),
		readCh:  make(chan smbReadResult, 16),
	}
	go c.readLoop()
	return c
}

func isFatalPipeError(err error) bool {
	if err == io.EOF {
		return true
	}
	s := strings.ToLower(err.Error())
	return strings.Contains(s, "broken") ||
		strings.Contains(s, "closed") ||
		strings.Contains(s, "reset") ||
		strings.Contains(s, "logoff") ||
		strings.Contains(s, "use of closed")
}

func (c *smbPipeConn) readLoop() {
	for {
		select {
		case <-c.closeCh:
			return
		default:
		}

		buf := make([]byte, 65536)
		c.ioMu.Lock()
		if _, seekErr := c.file.Seek(0, io.SeekStart); seekErr != nil {
			c.ioMu.Unlock()
			select {
			case c.readCh <- smbReadResult{err: seekErr}:
			case <-c.closeCh:
			}
			return
		}
		n, err := c.file.Read(buf)
		c.ioMu.Unlock()
		if n > 0 {
			data := make([]byte, n)
			copy(data, buf[:n])
			select {
			case c.readCh <- smbReadResult{data: data}:
			case <-c.closeCh:
				return
			}
		} else if err == nil {
			select {
			case <-time.After(50 * time.Millisecond):
			case <-c.closeCh:
				return
			}
		}
		if err != nil {
			if isFatalPipeError(err) {
				select {
				case c.readCh <- smbReadResult{err: err}:
				case <-c.closeCh:
				}
				return
			}
			select {
			case <-time.After(50 * time.Millisecond):
			case <-c.closeCh:
				return
			}
		}
	}
}

func (c *smbPipeConn) Read(b []byte) (int, error) {
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

func (c *smbPipeConn) Write(b []byte) (int, error) {
	c.ioMu.Lock()
	defer c.ioMu.Unlock()
	const maxChunk = 64 * 1024
	total := 0
	for total < len(b) {
		end := total + maxChunk
		if end > len(b) {
			end = len(b)
		}
		n, err := c.file.WriteAt(b[total:end], 0)
		if err != nil {
			return total, err
		}
		if n == 0 {
			return total, io.ErrShortWrite
		}
		total += n
	}
	return total, nil
}

func (c *smbPipeConn) Close() error {
	c.closeOnce.Do(func() {
		close(c.closeCh)
		c.file.Close()
		c.share.Umount()
		c.session.Logoff()
		c.tcpConn.Close()
	})
	return nil
}

func (c *smbPipeConn) LocalAddr() net.Addr                { return pipeAddr{} }
func (c *smbPipeConn) RemoteAddr() net.Addr               { return pipeAddr{} }
func (c *smbPipeConn) SetDeadline(t time.Time) error      { return c.SetReadDeadline(t) }
func (c *smbPipeConn) SetWriteDeadline(t time.Time) error { return nil }

func (c *smbPipeConn) SetReadDeadline(t time.Time) error {
	c.mu.Lock()
	c.deadline = t
	c.mu.Unlock()
	return nil
}

func (c *smbPipeConn) flushPivotPipe() error {
	return nil
}

func parseSMBPipePath(pipePath string) (host string, pipeName string, err error) {
	normalized := strings.ReplaceAll(pipePath, "/", "\\")
	normalized = strings.TrimPrefix(normalized, "\\\\")
	parts := strings.SplitN(normalized, "\\", 3)
	if len(parts) < 3 || !strings.EqualFold(parts[1], "pipe") {
		return "", "", fmt.Errorf("invalid SMB pipe path: %s (expected \\\\host\\pipe\\name)", pipePath)
	}
	return parts[0], parts[2], nil
}

func (p *Pivotter) LinkSMB(taskId uint32, params utils.ParamsLink) ([]byte, error) {
	host, pipeName, err := parseSMBPipePath(params.Target)
	if err != nil {
		return nil, err
	}

	if params.Username == "" {
		return nil, errors.New("SMB link from Linux requires credentials (username/password)")
	}

	tcpDialer := net.Dialer{Timeout: 45 * time.Second, KeepAlive: 30 * time.Second}
	tcpConn, err := tcpDialer.Dial("tcp", net.JoinHostPort(host, "445"))
	if err != nil {
		return nil, fmt.Errorf("failed to connect to %s:445: %w", host, err)
	}

	smbDial := &smb2.Dialer{
		Initiator: &smb2.NTLMInitiator{
			User:     params.Username,
			Password: params.Password,
			Domain:   params.Domain,
		},
	}

	session, err := smbDial.Dial(tcpConn)
	if err != nil {
		tcpConn.Close()
		return nil, fmt.Errorf("SMB authentication failed for %s: %w", host, err)
	}

	share, err := session.Mount(fmt.Sprintf(`\\%s\IPC$`, host))
	if err != nil {
		session.Logoff()
		tcpConn.Close()
		return nil, fmt.Errorf("failed to mount IPC$ on %s: %w", host, err)
	}

	file, err := share.OpenFile(pipeName, os.O_RDWR, 0666)
	if err != nil {
		share.Umount()
		session.Logoff()
		tcpConn.Close()
		return nil, fmt.Errorf("failed to open pipe %s on %s: %w", pipeName, host, err)
	}

	conn := newSmbPipeConn(file, session, share, tcpConn)

	beat, err := pivotRecvFrame(conn, 15*time.Second)
	if err != nil {
		conn.Close()
		return nil, fmt.Errorf("failed to read handshake from pipe %s on %s: %w", pipeName, host, err)
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
