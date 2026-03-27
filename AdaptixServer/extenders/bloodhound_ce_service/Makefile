all: clean
	@ echo "    * Building bloodhound_ce_service plugin"
	@ mkdir dist
	@ cp config.yaml bloodhound_ce.axs ./dist/
	@ GOEXPERIMENT=jsonv2,greenteagc go build -buildmode=plugin -ldflags="-s -w" -o ./dist/service_bloodhound_ce.so pl_main.go
	@ cp ./dist/service_bloodhound_ce.so ./service_bloodhound_ce.so
	@ echo "      done..."

clean:
	@ rm -rf dist
	@ rm -f service_bloodhound_ce.so
