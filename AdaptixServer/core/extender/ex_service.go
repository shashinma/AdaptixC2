package extender

import (
	"AdaptixServer/core/utils/logs"
	"encoding/json"
	"os"
	"path/filepath"
	"plugin"
	"strings"

	"github.com/Adaptix-Framework/axc2"
	"github.com/goccy/go-yaml"
)

// injectServiceName ensures service_name from yaml is present in the JSON passed to InitPlugin,
// so plugins never need to hardcode their own name.
func injectServiceName(name string, serviceConfig string) string {
	s := strings.TrimSpace(serviceConfig)
	if s == "" {
		s = "{}"
	}
	var m map[string]json.RawMessage
	if err := json.Unmarshal([]byte(s), &m); err != nil {
		return s
	}
	if _, exists := m["service_name"]; !exists {
		b, _ := json.Marshal(name)
		m["service_name"] = b
		out, err := json.Marshal(m)
		if err == nil {
			return string(out)
		}
	}
	return s
}

func (ex *AdaptixExtender) ExServiceLoad(configPath string) error {
	_, err := os.Stat(configPath)
	if err != nil {
		return err
	}

	configData, err := os.ReadFile(configPath)
	if err != nil {
		return err
	}

	var configService ExConfigService
	err = yaml.Unmarshal(configData, &configService)
	if err != nil {
		return err
	}

	if _, exists := ex.serviceModules[configService.ServiceName]; exists {
		return ErrServiceAlreadyLoaded
	}

	if err := validateServiceClientConfig(configService); err != nil {
		return err
	}

	pluginPath := filepath.Dir(configPath) + "/" + configService.ExtenderFile
	plug, err := plugin.Open(pluginPath)
	if err != nil {
		return err
	}

	sym, err := plug.Lookup("InitPlugin")
	if err != nil {
		return err
	}

	plInitPlugin, ok := sym.(func(ts any, moduleDir string, serviceConfig string) adaptix.PluginService)
	if !ok {
		return err
	}

	plService := plInitPlugin(ex.ts, filepath.Dir(pluginPath), injectServiceName(configService.ServiceName, configService.ServiceConfig))
	if plService == nil {
		return err
	}

	serviceInfo := ServiceInfo{
		Name:               configService.ServiceName,
		ClientConfigurable: configService.ClientConfigurable,
		ClientConfigSchema: strings.TrimSpace(configService.ClientConfigSchema),
		ConfigDefaults:     strings.TrimSpace(configService.ServiceConfig),
	}

	if configService.AxFile != "" {
		axPath := filepath.Dir(configPath) + "/" + configService.AxFile
		axContent, err := os.ReadFile(axPath)
		if err != nil {
			logs.Warn("", "failed to read ax file %s: %s", axPath, err.Error())
		} else {
			serviceInfo.AX = string(axContent)
		}
	}

	err = ex.ts.TsServiceReg(serviceInfo)
	if err != nil {
		return err
	}

	ex.serviceModules[serviceInfo.Name] = plService
	logs.Success("", "Service '%s' loaded", configService.ServiceName)
	return nil
}

func (ex *AdaptixExtender) ExServiceUnload(serviceName string) error {
	if _, exists := ex.serviceModules[serviceName]; !exists {
		return ErrServiceNotFound
	}

	err := ex.ts.TsServiceUnreg(serviceName)
	if err != nil {
		return err
	}

	delete(ex.serviceModules, serviceName)
	logs.Success("", "Service '%s' unloaded", serviceName)
	return nil
}

func (ex *AdaptixExtender) ExServiceCall(serviceName string, operator string, function string, args string) {
	module, err := ex.getServiceModule(serviceName)
	if err == nil {
		module.Call(operator, function, args)
	}
}

func (ex *AdaptixExtender) ExServiceList() []string {
	var services []string
	for name := range ex.serviceModules {
		services = append(services, name)
	}
	return services
}

func validateServiceClientConfig(configService ExConfigService) error {
	if !configService.ClientConfigurable {
		return nil
	}
	s := strings.TrimSpace(configService.ClientConfigSchema)
	if s == "" {
		return ErrInvalidClientConfigSchema
	}
	var arr []json.RawMessage
	if err := json.Unmarshal([]byte(s), &arr); err != nil {
		return ErrInvalidClientConfigSchema
	}
	if len(arr) == 0 {
		return ErrInvalidClientConfigSchema
	}
	return nil
}
