package extender

import (
	"errors"

	"github.com/Adaptix-Framework/axc2"
)

var (
	ErrModuleNotFound            = errors.New("module not found")
	ErrListenerNotFound          = errors.New("listener not found")
	ErrServiceNotFound           = errors.New("service not found")
	ErrServiceAlreadyLoaded      = errors.New("service already loaded")
	ErrInvalidClientConfigSchema = errors.New("client_config_schema must be a non-empty JSON array when client_configurable is true")
)

/// ExConfig Listener

type ExConfigListener struct {
	ExtenderType string `yaml:"extender_type"`
	ExtenderFile string `yaml:"extender_file"`
	AxFile       string `yaml:"ax_file"`
	ListenerName string `yaml:"listener_name"`
	ListenerType string `yaml:"listener_type"`
	Protocol     string `yaml:"protocol"`
}

/// ExConfig Agent

type ExConfigAgent struct {
	ExtenderType   string   `yaml:"extender_type"`
	ExtenderFile   string   `yaml:"extender_file"`
	AxFile         string   `yaml:"ax_file"`
	AgentName      string   `yaml:"agent_name"`
	AgentWatermark string   `yaml:"agent_watermark"`
	Listeners      []string `yaml:"listeners"`
	MultiListeners bool     `yaml:"multi_listeners"`
}

/// ExConfig Service

type ExConfigService struct {
	ExtenderType  string `yaml:"extender_type"`
	ExtenderFile  string `yaml:"extender_file"`
	AxFile        string `yaml:"ax_file"`
	ServiceName   string `yaml:"service_name"`
	ServiceConfig string `yaml:"service_config"`
	// ClientConfigurable: Adaptix Client lists the service under Settings → Services and shows a form driven by ClientConfigSchema (JSON array).
	ClientConfigurable bool `yaml:"client_configurable"`
	ClientConfigSchema string `yaml:"client_config_schema"`
}

/// Info

type ListenerInfo struct {
	Name     string
	Protocol string
	Type     string
	AX       string
}

type AgentInfo struct {
	Name           string
	Watermark      string
	AX             string
	Listeners      []string
	MultiListeners bool
}

type ServiceInfo struct {
	Name                 string
	AX                   string
	ClientConfigurable   bool
	ClientConfigSchema   string // JSON array of field descriptors for the client UI
	// ConfigDefaults: JSON object from yaml service_config; client merges into form when cache/draft lack keys.
	ConfigDefaults string
}

/// Plugin Interfaces

type Teamserver interface {
	TsListenerReg(listenerInfo ListenerInfo) error
	TsListenerRegByName(listenerName string) (string, error)
	TsAgentReg(agentInfo AgentInfo) error
	TsServiceReg(serviceInfo ServiceInfo) error
	TsServiceUnreg(serviceName string) error

	TsServiceWebProxyRegister(serviceName string, upstreamURL string, upstreamAuthorization string, rewriteConfigJSON string) error
	TsServiceWebProxyUnregister(serviceName string)
	TsClientAPIBaseURL() string

	TsExtenderDataSave(extenderName string, key string, value []byte) error
	TsExtenderDataLoad(extenderName string, key string) ([]byte, error)
	TsExtenderDataDelete(extenderName string, key string) error
	TsExtenderDataKeys(extenderName string) ([]string, error)
	TsExtenderDataDeleteAll(extenderName string) error

	TsEndpointRegister(method string, path string, handler func(username string, body []byte) (int, []byte)) error
	TsEndpointUnregister(method string, path string) error
	TsEndpointExists(method string, path string) bool

	TsEndpointRegisterPublic(method string, path string, handler func(body []byte) (int, []byte)) error
	TsEndpointUnregisterPublic(method string, path string) error
	TsEndpointExistsPublic(method string, path string) bool
}

type AdaptixExtender struct {
	ts              Teamserver
	listenerModules map[string]adaptix.PluginListener
	agentModules    map[string]adaptix.PluginAgent
	serviceModules  map[string]adaptix.PluginService
	activeListeners map[string]adaptix.ExtenderListener
}

/// Helper methods

func (ex *AdaptixExtender) getListenerModule(configType string) (adaptix.PluginListener, error) {
	module, ok := ex.listenerModules[configType]
	if !ok {
		return nil, ErrModuleNotFound
	}
	return module, nil
}

func (ex *AdaptixExtender) getActiveListener(name string) (adaptix.ExtenderListener, error) {
	listener, ok := ex.activeListeners[name]
	if !ok {
		return nil, ErrListenerNotFound
	}
	return listener, nil
}

func (ex *AdaptixExtender) getAgentModule(agentName string) (adaptix.PluginAgent, error) {
	module, ok := ex.agentModules[agentName]
	if !ok {
		return nil, ErrModuleNotFound
	}
	return module, nil
}

func (ex *AdaptixExtender) getServiceModule(serviceName string) (adaptix.PluginService, error) {
	module, ok := ex.serviceModules[serviceName]
	if !ok {
		return nil, ErrServiceNotFound
	}
	return module, nil
}
