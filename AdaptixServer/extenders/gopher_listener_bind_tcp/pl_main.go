package main

import (
	"bytes"
	"crypto/aes"
	"crypto/cipher"
	"encoding/hex"
	"encoding/json"
	"fmt"

	adaptix "github.com/Adaptix-Framework/axc2"
	"github.com/vmihailenco/msgpack/v5"
)

type Teamserver interface {
	TsAgentIsExists(agentId string) bool
	TsAgentCreate(agentCrc string, agentId string, beat []byte, listenerName string, ExternalIP string, Async bool) (adaptix.AgentData, error)
}

type PluginListener struct{}

var (
	ModuleDir       string
	ListenerDataDir string
	Ts              Teamserver
)

type InitPack struct {
	Id   uint   `msgpack:"id"`
	Type uint   `msgpack:"type"`
	Data []byte `msgpack:"data"`
}

func InitPlugin(ts any, moduleDir string, listenerDir string) adaptix.PluginListener {
	ModuleDir = moduleDir
	ListenerDataDir = listenerDir
	Ts = ts.(Teamserver)
	return &PluginListener{}
}

func (p *PluginListener) Create(name string, config string, customData []byte) (adaptix.ExtenderListener, adaptix.ListenerData, []byte, error) {
	var (
		listener     *Listener
		listenerData adaptix.ListenerData
		customdData  []byte
		conf         TransportConfig
		err          error
	)

	if customData == nil {
		if err = validConfig(config); err != nil {
			return nil, listenerData, customdData, err
		}

		err = json.Unmarshal([]byte(config), &conf)
		if err != nil {
			return nil, listenerData, customdData, err
		}

		conf.Protocol = "bind_tcp"
	} else {
		err = json.Unmarshal(customData, &conf)
		if err != nil {
			return nil, listenerData, customdData, err
		}
	}

	transport := &TransportTCP{
		Name:   name,
		Config: conf,
		Active: false,
	}

	listenerData = adaptix.ListenerData{
		BindHost:  "",
		BindPort:  "",
		AgentAddr: fmt.Sprintf("0.0.0.0:%d", transport.Config.Port),
		Status:    "Stopped",
	}

	var buffer bytes.Buffer
	err = json.NewEncoder(&buffer).Encode(transport.Config)
	if err != nil {
		return nil, listenerData, customdData, err
	}
	customdData = buffer.Bytes()

	listener = &Listener{transport: transport}

	return listener, listenerData, customdData, nil
}

func (l *Listener) Start() error {
	l.transport.Active = true
	return nil
}

func (l *Listener) Edit(config string) (adaptix.ListenerData, []byte, error) {
	var (
		listenerData adaptix.ListenerData
		customdData  []byte
	)

	listenerData = adaptix.ListenerData{
		BindHost:  "",
		BindPort:  "",
		AgentAddr: fmt.Sprintf("0.0.0.0:%d", l.transport.Config.Port),
		Status:    "Listen",
	}

	var buffer bytes.Buffer
	err := json.NewEncoder(&buffer).Encode(l.transport.Config)
	if err != nil {
		return listenerData, customdData, err
	}
	customdData = buffer.Bytes()

	return listenerData, customdData, nil
}

func (l *Listener) Stop() error {
	l.transport.Active = false
	return nil
}

func (l *Listener) GetProfile() ([]byte, error) {
	var buffer bytes.Buffer
	err := json.NewEncoder(&buffer).Encode(l.transport.Config)
	if err != nil {
		return nil, err
	}
	return buffer.Bytes(), nil
}

func (l *Listener) InternalHandler(data []byte) (string, error) {
	var agentId = ""

	encKey, err := hex.DecodeString(l.transport.Config.EncryptKey)
	if err != nil {
		return "", err
	}

	decrypted, err := DecryptData(data, encKey)
	if err != nil {
		return "", fmt.Errorf("failed to decrypt beat: %w", err)
	}

	var initPack InitPack
	err = msgpack.Unmarshal(decrypted, &initPack)
	if err != nil {
		return "", fmt.Errorf("failed to unmarshal beat: %w", err)
	}

	agentType := fmt.Sprintf("%08x", initPack.Type)
	agentId = fmt.Sprintf("%08x", initPack.Id)

	if !Ts.TsAgentIsExists(agentId) {
		_, err = Ts.TsAgentCreate(agentType, agentId, initPack.Data, l.transport.Name, "", false)
		if err != nil {
			return agentId, err
		}
	}

	return agentId, nil
}

func DecryptData(data []byte, key []byte) ([]byte, error) {
	block, err := aes.NewCipher(key)
	if err != nil {
		return nil, err
	}

	gcm, err := cipher.NewGCM(block)
	if err != nil {
		return nil, err
	}

	nonceSize := gcm.NonceSize()
	if len(data) < nonceSize {
		return nil, fmt.Errorf("ciphertext too short")
	}

	nonce, ciphertext := data[:nonceSize], data[nonceSize:]

	plaintext, err := gcm.Open(nil, nonce, ciphertext, nil)
	if err != nil {
		return nil, err
	}
	return plaintext, nil
}
